/*
 * Copyright (c) 2018 EKA2L1 Team
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

#include <core/services/fs/fs.h>
#include <core/services/fs/op.h>

#include <core/epoc/des.h>
#include <memory>

#include <common/cvt.h>
#include <common/log.h>
#include <common/path.h>

#include <common/e32inc.h>

#include <core/vfs.h>
#include <filesystem>

#include <core/core.h>

namespace fs = std::experimental::filesystem;

const TUint KEntryAttNormal = 0x0000;
const TUint KEntryAttReadOnly = 0x0001;
const TUint KEntryAttHidden = 0x0002;
const TUint KEntryAttSystem = 0x0004;
const TUint KEntryAttVolume = 0x0008;
const TUint KEntryAttDir = 0x0010;
const TUint KEntryAttArchive = 0x0020;
const TUint KEntryAttXIP = 0x0080;
const TUint KEntryAttRemote = 0x0100;
const TUint KEntryAttMaskFileSystemSpecific = 0x00FF0000;
const TUint KEntryAttMatchMask = (KEntryAttHidden | KEntryAttSystem | KEntryAttDir);

const TUint KDriveAttTransaction = 0x80;
const TUint KDriveAttPageable = 0x100;
const TUint KDriveAttLogicallyRemovable = 0x200;
const TUint KDriveAttHidden = 0x400;
const TUint KDriveAttExternal = 0x800;
const TUint KDriveAttAll = 0x100000;
const TUint KDriveAttExclude = 0x40000;
const TUint KDriveAttExclusive = 0x80000;

const TUint KDriveAttLocal = 0x01;
const TUint KDriveAttRom = 0x02;
const TUint KDriveAttRedirected = 0x04;

namespace eka2l1::epoc {
    enum TFileMode {
        EFileShareExclusive,
        EFileShareReadersOnly,
        EFileShareAny,
        EFileShareReadersOrWriters,
        EFileStream = 0,
        EFileStreamText = 0x100,
        EFileRead = 0,
        EFileWrite = 0x200,
        EFileReadAsyncAll = 0x400,
        EFileWriteBuffered = 0x00000800,
        EFileWriteDirectIO = 0x00001000,
        EFileReadBuffered = 0x00002000,
        EFileReadDirectIO = 0x00004000,
        EFileReadAheadOn = 0x00008000,
        EFileReadAheadOff = 0x00010000,
        EDeleteOnClose = 0x00020000,
        EFileBigFile = 0x00040000,
        EFileSequential = 0x00080000
    };
}

namespace eka2l1 {
    fs_handle_table::fs_handle_table() {
        for (size_t i = 0; i < nodes.size(); i++) {
            nodes[i].id = i + 1;
        }
    }

    size_t fs_handle_table::add_node(fs_node &node) {
        for (size_t i = 0; i < nodes.size(); i++) {
            if (!nodes[i].is_active) {
                nodes[i] = std::move(node);
                nodes[i].is_active = true;

                return i + 1;
            }
        }

        return 0;
    }

    bool fs_handle_table::close_nodes(size_t handle) {
        if (handle <= nodes.size() && nodes[handle - 1].is_active) {
            nodes[handle - 1].is_active = false;

            return true;
        }

        return false;
    }

    fs_node *fs_handle_table::get_node(size_t handle) {
        if (handle <= nodes.size() && nodes[handle - 1].is_active) {
            return &nodes[handle - 1];
        }

        return nullptr;
    }

    fs_node *fs_handle_table::get_node(const std::u16string &path) {
        for (auto &file_node : nodes) {
            if ((file_node.is_active && file_node.vfs_node->type == io_component_type::file) && (std::dynamic_pointer_cast<file>(file_node.vfs_node)->file_name() == path)) {
                return &file_node;
            }
        }

        return nullptr;
    }

    fs_server::fs_server(system *sys)
        : service::server(sys, "!FileServer", true) {
        REGISTER_IPC(fs_server, entry, EFsEntry, "Fs::Entry");
        REGISTER_IPC(fs_server, file_open, EFsFileOpen, "Fs::FileOpen");
        REGISTER_IPC(fs_server, file_size, EFsFileSize, "Fs::FileSize");
        REGISTER_IPC(fs_server, file_seek, EFsFileSeek, "Fs::FileSeek");
        REGISTER_IPC(fs_server, file_read, EFsFileRead, "Fs::FileRead");
        REGISTER_IPC(fs_server, file_replace, EFsFileReplace, "Fs::FileReplace");
        REGISTER_IPC(fs_server, open_dir, EFsDirOpen, "Fs::OpenDir");
        REGISTER_IPC(fs_server, read_dir, EFsDirReadOne, "Fs::ReadDir");
        REGISTER_IPC(fs_server, read_dir_packed, EFsDirReadPacked, "Fs::ReadDirPacked");
        REGISTER_IPC(fs_server, drive_list, EFsDriveList, "Fs::DriveList");
        REGISTER_IPC(fs_server, drive, EFsDrive, "Fs::Drive");
    }

    fs_node *fs_server::get_file_node(int handle) {
        return nodes_table.get_node(handle);
    }

    void fs_server::file_size(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        ctx.write_arg_pkg<uint64_t>(0, std::dynamic_pointer_cast<file>(node->vfs_node)->size());
        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_seek(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        symfile vfs_file = std::dynamic_pointer_cast<file>(node->vfs_node);

        std::optional<int> seek_mode = ctx.get_arg<int>(1);
        std::optional<int> seek_off = ctx.get_arg<int>(0);

        if (!seek_mode || !seek_off) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        file_seek_mode vfs_seek_mode;

        switch (*seek_mode) {
        case 0: // ESeekAddress. Handle this as a normal seek start
            vfs_seek_mode = file_seek_mode::beg;
            break;

        default:
            vfs_seek_mode = static_cast<file_seek_mode>(*seek_mode - 1);
            break;
        }

        vfs_file->seek(*seek_off, vfs_seek_mode);
        ctx.write_arg_pkg(3, static_cast<int>(vfs_file->tell()));

        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_read(service::ipc_context ctx) {
        std::optional<int> handle_res = ctx.get_arg<int>(3);

        if (!handle_res) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *node = get_file_node(*handle_res);

        if (node == nullptr || node->vfs_node->type != io_component_type::file) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        symfile vfs_file = std::dynamic_pointer_cast<file>(node->vfs_node);

        if (!(node->open_mode & READ_MODE)) {
            ctx.set_request_status(KErrAccessDenied);
            return;
        }

        int read_len = *ctx.get_arg<int>(1);
        int read_pos = *ctx.get_arg<int>(2);

        uint64_t last_pos = vfs_file->tell();
        uint64_t size = vfs_file->size();

        if (size - read_pos < read_len) {
            read_len = size - last_pos;
        }

        vfs_file->seek(read_pos, file_seek_mode::beg);

        std::vector<char> read_data;
        read_data.resize(read_len);

        vfs_file->read_file(read_data.data(), 1, read_len);

        ctx.write_arg_pkg(0, reinterpret_cast<uint8_t *>(read_data.data()), read_len);
        ctx.set_request_status(KErrNone);
    }

    void fs_server::new_file_subsession(service::ipc_context ctx, bool overwrite) {
        std::optional<std::u16string> name_res = ctx.get_arg<std::u16string>(0);
        std::optional<int> open_mode_res = ctx.get_arg<int>(1);

        if (!name_res || !open_mode_res) {
            ctx.set_request_status(KErrArgument);
        }

        LOG_INFO("Opening file: {}", common::ucs2_to_utf8(*name_res));

        int handle = new_node(ctx.sys->get_io_system(), ctx.msg->own_thr, *name_res, *open_mode_res, overwrite);

        if (handle <= 0) {
            ctx.set_request_status(handle);
            return;
        }

        ctx.write_arg_pkg<int>(3, handle);
        ctx.set_request_status(KErrNone);
    }

    void fs_server::file_open(service::ipc_context ctx) {
        new_file_subsession(ctx);
    }

    void fs_server::file_replace(service::ipc_context ctx) {
        new_file_subsession(ctx, true);
    }

    int fs_server::new_node(io_system *io, thread_ptr sender, std::u16string name, int org_mode, bool overwrite) {
        int real_mode = org_mode & ~(epoc::EFileStreamText | epoc::EFileReadAsyncAll | epoc::EFileBigFile);
        fs_node_share share_mode = (fs_node_share)-1;

        if (real_mode & epoc::EFileShareExclusive) {
            share_mode = fs_node_share::exclusive;
        } else if (real_mode & epoc::EFileShareReadersOnly) {
            share_mode = fs_node_share::share_read;
        } else if (real_mode & epoc::EFileShareReadersOrWriters) {
            share_mode = fs_node_share::share_read_write;
        } else if (real_mode & epoc::EFileShareAny) {
            share_mode = fs_node_share::any;
        }

        // Fetch open mode
        int access_mode = -1;

        if (!(real_mode & epoc::EFileStreamText)) {
            access_mode = BIN_MODE;
        } else {
            access_mode = 0;
        }

        if (real_mode & epoc::EFileWrite) {
            if (overwrite)
                access_mode |= WRITE_MODE;
            else
                access_mode |= APPEND_MODE;
        } else {
            // Since EFileRead = 0, they default to read mode if nothing is specified more
            access_mode |= READ_MODE;
        }

        if (access_mode & WRITE_MODE && share_mode == fs_node_share::share_read) {
            return KErrAccessDenied;
        }

        kernel::owner_type owner_type = kernel::owner_type::kernel;

        // Fetch owner
        if (share_mode == fs_node_share::exclusive) {
            owner_type = kernel::owner_type::process;
        }

        fs_node *cache_node = nodes_table.get_node(name);

        if (!cache_node) {
            fs_node new_node;
            new_node.vfs_node = io->open_file(name, access_mode);

            if (!new_node.vfs_node) {
                return KErrNotFound;
            }

            if ((int)share_mode == -1) {
                share_mode = fs_node_share::share_read_write;
            }

            new_node.mix_mode = real_mode;
            new_node.open_mode = access_mode;
            new_node.share_mode = share_mode;
            new_node.own_process = sender->owning_process();

            return nodes_table.add_node(new_node);
        }

        if ((int)share_mode != -1 && share_mode != cache_node->share_mode) {
            if (share_mode == fs_node_share::exclusive || cache_node->share_mode == fs_node_share::exclusive) {
                return KErrAccessDenied;
            }

            // Compare mode, compatible ?
            if ((share_mode == fs_node_share::share_read && cache_node->share_mode == fs_node_share::any)
                || (share_mode == fs_node_share::share_read && cache_node->share_mode == fs_node_share::share_read_write && (cache_node->open_mode & WRITE_MODE))
                || (share_mode == fs_node_share::share_read_write && (access_mode & WRITE_MODE) && cache_node->share_mode == fs_node_share::share_read)
                || (share_mode == fs_node_share::any && cache_node->share_mode == fs_node_share::share_read)) {
                return KErrAccessDenied;
            }

            // Let's promote mode

            // Since we filtered incompatible mode, so if the share mode of them two is read, share mode of both of them is read
            if (share_mode == fs_node_share::share_read || cache_node->share_mode == fs_node_share::share_read) {
                share_mode = fs_node_share::share_read;
                cache_node->share_mode = fs_node_share::share_read;
            }

            if (share_mode == fs_node_share::any || cache_node->share_mode == fs_node_share::any) {
                share_mode = fs_node_share::any;
                cache_node->share_mode = fs_node_share::any;
            }

            if (share_mode == fs_node_share::share_read_write || share_mode == fs_node_share::share_read_write) {
                share_mode = fs_node_share::share_read_write;
                cache_node->share_mode = fs_node_share::share_read_write;
            }
        } else {
            share_mode = cache_node->share_mode;
        }

        if (share_mode == fs_node_share::share_read && access_mode & WRITE_MODE) {
            return KErrAccessDenied;
        }

        // Check if mode is compatible
        if (cache_node->share_mode == fs_node_share::exclusive) {
            // Check if process is the same
            // Deninded if mode is exclusive
            if (cache_node->own_process != sender->owning_process()) {
                return KErrAccessDenied;
            }

            // If we have the same open mode as the cache node, don't create new, returns this :D
            if (cache_node->open_mode == real_mode) {
                return cache_node->id;
            }

            fs_node new_node;
            new_node.vfs_node = io->open_file(name, access_mode);

            if (!new_node.vfs_node) {
                return KErrNotFound;
            }

            new_node.mix_mode = real_mode;
            new_node.open_mode = access_mode;
            new_node.share_mode = share_mode;

            return nodes_table.add_node(new_node);
        }
    }

    bool is_e32img(symfile f) {
        int sig;

        f->seek(12, file_seek_mode::beg);
        f->read_file(&sig, 1, 4);

        if (sig != 0x434F5045) {
            return false;
        }

        return true;
    }

    void fs_server::entry(service::ipc_context ctx) {
        std::optional<std::u16string> fname_op = ctx.get_arg<std::u16string>(0);

        if (!fname_op) {
            ctx.set_request_status(KErrArgument);
        }

        std::string path = common::ucs2_to_utf8(*fname_op);

        LOG_INFO("Get entry of: {}", path);

        bool dir = false;

        io_system *io = ctx.sys->get_io_system();
        symfile file = io->open_file(*fname_op, READ_MODE | BIN_MODE);

        if (!file) {
            if (eka2l1::is_dir(io->get(path))) {
                dir = true;
            } else {
                ctx.set_request_status(KErrNotFound);
                return;
            }
        }

        epoc::TEntry entry;
        entry.aSize = dir ? 0 : file->size();

        if ((*fname_op)[0] == u'Z' || (*fname_op)[0] == u'z') {
            entry.aAttrib = KEntryAttReadOnly | KEntryAttSystem;

            if (!dir && (*fname_op).find(u".dll") && !is_e32img(file)) {
                entry.aAttrib |= KEntryAttXIP;
            }
        }

        if (dir) {
            entry.aAttrib |= KEntryAttDir;
        } else {
            entry.aAttrib |= KEntryAttNormal;
        }

        entry.aNameLength = (*fname_op).length();
        entry.aSizeHigh = 0; // This is never used, since the size is never >= 4GB as told by Nokia Doc

        memcpy(entry.aName, (*fname_op).data(), entry.aNameLength * 2);

        auto last_mod = fs::last_write_time(io->get(path));

        entry.aModified = epoc::TTime{ static_cast<uint64_t>(last_mod.time_since_epoch().count()) };
        ctx.write_arg_pkg<epoc::TEntry>(1, entry);

        ctx.set_request_status(KErrNone);
    }

    void fs_server::open_dir(service::ipc_context ctx) {
        auto dir = ctx.get_arg<std::u16string>(0);

        if (!dir) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node node;
        node.vfs_node = ctx.sys->get_io_system()->open_dir(*dir);

        if (!node.vfs_node) {
            ctx.set_request_status(KErrNotFound);
            return;
        }

        node.own_process = ctx.msg->own_thr->owning_process();
        size_t dir_handle = nodes_table.add_node(node);

        ctx.write_arg_pkg<int>(3, dir_handle);
        ctx.set_request_status(KErrNone);
    }

    void fs_server::read_dir(service::ipc_context ctx) {
        std::optional<int> handle = ctx.get_arg<int>(3);
        std::optional<int> entry_arr_vir_ptr = ctx.get_arg<int>(0);

        if (!handle || !entry_arr_vir_ptr) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *dir_node = nodes_table.get_node(*handle);

        if (!dir_node || dir_node->vfs_node->type != io_component_type::dir) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        std::shared_ptr<directory> dir = std::dynamic_pointer_cast<directory>(dir_node->vfs_node);
        epoc::TEntry entry;

        std::optional<entry_info> info = dir->get_next_entry();

        if (!info) {
            ctx.set_request_status(KErrEof);
            return;
        }

        switch (info->attribute) {
        case io_attrib::hidden: {
            entry.aAttrib = KEntryAttHidden;
            break;
        }

        default:
            break;
        }

        if (info->type == io_component_type::dir) {
            entry.aAttrib &= KEntryAttDir;
        } else {
            entry.aAttrib &= KEntryAttNormal;
        }

        entry.aSize = info->size;
        entry.aNameLength = info->full_path.length();

        // TODO: Convert this using a proper function
        std::u16string path_u16(info->full_path.begin(), info->full_path.end());
        std::copy(path_u16.begin(), path_u16.end(), entry.aName);

        ctx.set_request_status(KErrNone);
    }

    void fs_server::read_dir_packed(service::ipc_context ctx) {
        std::optional<int> handle = ctx.get_arg<int>(3);
        std::optional<int> entry_arr_vir_ptr = ctx.get_arg<int>(0);

        if (!handle || !entry_arr_vir_ptr) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        fs_node *dir_node = nodes_table.get_node(*handle);

        if (!dir_node || dir_node->vfs_node->type != io_component_type::dir) {
            ctx.set_request_status(KErrBadHandle);
            return;
        }

        std::shared_ptr<directory> dir = std::dynamic_pointer_cast<directory>(dir_node->vfs_node);

        epoc::TDes8 *entry_arr = reinterpret_cast<epoc::TDes8 *>(ctx.msg->own_thr->owning_process()->get_ptr_on_addr_space(*entry_arr_vir_ptr));
        epoc::TEntry *entry_buf = reinterpret_cast<epoc::TEntry *>(entry_arr->Ptr(ctx.msg->own_thr->owning_process()));

        epoc::TBuf8 *entry_arr_buf = reinterpret_cast<epoc::TBuf8 *>(entry_arr);

        size_t max_entries = entry_arr_buf->iMaxLength / sizeof(epoc::TEntry);
        size_t queried_entries = 0;

        for (; queried_entries < max_entries; queried_entries++) {
            std::optional<entry_info> info = dir->get_next_entry();

            if (!info) {
                break;
            }

            switch (info->attribute) {
            case io_attrib::hidden: {
                entry_buf[queried_entries].aAttrib = KEntryAttHidden;
                break;
            }

            default:
                break;
            }

            if (info->type == io_component_type::dir) {
                entry_buf[queried_entries].aAttrib &= KEntryAttDir;
            } else {
                entry_buf[queried_entries].aAttrib &= KEntryAttNormal;
            }

            entry_buf[queried_entries].aSize = info->size;
            entry_buf[queried_entries].aNameLength = info->name.length();

            // TODO: Convert this using a proper function
            std::u16string path_u16(info->name.begin(), info->name.end());
            std::copy(path_u16.begin(), path_u16.end(), entry_buf[queried_entries].aName);
        }

        entry_arr->SetLength(ctx.msg->own_thr->owning_process(), queried_entries * sizeof(epoc::TEntry));

        if (queried_entries < max_entries) {
            // All entries have been read, and it should return EOF now
            ctx.set_request_status(KErrEof);
            return;
        }

        ctx.set_request_status(KErrNone);
    }

    void fs_server::drive_list(service::ipc_context ctx) {
        std::optional<int> flags = ctx.get_arg<int>(1);

        if (!flags) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        std::vector<io_attrib> exclude_attribs;
        std::vector<io_attrib> include_attribs;

        // Fetch flags
        if (*flags & KDriveAttHidden) {
            if (*flags & KDriveAttExclude) {
                exclude_attribs.push_back(io_attrib::hidden);
            } else {
                include_attribs.push_back(io_attrib::hidden);
            }
        }

        std::array<char, drive_count> dlist;

        std::fill(dlist.begin(), dlist.end(), 0);

        for (size_t i = drive_a; i < drive_count; i += 1) {
            eka2l1::drive drv = ctx.sys->get_io_system()->get_drive_entry(static_cast<drive_number>(i));

            bool out = false;

            for (const auto &exclude : exclude_attribs) {
                if (static_cast<int>(exclude) & static_cast<int>(drv.attribute)) {
                    dlist[i] = 0;
                    out = true;

                    break;
                }
            }

            if (!out) {
                if (include_attribs.empty()) {
                    if (drv.media_type != drive_media::none) {
                        dlist[i] = 1;
                    }

                    continue;
                }

                auto meet_one_condition = std::find_if(include_attribs.begin(), include_attribs.end(),
                    [=](io_attrib attrib) { return static_cast<int>(attrib) & static_cast<int>(drv.attribute); });

                if (meet_one_condition != include_attribs.end()) {
                    dlist[i] = 1;
                }
            }
        }

        bool success = ctx.write_arg_pkg(0, reinterpret_cast<uint8_t *>(&dlist[0]), dlist.size());

        if (!success) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        ctx.set_request_status(KErrNone);
    }

    enum TMediaType {
        EMediaNotPresent,
        EMediaUnknown,
        EMediaFloppy,
        EMediaHardDisk,
        EMediaCdRom,
        EMediaRam,
        EMediaFlash,
        EMediaRom,
        EMediaRemote,
        EMediaNANDFlash,
        EMediaRotatingMedia
    };

    enum TBatteryState {
        EBatNotSupported,
        EBatGood,
        EBatLow
    };

    enum TConnectionBusType {
        EConnectionBusInternal,
        EConnectionBusUsb
    };

    struct TDriveInfo {
        TMediaType iType;
        TBatteryState iBattery;
        TUint iDriveAtt;
        TUint iMediaAtt;
        TConnectionBusType iConnectionBusType;
    };

    enum TDriveNumber {
        EDriveA,
        EDriveB,
        EDriveC,
        EDriveD,
        EDriveE,
        EDriveF,
        EDriveG,
        EDriveH,
        EDriveI,
        EDriveJ,
        EDriveK,
        EDriveL,
        EDriveM,
        EDriveN,
        EDriveO,
        EDriveP,
        EDriveQ,
        EDriveR,
        EDriveS,
        EDriveT,
        EDriveU,
        EDriveV,
        EDriveW,
        EDriveX,
        EDriveY,
        EDriveZ
    };

    /* Simple for now only, in the future this should be more advance. */
    void fs_server::drive(service::ipc_context ctx) {
        TDriveNumber drv = static_cast<TDriveNumber>(*ctx.get_arg<int>(1));
        std::optional<TDriveInfo> info = ctx.get_arg_packed<TDriveInfo>(0);

        eka2l1::drive io_drive = ctx.sys->get_io_system()->get_drive_entry(static_cast<drive_number>(drv));

        if (io_drive.media_type == drive_media::none) {
            info->iType = EMediaUnknown;

            ctx.write_arg_pkg<TDriveInfo>(0, *info);
            ctx.set_request_status(KErrNone);
            return;
        }

        if (!info) {
            ctx.set_request_status(KErrArgument);
            return;
        }

        switch (io_drive.media_type) {
        case drive_media::physical: {
            info->iType = EMediaHardDisk;
            info->iDriveAtt = KDriveAttLocal;

            break;
        }

        case drive_media::rom: {
            info->iType = EMediaRom;
            info->iDriveAtt = KDriveAttRom;

            break;
        }

        case drive_media::reflect: {
            info->iType = EMediaRotatingMedia;
            info->iDriveAtt = KDriveAttRedirected;

            break;
        }

        default:
            break;
        }

        switch (io_drive.attribute) {
        case io_attrib::hidden: {
            info->iDriveAtt &= KEntryAttHidden;
            break;
        }

        default:
            break;
        }

        info->iBattery = EBatNotSupported;
        info->iConnectionBusType = EConnectionBusInternal;

        ctx.write_arg_pkg<TDriveInfo>(0, *info);
        ctx.set_request_status(KErrNone);
    }
}