#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests for scripts/generate-manifest.sh
# Verifies manifest JSON generation from release tarballs.

SCRIPT="scripts/generate-manifest.sh"

setup() {
    # Create temp directory with test tarballs
    TEST_DIR="$(mktemp -d)"
    # Create dummy tarballs for each platform
    dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip > "$TEST_DIR/helixscreen-pi-v0.9.5.tar.gz"
    dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip > "$TEST_DIR/helixscreen-pi32-v0.9.5.tar.gz"
    dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip > "$TEST_DIR/helixscreen-ad5m-v0.9.5.tar.gz"
    dd if=/dev/zero bs=1024 count=1 2>/dev/null | gzip > "$TEST_DIR/helixscreen-k1-v0.9.5.tar.gz"
}

teardown() {
    rm -rf "$TEST_DIR"
}

@test "generate-manifest.sh exists and is executable" {
    [ -f "$SCRIPT" ]
    [ -x "$SCRIPT" ]
}

@test "generate-manifest.sh passes shellcheck" {
    if ! command -v shellcheck &>/dev/null; then
        skip "shellcheck not installed"
    fi
    shellcheck "$SCRIPT"
}

@test "generate-manifest.sh has valid bash syntax" {
    bash -n "$SCRIPT"
}

@test "generate-manifest.sh --help shows usage" {
    run bash "$SCRIPT" --help
    [ "$status" -eq 0 ]
    [[ "$output" == *"Usage"* ]]
}

@test "generates valid JSON with all platforms" {
    run bash "$SCRIPT" \
        --version "0.9.5" \
        --tag "v0.9.5" \
        --notes "Test release" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ -f "$TEST_DIR/manifest.json" ]

    # Validate JSON structure
    run jq -e '.version' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "$output" = '"0.9.5"' ]

    run jq -e '.tag' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "$output" = '"v0.9.5"' ]

    run jq -e '.notes' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "$output" = '"Test release"' ]
}

@test "manifest includes all four platforms" {
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    # Check each platform exists
    for plat in pi pi32 ad5m k1; do
        run jq -e ".assets.${plat}" "$TEST_DIR/manifest.json"
        [ "$status" -eq 0 ]
    done
}

@test "manifest includes SHA256 hashes" {
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    # SHA256 hashes should be non-empty 64-char hex strings
    for plat in pi pi32 ad5m k1; do
        run jq -re ".assets.${plat}.sha256" "$TEST_DIR/manifest.json"
        [ "$status" -eq 0 ]
        [ "${#output}" -eq 64 ]
    done
}

@test "manifest includes correct URLs" {
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    run jq -re '.assets.pi.url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [[ "$output" == "https://releases.helixscreen.org/dev/helixscreen-pi-v0.9.5.tar.gz" ]]
}

@test "zip_url is emitted by default when a .zip is present; url stays tar.gz" {
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-pi.zip"
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    # Legacy url still points at the tarball — pre-v0.99.31 clients read this.
    run jq -re '.assets.pi.url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [[ "$output" == *"helixscreen-pi-v0.9.5.tar.gz" ]]

    # zip_url is the preferred asset for v0.99.31+ clients.
    run jq -re '.assets.pi.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [[ "$output" == "https://releases.helixscreen.org/dev/helixscreen-pi.zip" ]]

    run jq -re '.assets.pi.zip_sha256' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [ "${#output}" -eq 64 ]
}

@test "--no-include-zip suppresses zip_url even when a .zip is present" {
    printf 'PK\003\004dummyzip' > "$TEST_DIR/helixscreen-pi.zip"
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json" --no-include-zip

    run jq -e '.assets.pi.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
    # Legacy tarball url is unaffected by suppression.
    run jq -re '.assets.pi.url' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    [[ "$output" == *"helixscreen-pi-v0.9.5.tar.gz" ]]
}

@test "no zip_url when a platform has no .zip (default on, skips gracefully)" {
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    run jq -e '.assets.pi.zip_url' "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
}

@test "manifest includes published_at timestamp" {
    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    run jq -re '.published_at' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    # Should be ISO 8601 format
    [[ "$output" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T ]]
}

@test "handles subset of platforms" {
    # Remove some tarballs
    rm "$TEST_DIR/helixscreen-ad5m-v0.9.5.tar.gz"
    rm "$TEST_DIR/helixscreen-k1-v0.9.5.tar.gz"

    bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Pi only" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"

    # pi and pi32 should exist
    run jq -e '.assets.pi' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]
    run jq -e '.assets.pi32' "$TEST_DIR/manifest.json"
    [ "$status" -eq 0 ]

    # ad5m and k1 should NOT exist
    run jq -e '.assets.ad5m' "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
    run jq -e '.assets.k1' "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
}

@test "fails with no tarballs in directory" {
    local empty_dir
    empty_dir="$(mktemp -d)"

    run bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Empty" \
        --dir "$empty_dir" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$empty_dir/manifest.json"
    [ "$status" -ne 0 ]

    rm -rf "$empty_dir"
}

@test "fails with missing --version" {
    run bash "$SCRIPT" \
        --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
}

@test "fails with missing --dir" {
    run bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --base-url "https://releases.helixscreen.org/dev" \
        --output "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
}

@test "fails with missing --base-url" {
    run bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --output "$TEST_DIR/manifest.json"
    [ "$status" -ne 0 ]
}

@test "fails with missing --output" {
    run bash "$SCRIPT" \
        --version "0.9.5" --tag "v0.9.5" --notes "Test" \
        --dir "$TEST_DIR" \
        --base-url "https://releases.helixscreen.org/dev"
    [ "$status" -ne 0 ]
}
