#!/usr/bin/env bash
# Regenerate assets/zoneinfo/ from the host system's /usr/share/zoneinfo/.
#
# Ships a minimal TZif zoneinfo subset with the helixscreen install so that
# `setenv("TZ", "America/New_York") + tzset()` works on devices that don't
# bundle tzdata (notably Elegoo Centauri Carbon running OpenCentauri COSMOS).
#
# The zone list here must match TIMEZONE_ENTRIES in
# src/system/display_settings_manager.cpp. If you add a zone there, add it here
# and re-run this script.
#
# Usage: scripts/regen_zoneinfo.sh
set -euo pipefail

SRC="/usr/share/zoneinfo"
DEST_REL="assets/zoneinfo"
DEST="$(cd "$(dirname "$0")/.." && pwd)/${DEST_REL}"

ZONES=(
    UTC
    Pacific/Honolulu
    America/Anchorage
    America/Los_Angeles
    America/Denver
    America/Chicago
    America/New_York
    America/Halifax
    America/St_Johns
    America/Sao_Paulo
    Atlantic/Cape_Verde
    Europe/London
    Europe/Berlin
    Europe/Bucharest
    Europe/Moscow
    Asia/Tehran
    Asia/Dubai
    Asia/Kabul
    Asia/Karachi
    Asia/Kolkata
    Asia/Kathmandu
    Asia/Dhaka
    Asia/Yangon
    Asia/Bangkok
    Asia/Shanghai
    Asia/Hong_Kong
    Asia/Tokyo
    Australia/Perth
    Australia/Adelaide
    Australia/Sydney
    Pacific/Noumea
    Pacific/Auckland
)

if [ ! -d "$SRC" ]; then
    echo "ERROR: $SRC does not exist on this host — install tzdata first." >&2
    exit 1
fi

rm -rf "$DEST"
mkdir -p "$DEST"

for z in "${ZONES[@]}"; do
    src_file="$SRC/$z"
    dest_file="$DEST/$z"
    if [ ! -f "$src_file" ]; then
        echo "ERROR: missing zone file $src_file" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$dest_file")"
    cp "$src_file" "$dest_file"
done

total_bytes=$(du -sb "$DEST" | cut -f1)
total_files=$(find "$DEST" -type f | wc -l)
echo "Regenerated $total_files zone files in $DEST_REL/ (${total_bytes} bytes)"
