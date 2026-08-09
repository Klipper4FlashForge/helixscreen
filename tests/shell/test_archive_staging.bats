#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for archive-staging filename logic in scripts/lib/installer/release.sh.
# Covers _archive_tmp_path() (the single source of truth for the staged local
# path), the _ARCHIVE_FORMAT / _ARCHIVE_VERSION / _ARCHIVE_PLATFORM globals, and
# download_release()'s versioned zip_dest/tar_dest naming. The load-bearing
# invariant these pin: the name download_release() writes to must stay identical
# to what extract_release()/use_local_tarball() read back via _archive_tmp_path(),
# and the versioned name must never leak into the fixed server-side fetch URL.

RELEASE_SH="scripts/lib/installer/release.sh"

setup() {
    source tests/shell/helpers.bash
    export GITHUB_REPO="prestonbrown/helixscreen"

    # Reset source guard so the module re-sources cleanly per test.
    unset _HELIX_RELEASE_SOURCED
    source "$RELEASE_SH"

    export TMP_DIR="$BATS_TEST_TMPDIR/tmp"
    export SUDO=""
    export CLEANUP_TMP=""
    export _R2_MANIFEST=""          # skip manifest URL parsing
    # download_release() refetches the channel manifest for its SHA256s when the
    # cache is empty (get_latest_version's copy dies in its own subshell). These
    # tests are about staged filenames, not integrity, and must not hit the
    # network — stub the refetch to "no manifest available".
    _ensure_manifest() { return 1; }
    export R2_BASE_URL="https://cdn.example"
    export HTTP_BASE_URL="http://mirror.example"
    export _DOWNLOAD_HTTP_CODE=""
    mkdir -p "$TMP_DIR"
}

# --- _archive_tmp_path(): pure helper ------------------------------------

@test "_archive_tmp_path: tar.gz format carries platform + version" {
    _ARCHIVE_FORMAT="tar.gz"; _ARCHIVE_PLATFORM="ad5m"; _ARCHIVE_VERSION="v1.2.3"
    run _archive_tmp_path
    [ "$status" -eq 0 ]
    [ "$output" = "$TMP_DIR/helixscreen-ad5m-v1.2.3.tar.gz" ]
}

@test "_archive_tmp_path: zip format carries platform + version" {
    _ARCHIVE_FORMAT="zip"; _ARCHIVE_PLATFORM="pi"; _ARCHIVE_VERSION="v1.2.3"
    run _archive_tmp_path
    [ "$status" -eq 0 ]
    [ "$output" = "$TMP_DIR/helixscreen-pi-v1.2.3.zip" ]
}

@test "_archive_tmp_path: empty globals keep the bare unversioned name" {
    # Only download_release() sets the version/platform; every other path
    # (--local installs, pre-staged tests) must get the plain helixscreen.<ext>.
    unset _ARCHIVE_FORMAT; _ARCHIVE_PLATFORM=""; _ARCHIVE_VERSION=""
    run _archive_tmp_path
    [ "$status" -eq 0 ]
    [ "$output" = "$TMP_DIR/helixscreen.tar.gz" ]
}

@test "_archive_tmp_path: zip format with empty globals is bare helixscreen.zip" {
    _ARCHIVE_FORMAT="zip"; _ARCHIVE_PLATFORM=""; _ARCHIVE_VERSION=""
    run _archive_tmp_path
    [ "$status" -eq 0 ]
    [ "$output" = "$TMP_DIR/helixscreen.zip" ]
}

# --- download_release(): globals, staged name, sync invariant ------------

# Stub the network gate. Records every fetch URL it is handed, and "wins" only
# for the extension named by $WIN_EXT (touching the dest so ls -lh succeeds).
_stub_candidate() {
    _try_download_candidate() {
        printf '%s\n' "$1" >> "$TMP_DIR/urls.log"
        case "$2" in
            *."$WIN_EXT") : > "$2"; return 0 ;;
            *) return 1 ;;
        esac
    }
}

@test "download_release: zip win sets globals and stages at _archive_tmp_path" {
    WIN_EXT=zip; _stub_candidate
    local plat; plat="$(get_release_platform ad5m)"

    download_release "v9.9.9" "ad5m"

    [ "$_ARCHIVE_FORMAT" = "zip" ]
    [ "$_ARCHIVE_VERSION" = "v9.9.9" ]
    [ "$_ARCHIVE_PLATFORM" = "$plat" ]
    # Staged at the versioned local name...
    [ -f "$TMP_DIR/helixscreen-${plat}-v9.9.9.zip" ]
    # ...and that name is byte-identical to what extract_release() reads back.
    [ "$(_archive_tmp_path)" = "$TMP_DIR/helixscreen-${plat}-v9.9.9.zip" ]
    [ -f "$(_archive_tmp_path)" ]
}

@test "download_release: tar.gz fallback stages at versioned tar name" {
    WIN_EXT=tar.gz; _stub_candidate
    local plat; plat="$(get_release_platform ad5m)"

    download_release "v9.9.9" "ad5m"

    [ "$_ARCHIVE_FORMAT" = "tar.gz" ]
    [ -f "$TMP_DIR/helixscreen-${plat}-v9.9.9.tar.gz" ]
    [ "$(_archive_tmp_path)" = "$TMP_DIR/helixscreen-${plat}-v9.9.9.tar.gz" ]
}

@test "download_release: bare version is normalized to a v-prefixed tag" {
    WIN_EXT=zip; _stub_candidate
    local plat; plat="$(get_release_platform ad5m)"

    download_release "9.9.9" "ad5m"

    [ "$_ARCHIVE_VERSION" = "v9.9.9" ]
    [ -f "$TMP_DIR/helixscreen-${plat}-v9.9.9.zip" ]
}

@test "download_release: server-side fetch URL stays unversioned" {
    WIN_EXT=zip; _stub_candidate
    local plat; plat="$(get_release_platform ad5m)"

    download_release "v9.9.9" "ad5m"

    # The fetched asset name is the fixed unversioned server name; the version
    # lives only in the local staged path. Guard the two from ever converging.
    grep -q "/helixscreen-${plat}\.zip\$" "$TMP_DIR/urls.log"
    ! grep -q "helixscreen-${plat}-v9.9.9\.zip" "$TMP_DIR/urls.log"
}
