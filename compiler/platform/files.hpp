// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// Everything the compiler needs from the operating system, kept in one place so the
// rest of it can also be built for a browser.

#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace one::platform {

    inline std::optional<std::string> read_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    // Every .one file under the given paths, in a stable order. A path that's a file is
    // taken as it is.
    inline std::vector<std::string> find_one_files(const std::vector<std::string>& roots) {
        namespace fs = std::filesystem;
        std::vector<std::string> found;
        for (const auto& root : roots) {
            std::error_code error;
            if (fs::is_regular_file(root, error)) {
                found.push_back(root);
                continue;
            }
            for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
                if (it->is_regular_file(error) && it->path().extension() == ".one") {
                    found.push_back(it->path().string());
                }
            }
        }
        std::sort(found.begin(), found.end());
        return found;
    }

    // Every file directly in `dir` with the given extension, like ".md", in order.
    inline std::vector<std::string> files_in(const std::string& dir, const std::string& extension) {
        namespace fs = std::filesystem;
        std::vector<std::string> found;
        std::error_code error;
        for (fs::directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
            if (it->is_regular_file(error) && it->path().extension() == extension) {
                found.push_back(it->path().string());
            }
        }
        std::sort(found.begin(), found.end());
        return found;
    }

    // Every file inside `dir`, at any depth, relative to it and in order.
    inline std::vector<std::string> files_under(const std::string& dir) {
        namespace fs = std::filesystem;
        std::vector<std::string> found;
        std::error_code error;
        for (fs::recursive_directory_iterator it(dir, error), end; !error && it != end; it.increment(error)) {
            if (it->is_regular_file(error)) found.push_back(fs::relative(it->path(), dir).generic_string());
        }
        std::sort(found.begin(), found.end());
        return found;
    }

    // Writes a file, creating the folders it's in. Returns false when it can't.
    inline bool write_file(const std::string& path, const std::string& content) {
        namespace fs = std::filesystem;
        std::error_code error;
        fs::create_directories(fs::path(path).parent_path(), error);
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        out << content;
        return static_cast<bool>(out);
    }

    inline bool make_executable(const std::string& path) {
        namespace fs = std::filesystem;
        std::error_code error;
        fs::permissions(path, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, error);
        return !error;
    }

    // The path from folder `from` to `to`, with forward slashes, starting with ./ or
    // ../ so it can be used as an import.
    inline std::string relative_import(const std::string& from, const std::string& to) {
        namespace fs = std::filesystem;
        std::string path = fs::relative(fs::weakly_canonical(to), fs::weakly_canonical(from)).generic_string();
        return path.starts_with("../") ? path : "./" + path;
    }

    inline std::string resolve(const std::string& base, const std::string& path) {
        namespace fs = std::filesystem;
        return fs::path(path).is_absolute() ? path : (fs::path(base) / path).lexically_normal().string();
    }

} // namespace one::platform
