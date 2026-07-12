#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# End-to-end scenario for the read-only-/tmp self-update failure (debug bundle
# W9Q93WXM, OrangePi Zero3). Runs INSIDE a user+mount namespace so it can mount
# a genuine read-only tmpfs over /tmp and /var/tmp without touching the host.
#
# It does two things against that real read-only filesystem:
#   REPRO  — with no writable sibling, detect_tmp_dir() falls back to
#            /tmp/helixscreen-install and the installer's mkdir dies exactly as
#            it did in the bundle ("Read-only file system").
#   FIX    — with INSTALL_DIR set (the app-provided staging handoff), it selects
#            the writable SIBLING of the install dir and a real tar extraction
#            succeeds there.
#
# Invoked as: unshare --user --map-root-user --mount bash <this> <shmwork> <platform.sh>
# All working files live under <shmwork> (on /dev/shm) so they survive the
# read-only remount of /tmp.

set -uo pipefail

shmwork="${1:?work dir required}"
platform="${2:?platform.sh path required}"

# --- Make /tmp and /var/tmp genuinely read-only (the OrangePi Zero3 condition) ---
if ! mount -t tmpfs -o ro tmpfs /tmp; then
    echo "MOUNT_TMP_FAIL"
    exit 2
fi
mount -t tmpfs -o ro tmpfs /var/tmp 2>/dev/null || true

# detect_tmp_dir() also probes device-specific writable partitions (/mnt/data,
# /data, /user-resource) BEFORE the /tmp last resort. Those parents don't exist
# on the OrangePi we're reproducing, but a host can have them writable — the
# GitHub Actions runner mounts a large world-writable temp disk at /mnt, so
# /mnt/data would be chosen and the REPRO would never reach the /tmp fallback.
# Shadow those parents with read-only tmpfs (best-effort; skip ones that don't
# exist) so /tmp is the only remaining candidate on any host. /usr (parent of
# the /usr/data candidate) is left alone — it holds the binaries this script
# execs, and is non-writable to a namespaced non-root uid regardless.
for parent in /mnt /data /user-resource; do
    [ -d "$parent" ] && mount -t tmpfs -o ro tmpfs "$parent" 2>/dev/null || true
done

# A read-only filesystem rejects writes even for (namespace) root — an EROFS
# the kernel enforces regardless of uid, unlike a chmod which root bypasses.
if mkdir /tmp/helixscreen-install 2>/dev/null; then
    echo "TMP_NOT_READONLY"
    exit 3
fi

# Silence the installer's logging helpers.
log_info() { :; }
log_warn() { :; }
log_error() { :; }
log_success() { :; }
export -f log_info log_warn log_error log_success
export SUDO=""

# ---------------------------------------------------------------------------
# REPRO: no writable sibling → fall back to /tmp → the installer's mkdir fails.
# ---------------------------------------------------------------------------
unset _HELIX_PLATFORM_SOURCED
unset INSTALL_DIR HOME
export TMP_DIR=""
# shellcheck disable=SC1090
. "$platform"
detect_tmp_dir

if [ "$TMP_DIR" != "/tmp/helixscreen-install" ]; then
    # A writable standard candidate existed on this host (e.g. a real /data);
    # the read-only-/tmp fallback path isn't what we're exercising then.
    echo "REPRO_UNEXPECTED_TMP: $TMP_DIR"
    exit 4
fi
if mkdir -p "$TMP_DIR" 2>/dev/null; then
    echo "REPRO_MKDIR_SHOULD_HAVE_FAILED"
    exit 5
fi
echo "REPRO_OK"

# ---------------------------------------------------------------------------
# FIX: app hands a staging dir via INSTALL_DIR's sibling → real extraction works.
# ---------------------------------------------------------------------------
export INSTALL_DIR="$shmwork/root/helixscreen"
mkdir -p "$INSTALL_DIR"
export HOME="$shmwork/home"
mkdir -p "$HOME"

unset _HELIX_PLATFORM_SOURCED
export TMP_DIR=""
# shellcheck disable=SC1090
. "$platform"
detect_tmp_dir

case "$TMP_DIR" in
    /tmp/*)
        echo "FIX_PICKED_READONLY_TMP: $TMP_DIR"
        exit 6
        ;;
esac
expected="$shmwork/root/.helixscreen-install"
if [ "$TMP_DIR" != "$expected" ]; then
    echo "FIX_WRONG_TMP: $TMP_DIR (want $expected)"
    exit 7
fi

# The step that died at "mkdir: Read-only file system" before the fix.
mkdir -p "$TMP_DIR/extract" || { echo "FIX_MKDIR_FAIL"; exit 8; }
# --no-same-owner: inside the user namespace only root is mapped, so the
# tarball's original uid/gid can't be restored (and real non-root installs
# extract this way regardless).
tar --no-same-owner -xzf "$shmwork/update.tar.gz" -C "$TMP_DIR/extract" \
    || { echo "FIX_EXTRACT_FAIL"; exit 9; }
[ -f "$TMP_DIR/extract/helixscreen/helix-screen" ] || { echo "FIX_PAYLOAD_MISSING"; exit 10; }
echo "FIX_OK"
