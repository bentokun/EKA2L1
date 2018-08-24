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

#include <common/types.h>
#include <cstdint>
#include <optional>
#include <vector>

namespace eka2l1 {
    namespace loader {
        struct demand_paging_config {
            uint16_t min_pages;
            uint16_t max_pages;
            uint16_t young_old_ratio;
            uint16_t spare[3];
        };

        struct security_info {
            uint32_t secure_id;
            uint32_t vendor_id;
            uint32_t caps[2];
        };

        struct rom_page_info {
            enum attrib : uint8_t {
                pageable = 1 << 0
            };

            enum compression : uint8_t {
                none,
                bytepair
            };

            uint32_t data_start;
            uint16_t data_size;
            compression compression_type;
            attrib paging_attrib;
        };

        struct rom_header {
            uint8_t jump[124];
            address restart_vector;
            int64_t time;
            uint32_t time_high;
            // 8c = 16 * 8 + 12 = 128 + 12 = 140
            address rom_base;
            uint32_t rom_size;
            address rom_root_dir_list;
            address kern_data_address;
            address kern_limit;
            // Offset to the first file
            address primary_file;
            address secondary_file;
            uint32_t checksum;
            uint32_t hardware;
            int64_t lang;
            uint32_t kern_config_flags;
            address rom_exception_search_tab;
            uint32_t rom_header_size;

            address rom_section_header;
            int32_t total_sv_data_size;

            address variant_file; // Offset to file describes variant
            address extension_file;
            address reloc_info;
            uint32_t old_trace_mask;
            address user_data_addr;
            int32_t total_user_data_size;

            uint32_t debug_port;

            uint8_t major;
            uint8_t minor;
            uint16_t build;

            uint32_t compress_type;
            uint32_t compress_size;

            uint32_t uncompress_size;

            uint32_t disabled_caps[2];
            uint32_t trace_mask[8];
            uint32_t initial_btrace_filter[8];

            int initial_btrace_buf;
            int initial_btrace_mode;

            int pageable_rom_start;
            int pageable_rom_size;

            int rom_page_idx;

            uint32_t compressed_unpaged_start;
            uint32_t unpaged_compressed_size;

            uint32_t hcr_file_addr;
            uint32_t spare[36];
        };

        struct rom_section_header {
            uint8_t major;
            uint8_t minor;
            uint16_t build;

            uint32_t checksum;
            uint64_t lang;
        };

        struct rom_entry;

        struct rom_dir {
            int size;
            utf16_str name;

            std::vector<rom_entry> entries;
            std::vector<rom_dir> subdirs;
        };

        struct rom_entry {
            uint32_t size;
            uint32_t address_lin;
            uint8_t attrib;
            uint8_t name_len;
            utf16_str name;

            // If the entry is really pointed to a directory entry
            // this will provide it
            std::optional<rom_dir> dir;
        };

        struct rom_dir_sort_info {
            uint16_t subdir_count;
            uint16_t file_count;

            uint16_t *entry_offset;
        };

        struct root_dir {
            uint32_t hardware_variant;
            address addr_lin;

            rom_dir dir;
        };

        struct root_dir_list {
            int num_root_dirs;
            std::vector<root_dir> root_dirs;

            root_dir &operator[](uint32_t idx) {
                return root_dirs[idx];
            }
        };

        struct rom {
            rom_header header;
            rom_section_header section_header;

            FILE *handler;

            root_dir_list root;
        };

        std::optional<rom> load_rom(const std::string &path);
    }
}
