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

#pragma once

#include <array>
#include <condition_variable>
#include <memory>
#include <optional>
#include <stack>
#include <string>

#include <common/resource.h>
#include <arm/arm_factory.h>

#include <epoc/kernel/chunk.h>
#include <epoc/kernel/object_ix.h>

#include <epoc/utils/reqsts.h>

#include <epoc/ipc.h>
#include <epoc/ptr.h>

namespace eka2l1 {
    class kernel_system;
    class timing_system;
    class memory;

    namespace kernel {
        class mutex;
        class semaphore;
        class process;
    }

    using chunk_ptr = std::shared_ptr<kernel::chunk>;
    using mutex_ptr = std::shared_ptr<kernel::mutex>;
    using sema_ptr = std::shared_ptr<kernel::semaphore>;
    using process_ptr = std::shared_ptr<kernel::process>;

    namespace kernel {
        using address = std::uint32_t;
        using thread_stack = common::resource<address>;
        using thread_stack_ptr = std::unique_ptr<thread_stack>;

        class thread_scheduler;

        enum class thread_state {
            create,
            run,
            wait,
            ready,
            stop,
            wait_fast_sema, // Wait for semaphore
            wait_mutex,
            wait_mutex_suspend,
            wait_fast_sema_suspend,
            hold_mutex_pending,
            wait_dfc, // Unused
            wait_hle // Wait in case an HLE event is taken place - e.g GUI
        };

        enum thread_priority {
            priority_null = -30,
            priority_much_less = -20,
            priority_less = -10,
            priority_normal = 0,
            priority_more = 10,
            priority_much_more = 20,
            priority_real_time = 30,
            priority_absolute_very_low = 100,
            priority_absolute_low = 200,
            priority_absolute_background = 300,
            priorty_absolute_foreground = 400,
            priority_absolute_high = 500
        };

        struct tls_slot {
            int handle = -1;
            int uid = -1;
            ptr<void> pointer;
        };

        struct thread_local_data {
            ptr<void> heap;
            ptr<void> scheduler;
            ptr<void> trap_handler;
            std::uint32_t thread_id;

            // We don't use this. We use our own heap
            ptr<void> tls_heap;
            std::array<tls_slot, 50> tls_slots;
        };

        struct debug_function_trace {
            arm::arm_interface::thread_context ctx;
            std::string func_name;
        };

        class thread : public kernel_obj {
            friend class eka2l1::kernel_system;
            
            friend class thread_scheduler;
            friend class mutex;
            friend class semaphore;

            process_ptr own_process;

            thread_state state;
            std::mutex mut;
            std::condition_variable todo;

            // Thread context to save when suspend the execution
            arm::arm_interface::thread_context ctx;

            thread_priority priority;
            
            int last_priority;
            int real_priority;

            int stack_size;
            int min_heap_size, max_heap_size;

            ptr<void> usrdata;

            memory_system *mem;
            timing_system *timing;

            std::uint64_t lrt;

            std::uint32_t stack_chunk;
            std::uint32_t name_chunk;
            std::uint32_t tls_chunk;

            thread_local_data ldata;

            std::shared_ptr<thread_scheduler> scheduler;
            std::stack<debug_function_trace> call_stacks;

            sema_ptr request_sema;
            std::uint32_t flags;
            ipc_msg_ptr sync_msg;

            void reset_thread_ctx(std::uint32_t entry_point, std::uint32_t stack_top, bool inital);
            void create_stack_metadata(ptr<void> stack_ptr, ptr<void> allocator_ptr, std::uint32_t name_len, address name_ptr, address epa);

            int leave_depth = -1;

            object_ix thread_handles;

            int wakeup_handle;

            int rendezvous_reason = 0;
            int exit_reason = 0;

            struct logon_request_form {
                thread_ptr requester;
                epoc::request_status *request_status;

                explicit logon_request_form(thread_ptr thr, epoc::request_status *rsts)
                    : requester(thr)
                    , request_status(rsts) {}
            };

            std::vector<logon_request_form> logon_requests;
            std::vector<logon_request_form> rendezvous_requests;

            std::uint64_t create_time = 0;

            epoc::request_status *sleep_nof_sts;
            epoc::request_status *timeout_sts;

        public:
            kernel_obj_ptr get_object(std::uint32_t handle);
            kernel_obj *wait_obj;

            void logon(epoc::request_status *logon_request, bool rendezvous);
            bool logon_cancel(epoc::request_status *logon_request, bool rendezvous);

            void rendezvous(int rendezvous_reason);

            void finish_logons();

            void set_exit_reason(int reason) {
                exit_reason = reason;
            }

            int get_exit_reason() const {
                return exit_reason;
            }

            std::optional<debug_function_trace> get_top_call() {
                if (call_stacks.size() == 0) {
                    return std::optional<debug_function_trace>{};
                }

                return call_stacks.top();
            }

            void push_call(const std::string &func_name,
                const arm::arm_interface::thread_context &ctx);

            void pop_call();

            thread();
            thread(kernel_system *kern, memory_system *mem, timing_system *timing, process_ptr owner, kernel::access_type access,
                const std::string &name, const address epa, const size_t stack_size,
                const size_t min_heap_size, const size_t max_heap_size,
                bool initial,
                ptr<void> usrdata = 0,
                ptr<void> allocator = 0,
                thread_priority pri = priority_normal);

            ~thread();

            // Physically we can't compare thread.
            bool operator>(const thread &rhs);
            bool operator<(const thread &rhs);
            bool operator==(const thread &rhs);
            bool operator>=(const thread &rhs);
            bool operator<=(const thread &rhs);

            chunk_ptr get_stack_chunk();

            tls_slot *get_tls_slot(std::uint32_t handle, std::uint32_t dll_uid);
            void close_tls_slot(tls_slot &slot);

            void update_priority();

            bool suspend();
            bool resume();

            void wait_for_any_request();
            void signal_request(int count = 1);

            void set_priority(const thread_priority new_pri);

            bool sleep(std::uint32_t mssecs);
            bool sleep_nof(epoc::request_status *sts, std::uint32_t mssecs);

            void after(epoc::request_status *sts, std::uint32_t mssecs);

            void notify_sleep(const int errcode);
            void notify_after(const int errcode);

            bool stop();

            thread_priority get_priority() const {
                return priority;
            }

            std::uint32_t get_flags() const {
                return flags;
            }

            void set_flags(const std::uint32_t new_flags) {
                flags = new_flags;
            }

            thread_local_data &get_local_data() {
                return ldata;
            }

            std::shared_ptr<thread_scheduler> get_scheduler() {
                return scheduler;
            }

            process_ptr owning_process() {
                return own_process;
            }

            arm::arm_interface::thread_context &get_thread_context() {
                return ctx;
            }

            void owning_process(process_ptr pr);

            thread_state current_state() const {
                return state;
            }

            int current_real_priority() const {
                return real_priority;
            }

            void current_state(thread_state st) {
                state = st;
            }

            ipc_msg_ptr &get_sync_msg() {
                return sync_msg;
            }

            void increase_leave_depth() {
                leave_depth++;
            }

            void decrease_leave_depth() {
                leave_depth--;
            }

            bool is_invalid_leave() const {
                return leave_depth > 0;
            }

            int get_leave_depth() const {
                return leave_depth;
            }

            std::uint32_t last_handle() {
                return thread_handles.last_handle();
            }
        };

        using thread_ptr = std::shared_ptr<kernel::thread>;
    }
}