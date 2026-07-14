// SPDX-License-Identifier: GPL-3.0-or-later
#include "config_storage.h"

#include "app_constants.h"
#include "config_backup.h"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

using AppConstants::Update::config_backup_fallback;
using AppConstants::Update::CONFIG_BACKUP_PRIMARY;
using helix::config_backup::write_rolling_backup;

namespace helix {

namespace {

std::string errno_reason(int err) {
    switch (err) {
    case ENOSPC:
        return "disk full";
    case EROFS:
        return "read-only filesystem";
    case EACCES:
        return "permission denied";
    default:
        return strerror(err);
    }
}

class FileConfigStorage : public ConfigStorage {
  public:
    explicit FileConfigStorage(std::string path) : path_(std::move(path)) {}

    std::optional<std::string> load() override {
        struct stat st;
        if (stat(path_.c_str(), &st) != 0) {
            return std::nullopt;
        }
        std::ifstream in(path_);
        if (!in.is_open()) {
            return std::nullopt;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    bool store(const std::string& bytes) override {
        // Atomic save: symlink-resolve, write .tmp, fsync, rename, fsync
        // parent dir. Moved verbatim from Config::save() (see #943: without
        // the fsyncs a power cycle can leave settings.json empty on
        // flash-backed filesystems).
        try {
            std::string target_path = path_;
            {
                std::error_code ec;
                if (fs::is_symlink(path_, ec)) {
                    auto real = fs::canonical(path_, ec);
                    if (!ec) {
                        spdlog::debug("[ConfigStorage] Resolved symlink {} -> {}", path_,
                                      real.string());
                        target_path = real.string();
                    }
                }
            }

            std::string tmp_path = target_path + ".tmp";
            {
                std::ofstream o(tmp_path);
                if (!o.is_open()) {
                    spdlog::error("[ConfigStorage] open failed: {} ({})", tmp_path,
                                  errno_reason(errno));
                    return false;
                }
                o << bytes;
                o.flush();
                if (!o.good()) {
                    spdlog::error("[ConfigStorage] write failed: {} ({})", tmp_path,
                                  errno_reason(errno));
                    std::remove(tmp_path.c_str());
                    return false;
                }
            }

            {
                int fd = ::open(tmp_path.c_str(), O_RDONLY);
                if (fd >= 0) {
                    (void)::fsync(fd);
                    ::close(fd);
                }
            }

            if (std::rename(tmp_path.c_str(), target_path.c_str()) != 0) {
                spdlog::error("[ConfigStorage] rename '{}' -> '{}' failed: {}", tmp_path,
                              target_path, strerror(errno));
                std::remove(tmp_path.c_str());
                return false;
            }

            {
                std::string dir = fs::path(target_path).parent_path().string();
                if (!dir.empty()) {
                    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
                    if (dfd >= 0) {
                        (void)::fsync(dfd);
                        ::close(dfd);
                    }
                }
            }

            // Rolling backup outside install dir (survives Moonraker wipes).
            write_rolling_backup(path_, CONFIG_BACKUP_PRIMARY, config_backup_fallback());
            return true;
        } catch (const std::exception& e) {
            spdlog::error("[ConfigStorage] exception saving {}: {}", path_, e.what());
            return false;
        }
    }

    void preserve_corrupt() override {
        std::string corrupt_path = path_ + ".corrupt";
        std::rename(path_.c_str(), corrupt_path.c_str());
        spdlog::info("[ConfigStorage] Corrupt config saved to {}", corrupt_path);
    }

    bool read_only() override {
        // Write-probe, moved verbatim from Config::init() (lines 1340-1359).
        fs::path config_dir = fs::path(path_).parent_path();
        std::string probe_path = (config_dir / ".helix-write-probe").string();
        std::ofstream probe(probe_path);
        if (!probe.is_open()) {
            int err = errno;
            if (err == EROFS || err == EACCES) {
                spdlog::warn("[ConfigStorage] Read-only filesystem detected ({})",
                             strerror(err));
                return true;
            }
            return false;
        }
        probe.close();
        std::remove(probe_path.c_str());
        return false;
    }

    std::string describe() const override { return path_; }

  private:
    std::string path_;
};

} // namespace

std::unique_ptr<ConfigStorage> make_file_config_storage(const std::string& path) {
    return std::make_unique<FileConfigStorage>(path);
}

} // namespace helix
