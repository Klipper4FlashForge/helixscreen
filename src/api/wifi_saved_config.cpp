// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "wifi_saved_config.h"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif
#ifndef RAMFS_MAGIC
#define RAMFS_MAGIC 0x858458f6
#endif

namespace helix::wifi {

namespace detail {

bool is_volatile_path(const std::string& path) {
    struct statfs sfs {};
    if (::statfs(path.c_str(), &sfs) != 0)
        return false; // unknown — assume persistent rather than churn files

    const auto type = static_cast<unsigned long>(sfs.f_type);
    return type == static_cast<unsigned long>(TMPFS_MAGIC) ||
           type == static_cast<unsigned long>(RAMFS_MAGIC);
}

} // namespace detail

namespace {

std::mutex g_target_mutex;
std::string g_persistent_target;

/// Mirror the target's mode and owner: an existing file keeps its own, a new
/// one inherits from its parent directory. We are writing the platform's file,
/// not ours.
struct FileIdentity {
    mode_t mode = 0644;
    uid_t uid = static_cast<uid_t>(-1);
    gid_t gid = static_cast<gid_t>(-1);
};

FileIdentity identity_to_mirror(const std::string& path) {
    FileIdentity id;
    struct stat st {};

    if (::stat(path.c_str(), &st) == 0) {
        id.mode = st.st_mode & 07777;
        id.uid = st.st_uid;
        id.gid = st.st_gid;
        return id;
    }

    const size_t slash = path.find_last_of('/');
    const std::string dir = (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
    if (::stat(dir.c_str(), &st) == 0) {
        id.mode = st.st_mode & 0666; // directory execute bits are not for files
        id.uid = st.st_uid;
        id.gid = st.st_gid;
    }
    return id;
}

bool write_mirrored(const std::string& path, const std::string& contents, const FileIdentity& id) {
    const std::string tmp = path + ".helix.tmp";

    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, id.mode);
    if (fd < 0) {
        spdlog::error("[WifiSavedConfig] Cannot create {}: {}", tmp, std::strerror(errno));
        return false;
    }

    bool ok = true;
    const char* data = contents.data();
    size_t remaining = contents.size();
    while (remaining > 0) {
        const ssize_t n = ::write(fd, data, remaining);
        if (n <= 0) {
            spdlog::error("[WifiSavedConfig] Write failed on {}: {}", tmp, std::strerror(errno));
            ok = false;
            break;
        }
        data += n;
        remaining -= static_cast<size_t>(n);
    }

    if (ok) {
        ::fchmod(fd, id.mode);
        if (id.uid != static_cast<uid_t>(-1) && ::fchown(fd, id.uid, id.gid) != 0) {
            // Non-fatal: probably not root. Say so rather than fail the connect.
            spdlog::debug("[WifiSavedConfig] Could not mirror ownership on {}: {}", tmp,
                          std::strerror(errno));
        }
    }
    ::close(fd);

    if (!ok) {
        ::unlink(tmp.c_str());
        return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        spdlog::error("[WifiSavedConfig] Cannot replace {}: {}", path, std::strerror(errno));
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace

void remember_persistent_target(const std::string& conf_path) {
    std::lock_guard<std::mutex> lock(g_target_mutex);
    g_persistent_target.clear();

    if (conf_path.empty())
        return;

    // Only a symlink tells us the platform intended somewhere else. A plain
    // file is either already persistent (nothing to do) or a link a previous
    // save already ate (nothing we can learn from).
    struct stat lst {};
    if (::lstat(conf_path.c_str(), &lst) != 0 || !S_ISLNK(lst.st_mode))
        return;

    char resolved[PATH_MAX];
    if (::realpath(conf_path.c_str(), resolved) == nullptr)
        return;

    const std::string target(resolved);
    if (target == conf_path)
        return;

    // If the link points at volatile storage too, mirroring buys nothing.
    if (detail::is_volatile_path(target)) {
        spdlog::debug("[WifiSavedConfig] {} resolves to {}, also volatile — nothing to mirror",
                      conf_path, target);
        return;
    }

    g_persistent_target = target;
    spdlog::info("[WifiSavedConfig] wpa_supplicant config {} is a link to persistent {} — will "
                 "restore it there if a save replaces the link",
                 conf_path, target);
}

std::string persistent_target() {
    std::lock_guard<std::mutex> lock(g_target_mutex);
    return g_persistent_target;
}

bool mirror_to_persistent(const std::string& conf_path) {
    const std::string target = persistent_target();
    if (target.empty() || conf_path.empty())
        return false;

    std::string contents;
    {
        std::ifstream in(conf_path, std::ios::binary);
        if (!in.is_open()) {
            spdlog::error("[WifiSavedConfig] Cannot read {} to mirror it", conf_path);
            return false;
        }
        contents.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (contents.empty())
        return false;

    if (!write_mirrored(target, contents, identity_to_mirror(target)))
        return false;

    // No SSID or PSK in the log: the file we just wrote holds the secret, and
    // the log must not become the second copy.
    spdlog::info("[WifiSavedConfig] wpa_supplicant saved to volatile {} — mirrored to {} so the "
                 "network survives a reboot",
                 conf_path, target);
    return true;
}

} // namespace helix::wifi
