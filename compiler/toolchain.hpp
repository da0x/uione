// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// The compiler a project is for. A project's block names it, one "0.4.0", the last
// version it was checked clean with, and any one run on the project hands the work
// to that version: fetched once from its GitHub release, checked against the
// release's SHA256SUMS, and kept in ~/.cache/uione/<version>/one. So a project is
// always built by the compiler it was written for, and moves to a newer one only
// with `one upgrade`. UIONE_TOOLCHAIN=local turns this off, for working on the
// compiler itself.

#pragma once

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <regex>
#include <string>
#include <sys/utsname.h>
#include <unistd.h>
#include <vector>

#include "platform/files.hpp"

namespace one::toolchain {

    // The version a project names, read from its text rather than parsed, since it may
    // be written for a compiler whose language this one doesn't read.
    inline std::optional<std::string> pinned(const std::string& root) {
        static const std::regex block(R"re((^|\n)[ \t]*project[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*\{)re");
        static const std::regex line(R"re((^|\n)[ \t]*one[ \t]+"(\d+\.\d+\.\d+)")re");
        for (const auto& path : platform::find_one_files({root})) {
            auto text = platform::read_file(path);
            std::smatch found;
            if (!text || !std::regex_search(*text, found, block)) continue;
            std::size_t start = static_cast<std::size_t>(found.position(0) + found.length(0));
            std::size_t end = text->find("\n}", start);
            std::string inside = text->substr(start, end == std::string::npos ? std::string::npos : end - start);
            std::smatch version;
            if (std::regex_search(inside, version, line)) return version[2].str();
            return std::nullopt;
        }
        return std::nullopt;
    }

    // The release's name for this computer, as the release workflow builds them.
    inline std::optional<std::string> system_name() {
        utsname about{};
        if (uname(&about) != 0) return std::nullopt;
        std::string system = about.sysname, machine = about.machine;
        if (system == "Linux" && (machine == "x86_64" || machine == "amd64")) return "linux-x86_64";
        if (system == "Linux" && (machine == "aarch64" || machine == "arm64")) return "linux-aarch64";
        if (system == "Darwin" && machine == "arm64") return "macos-arm64";
        return std::nullopt;
    }

    // Where a version is kept once it's fetched.
    inline std::string kept(const std::string& version) {
        const char* cache = std::getenv("XDG_CACHE_HOME");
        const char* home = std::getenv("HOME");
        std::string base = cache && *cache ? cache : std::string(home ? home : ".") + "/.cache";
        return base + "/uione/" + version + "/one";
    }

    // Fetches a version from its GitHub release, checks it against the release's
    // checksums, and keeps it. UIONE_RELEASES names another place releases are, for a
    // mirror. The version is only digits and dots, so it's safe in the command.
    inline bool fetch(const std::string& version, std::string& why) {
        auto name = system_name();
        if (!name) {
            why = "there's no build of one for this computer to fetch; build " + version + " from source";
            return false;
        }
        const char* mirror = std::getenv("UIONE_RELEASES");
        std::string releases = mirror && *mirror ? mirror : "https://github.com/da0x/uione/releases/download";
        std::string target = kept(version);
        std::string archive = "one-" + *name + ".tar.gz";
        std::string script = "set -e\n"
                             "release='" + releases + "/v" + version + "'\n"
                             "target='" + target + "'\n"
                             "archive='" + archive + "'\n"
                             "work=$(mktemp -d)\n"
                             "trap 'rm -rf \"$work\"' EXIT\n"
                             "curl -fsSL \"$release/$archive\" -o \"$work/$archive\"\n"
                             "curl -fsSL \"$release/SHA256SUMS\" -o \"$work/SHA256SUMS\"\n"
                             "expected=$(awk -v name=\"$archive\" '$2 == name || $2 == \"*\" name { print $1 }' \"$work/SHA256SUMS\")\n"
                             "[ -n \"$expected\" ]\n"
                             "if command -v sha256sum >/dev/null; then actual=$(sha256sum \"$work/$archive\" | awk '{ print $1 }');\n"
                             "else actual=$(shasum -a 256 \"$work/$archive\" | awk '{ print $1 }'); fi\n"
                             "[ \"$expected\" = \"$actual\" ]\n"
                             "tar -xzf \"$work/$archive\" -C \"$work\" one\n"
                             "mkdir -p \"$(dirname \"$target\")\"\n"
                             "chmod +x \"$work/one\"\n"
                             "mv \"$work/one\" \"$target\"\n";
        std::string command = "sh -c '" + std::regex_replace(script, std::regex("'"), "'\\''") + "'";
        if (std::system(command.c_str()) != 0) {
            why = "couldn't fetch one " + version + " from " + releases + "/v" + version;
            return false;
        }
        return true;
    }

    // What to do about a project's version: nothing when it names this one, or none,
    // or switching is turned off; otherwise the version to hand the work to.
    inline std::optional<std::string> wanted(const std::vector<std::string>& roots, std::string_view own, std::string& why) {
        const char* setting = std::getenv("UIONE_TOOLCHAIN");
        if (setting && std::string_view(setting) == "local") return std::nullopt;
        std::optional<std::string> found;
        for (const auto& root : roots) {
            auto version = pinned(root);
            if (!version) continue;
            if (found && *found != *version) {
                why = "these projects are for different compilers, " + *found + " and " + *version + "; check them one at a time";
                return std::nullopt;
            }
            found = version;
        }
        if (!found || *found == own) return std::nullopt;
        return found;
    }

    // Runs a kept version with the arguments this one was given, in its place. It
    // only returns when that couldn't start.
    inline void hand_over(const std::string& version, char** argv) {
        setenv("UIONE_TOOLCHAIN", "local", 1);  // the version handed to doesn't hand over again
        std::string path = kept(version);
        argv[0] = path.data();
        execv(path.c_str(), argv);
    }

} // namespace one::toolchain
