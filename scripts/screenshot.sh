#!/bin/bash

# Copyright (C) 2025-2026 356C LLC
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

# Show help
show_help() {
    cat << 'EOF'
Usage: screenshot.sh [BINARY] [NAME] [TOKEN] [FLAGS...]

Capture a screenshot of the HelixScreen UI, driven by helix-screen ctl.

Launches the binary with its remote-control server on a private socket, drives
the UI to the requested screen with a navigation recipe, captures a screenshot,
converts it to PNG, and shuts the instance down. Each capture is an isolated,
freshly-booted process — no state leaks between shots.

Arguments:
  BINARY    Binary name in build/bin/ (default: helix-screen)
  NAME      Output filename suffix (default: timestamp)
            Screenshot saved to: /tmp/ui-screenshot-<NAME>.png
  TOKEN     Screen to capture (optional; default: home). May be a base panel
            (home, controls, filament, settings, advanced, print-select), an
            overlay (motion, bed-mesh, network, zoffset, ...), or a sample-data
            screen (preflight-check, runout-modal, lock-screen, print-status,
            print-tune). See scripts/screenshot-recipes.sh for the full list.
            An unknown token is tried as a bare `navigate <token>`.
  FLAGS     Additional flags passed to the binary (e.g., --test, --dark,
            -s 800x480, --layout ultrawide). Pass --wizard to capture the
            first-run wizard (suppresses --skip-wizard).

Environment Variables:
  HELIX_SCREENSHOT_DISPLAY   Display index to open the window on (default: auto)
  HELIX_SCREENSHOT_TIMEOUT   Max seconds to wait for the control socket (default: 20)
  HELIX_SCREENSHOT_DELAY     Settle seconds after the recipe before capture (default: 1.5)
  HELIX_SCREENSHOT_OPEN      If set, opens the screenshot in a viewer

Examples:
  ./scripts/screenshot.sh                                 # default binary, home
  ./scripts/screenshot.sh helix-screen home-panel home --test
  ./scripts/screenshot.sh helix-screen motion motion --test -s small
  ./scripts/screenshot.sh helix-screen zoffset zoffset --test
  ./scripts/screenshot.sh helix-screen preflight preflight-check --test
  ./scripts/screenshot.sh helix-screen wizard-wifi "" --wizard --test

Output:
  Screenshots are saved to /tmp/ui-screenshot-<NAME>.png (BMP auto-converted).

Dependencies:
  - ImageMagick (apt install imagemagick / brew install imagemagick)
EOF
    exit 0
}

case "${1:-}" in
    -h|--help|help) show_help ;;
esac

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
info() { echo -e "${BLUE}ℹ${NC} $1"; }
success() { echo -e "${GREEN}✓${NC} $1"; }
warn() { echo -e "${YELLOW}⚠${NC} $1"; }
error() { echo -e "${RED}✗${NC} $1"; }

# Project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# shellcheck source=screenshot-recipes.sh
source "$SCRIPT_DIR/screenshot-recipes.sh"

BINARY="${1:-helix-screen}"
BINARY_PATH="./build/bin/${BINARY}"
HELIXCTL=("./build/bin/helix-screen" ctl)

NAME="${2:-$(date +%s)}"
BMP_FILE="/tmp/ui-screenshot-${NAME}.bmp"
PNG_FILE="/tmp/ui-screenshot-${NAME}.png"

# Third arg: TOKEN (a screen) unless it starts with '-', in which case it's a flag.
TOKEN=""
if [ $# -ge 3 ]; then
    if [[ "${3}" == -* ]]; then
        shift 2; EXTRA_ARGS=("$@")
    else
        TOKEN="${3}"; shift 3 2>/dev/null || true; EXTRA_ARGS=("$@")
    fi
else
    shift 2 2>/dev/null || true; EXTRA_ARGS=("$@")
fi

# Wizard capture: the first-run wizard shows itself on boot, so don't skip it and
# run no recipe (we just capture the boot screen).
WIZARD_MODE=0
for a in "${EXTRA_ARGS[@]}"; do
    [ "$a" = "--wizard" ] && WIZARD_MODE=1
done

# On a Wayland desktop, force SDL's native Wayland driver (avoids XWayland GLX crash).
if [ -n "$WAYLAND_DISPLAY" ] && [ -z "$SDL_VIDEODRIVER" ]; then
    export SDL_VIDEODRIVER=wayland
    info "Wayland session detected — using SDL_VIDEODRIVER=wayland"
fi

# Display index
if [ -z "$HELIX_SCREENSHOT_DISPLAY" ]; then
    if [ -n "$WAYLAND_DISPLAY" ]; then HELIX_SCREENSHOT_DISPLAY=0; else HELIX_SCREENSHOT_DISPLAY=1; fi
fi

SOCKET_TIMEOUT="${HELIX_SCREENSHOT_TIMEOUT:-20}"
SETTLE="${HELIX_SCREENSHOT_DELAY:-1.5}"

# ImageMagick
if command -v magick &> /dev/null; then MAGICK_CMD="magick"
elif command -v convert &> /dev/null; then MAGICK_CMD="convert"
else error "ImageMagick not found (install with: sudo apt install imagemagick)"; exit 1; fi

# Binary present + executable
if [ ! -f "$BINARY_PATH" ]; then
    error "Binary not found: $BINARY_PATH"; info "Build first with: make"; exit 1
fi
[ -x "$BINARY_PATH" ] || chmod +x "$BINARY_PATH"
if [ ! -x "./build/bin/helix-screen" ]; then
    error "helix-screen not found: ./build/bin/helix-screen"; info "Build it with: make -j"; exit 1
fi

# Private per-invocation socket so we never collide with a dev instance.
SOCK="/tmp/helix-shot-$$.sock"
LOG="/tmp/helix-shot-$$.log"
rm -f "$SOCK" /tmp/ui-screenshot-*.bmp 2>/dev/null || true

HELIX_PID=""
cleanup() {
    [ -n "$HELIX_PID" ] && kill "$HELIX_PID" 2>/dev/null || true
    rm -f "$SOCK" "$LOG" 2>/dev/null || true
}
trap cleanup EXIT

# Assemble launch flags. --skip-splash for speed; --remote for the control server.
LAUNCH_FLAGS=(--remote --remote-socket "$SOCK" --skip-splash
              --display "$HELIX_SCREENSHOT_DISPLAY")
[ "$WIZARD_MODE" = "0" ] && LAUNCH_FLAGS+=(--skip-wizard)

info "Launching ${BINARY} (private socket $SOCK)..."
"$BINARY_PATH" "${LAUNCH_FLAGS[@]}" "${EXTRA_ARGS[@]}" > "$LOG" 2>&1 &
HELIX_PID=$!

# Wait for the control socket.
waited=0
while [ ! -S "$SOCK" ]; do
    if ! kill -0 "$HELIX_PID" 2>/dev/null; then
        error "Binary exited before the control socket appeared"
        tail -15 "$LOG" 2>/dev/null
        exit 1
    fi
    if [ "$waited" -ge "$((SOCKET_TIMEOUT * 2))" ]; then
        error "Timed out after ${SOCKET_TIMEOUT}s waiting for control socket"
        exit 1
    fi
    sleep 0.5; waited=$((waited + 1))
done

# Run the navigation recipe (skip in wizard mode — the wizard shows itself).
if [ "$WIZARD_MODE" = "0" ]; then
    RECIPE="$(screenshot_recipe_for "${TOKEN:-home}")"
    info "Recipe: $RECIPE"
    IFS=';' read -ra STEPS <<< "$RECIPE"
    for step in "${STEPS[@]}"; do
        # trim leading/trailing whitespace
        step="$(echo "$step" | sed 's/^ *//;s/ *$//')"
        [ -z "$step" ] && continue
        if ! "${HELIXCTL[@]}" -s "$SOCK" $step >/dev/null 2>&1; then
            warn "Recipe step failed: '$step'"
        fi
    done
else
    info "Wizard mode: capturing boot screen (no recipe)"
fi

# Let animations/transitions settle, then capture.
sleep "$SETTLE"
if ! "${HELIXCTL[@]}" -s "$SOCK" screenshot >/dev/null 2>&1; then
    error "helix-screen ctl screenshot failed"; exit 1
fi

# Locate the newest BMP (save_screenshot writes /tmp/ui-screenshot-<timestamp>.bmp).
LATEST_BMP=$(ls -t /tmp/ui-screenshot-*.bmp 2>/dev/null | head -1)
if [ -z "$LATEST_BMP" ]; then
    error "Screenshot not captured"; tail -10 "$LOG" 2>/dev/null; exit 1
fi
[ "$LATEST_BMP" != "$BMP_FILE" ] && mv "$LATEST_BMP" "$BMP_FILE"

# Convert to PNG
if ! $MAGICK_CMD "$BMP_FILE" "$PNG_FILE" 2>/dev/null; then
    error "PNG conversion failed"; warn "BMP left at: $BMP_FILE"; exit 1
fi
rm -f "$BMP_FILE"

PNG_SIZE=$(ls -lh "$PNG_FILE" | awk '{print $5}')
echo ""
success "Screenshot ready!"
echo "  File:  $PNG_FILE ($PNG_SIZE)"
echo "  Token: ${TOKEN:-home}"
echo ""

if [ -n "$HELIX_SCREENSHOT_OPEN" ]; then
    command -v open &>/dev/null && open "$PNG_FILE" || { command -v xdg-open &>/dev/null && xdg-open "$PNG_FILE"; }
fi
