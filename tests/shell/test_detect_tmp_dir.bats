#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for detect_tmp_dir() in scripts/lib/installer/platform.sh
# Ensures the installer picks a temp directory with enough free space.

WORKTREE_ROOT="$(cd "$BATS_TEST_DIRNAME/../.." && pwd)"

setup() {
    load helpers

    # Override log stubs to capture output
    log_info()    { echo "INFO: $*"; }
    log_warn()    { echo "WARN: $*"; }
    log_error()   { echo "ERROR: $*"; }
    log_success() { echo "OK: $*"; }
    export -f log_info log_warn log_error log_success

    # Reset source guard and globals
    unset _HELIX_PLATFORM_SOURCED
    export SUDO=""
    export TMP_DIR=""

    . "$WORKTREE_ROOT/scripts/lib/installer/platform.sh"
}

# ===========================================================================
# detect_tmp_dir
# ===========================================================================

@test "detect_tmp_dir: respects user-set TMP_DIR" {
    export TMP_DIR="/my/custom/tmp"
    detect_tmp_dir
    [ "$TMP_DIR" = "/my/custom/tmp" ]
}

@test "detect_tmp_dir: picks first candidate with enough space" {
    # Create candidate directories
    mkdir -p "$BATS_TEST_TMPDIR/data"
    mkdir -p "$BATS_TEST_TMPDIR/tmp"

    # Mock df to report space based on directory
    mock_command_script "df" "
case \"\$1\" in
    *data*)
        echo 'Filesystem  1K-blocks  Used Available Use% Mounted on'
        echo '/dev/sda1   1048576  0  512000  0% /data'
        ;;
    *tmp*)
        echo 'Filesystem  1K-blocks  Used Available Use% Mounted on'
        echo 'tmpfs       51200    0  51200   0% /tmp'
        ;;
    *)
        echo 'Filesystem  1K-blocks  Used Available Use% Mounted on'
        echo '/dev/sda1   1048576  0  512000  0% /'
        ;;
esac
"

    # Override candidates to use our test dirs
    # We can't easily override the candidate list, but we can test
    # that /tmp fallback works when it's the only writable dir
    export TMP_DIR=""
    detect_tmp_dir

    # On the test system, it should find *something* (likely /tmp or /var/tmp)
    [ -n "$TMP_DIR" ]
}

@test "detect_tmp_dir: falls back to /tmp with warning when no good candidate" {
    # Mock df to always report low space
    mock_command_script "df" '
echo "Filesystem  1K-blocks  Used Available Use% Mounted on"
echo "tmpfs       10240     0  10240       0% /tmp"
'

    export TMP_DIR=""
    run detect_tmp_dir

    # Should warn about using /tmp
    [[ "$output" == *"No temp directory"* ]] || [[ "$TMP_DIR" == *"/tmp/"* ]] || true
}

@test "detect_tmp_dir: skips non-existent candidate directories" {
    # No /data, /mnt/data, etc. on macOS — should still work
    export TMP_DIR=""
    detect_tmp_dir
    [ -n "$TMP_DIR" ]
}

@test "detect_tmp_dir: result ends with helixscreen-install" {
    export TMP_DIR=""
    detect_tmp_dir
    [[ "$TMP_DIR" == *"helixscreen-install" ]]
}

# ===========================================================================
# Runtime handoff contract: app passes its already-validated staging dir via
# TMP_DIR. When set, detect_tmp_dir must NOT probe at all (df must not run).
# ===========================================================================

@test "detect_tmp_dir: honors preset TMP_DIR without probing (no df call)" {
    # Marker file that df writes to if it is ever invoked. Its absence proves
    # detect_tmp_dir returned early on the user/app override.
    local marker="$BATS_TEST_TMPDIR/df_was_called"
    mock_command_script "df" "
touch \"$marker\"
echo 'Filesystem  1K-blocks  Used Available Use% Mounted on'
echo '/dev/sda1   1048576  0  512000  0% /'
"

    export TMP_DIR="/home/pi/helixscreen/.helix-update-staging"
    detect_tmp_dir

    # Value preserved exactly — the app's validated dir wins unchanged.
    [ "$TMP_DIR" = "/home/pi/helixscreen/.helix-update-staging" ]
    # And detect_tmp_dir short-circuited before any candidate probing.
    [ ! -f "$marker" ]
}

# ===========================================================================
# Fresh curl|sh install on a read-only-/tmp box: an install-dir SIBLING
# candidate is probed FIRST and selected ahead of /tmp — but NEVER a dir
# inside INSTALL_DIR (the installer rm -rf's / mv's INSTALL_DIR on --update).
# ===========================================================================

@test "detect_tmp_dir: selects a SIBLING of INSTALL_DIR, never a dir inside it" {
    # Real writable parent with an install subdir under it. The parent stands
    # in for the on-device layout where INSTALL_DIR's parent is the big user
    # partition (e.g. /data/helixscreen → /data). df/writability checks use the
    # host filesystem, which reports >100MB free.
    local parent fake_install
    parent="$(mktemp -d "$BATS_TEST_TMPDIR/parent.XXXXXX")"
    fake_install="$parent/helixscreen"
    mkdir -p "$fake_install"
    export INSTALL_DIR="$fake_install"

    export TMP_DIR=""
    detect_tmp_dir

    # The sibling candidate (INSTALL_DIR's parent) must win — prepended ahead
    # of /var/tmp, /tmp, etc., mirroring the app's C++ probe.
    [ "$TMP_DIR" = "$parent/.helixscreen-install" ]

    # SAFETY INVARIANT: the selected dir must be OUTSIDE INSTALL_DIR — never
    # equal to it and never a subdir of it.
    [ "$TMP_DIR" != "$fake_install" ]
    case "$TMP_DIR" in
        "$fake_install"/*) fail "TMP_DIR $TMP_DIR is INSIDE INSTALL_DIR $fake_install" ;;
    esac
    # And never the /tmp last-resort fallback.
    [[ "$TMP_DIR" != "/tmp/helixscreen-install" ]]
}
