// SPDX-License-Identifier: GPL-3.0-or-later
#include "android_asset_extractor.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace helix {

AssetExtractionResult extract_assets_if_needed(const std::string& source_dir,
                                               const std::string& target_dir,
                                               const std::string& current_version) {
    // Check if target already has matching version
    fs::path version_file = fs::path(target_dir) / "VERSION";
    if (fs::exists(version_file)) {
        std::ifstream ifs(version_file);
        std::string existing_version;
        std::getline(ifs, existing_version);
        if (existing_version == current_version) {
            spdlog::debug("Assets already at version {}, skipping extraction", current_version);
            return AssetExtractionResult::ALREADY_CURRENT;
        }
        spdlog::info("Asset version mismatch: have '{}', need '{}' - re-extracting",
                     existing_version, current_version);
    }

    // Create target directory if needed
    std::error_code ec;
    fs::create_directories(target_dir, ec);
    if (ec) {
        spdlog::error("Failed to create target directory '{}': {}", target_dir, ec.message());
        return AssetExtractionResult::FAILED;
    }

    // Verify source directory exists
    if (!fs::exists(source_dir) || !fs::is_directory(source_dir)) {
        spdlog::error("Source directory '{}' does not exist or is not a directory", source_dir);
        return AssetExtractionResult::FAILED;
    }

    // Copy all files recursively from source to target
    fs::copy(source_dir, target_dir,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        spdlog::error("Failed to copy assets from '{}' to '{}': {}", source_dir, target_dir,
                      ec.message());
        return AssetExtractionResult::FAILED;
    }

    // Write version marker
    {
        std::ofstream ofs(version_file, std::ios::trunc);
        if (!ofs) {
            spdlog::error("Failed to write version marker to '{}'", version_file.string());
            return AssetExtractionResult::FAILED;
        }
        ofs << current_version;
    }

    spdlog::info("Extracted assets to '{}' (version {})", target_dir, current_version);
    return AssetExtractionResult::EXTRACTED;
}

#ifdef __ANDROID__

#include "helix_version.h"

#include <SDL.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <cstdlib>
#include <jni.h>
#include <sstream>
#include <vector>

// Get AAssetManager from the Android Activity via JNI
static AAssetManager* get_asset_manager() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity) {
        spdlog::error("[AndroidAssets] Failed to get JNI env or activity");
        return nullptr;
    }

    jclass activity_class = env->GetObjectClass(activity);
    jmethodID get_assets =
        env->GetMethodID(activity_class, "getAssets", "()Landroid/content/res/AssetManager;");
    jobject java_asset_mgr = env->CallObjectMethod(activity, get_assets);

    env->DeleteLocalRef(activity_class);
    env->DeleteLocalRef(activity);

    if (!java_asset_mgr) {
        spdlog::error("[AndroidAssets] Failed to get AssetManager from activity");
        return nullptr;
    }

    AAssetManager* mgr = AAssetManager_fromJava(env, java_asset_mgr);
    env->DeleteLocalRef(java_asset_mgr);
    return mgr;
}

// Recursively extract all files from an APK asset directory to the filesystem
static int extract_asset_dir(AAssetManager* mgr, const std::string& asset_path,
                             const std::string& target_path) {
    int count = 0;

    // Create target directory
    std::error_code ec;
    fs::create_directories(target_path, ec);
    if (ec) {
        spdlog::error("[AndroidAssets] Failed to create dir '{}': {}", target_path, ec.message());
        return -1;
    }

    AAssetDir* dir = AAssetManager_openDir(mgr, asset_path.c_str());
    if (!dir) {
        spdlog::error("[AndroidAssets] Failed to open asset dir '{}'", asset_path);
        return -1;
    }

    // List and copy all files in this directory
    const char* filename;
    while ((filename = AAssetDir_getNextFileName(dir)) != nullptr) {
        std::string asset_file =
            asset_path.empty() ? std::string(filename) : asset_path + "/" + filename;
        std::string target_file = target_path + "/" + filename;

        // Preserve user-owned state. settings.json holds the user's printer
        // host, preferences, and customizations. The APK ships a default, but
        // overwriting it on every extraction wipes the user's config on every
        // upgrade (#1245). Skip it when it already exists — first install
        // still gets the shipped default.
        if (asset_path == "config" && std::string(filename) == "settings.json" &&
            fs::exists(target_file)) {
            spdlog::info("[AndroidAssets] Preserving existing {}", target_file);
            continue;
        }

        // Defense-in-depth: never extract runtime artifacts even if they
        // somehow made it into the APK (copyAssets excludes them, but a
        // stale local file could slip through). Crash dumps cause stale
        // crash dialogs; telemetry/spool state is user-owned.
        if (asset_path == "config") {
            std::string fname(filename);
            if (fname.rfind("crash", 0) == 0 || fname == ".crash_restart_count" ||
                fname == "tool_spools.json" || fname == "telemetry_device.json" ||
                fname == "telemetry_queue.json" || fname == "crash_history.json") {
                spdlog::debug("[AndroidAssets] Skipping runtime artifact {}", fname);
                continue;
            }
        }

        AAsset* asset = AAssetManager_open(mgr, asset_file.c_str(), AASSET_MODE_STREAMING);
        if (!asset) {
            spdlog::warn("[AndroidAssets] Could not open asset '{}'", asset_file);
            continue;
        }

        off_t size = AAsset_getLength(asset);
        std::vector<char> buf(size);
        int bytes_read = AAsset_read(asset, buf.data(), size);
        AAsset_close(asset);

        if (bytes_read != size) {
            spdlog::warn("[AndroidAssets] Short read for '{}': {} of {}", asset_file, bytes_read,
                         size);
            continue;
        }

        std::ofstream ofs(target_file, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            spdlog::warn("[AndroidAssets] Could not write '{}'", target_file);
            continue;
        }
        ofs.write(buf.data(), size);
        count++;
    }

    AAssetDir_close(dir);
    return count;
}

// Read MANIFEST.txt from the APK root. The manifest is produced at build time
// by scripts/gen-packaging-manifest.sh and lists every subdirectory (one per
// line, project-relative) that ships in the APK. AAssetDir can only list
// files, not subdirs — the manifest is how the extractor learns the tree.
//
// Returns empty vector on failure; caller must treat that as fatal since
// there is no safe fallback (a hardcoded list is exactly the drift we are
// trying to eliminate).
static std::vector<std::string> read_manifest(AAssetManager* mgr) {
    std::vector<std::string> dirs;
    AAsset* asset = AAssetManager_open(mgr, "MANIFEST.txt", AASSET_MODE_BUFFER);
    if (!asset) {
        spdlog::error("[AndroidAssets] BUILD ERROR: MANIFEST.txt missing from APK. "
                      "Check android/app/build.gradle — the genManifest task must run "
                      "before copyAssets/mergeAssets.");
        return dirs;
    }

    off_t size = AAsset_getLength(asset);
    std::string content(static_cast<size_t>(size), '\0');
    int bytes_read = AAsset_read(asset, content.data(), size);
    AAsset_close(asset);
    if (bytes_read != size) {
        spdlog::error("[AndroidAssets] Short read on MANIFEST.txt: {} of {}", bytes_read, size);
        return dirs;
    }

    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        dirs.push_back(std::move(line));
    }
    return dirs;
}

// Read a small text asset from the APK root (BUILD_STAMP, etc.). Returns an
// empty string if the asset is absent or short-reads; callers treat that as
// "not present" and fall through to extraction.
static std::string read_asset_text(AAssetManager* mgr, const char* name) {
    AAsset* asset = AAssetManager_open(mgr, name, AASSET_MODE_BUFFER);
    if (!asset)
        return {};
    off_t size = AAsset_getLength(asset);
    std::string content(static_cast<size_t>(size), '\0');
    int bytes_read = AAsset_read(asset, content.data(), size);
    AAsset_close(asset);
    if (bytes_read != size)
        return {};
    return content;
}

void android_extract_assets_if_needed() {
    const char* internal_path = SDL_AndroidGetInternalStoragePath();
    if (!internal_path) {
        spdlog::error("[AndroidAssets] Could not get internal storage path from SDL");
        return;
    }

    std::string target_dir = std::string(internal_path) + "/data";
    spdlog::info("[AndroidAssets] Target directory: {}", target_dir);

    AAssetManager* mgr = get_asset_manager();
    if (!mgr) {
        spdlog::error("[AndroidAssets] Could not get AAssetManager, app will lack UI resources");
        setenv("HELIX_DATA_DIR", target_dir.c_str(), 1);
        return;
    }

    // Per-build stamp gate: Gradle writes BUILD_STAMP into the APK assets on
    // every build (scripts/gen-git-hash.sh cannot help here — its header is
    // regenerated at CMake *configure* time, not build time, so the hash goes
    // stale across same-version dev rebuilds). Comparing the APK stamp to the
    // one written at the end of the last extraction makes every install
    // re-extract, so iterating on XML/assets on a connected device always
    // picks up edits, while a stable install still skips the copy at launch.
    // The version-number gate this replaces silently served stale XML for
    // same-version dev rebuilds.
    std::string apk_stamp = read_asset_text(mgr, "BUILD_STAMP");
    while (!apk_stamp.empty() &&
           (apk_stamp.back() == '\n' || apk_stamp.back() == '\r' || apk_stamp.back() == ' '))
        apk_stamp.pop_back();

    fs::path stamp_file = fs::path(target_dir) / "BUILD_STAMP";
    std::string disk_stamp;
    if (fs::exists(stamp_file)) {
        std::ifstream ifs(stamp_file);
        std::getline(ifs, disk_stamp);
    }

    if (!apk_stamp.empty() && apk_stamp == disk_stamp) {
        spdlog::info("[AndroidAssets] Build stamp matches ({}), skipping extraction", disk_stamp);
        setenv("HELIX_DATA_DIR", target_dir.c_str(), 1);
        return;
    }
    spdlog::info("[AndroidAssets] Build stamp differs: apk='{}' disk='{}' - re-extracting",
                 apk_stamp, disk_stamp);

    // Remove pre-split stale seeds under {target_dir}/config/. Before bfeba7c26
    // these paths held the shipped RO seeds; after the split they moved to
    // assets/config/, but an upgrade-in-place leaves the old copies on disk
    // where find_readable() would return them first and shadow the new seeds.
    // Safe to delete: all entries here are RO shipped content, not user state.
    {
        std::error_code ec;
        const std::string cfg = target_dir + "/config";
        for (const char* stale : {"printer_database.json", "printing_tips.json",
                                  "default_layout.json", "helix_macros.cfg"}) {
            fs::remove(cfg + "/" + stale, ec);
        }
        for (const char* stale_dir :
             {"presets", "print_start_profiles", "platform", "sounds", "themes/defaults"}) {
            fs::remove_all(cfg + "/" + stale_dir, ec);
        }
    }

    // Walk MANIFEST.txt — every directory in the APK, one per line, in a stable
    // sort order. For each entry, extract the files it contains (AAssetDir can
    // only enumerate files, not subdirs, which is why the manifest exists).
    // Adding a new source-tree directory doesn't require touching this code:
    // the build-time script regenerates the manifest automatically.
    std::vector<std::string> manifest = read_manifest(mgr);
    if (manifest.empty()) {
        spdlog::error("[AndroidAssets] No manifest — aborting extraction. App will lack "
                      "UI resources (printer database, themes, presets, etc.).");
        setenv("HELIX_DATA_DIR", target_dir.c_str(), 1);
        return;
    }

    int total = 0;
    for (const std::string& rel : manifest) {
        int n = extract_asset_dir(mgr, rel, target_dir + "/" + rel);
        if (n > 0)
            total += n;
    }
    spdlog::info("[AndroidAssets] Total: {} files extracted across {} dirs to '{}'", total,
                 manifest.size(), target_dir);

    // Write markers: VERSION (informational) and BUILD_STAMP (the gate).
    {
        std::error_code ec;
        fs::create_directories(target_dir, ec);
        std::ofstream vofs(fs::path(target_dir) / "VERSION", std::ios::trunc);
        if (vofs)
            vofs << helix_version();
        std::ofstream sofs(stamp_file, std::ios::trunc);
        if (sofs)
            sofs << apk_stamp;
    }

    // Set HELIX_DATA_DIR so ensure_project_root_cwd() chdir's here
    setenv("HELIX_DATA_DIR", target_dir.c_str(), 1);
    spdlog::info("[AndroidAssets] Set HELIX_DATA_DIR={}", target_dir);
}

#endif // __ANDROID__

} // namespace helix
