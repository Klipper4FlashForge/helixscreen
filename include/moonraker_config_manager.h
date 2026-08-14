// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
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

/// How well a config file's contents line up with the sections Moonraker reported.
///
/// Moonraker serves the section list it parsed when it last started, so a file edited
/// since then legitimately disagrees with it. Only a wholesale disagreement means the
/// file is not the one Moonraker loaded.
enum class SectionMatch {
    Match,    ///< every reported section is present
    Drifted,  ///< a majority are present: the loaded file, edited since the last restart
    Mismatch, ///< too few present to be the file Moonraker loaded
};

struct SectionMatchResult {
    SectionMatch verdict = SectionMatch::Mismatch;
    size_t matched = 0;
    size_t total = 0;
    /// Reported sections absent from the content, in the order they were reported.
    std::vector<std::string> missing;
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

    /// Grade `content` against the section list Moonraker reported for it.
    ///
    /// The strict superset test above cannot tell "this is a different file" from
    /// "this is the file, edited since Moonraker last restarted" — and the latter is
    /// ordinary: uninstalling HelixScreen removes `[update_manager helixscreen]` from
    /// moonraker.conf while a long-running Moonraker keeps reporting it. A strict
    /// majority of matching sections is treated as drift and is safe to proceed on;
    /// anything less is a genuinely different file.
    ///
    /// An empty `required` yields Match vacuously — callers that need a section list
    /// to prove anything must guard that case themselves.
    static SectionMatchResult classify_section_match(const std::string& content,
                                                     const std::vector<std::string>& required);

    /// Index of the file best able to *prove* the config root is addressable.
    ///
    /// This is the entry defining `[server]`, chosen for its rich section list: the
    /// spoolman flow downloads it through the file API and checks it still defines
    /// every section Moonraker reported, which is how an unreachable config is caught
    /// without an absolute path (stock Creality K2). If no entry claims `[server]`,
    /// fall back to the first, which is the order Moonraker reports. Returns -1 when
    /// the list is empty or holds no usable filename.
    ///
    /// This is deliberately NOT the file to write to — see select_root_config_index().
    static int select_primary_config_index(const std::vector<LoadedConfigFile>& files);

    /// Index of the user-editable root config within a server.config `files[]` list.
    ///
    /// Moonraker reports its config chain root-first, then in include order, so the
    /// first usable entry is the file it was pointed at. Verified against six
    /// firmwares (Raspberry Pi, BTT CB1, Creality K2 and SonicPad, Flashforge AD5M,
    /// Snapmaker U1, Elegoo COSMOS) on 2026-08-09.
    ///
    /// This is the write target. It differs from select_primary_config_index() only
    /// when the root does not itself define `[server]`, which is exactly the COSMOS
    /// case: there the root holds nothing but includes and `[server]` lives in a
    /// vendor directory the firmware replaces on upgrade, so a section written there
    /// is lost (#1242). Returns -1 when no entry has a usable filename.
    static int select_root_config_index(const std::vector<LoadedConfigFile>& files);

    /// Indices of loaded config files that define `section_name`.
    ///
    /// Because this walks the files Moonraker actually loaded, a helixscreen.conf
    /// pulled in by an `[include]` from a previous run is counted just like a natively
    /// defined section — which is exactly what makes duplicate detection work.
    static std::vector<size_t>
    find_files_defining_section(const std::vector<LoadedConfigFile>& files,
                                const std::string& section_name);

    /// Build a ConfigPathInfo from a filename reported by files[].
    ///
    /// Moonraker names each loaded file relative to the ROOT config file's parent
    /// directory, falling back to the full absolute path when the file lies outside it.
    /// An absolute name is therefore not automatically out of reach: when it sits under
    /// the file manager's writable `config` root it is addressable through the file API.
    /// Pass that root as `config_root_abs` to have the prefix stripped (on a path
    /// component boundary, trailing slash optional); with no root supplied an absolute
    /// name is rejected, as there is nothing to judge it against.
    ///
    /// Paths escaping the root via `..` are always rejected — the file API rejects them
    /// and so do we.
    static ConfigPathInfo config_path_from_relative(const std::string& filename,
                                                    const std::string& config_root_abs = "");

    /// Candidate config-root-relative paths for a filename Moonraker reported,
    /// most trustworthy first. Each must still be proved by content before use.
    ///
    /// server.config names files relative to the ROOT config's parent directory,
    /// falling back to an absolute path for anything outside it; the file API
    /// addresses files relative to the file manager's "config" root. Those are the
    /// same directory on a stock install and different ones on real firmware, so
    /// the reported name cannot be handed to server.files.* unexamined. In order:
    ///
    ///   1. Already relative — the file API takes it as-is (K1, U1, K2).
    ///   2. Absolute and under `config_root_abs` — strip the prefix, on a path
    ///      component boundary so ".../config" cannot swallow ".../config_backup".
    ///   3. Absolute and outside it — SPECULATE from the tail: the part after the
    ///      last "config/" component, then the bare basename. Flashforge AD5M
    ///      reports /root/printer_data/config/... while its file manager root is
    ///      /opt/config, and the same files are served under the root by their
    ///      tail (the two trees are linked). A path test alone would reject a
    ///      perfectly writable config.
    ///
    /// A speculative candidate is safe because nothing is written on the strength
    /// of a path: verify_config_reachable() downloads the candidate and grades it
    /// with classify_section_match() against the sections Moonraker reported. A
    /// coincidentally-named stray file fails that and is refused.
    ///
    /// Any `..` anywhere in the reported name discards it entirely rather than
    /// being sanitised, so a traversal can never reach a write target.
    static std::vector<std::string> candidate_config_paths(const std::string& reported_filename,
                                                           const std::string& config_root_abs = "");

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

    /// True when `moonraker_content` already includes exactly `include_target`.
    ///
    /// The comparison is literal, and deliberately so: Moonraker resolves a relative
    /// include against the *including* file's directory, so "helixscreen.conf" in a
    /// vendor moonraker.conf under /usr/share names a different file than the
    /// absolute path to the one under the writable config root. Treating them as
    /// interchangeable would leave the real include unwritten (Creality K2).
    static bool has_include_line(const std::string& moonraker_content,
                                 const std::string& include_target = "helixscreen.conf");

    /// Insert `[include <include_target>]` before the first section, if absent.
    static std::string add_include_line(const std::string& moonraker_content,
                                        const std::string& include_target = "helixscreen.conf");
    static std::string get_section_value(const std::string& content,
                                         const std::string& section_name, const std::string& key);
};
} // namespace helix
