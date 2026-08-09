// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace helix {

/// Where a Moonraker config file lives relative to the uploadable file-manager root.
///
/// Moonraker's file API root "config" maps to `<data_path>/config`. On some stock
/// firmwares (e.g. Creality K2) the loaded `moonraker.conf` lives entirely outside
/// `data_path`, so an upload through the file API writes a file Moonraker never reads.
struct ConfigPathInfo {
    /// true when the config file is reachable through the file API's "config" root.
    bool uploadable = false;
    /// Subdirectory under the "config" root ("" when the file sits at the root).
    std::string upload_subdir;
    /// Base name of the config file (e.g. "moonraker.conf").
    std::string config_filename;
    /// Human-readable explanation when uploadable == false.
    std::string error;

    /// Build a file-API path for a sibling of the config file (e.g. "helixscreen.conf").
    std::string path_for(const std::string& filename) const {
        return upload_subdir.empty() ? filename : upload_subdir + "/" + filename;
    }
};

/// One entry of server.config's `files[]` array: a config file Moonraker actually
/// loaded, plus the sections it defines.
///
/// `filename` is normally relative to Moonraker's config root (e.g. "moonraker.conf"),
/// which is exactly what the file API's "config" root addresses. Some builds report an
/// absolute path instead.
struct LoadedConfigFile {
    std::string filename;
    std::vector<std::string> sections;
};

class MoonrakerConfigManager {
  public:
    static bool has_section(const std::string& content, const std::string& section_name);

    /// Names of every section defined in `content`, in order of appearance.
    static std::vector<std::string> list_sections(const std::string& content);

    /// True when every section named in `required` is defined in `content`.
    ///
    /// Deliberately a subset test, not equality: a reachable config file may legitimately
    /// carry sections Moonraker does not report back (notably the `[include ...]` line
    /// HelixScreen adds), but it can never be missing one that Moonraker says it loaded.
    static bool defines_all_sections(const std::string& content,
                                     const std::vector<std::string>& required);

    /// Index of the root config file within a server.config `files[]` list.
    ///
    /// Moonraker's root config is the one defining `[server]`; if no entry claims it,
    /// fall back to the first, which is the order Moonraker reports. Returns -1 when the
    /// list is empty or holds no usable filename.
    static int select_primary_config_index(const std::vector<LoadedConfigFile>& files);

    /// Indices of loaded config files that define `section_name`.
    ///
    /// Because this walks the files Moonraker actually loaded, a helixscreen.conf
    /// pulled in by an `[include]` from a previous run is counted just like a natively
    /// defined section — which is exactly what makes duplicate detection work.
    static std::vector<size_t>
    find_files_defining_section(const std::vector<LoadedConfigFile>& files,
                                const std::string& section_name);

    /// Build a ConfigPathInfo from a config-root-relative filename reported by files[].
    ///
    /// Rejects absolute paths and paths escaping the root — those cannot be addressed
    /// through the file API's "config" root.
    static ConfigPathInfo config_path_from_relative(const std::string& relative_filename);

    /// Append a section if it is not already present.
    ///
    /// Idempotent by design: when the section exists the content is returned unchanged.
    /// Callers that need existing keys refreshed must use upsert_section() instead.
    static std::string add_section(const std::string& content, const std::string& section_name,
                                   const std::vector<std::pair<std::string, std::string>>& entries,
                                   const std::string& comment = "");

    /// Add the section when missing, or update the given keys in place when it exists.
    ///
    /// Keys already present in the section have their values replaced; keys absent from
    /// the section are appended to it. Keys in the section that are not listed in
    /// `entries` are preserved, as are comments, blank lines and every other section.
    static std::string
    upsert_section(const std::string& content, const std::string& section_name,
                   const std::vector<std::pair<std::string, std::string>>& entries,
                   const std::string& comment = "");

    /// Determine whether `config_file_abs` (Moonraker's authoritative loaded config path)
    /// is writable through the file API, given Moonraker's `data_path`.
    static ConfigPathInfo resolve_config_upload_location(const std::string& config_file_abs,
                                                         const std::string& data_path);

    static std::string remove_section(const std::string& content, const std::string& section_name);
    static bool has_include_line(const std::string& moonraker_content);
    static std::string add_include_line(const std::string& moonraker_content);
    static std::string get_section_value(const std::string& content,
                                         const std::string& section_name, const std::string& key);
};
} // namespace helix
