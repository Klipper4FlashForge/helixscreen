// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "moonraker_api.h"
#include "thumbnail_load_context.h"
#include "thumbnail_processor.h"
#include "thumbnail_write_journal.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/**
 * @file thumbnail_cache.h
 * @brief Centralized thumbnail caching for print files and history
 *
 * ThumbnailCache provides a unified approach to downloading and caching
 * thumbnail images from Moonraker. It handles:
 * - Hash-based filename generation for cache files
 * - Cache directory creation
 * - Async download with callbacks
 * - LVGL-compatible path formatting ("A:" prefix)
 *
 * ## Usage Example
 * ```cpp
 * ThumbnailCache cache;
 *
 * // Check if already cached (sync)
 * std::string lvgl_path = cache.get_if_cached(relative_path);
 * if (!lvgl_path.empty()) {
 *     lv_image_set_src(img, lvgl_path.c_str());
 *     return;
 * }
 *
 * // Download async
 * cache.fetch(api_, relative_path,
 *     [this](const std::string& lvgl_path) {
 *         // Update UI on main thread
 *         lv_image_set_src(img, lvgl_path.c_str());
 *     },
 *     [](const std::string& error) {
 *         spdlog::warn("Thumbnail download failed: {}", error);
 *     });
 * ```
 *
 * @see MoonrakerAPI::download_thumbnail
 */
class ThumbnailCache {
  public:
    /// Default cache subdirectory name (appended to base cache dir)
    static constexpr const char* CACHE_SUBDIR = "helix_thumbs";

    /// Minimum cache size (5 MB) - floor for very constrained systems
    static constexpr size_t MIN_CACHE_SIZE = 5 * 1024 * 1024;

    /// Default maximum cache size (20 MB) - conservative for AD5M, override via config
    static constexpr size_t DEFAULT_MAX_CACHE_SIZE = 20 * 1024 * 1024;

    /// Default percentage of available disk space to use for cache
    static constexpr double DEFAULT_DISK_PERCENT = 0.05; // 5%

    /// Default critical disk threshold (5 MB) - conservative for AD5M
    static constexpr size_t DEFAULT_DISK_CRITICAL = 5 * 1024 * 1024;

    /// Default low disk threshold (20 MB) - conservative for AD5M
    static constexpr size_t DEFAULT_DISK_LOW = 20 * 1024 * 1024;

    /// Callback for successful thumbnail fetch (receives LVGL-ready path with "A:" prefix)
    using SuccessCallback = std::function<void(const std::string& lvgl_path)>;

    /// Callback for failed thumbnail fetch (receives error message)
    using ErrorCallback = std::function<void(const std::string& error)>;

    /**
     * @brief Default constructor - auto-sizes based on available disk space
     *
     * Creates cache directory if it doesn't exist.
     * Cache size is calculated as:
     *   clamp(available_space * 5%, MIN_CACHE_SIZE, MAX_CACHE_SIZE)
     */
    ThumbnailCache();

    /**
     * @brief Constructor with explicit max size (for testing)
     *
     * @param max_size Maximum cache size in bytes
     */
    explicit ThumbnailCache(size_t max_size);

    /**
     * @brief Get the current cache directory path
     *
     * @return Absolute path to cache directory (e.g., "/home/user/.cache/helix_thumbs")
     */
    [[nodiscard]] std::string get_cache_dir() const {
        return cache_dir_; // Return by value for thread safety
    }

    /**
     * @brief Compute the local cache path for a relative Moonraker path
     *
     * Uses hash-based filename: `{cache_dir}/{hash}.png`
     *
     * @param relative_path Moonraker relative path (e.g., ".thumbnails/file.png")
     * @return Local filesystem path for the cached file
     */
    [[nodiscard]] std::string get_cache_path(const std::string& relative_path) const;

    /**
     * @brief Get LVGL path if thumbnail is already cached
     *
     * Checks if the file exists locally without network request.
     * Useful for instant display when revisiting cached content.
     *
     * @param relative_path Moonraker relative path
     * @param source_modified Optional source file modification time (Unix timestamp).
     *        If provided and the cached file is older than this, the cache is
     *        invalidated and empty string is returned. Use 0 to skip validation.
     * @return LVGL-ready path ("A:{cache_dir}/...") if cached, empty string otherwise
     */
    [[nodiscard]] std::string get_if_cached(const std::string& relative_path,
                                            time_t source_modified = 0) const;

    /**
     * @brief Check if a path is already in LVGL format
     *
     * @param path Path to check
     * @return true if path starts with "A:" (already processed)
     */
    [[nodiscard]] static bool is_lvgl_path(const std::string& path);

    /**
     * @brief Convert a local filesystem path to LVGL format
     *
     * @param local_path Local filesystem path
     * @return LVGL-ready path with "A:" prefix
     */
    [[nodiscard]] static std::string to_lvgl_path(const std::string& local_path);

    /**
     * @brief Fetch thumbnail, downloading if not cached
     *
     * This is the main async entry point. It:
     * 1. Checks if already cached (returns immediately if so)
     * 2. Downloads from Moonraker if not cached
     * 3. Calls success callback with LVGL-ready path
     *
     * @param api MoonrakerAPI instance for downloading
     * @param relative_path Moonraker relative path (e.g., ".thumbnails/file.png")
     * @param on_success Called with LVGL path on success (may be called synchronously if cached)
     * @param on_error Called with error message on failure
     *
     * @note Callbacks may be invoked from background thread - use ui_queue_update() for UI updates
     */
    void fetch(MoonrakerAPI* api, const std::string& relative_path, SuccessCallback on_success,
               ErrorCallback on_error);

    /**
     * @brief Fetch thumbnail with pre-scaling optimization
     *
     * Similar to fetch(), but produces pre-scaled LVGL binary files (.bin) for
     * optimal display performance. Pre-scaled thumbnails render instantly without
     * runtime scaling overhead.
     *
     * Flow:
     * 1. Check for pre-scaled .bin (instant return if found)
     * 2. Check for cached PNG (queue for background pre-scaling)
     * 3. Download PNG if needed, then pre-scale
     * 4. Return .bin path on success
     *
     * @param api MoonrakerAPI instance for downloading
     * @param relative_path Moonraker relative path (e.g., ".thumbnails/file.png")
     * @param target Target dimensions for pre-scaling (from
     * ThumbnailProcessor::get_target_for_display())
     * @param on_success Called with LVGL path to .bin on success
     * @param on_error Called with error message on failure
     * @param source_modified Optional source file modification time (Unix timestamp).
     *        If provided and the cached file is older than this, the cache is
     *        invalidated and a fresh download is triggered. Use 0 to skip validation.
     *
     * @note Falls back to PNG on pre-scaling failure - display still works, just slower
     * @see docs/THUMBNAIL_OPTIMIZATION_PLAN.md
     */
    void fetch_optimized(MoonrakerAPI* api, const std::string& relative_path,
                         const helix::ThumbnailTarget& target, SuccessCallback on_success,
                         ErrorCallback on_error, time_t source_modified = 0);

    /**
     * @brief Check if a pre-scaled version exists in cache
     *
     * Fast synchronous lookup for pre-scaled .bin files.
     * Does not trigger download or processing.
     *
     * @param relative_path Moonraker relative path
     * @param target Target dimensions to check for
     * @param source_modified Optional source file modification time (Unix timestamp).
     *        If provided and the cached file is older than this, the cache is
     *        invalidated and empty string is returned. Use 0 to skip validation.
     * @return LVGL path (A:/...) to .bin if cached and fresh, empty string otherwise
     */
    [[nodiscard]] std::string get_if_optimized(const std::string& relative_path,
                                               const helix::ThumbnailTarget& target,
                                               time_t source_modified = 0) const;

    // =========================================================================
    // HIGH-LEVEL SEMANTIC METHODS
    // =========================================================================
    // These methods encode the correct strategy for each use case, preventing
    // accidental use of the wrong format (e.g., using pre-scaled .bin for a
    // detail view where full PNG quality is needed).

    /**
     * @brief Fetch thumbnail for a detail/large view (pre-scaled .bin at detail size)
     *
     * Use this for:
     * - Print Status panel thumbnail
     * - Print File Detail view
     * - Any large thumbnail display
     *
     * Internally uses fetch_optimized() with ThumbnailSize::Detail for larger
     * pre-scaled targets (200–400px) than card views.
     *
     * The success callback is automatically guarded by ctx.is_valid() - it will
     * only be invoked if the caller is still alive and the generation matches.
     * This eliminates the need for manual validity checks in each callback.
     *
     * @param api MoonrakerAPI instance for downloading
     * @param relative_path Moonraker relative path (e.g., ".thumbnails/file.png")
     * @param ctx Async safety context (created via ThumbnailLoadContext::create())
     * @param on_success Called with LVGL path on success (only if ctx.is_valid())
     * @param on_error Optional error callback (NOT guarded - always called on error)
     *
     * @note Callbacks may be invoked from background thread - use ui_queue_update() for UI updates
     * @see ThumbnailLoadContext::create
     */
    void fetch_for_detail_view(MoonrakerAPI* api, const std::string& relative_path,
                               ThumbnailLoadContext ctx, SuccessCallback on_success,
                               ErrorCallback on_error = nullptr);

    /**
     * @brief Fetch thumbnail for a card/small view (pre-scaled .bin for speed)
     *
     * Use this for:
     * - Print Select file cards
     * - History list items
     * - Any small thumbnail where rendering speed matters more than quality
     *
     * Internally uses fetch_optimized() with display-appropriate dimensions.
     *
     * The success callback is automatically guarded by ctx.is_valid() - it will
     * only be invoked if the caller is still alive and the generation matches.
     * This eliminates the need for manual validity checks in each callback.
     *
     * @param api MoonrakerAPI instance for downloading
     * @param relative_path Moonraker relative path (e.g., ".thumbnails/file.png")
     * @param ctx Async safety context (created via ThumbnailLoadContext::create())
     * @param on_success Called with LVGL path on success (only if ctx.is_valid())
     * @param on_error Optional error callback (NOT guarded - always called on error)
     * @param source_modified Optional source file modification time for cache invalidation
     *
     * @note Callbacks may be invoked from background thread - use ui_queue_update() for UI updates
     * @see ThumbnailLoadContext::create
     */
    void fetch_for_card_view(MoonrakerAPI* api, const std::string& relative_path,
                             ThumbnailLoadContext ctx, SuccessCallback on_success,
                             ErrorCallback on_error = nullptr, time_t source_modified = 0);

    /**
     * @brief Save raw PNG data directly to cache
     *
     * Saves decoded PNG bytes (e.g., from base64-encoded gcode thumbnails)
     * directly to the cache. The source_identifier is hashed to generate the
     * cache filename, same as thumbnails downloaded from Moonraker.
     *
     * Use this when thumbnail data is extracted from gcode files instead of
     * downloaded via Moonraker's HTTP API (e.g., USB files where Moonraker
     * can't write .thumbs directory).
     *
     * @param source_identifier Unique identifier for this thumbnail (typically
     *        the relative_path that would be used with fetch(), e.g., "usb/file.gcode")
     * @param png_data Raw PNG bytes (must be valid PNG with magic header)
     * @return LVGL path ("A:...") to saved file, or empty string on failure
     *
     * @note Validates PNG magic bytes before saving
     * @note Triggers cache eviction if needed after saving
     */
    std::string save_raw_png(const std::string& source_identifier,
                             const std::vector<uint8_t>& png_data);

    /**
     * @brief Clear all cached thumbnails
     *
     * Removes all files from the cache directory.
     * Useful for testing or manual cache invalidation.
     *
     * @return Number of files removed
     */
    size_t clear_cache();

    /**
     * @brief Invalidate cached thumbnails for a specific file
     *
     * Removes PNG and all pre-scaled .bin variants for the given path.
     * Call this when a G-code file is overwritten with new content.
     *
     * @param relative_path Moonraker relative path (e.g., ".thumbnails/file.png")
     * @return Number of files removed
     */
    size_t invalidate(const std::string& relative_path);

    /**
     * @brief Get the total size of cached thumbnails
     *
     * @return Total size in bytes
     */
    [[nodiscard]] size_t get_cache_size() const;

    /**
     * @brief Get the maximum cache size
     *
     * @return Maximum cache size in bytes
     */
    [[nodiscard]] size_t get_max_size() const;

    /**
     * @brief Set maximum cache size
     *
     * If new size is smaller than current cache, eviction will occur.
     *
     * @param max_size New maximum size in bytes
     */
    void set_max_size(size_t max_size);

    /**
     * @brief Disk pressure levels for adaptive cache management
     */
    enum class DiskPressure {
        Normal,  ///< Plenty of space - normal caching behavior
        Low,     ///< Below DISK_LOW_THRESHOLD - evict aggressively
        Critical ///< Below DISK_CRITICAL_THRESHOLD - skip caching entirely
    };

    /**
     * @brief Check current disk pressure level
     *
     * Queries available disk space and returns appropriate pressure level.
     * Used to adapt caching behavior to real-time disk conditions.
     *
     * @return Current disk pressure level
     */
    [[nodiscard]] DiskPressure get_disk_pressure() const;

    /**
     * @brief Get available disk space in bytes
     *
     * @return Available bytes, or 0 on error
     */
    [[nodiscard]] size_t get_available_disk_space() const;

    /**
     * @brief Check if caching is currently allowed
     *
     * Returns false when disk is critically low to prevent filling up the filesystem.
     *
     * @return true if caching is allowed, false if disk is critical
     */
    [[nodiscard]] bool is_caching_allowed() const;

    /**
     * @brief Diagnostics: full cache-directory walks performed by this instance
     *
     * Every walk is O(files in cache) `readdir` + `stat` syscalls, so this is the
     * quantity the in-memory index exists to keep flat. It was the missing signal
     * in prestonbrown/helixscreen#1207 — the per-fetch cost was invisible because
     * nothing counted it.
     *
     * @return Monotonic count of directory walks since construction
     */
    [[nodiscard]] size_t get_full_scan_count() const {
        return full_scans_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Diagnostics: per-file metadata queries issued by this instance
     *
     * One per file examined, whether during a full walk or a single-file index
     * update. (Each examined file costs up to three `stat`-family calls —
     * `is_regular_file`, `last_write_time`, `file_size` — so this is a lower
     * bound on syscalls, not an exact one.)
     *
     * @return Monotonic count of files stat'd since construction
     */
    [[nodiscard]] size_t get_stat_count() const {
        return stat_calls_.load(std::memory_order_relaxed);
    }

  private:
    std::string cache_dir_; ///< Absolute path to cache directory (const after construction)
    size_t max_size_;       ///< Maximum cache size before LRU eviction — guarded by mutex_
    size_t disk_critical_;  ///< Stop caching below this available space (const after construction)
    size_t disk_low_;       ///< Evict aggressively below this available space (const after ctor)
    size_t configured_max_; ///< Max size from config, before dynamic sizing (const after ctor)

    /// Serializes cache accounting: the directory scan, the eviction pass, and
    /// max_size_.
    ///
    /// evict_if_needed() runs on the main thread AND on every HttpExecutor worker
    /// — MoonrakerFileTransferAPI invokes download_thumbnail's success callback on
    /// the pool thread, unmarshalled. Measured: 104 eviction events across 5
    /// threads in 90s, up to 4 in the same millisecond, each independently
    /// stat'ing every file and selecting the same victims
    /// (prestonbrown/helixscreen#1207).
    ///
    /// Held across scan-sort-remove, so a scan cannot observe a directory another
    /// thread is halfway through unlinking. That is what made the walks throw
    /// mid-iteration and silently abandon the pass.
    mutable std::mutex mutex_;

    /// One cached file, as seen by a directory scan.
    struct CacheEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type mtime;
        std::uintmax_t size; ///< Matches std::filesystem::file_size()'s return type
    };

    /**
     * @brief Walk the cache directory once, collecting every readable entry.
     *
     * Per-entry failures are skipped rather than fatal. A single un-stat'able
     * file (concurrently unlinked, a broken link, a permission change) must cost
     * only that file — wrapping the whole loop in one try/catch returned the
     * partial sum accumulated before the throw, which reads as "the cache is
     * smaller than it is" and silently disables eviction.
     *
     * @param total_out Receives the summed size of all readable entries.
     * @return Entries in directory order (unsorted).
     * @pre mutex_ is held.
     */
    [[nodiscard]] std::vector<CacheEntry> scan_locked(size_t* total_out) const;

    // =========================================================================
    // In-memory index (prestonbrown/helixscreen#1207, defect 2)
    // =========================================================================
    //
    // Before this, every fetch()/fetch_optimized()/save_raw_png() walked the
    // whole cache directory with a stat per file, whether or not anything got
    // evicted. Scrolling 850 files x 3 thumbnails made that a per-scroll-tick
    // cost, much of it on the main thread.
    //
    // The index turns the steady-state check into arithmetic. What makes it
    // *honest* rather than merely fast is having an answer for each way it can
    // drift from the directory it describes:
    //
    //   1. Files this cache writes itself (downloaded PNGs, save_raw_png) —
    //      recorded at the point of writing, one stat each.
    //   2. Files ThumbnailProcessor writes (the pre-scaled .bin variants) —
    //      reported through journal_, one stat each. This is the case that made
    //      an index non-trivial: the processor writes into this directory and
    //      holds no reference to this class.
    //   3. Files that vanish behind our back — the eviction loop already
    //      distinguishes "removed" from "was already gone", so a ghost costs no
    //      credited bytes, and seeing one triggers a resync.
    //   4. Anything else, including a directory populated by a previous run —
    //      covered by the cold-start scan and by a periodic reconcile.
    //
    // The index is allowed to over-count (it evicts too eagerly, which is
    // survivable) but must never under-count, because an under-counting cache
    // reads as "well under the limit" and stops evicting altogether.

    /// One indexed file. Same fields a directory walk would produce.
    struct IndexEntry {
        std::filesystem::file_time_type mtime;
        std::uintmax_t size;
    };

    /// Eviction checks between forced full rescans.
    ///
    /// The backstop for drift this class cannot observe — a future writer that
    /// forgets to report, an external delete, a shell. Bounds how long the index
    /// may disagree with the directory, at a cost of one walk per this many
    /// checks instead of one per check. Ordered map, so a rescan is also what
    /// re-establishes exact totals after any anomaly.
    static constexpr size_t INDEX_RECONCILE_INTERVAL = 64;

    /// Rebuild the index from the filesystem. The only O(cache) path left.
    /// @pre mutex_ is held.
    void rescan_locked() const;

    /// Bring the index up to date cheaply: absorb journalled writes, or rescan
    /// if the index is cold, overflowed, or due for reconciliation.
    /// @pre mutex_ is held.
    void refresh_index_locked() const;

    /// Stat one file and fold it into the index. Rejects paths outside
    /// cache_dir_ — set_cache_dir() on the processor can retarget it while a
    /// write is in flight, and a stale journal entry must not add a foreign
    /// file to this cache's accounting.
    /// @pre mutex_ is held.
    void index_file_locked(const std::filesystem::path& raw_path) const;

    /// Drop a path from the index, crediting its bytes back.
    /// @pre mutex_ is held.
    void forget_file_locked(const std::filesystem::path& path) const;

    /// Record a file this cache just wrote, then run an eviction check. The
    /// shape every write site uses so no write can reach eviction unindexed.
    void note_write_and_evict(const std::string& path);

    /**
     * @brief Eviction pass proper.
     * @pre mutex_ is held.
     */
    void evict_locked();

    /// Diagnostics — see get_full_scan_count() / get_stat_count(). Atomic rather
    /// than mutex_-guarded so the accessors stay lock-free and cannot deadlock a
    /// caller that already holds the lock.
    mutable std::atomic<size_t> full_scans_{0};
    mutable std::atomic<size_t> stat_calls_{0};

    /// The index and its bookkeeping. All guarded by mutex_; mutable because
    /// get_cache_size() is const and still has to prime and reconcile.
    mutable std::map<std::filesystem::path, IndexEntry> index_;
    mutable size_t index_total_ = 0;
    mutable bool index_primed_ = false;
    mutable size_t checks_since_scan_ = 0;

    /// Owned here, observed weakly by ThumbnailProcessor, so destroying this
    /// cache cannot leave the processor holding a dangling pointer. Created
    /// eagerly: refresh_index_locked() must never have to null-check it.
    std::shared_ptr<helix::ThumbnailWriteJournal> journal_{
        std::make_shared<helix::ThumbnailWriteJournal>()};

    /**
     * @brief Determine the optimal cache base directory
     *
     * Selection order:
     * 1. Config setting /cache/directory if specified
     * 2. XDG_CACHE_HOME environment variable + "/helix"
     * 3. $HOME/.cache/helix (if HOME is set and writable)
     * 4. /tmp/helix_cache (fallback for systems without home dir)
     *
     * @return Absolute path to the cache base directory (not including CACHE_SUBDIR)
     */
    static std::string determine_cache_dir();

    /**
     * @brief Load cache settings from settings.json
     *
     * Reads cache/thumbnail_max_mb, cache/disk_critical_mb, cache/disk_low_mb.
     * Falls back to defaults if config not available.
     */
    void load_config();

    /**
     * @brief Ensure cache directory exists
     */
    void ensure_cache_dir() const;

    /**
     * @brief Compute hash for a path string
     *
     * @param path Path to hash
     * @return Hash value as string
     */
    [[nodiscard]] static std::string compute_hash(const std::string& path);

    /**
     * @brief Evict oldest files if cache exceeds max size
     *
     * Uses file modification time (mtime) as LRU approximation.
     * Removes oldest files until cache is under max_size_.
     */
    void evict_if_needed();

    /**
     * @brief Process PNG and invoke callback with result
     *
     * Helper for fetch_optimized(). Reads PNG, queues for pre-scaling,
     * and invokes callback with .bin path on success or PNG fallback on error.
     *
     * @param png_lvgl_path LVGL path to the cached PNG
     * @param source_path Original Moonraker relative path (for cache key)
     * @param target Target dimensions for pre-scaling
     * @param on_success Success callback
     * @param on_error Error callback (not currently used - fallback to PNG instead)
     */
    void process_and_callback(const std::string& png_lvgl_path, const std::string& source_path,
                              const helix::ThumbnailTarget& target, SuccessCallback on_success,
                              ErrorCallback on_error);
};

/**
 * @brief Global singleton accessor
 *
 * Provides a single shared cache instance for the application.
 *
 * @return Reference to the global ThumbnailCache
 */
ThumbnailCache& get_thumbnail_cache();
