/*
 * Copyright (c) 2018 EKA2L1 Team.
 * 
 * This file is part of EKA2L1 project 
 * (see bentokun.github.com/EKA2L1).
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <algorithm>
#include <atomic>
#include <queue>
#include <thread>

#include <common/log.h>
#include <core/core.h>
#include <core/core_kernel.h>
#include <core/core_mem.h>
#include <core/hle/libmanager.h>
#include <core/kernel/scheduler.h>
#include <core/kernel/thread.h>
#include <core/loader/romimage.h>
#include <core/manager/manager.h>
#include <core/ptr.h>
#include <core/vfs.h>

#include <core/services/init.h>

namespace eka2l1 {
    void kernel_system::init(system *esys, timing_system *timing_sys, manager_system *mngrsys,
        memory_system *mem_sys, io_system *io_sys, hle::lib_manager *lib_sys, arm::jit_interface *cpu) {
        timing = timing_sys;
        mngr = mngrsys;
        mem = mem_sys;
        libmngr = lib_sys;
        io = io_sys;
        sys = esys;

        thr_sch = std::make_shared<kernel::thread_scheduler>(this, timing, *cpu);

        kernel_handles = kernel::object_ix(this, kernel::handle_array_owner::kernel);
        service::init_services(sys);
    }

    void kernel_system::shutdown() {
        thr_sch.reset();
        destroy_all_processes();
    }

    bool kernel_system::run_thread(uint32_t handle) {
        auto res = get_thread_by_handle(handle);

        if (!res) {
            return false;
        }

        thr_sch->schedule(res);

        return true;
    }

    bool kernel_system::run_process(uint32_t handle) {
        auto res = get_process(handle);

        if (!res) {
            return false;
        }

        return res->run();
    }

    thread_ptr kernel_system::crr_thread() {
        return thr_sch->current_thread();
    }

    process_ptr kernel_system::crr_process() {
        return thr_sch->current_process();
    }

    uint32_t kernel_system::spawn_new_process(std::string &path, std::string name, uint32_t uid, kernel::owner_type owner) {
        std::u16string path16 = std::u16string(path.begin(), path.end());
        symfile f = io->open_file(path16, READ_MODE | BIN_MODE);

        if (!f) {
            return 0xFFFFFFFF;
        }

        auto temp = loader::parse_eka2img(f, true);

        if (!temp) {
            f->seek(0, eka2l1::file_seek_mode::beg);
            auto romimg = loader::parse_romimg(f, mem);

            if (romimg) {
                // Lib manager needs the system to call HLE function
                libmngr->init(sys, this, io, mem, kern_ver);

                loader::romimg_ptr img_ptr = libmngr->load_romimg(path16, false);
                libmngr->open_romimg(img_ptr);

                if (sys->get_bool_config("force_load_euser")) {
                    // Use for debugging rom image
                    loader::romimg_ptr euser_force = libmngr->load_romimg(u"euser", false);
                    libmngr->open_romimg(euser_force);
                }

                if (sys->get_bool_config("force_load_bafl")) {
                    // Use for debugging rom image
                    loader::romimg_ptr bafl_force = libmngr->load_romimg(u"bafl", false);
                    libmngr->open_romimg(bafl_force);
                }

                process_ptr pr = std::make_shared<kernel::process>(this, mem, uid, name, path16, u"", img_ptr,
                    static_cast<kernel::process_priority>(img_ptr->header.priority));

                objects.push_back(std::move(pr));

                uint32_t h = create_handle_lastest(owner);
                run_process(h);

                f->close();

                return h;
            }

            f->close();
            return false;
        } else {
            // Lib manager needs the system to call HLE function
            libmngr->init(sys, this, io, mem, kern_ver);
        }

        auto res2 = libmngr->load_e32img(path16);

        if (!res2) {
            return 0xFFFFFFFF;
        }

        libmngr->open_e32img(res2);
        libmngr->patch_hle();

        process_ptr pr = std::make_shared<kernel::process>(this, mem, uid, name, path16, u"", res2,
            static_cast<kernel::process_priority>(res2->header.priority));

        objects.push_back(std::move(pr));

        uint32_t h = create_handle_lastest(owner);
        run_process(h);

        f->close();

        return h;
    }

    uint32_t kernel_system::spawn_new_process(uint32_t uid, kernel::owner_type owner) {
        return spawn_new_process(mngr->get_package_manager()->get_app_executable_path(uid),
            mngr->get_package_manager()->get_app_name(uid), uid, owner);
    }

    bool kernel_system::destroy_all_processes() {
        for (auto &obj : objects) {
            if (obj && obj->get_object_type() == kernel::object_type::process) {
                obj.reset();
            }
        }

        return true;
    }

    process_ptr kernel_system::get_process(std::string &name) {
        auto pr_find = std::find_if(objects.begin(), objects.end(),
            [&](auto &obj) {
                if (obj && obj->get_object_type() == kernel::object_type::process && obj->name() == name) {
                    return true;
                }
            });

        if (pr_find == objects.end()) {
            return nullptr;
        }

        return std::dynamic_pointer_cast<kernel::process>(*pr_find);
    }

    process_ptr kernel_system::get_process(uint32_t handle) {
        kernel_obj_ptr obj = get_kernel_obj(handle);

        if (!obj || obj->get_object_type() != kernel::object_type::process) {
            return nullptr;
        }

        return std::dynamic_pointer_cast<kernel::process>(obj);
    }

    void kernel_system::prepare_reschedule() {
        sys->prepare_reschedule();
    }

    uint32_t kernel_system::create_handle_lastest(kernel::owner_type owner) {
        switch (owner) {
        case kernel::owner_type::kernel:
            return kernel_handles.add_object(objects.back());

        case kernel::owner_type::process:
            return crr_process()->process_handles.add_object(objects.back());

        case kernel::owner_type::thread:
            return crr_thread()->thread_handles.add_object(objects.back());

        default:
            return -1;
        }
    }

    uint32_t kernel_system::create_chunk(std::string name, const address bottom, const address top, const size_t size, prot protection,
        kernel::chunk_type type, kernel::chunk_access access, kernel::chunk_attrib attrib, kernel::owner_type owner) {
        chunk_ptr new_chunk = std::make_shared<kernel::chunk>(this, mem, name, bottom, top, size, protection, type, access, attrib);
        objects.push_back(std::move(new_chunk));

        return create_handle_lastest(owner);
    }

    uint32_t kernel_system::create_mutex(std::string name, bool init_locked,
        kernel::owner_type own,
        kernel::access_type access) {
        mutex_ptr new_mutex = std::make_shared<kernel::mutex>(this, name, init_locked, access);
        objects.push_back(std::move(new_mutex));

        return create_handle_lastest(own);
    }

    uint32_t kernel_system::create_sema(std::string sema_name,
        int32_t init_count,
        int32_t max_count,
        kernel::owner_type own_type,
        kernel::access_type access) {
        sema_ptr new_sema = std::make_shared<kernel::semaphore>(this, sema_name, init_count, max_count, access);
        objects.push_back(std::move(new_sema));

        return create_handle_lastest(own_type);
    }

    uint32_t kernel_system::create_timer(std::string name, kernel::reset_type rt,
        kernel::owner_type owner,
        kernel::access_type access) {
        timer_ptr new_timer = std::make_shared<kernel::timer>(this, timing, name, rt, access);
        objects.push_back(std::move(new_timer));

        return create_handle_lastest(owner);
    }

    uint32_t kernel_system::create_change_notifier(kernel::owner_type owner) {
        change_notifier_ptr new_nof = std::make_shared<kernel::change_notifier>(this);
        objects.push_back(std::move(new_nof));

        return create_handle_lastest(owner);
    }

    uint32_t kernel_system::create_thread(kernel::owner_type owner, process_ptr own_pr, kernel::access_type access,
        const std::string &name, const address epa, const size_t stack_size,
        const size_t min_heap_size, const size_t max_heap_size,
        bool initial,
        ptr<void> usrdata,
        kernel::thread_priority pri) {
        thread_ptr new_thread = std::make_shared<kernel::thread>(this, mem, own_pr, access,
            name, epa, stack_size, min_heap_size, max_heap_size, initial, usrdata, pri);

        objects.push_back(std::move(new_thread));

        return create_handle_lastest(owner);
    }

    uint32_t kernel_system::create_server(std::string name) {
        server_ptr new_server = std::make_shared<service::server>(sys, name);
        objects.push_back(std::move(new_server));

        return create_handle_lastest(kernel::owner_type::kernel);
    }

    uint32_t kernel_system::create_session(server_ptr cnn_svr, int async_slots) {
        session_ptr new_session = std::make_shared<service::session>(this, cnn_svr, async_slots);
        objects.push_back(std::move(new_session));

        return create_handle_lastest(kernel::owner_type::thread);
    }

    uint32_t kernel_system::create_prop(kernel::owner_type owner) {
        property_ptr new_prop = std::make_shared<service::property>(this);
        objects.push_back(std::move(new_prop));

        return create_handle_lastest(owner);
    }

    uint32_t kernel_system::create_process(uint32_t uid,
        const std::string &process_name, const std::u16string &exe_path,
        const std::u16string &cmd_args, loader::e32img_ptr &img,
        const kernel::process_priority pri, kernel::owner_type own) {
        process_ptr pr = std::make_shared<kernel::process>(this, mem, uid, process_name, exe_path, cmd_args, img,
            pri);
        objects.push_back(std::move(pr));

        return create_handle_lastest(own);
    }

    uint32_t kernel_system::create_process(uint32_t uid,
        const std::string &process_name, const std::u16string &exe_path,
        const std::u16string &cmd_args, loader::romimg_ptr &img,
        const kernel::process_priority pri, kernel::owner_type own) {
        process_ptr pr = std::make_shared<kernel::process>(this, mem, uid, process_name, exe_path, cmd_args, img,
            pri);
        objects.push_back(std::move(pr));

        return create_handle_lastest(own);
    }

    uint32_t kernel_system::create_library(const std::string &name, loader::romimg_ptr &img,
        kernel::owner_type own) {
        library_ptr lib = std::make_shared<kernel::library>(this, name, img);
        objects.push_back(std::move(lib));

        return create_handle_lastest(own);
    }

    uint32_t kernel_system::create_library(const std::string &name, loader::e32img_ptr &img,
        kernel::owner_type own) {
        library_ptr lib = std::make_shared<kernel::library>(this, name, img);
        objects.push_back(std::move(lib));

        return create_handle_lastest(own);
    }

    ipc_msg_ptr kernel_system::create_msg(kernel::owner_type owner) {
        auto &slot_free = std::find_if(msgs.begin(), msgs.end(),
            [](auto slot) { return !slot || slot->free; });

        if (slot_free != msgs.end()) {
            ipc_msg_ptr msg
                = std::make_shared<ipc_msg>(crr_thread());

            if (!*slot_free) {
                *slot_free = std::move(msg);
            }

            slot_free->get()->free = false;
            slot_free->get()->id = slot_free - msgs.begin();

            return *slot_free;
        }

        return nullptr;
    }

    ipc_msg_ptr kernel_system::get_msg(int handle) {
        if (msgs.size() <= handle) {
            return nullptr;
        }

        return msgs[handle];
    }

    bool kernel_system::destroy(kernel_obj_ptr obj) {
        auto &obj_ite = std::find(objects.begin(), objects.end(), obj);

        if (obj_ite != objects.end()) {
            objects.erase(obj_ite);
            return true;
        }

        return false;
    }

    bool kernel_system::close(uint32_t handle) {
        kernel::handle_inspect_info info = kernel::inspect_handle(handle);

        if (info.no_close) {
            return false;
        }

        if (info.handle_array_local) {
            return crr_thread()->thread_handles.close(handle);
        }

        bool res = crr_process()->process_handles.close(handle);

        if (!res) {
            res = kernel_handles.close(handle);

            return res;
        }

        return true;
    }

    thread_ptr kernel_system::get_thread_by_handle(uint32_t handle) {
        kernel_obj_ptr obj = get_kernel_obj(handle);

        if (!obj || obj->get_object_type() != kernel::object_type::thread) {
            return nullptr;
        }

        return std::dynamic_pointer_cast<kernel::thread>(obj);
    }

    kernel_obj_ptr kernel_system::get_kernel_obj(uint32_t handle) {
        if (handle == 0xFFFF8000) {
            return crr_process();
        } else if (handle == 0xFFFF8001) {
            return crr_thread();
        }

        kernel::handle_inspect_info info = kernel::inspect_handle(handle);

        if (info.handle_array_local) {
            return crr_thread()->thread_handles.get_object(handle);
        }

        kernel_obj_ptr res;

        if (crr_process())
            res = crr_process()->process_handles.get_object(handle);

        if (!res) {
            res = kernel_handles.get_object(handle);

            return res;
        }

        return res;
    }

    kernel_obj_ptr kernel_system::get_kernel_obj_by_id(uint64_t id) {
        auto &res = std::find_if(objects.begin(), objects.end(),
            [=](kernel_obj_ptr obj) { return obj && (obj->unique_id() == id); });

        if (res != objects.end()) {
            return *res;
        }

        return nullptr;
    }

    thread_ptr kernel_system::get_thread_by_name(const std::string &name) {
        auto thr_find = std::find_if(objects.begin(), objects.end(),
            [&](auto &obj) {
                if (obj && obj->get_object_type() == kernel::object_type::thread && obj->name() == name) {
                    return true;
                }
            });

        if (thr_find == objects.end()) {
            return nullptr;
        }

        return std::dynamic_pointer_cast<kernel::thread>(*thr_find);
    }

    std::vector<thread_ptr> kernel_system::get_all_thread_own_process(process_ptr pr) {
        std::vector<thread_ptr> thr_list;

        for (kernel_obj_ptr &obj : objects) {
            if (obj->get_object_type() == kernel::object_type::thread) {
                thread_ptr thr = std::dynamic_pointer_cast<kernel::thread>(obj);

                if (thr->own_process == pr) {
                    thr_list.push_back(thr);
                }
            }
        }

        return thr_list;
    }

    std::vector<process_ptr> kernel_system::get_all_processes() {
        std::vector<process_ptr> process_list;

        for (kernel_obj_ptr &obj : objects) {
            if (obj->get_object_type() == kernel::object_type::process) {
                process_ptr pr = std::dynamic_pointer_cast<kernel::process>(obj);
                process_list.push_back(pr);
            }
        }

        return process_list;
    }

    void kernel_system::free_msg(ipc_msg_ptr msg) {
        msg->free = true;
    }

    /*! \brief Completely destroy a message. */
    void kernel_system::destroy_msg(ipc_msg_ptr msg) {
        (msgs.begin() + msg->id)->reset();
    }

    server_ptr kernel_system::get_server_by_name(const std::string name) {
        auto svr_find = std::find_if(objects.begin(), objects.end(),
            [&](auto &obj) {
                if (obj && obj->get_object_type() == kernel::object_type::server && obj->name() == name) {
                    return true;
                }
            });

        if (svr_find == objects.end()) {
            return nullptr;
        }

        return std::dynamic_pointer_cast<service::server>(*svr_find);
    }

    server_ptr kernel_system::get_server(uint32_t handle) {
        kernel_obj_ptr obj = get_kernel_obj(handle);

        if (!obj || obj->get_object_type() != kernel::object_type::server) {
            return nullptr;
        }

        return std::dynamic_pointer_cast<service::server>(obj);
    }

    session_ptr kernel_system::get_session(uint32_t handle) {
        kernel_obj_ptr obj = get_kernel_obj(handle);

        if (!obj || obj->get_object_type() != kernel::object_type::session) {
            return nullptr;
        }

        return std::dynamic_pointer_cast<service::session>(obj);
    }

    property_ptr kernel_system::get_prop(int cagetory, int key) {
        auto &prop_res = std::find_if(objects.begin(), objects.end(),
            [=](const auto &prop) {
                if (prop && prop->get_object_type() == kernel::object_type::prop) {
                    property_ptr real_prop = std::dynamic_pointer_cast<service::property>(prop);

                    if (real_prop->first == cagetory && real_prop->second == key) {
                        return true;
                    }
                }

                return false;
            });

        if (prop_res == objects.end()) {
            return property_ptr(nullptr);
        }

        return std::dynamic_pointer_cast<service::property>(*prop_res);
    }

    property_ptr kernel_system::get_prop(uint32_t handle) {
        kernel_obj_ptr obj = get_kernel_obj(handle);

        if (!obj || obj->get_object_type() != kernel::object_type::prop) {
            return nullptr;
        }

        return std::dynamic_pointer_cast<service::property>(obj);
    }

    uint32_t kernel_system::mirror(thread_ptr own_thread, uint32_t handle, kernel::owner_type owner) {
        kernel_obj_ptr target_obj;
        kernel::handle_inspect_info info = kernel::inspect_handle(handle);

        if (handle == 0xFFFF8000) {
            target_obj = crr_process();
        } else if (handle == 0xFFFF8001) {
            target_obj = crr_thread();
        } else if (info.handle_array_local) {
            // Use the thread to get the kernel object
            target_obj = own_thread->thread_handles.get_object(handle);
        } else {
            target_obj = get_kernel_obj(handle);
        }

        switch (owner) {
        case kernel::owner_type::kernel:
            return kernel_handles.add_object(target_obj);

        case kernel::owner_type::thread: {
            if (!own_thread) {
                return 0xFFFFFFFF;
            }

            return own_thread->thread_handles.add_object(target_obj);
        }

        case kernel::owner_type::process: {
            return crr_process()->process_handles.add_object(target_obj);
        }

        default:
            return 0xFFFFFFFF;
        }
    }

    uint32_t kernel_system::mirror(kernel_obj_ptr obj, kernel::owner_type owner) {
        switch (owner) {
        case kernel::owner_type::kernel:
            return kernel_handles.add_object(obj);

        case kernel::owner_type::thread: {
            return crr_thread()->thread_handles.add_object(obj);
        }

        case kernel::owner_type::process: {
            return crr_process()->process_handles.add_object(obj);
        }

        default:
            return 0xFFFFFFFF;
        }
    }

    uint32_t kernel_system::open_handle(kernel_obj_ptr obj, kernel::owner_type owner) {
        switch (owner) {
        case kernel::owner_type::kernel:
            return kernel_handles.add_object(obj);

        case kernel::owner_type::thread: {
            return crr_thread()->thread_handles.add_object(obj);
        }

        case kernel::owner_type::process: {
            return crr_process()->process_handles.add_object(obj);
        }

        default:
            return 0xFFFFFFFF;
        }
    }

    void kernel_system::processing_requests() {
        std::vector<server_ptr> svrs;

        for (auto &obj : objects) {
            if (obj && obj->get_object_type() == kernel::object_type::server) {
                server_ptr svr = std::dynamic_pointer_cast<service::server>(obj);

                if (svr->is_hle()) {
                    svrs.push_back(svr);
                }
            }
        }

        for (server_ptr &svr : svrs) {
            svr->process_accepted_msg();
        }
    }

    uint64_t kernel_system::next_uid() const {
        ++uid_counter;
        return uid_counter.load();
    }

    std::optional<find_handle> kernel_system::find_object(const std::string &name, int start, kernel::object_type type) {
        find_handle handle_find_info;

        if (start >= objects.size()) {
            return std::optional<find_handle>{};
        }

        const auto &obj = std::find_if(objects.begin() + start, objects.end(),
            [&](kernel_obj_ptr obj) { return obj && (obj->name() == name) && (obj->get_object_type() == type); });

        if (obj != objects.end()) {
            handle_find_info.index = obj - objects.begin();
            handle_find_info.object_id = (*obj)->unique_id();

            return handle_find_info;
        }

        return std::optional<find_handle>{};
    }

    bool kernel_system::should_terminate() {
        return thr_sch->should_terminate();
    }
}
