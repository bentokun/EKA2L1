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

#include <arm/jit_factory.h>
#include <core_kernel.h>
#include <core_mem.h>
#include <core_timing.h>
#include <disasm/disasm.h>
#include <hle/libmanager.h>
#include <loader/rom.h>
#include <manager/manager.h>
#include <manager/package_manager.h>
#include <vfs.h>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <process.h>
#include <tuple>

namespace eka2l1 {
    enum class availdrive {
        c,
        e
    };

    class system {
        process *crr_process;
        std::mutex mut;

        hle::lib_manager hlelibmngr;
        arm::jitter cpu;
        arm::jitter_arm_type jit_type;

        memory_system mem;
        kernel_system kern;
        timing_system timing;

        manager_system mngr;
        io_system io;

        disasm asmdis;

        loader::rom romf;

        bool reschedule_pending;

        epocver ver = epocver::epoc9;

    public:
        system(arm::jitter_arm_type jit_type = arm::jitter_arm_type::unicorn)
            : jit_type(jit_type) {}

        void set_symbian_version_use(const epocver ever) {
            ver = ever;
        }

        epocver get_symbian_version_use() const {
            return ver;
        }

        void init();
        process *load(uint64_t id);
        int loop();
        void shutdown();

        memory_system *get_memory_system() {
            return &mem;
        }

        kernel_system *get_kernel_system() {
            return &kern;
        }

        arm::jitter &get_cpu() {
            return cpu;
        }

        void mount(availdrive drv, std::string path);

        bool install_package(std::u16string path, uint8_t drv);
        bool load_rom(const std::string &path);

        uint32_t total_app() {
            return mngr.get_package_manager()->app_count();
        }

        std::vector<manager::app_info> app_infos() {
            return mngr.get_package_manager()->get_apps_info();
        }
    };
}

