#include <common/path.h>
#include <cstring>
#include <iostream>

namespace eka2l1 {
    bool is_separator(const char sep) {
       if (sep == '/' || sep == '\\') {
            return true;
        }

       return false;
    }

    const char get_separator(bool symbian_use = false) {
        if (symbian_use) {
            return '\\';
        }

#ifdef WIN32
        return '\\';
#else
        return '/';
#endif
    }

    bool is_absolute(std::string str, std::string current_dir, bool symbian_use) {
        return absolute_path(str, current_dir, symbian_use) == str;
    }

    std::string add_path(const std::string& path1, const std::string& path2, bool symbian_use) {
        std::string nstring = "";

        bool end_sep = is_separator(path1[path1.size() - 1]);
        bool beg_sep = is_separator(path2[0]);

        if (end_sep && beg_sep) {
            auto pos_sub = path2.find_first_not_of(path2[0]);

            if (pos_sub == std::string::npos) {
                return path1;
            }

            nstring = path2.substr(pos_sub);
        } else if (!end_sep && !beg_sep) {
            nstring = std::string("/") + path2;
        } else
        {
            nstring = path2;
        }

        auto merge =  path1 + nstring;

        // Turn all slash into / (quick hack)

        int crr_point = 0;
        char dsep = 0;
        char rsep = get_separator(symbian_use);

        if (get_separator(symbian_use) == '\\') {
            dsep = '/';
        } else dsep = '\\';

        while (crr_point < merge.size() - 1) {
            crr_point = merge.find_first_of(dsep, crr_point);

            if (crr_point == std::string::npos) {
                break;
            }

            merge[crr_point] = rsep;
            crr_point += 1;
        }

        return merge;
     }

    std::string absolute_path(std::string str, std::string current_dir,bool symbian_use) {
         bool root_dirb = has_root_dir(str, symbian_use);
         bool root_drive = has_root_name(str, symbian_use);

         std::cout << "ROOT DIR: " << root_dirb << " " << root_drive << std::endl;

         // Absoluted
         if (root_dirb) {
             return str;
         }

         std::string new_str = "";

         if (!root_dirb && !root_drive) {
             new_str = add_path(current_dir,  str, symbian_use);

             return new_str;
         }

         if (!root_drive && root_dirb) {
             std::string n_root_drive = root_name(current_dir, symbian_use);
             new_str = add_path(n_root_drive, str, symbian_use);

             return new_str;
         }

         if (root_drive && !root_dirb) {
             std::string root_drive_p = root_name(str, symbian_use);
             std::string root_dir_n = root_dir(current_dir, symbian_use);
             std::string relative_path_n = relative_path(current_dir, symbian_use);
             std::string relative_path_p = relative_path(str, symbian_use);

             new_str = add_path(root_drive_p, root_dir_n, symbian_use);
             new_str = add_path(new_str, relative_path_n, symbian_use);
             new_str = add_path(new_str, relative_path_p, symbian_use);

             return new_str;
         }

         return "";
    }

    bool is_relative(const std::string& str, bool symbian_use) {
         return !relative_path(str, symbian_use).empty();
    }

    std::string relative_path(std::string str, bool symbian_use) {
         std::string root = root_path(str, symbian_use);

         return str.substr(root.size());
    }

    bool has_root_name(std::string path, bool symbian_use) {
        return !root_name(path, symbian_use).empty();
    }

    std::string root_name(std::string path, bool symbian_use) {
         bool has_drive =
                 strncmp(&path[1], ":", 1) == 0;

         bool has_net = is_separator(path[0]) && (path[0] == path[1]);

         if (has_drive) {
             return path.substr(0, 2);
         } else if (has_net) {
             auto res = path.find_first_of(get_separator(symbian_use), 2);

             if (res == std::string::npos) {
                 return "";
             }

             return path.substr(0, res);
         }

         return "";
    }

    bool has_root_dir(std::string path, bool symbian_use) {
        return !root_dir(path, symbian_use).empty();
    }

    std::string root_dir(std::string path, bool symbian_use) {
        bool has_drive = strncmp(&path[1], ":", 1) == 0;
        bool has_net = is_separator(path[0]) && (path[0] == path[1]);

        if (has_drive) {
            if (path.size() > 2 && is_separator(path[2])) {
                return path.substr(2, 1);
            }
        } else if (has_net) {
            auto res = path.find_first_of(get_separator(symbian_use), 2);

            if (res == std::string::npos) {
                return "";
            }

            return path.substr(res, 1);
        } else {
            if (path.size() > 1 && is_separator(path[0])) {
                std::cout << "PATH: " << path.c_str() << std::endl;
                return path.substr(0, 1);
            }
        }

        return "";
    }

    bool has_root_path(std::string path, bool symbian_use) {
        return !root_path(path, symbian_use).empty();
    }

    std::string root_path(std::string path, bool symbian_use) {
        bool has_drive =
                strncmp(&path[1], ":", 1) == 0;
        bool has_net = is_separator(path[0]) && (path[0] == path[1]);

        if (has_drive) {
            if (path.size() > 2 && is_separator(path[2])) {
                return path.substr(0, 3);
            }
            else {
                return path.substr(0,2);
            }
        } else if (has_net) {
            auto res = path.find_first_of(get_separator(symbian_use), 2);

            if (res == std::string::npos) {
                return "";
            }

            return path.substr(0, res);
        } else {
            if (is_separator(path[0])) {
                return path.substr(0, 1);
            }
        }

        return "";
    }
}