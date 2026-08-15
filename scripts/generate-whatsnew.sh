#!/usr/bin/env bash
# Extract the current version's CHANGELOG section into a Play Store "What's new"
# file: android/fastlane/metadata/android/en-US/changelogs/<versionCode>.txt
#
# The Play Store "What's new" field is per-versionCode and capped at 500 chars
# (UTF-16 code units). This script extracts the section from CHANGELOG.md for
# the current VERSION.txt, strips markdown headings/bullet markup, and
# truncates on a sentence boundary when over the cap.
#
# Usage:
#   scripts/generate-whatsnew.sh                    # writes to default path
#   scripts/generate-whatsnew.sh /tmp/whatsnew.txt  # writes to given path
#
# Exit 0 on success, non-zero if CHANGELOG has no entry for the version.

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(tr -d '[:space:]' < "$repo_root/VERSION.txt")"

# Compute versionCode = major*1000000 + minor*1000 + patch (matches build.gradle).
IFS='.' read -r v_major v_minor v_patch_raw <<< "$version"
v_patch="${v_patch_raw%%-*}"  # strip pre-release suffix (e.g. 0-beta → 0)
if [ "$v_minor" -gt 999 ] || [ "$v_patch" -gt 999 ]; then
    echo "error: version '$version' overflows the versionCode lanes (minor and patch" >&2
    echo "       must each stay under 1000); widen the packing here and in" >&2
    echo "       android/app/build.gradle together." >&2
    exit 1
fi
version_code=$(( v_major * 1000000 + v_minor * 1000 + v_patch ))

out_path="${1:-$repo_root/android/fastlane/metadata/android/en-US/changelogs/$version_code.txt}"
mkdir -p "$(dirname "$out_path")"

# Extract the section for this version from CHANGELOG.md.
# Matches a "## [version]" heading and prints lines until the next "## [" heading.
section="$(awk -v ver="$version" '
    /^## \[/ {
        if (found) exit
        if (index($0, "[" ver "]")) { found=1; next }
        next
    }
    found { print }
' "$repo_root/CHANGELOG.md")"

if [ -z "$(printf '%s' "$section" | tr -d '[:space:]')" ]; then
    echo "error: no CHANGELOG.md entry for version $version" >&2
    exit 1
fi

# Strip markdown: drop "### Added/Fixed/Changed/..." subheadings, flatten
# bullets to "- ", collapse blank-line runs, remove bold/italic/link markup.
cleaned="$(printf '%s' "$section" | awk '
    /^### / { section=substr($0, 5); print ""; print section ":"; next }
    /^[[:space:]]*$/ { print ""; next }
    /^[[:space:]]*-[[:space:]]/ { print; next }
    { print }
' | sed -E '
    s/\*\*([^*]+)\*\*/\1/g
    s/\*([^*]+)\*/\1/g
    s/`([^`]+)`/\1/g
    s/\[([^]]+)\]\([^)]+\)/\1/g
' | awk 'BEGIN{blank=0} /^[[:space:]]*$/ { if (blank) next; blank=1; print; next } { blank=0; print }')"

# Trim leading/trailing blank lines.
cleaned="$(printf '%s' "$cleaned" | sed -e '/./,$!d' | tac | sed -e '/./,$!d' | tac)"

# Truncate to 500 chars on a sentence or line boundary.
limit=500
if [ "${#cleaned}" -gt "$limit" ]; then
    truncated="${cleaned:0:$limit}"
    # Back up to the last sentence or newline boundary we crossed.
    cut_at="$(printf '%s' "$truncated" | awk '
        {
            for (i=length($0); i>0; i--) {
                c = substr($0, i, 1)
                if (c == "." || c == "\n") { print i; exit }
            }
            print length($0)
        }' | head -1)"
    if [ -n "$cut_at" ] && [ "$cut_at" -gt 100 ]; then
        cleaned="${truncated:0:$cut_at}"
    else
        cleaned="${truncated%[[:space:]]*}…"
    fi
fi

printf '%s\n' "$cleaned" > "$out_path"
echo "Wrote $(wc -c < "$out_path") bytes to $out_path (version $version, versionCode $version_code)"
