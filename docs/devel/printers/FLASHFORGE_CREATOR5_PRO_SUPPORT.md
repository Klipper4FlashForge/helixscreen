<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# FlashForge Creator 5 Pro Support

HelixScreen has a cross-compilation target (`creator5`) for the FlashForge Creator 5 Pro,
a 4-tool toolchanger running a FlashForge Klipper fork on an Ingenic X2000 MIPS host.

**Status: builds and executes on the printer** (`helix-screen --version` answers over SSH
with the NaN2008 toolchain; the first, legacy-NaN build was rejected by the kernel with
ENOEXEC — see "Toolchain choice"). On-device bring-up (stock UI coexistence, touch,
Moonraker, first UI run) is open work.

## Hardware (from the stock `firmwareExe` ELF and the device rootfs)

| Spec | Value |
|------|-------|
| SoC | Ingenic X2000 (XBurst2, MIPS32r2, 2× 1.2 GHz) — same family as the Creality K1's X2000E |
| FPU / ABI | Hard float, 64-bit FPU (`-mfp64`), **NaN2008** (`/lib/ld-linux-mipsn8.so.1`) |
| RAM | 128–256 MB |
| Display | 800×480 panel; the framebuffer is exposed **portrait (480×800)**, so the preset sets `display.rotate = 90` (verified on the printer); fbdev `/dev/fb0`, no X11/Wayland/DRM |
| Touch | evdev, `/dev/input/event2` (auto-detected; override with `HELIX_TOUCH_DEVICE`) |
| Stock UI | `firmwareExe` — LVGL 8 on fbdev. **Also** hosts the port-8898 REST API, the MQTT cloud link, and the tool-pickup orchestration for UI-started prints |
| C library | glibc 2.33, built with Ingenic "MIPS Linux Tools GCC12.1 Release6.0.1 xburst2" (`mips-gcc1210-glibc233`), min kernel 3.10.14 |
| Stock libs on device | libcurl, OpenSSL 1.0.0, zlib, OpenCV 4.2, FFmpeg (libav* 56/58), libzip 5, libstdc++ (GCC 12) |
| Python | 3.8.2 at `/usr/prog/Python-3.8.2` |
| Klipper | FlashForge fork at `/usr/prog/klipper`, API socket `/tmp/uds`, config `/usr/data/config/printer.cfg` |
| Moonraker | Present (Mainsail works against the printer), default port 7125 |

### Filesystem layout

FlashForge layout, same family as the AD5X: `/usr/prog/` (programs), `/usr/data/` (user data,
large partition), `/usr/data/config/` (Klipper/Moonraker config), `/usr/data/firmwareRes/`
(stock UI resources, per-unit tool offsets in `config/device_config.conf` — back that up).

> Because `/usr/prog` exists here, the generic `HELIX_PLATFORM_MIPS` runtime sniff in
> `UpdateChecker::get_platform_key()` would classify the machine as an **AD5X** and the
> self-updater would download the AD5X build (glibc, MIPS32r5 — wrong ABI). The `creator5`
> target therefore adds `-DHELIX_PLATFORM_CREATOR5`, which short-circuits that to `"creator5"`.
> There is no published `helixscreen-creator5.zip` release asset yet, so self-update is
> effectively a no-op on this platform.

## Toolchain choice

**The Creator 5 Pro kernel refuses legacy-NaN executables.** Verified on the device:
the K1-toolchain (Bootlin musl) build fails with `execve(...) = -1 ENOEXEC (Exec format
error)`, while every stock binary carries the `nan2008` ELF flag (busybox: `e_flags
0x70001405, noreorder, cpic, nan2008, o32, mips32r2`; ours was `0x70001007` — identical except
for that bit). This is the one place it differs from the Creality K1, whose kernel accepts
both encodings. `-mnan=2008` alone cannot fix the K1 toolchain: its `libc.a` is legacy-NaN
and `ld` refuses to link mixed NaN encodings.

| | NaN2008 static musl (**chosen**, `creator5`) | Bootlin musl (K1 image) | Dynamic glibc 2.33 nan2008 |
|---|---|---|---|
| Toolchain | crosstool-NG 1.26: GCC 12 `--with-nan=2008` + musl 1.2.4 + binutils 2.40 (`docker/Dockerfile.creator5`, `docker/creator5-ct-ng.config`) | `docker/Dockerfile.k1` | crosstool-NG glibc 2.33 `--with-nan=2008` like `Dockerfile.k1-dynamic`, or Ingenic's `mips-xburst2-linux-toolchain-r6.x` (FTP `ftp.ingenic.com.cn/Ingenic-MIPS-Toolchain/releases/`) |
| Runs on the device | Yes (NaN2008 flag, static) | **No — ENOEXEC** | Yes, if glibc ≤ 2.33 / FP64 / sysroot from device all match |
| ABI coupling to stock rootfs | None — fully static | — | Must match glibc symbol versions; needs `/lib`,`/usr/lib` pulled from the device |
| Image build time | ~30 min once (GCC from source) | ~5 min | ~30 min + sysroot extraction |

NaN2008 is baked in as the compiler *default* (`--with-nan=2008`, `CT_TARGET_CFLAGS`), so
OpenSSL, zlib, libnl and the wpa_supplicant client inherit it without per-project flag
plumbing; `mk/cross.mk` still passes `-mnan=2008` explicitly so a wrong toolchain fails at
link time rather than on the printer. FP mode is `-mfp64` (FR=1) like every stock
binary: the XBurst2 FPU does not run FR=0, so an fpxx/fp32 build ends up in the kernel's
FPU emulator, which oopses in `mips_dsemul` (`readelf -A` must say
`FP ABI: Hard float (32-bit CPU, 64-bit FPU)`).

## Building

### Docker (Linux/macOS, or WSL with Docker integration enabled)

```bash
make creator5-docker          # builds helixscreen/toolchain-creator5 image on first use (~10 min on 24 cores)
file build/creator5/bin/helix-screen
# ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1 (SYSV), statically linked, stripped
readelf -h build/creator5/bin/helix-screen | grep Flags
# Flags: 0x70001407, noreorder, pic, cpic, nan2008, o32, mips32r2   <- nan2008 is the point
```

Each `g++` for this codebase peaks at 0.5–1 GB, so keep the job count in line with RAM —
`make creator5-docker` already caps the container at `NPROC_DOCKER_RUN` (8) jobs; on a 16 GB
host with Docker Desktop that is about the limit before the VM starts swapping.

### Release package

```bash
make release-creator5         # releases/helixscreen-creator5-v<ver>.tar.gz + helixscreen-creator5.zip
```

Same layout as every other platform tarball (`bin/`, `ui_xml/`, `assets/`, `config/`,
`certs/`, `install.sh`); needs the prerendered images first (`make venv-setup &&
make gen-all-images`, as `release.yml` does). `creator5` is in the `release.yml` build
matrix, so tagging `v*` builds and attaches it like the other platforms. The tarball bakes
`assets/config/presets/creator5.json` as `config/settings.json`, so first boot runs only the
abbreviated wizard (language + telemetry).

The binary locates its data root relative to itself (`<root>/bin/helix-screen` →
`<root>/ui_xml`), so unpack the tarball as a whole (e.g. to `/usr/data/helixscreen/`)
and run `bin/helix-screen` from there — a bare binary fails with "Could not find
HelixScreen data root". `HELIX_DATA_DIR` overrides the lookup.

### Direct (toolchain on host)

```bash
# needs the mipsel-creator5-linux-musl-* toolchain from docker/creator5-ct-ng.config in PATH,
# plus static OpenSSL 1.1.1 and zlib installed into its sysroot (see docker/Dockerfile.creator5)
make PLATFORM_TARGET=creator5 -j
```

### Build configuration

| Setting | Value |
|---------|-------|
| `PLATFORM_TARGET` | `creator5` |
| Toolchain image | `helixscreen/toolchain-creator5` (`docker/Dockerfile.creator5`, crosstool-NG) |
| Architecture | `-march=mips32r2 -mtune=mips32r2 -mnan=2008 -mfp64`, little-endian, hard-float FR=1 |
| Linking | Fully static (musl), `-Os`, gc-sections. **No LTO**: the static crosstool-NG toolchain has no `liblto_plugin.so` (same as `k1-dynamic`), so `-flto` breaks every configure test and `gcc-ar` cannot start; plain `ar`/`ranlib` are used |
| Display backend | fbdev (`/dev/fb0`, size auto-detected), no rotation (panel is native landscape) |
| Input | evdev (auto-detect; `HELIX_TOUCH_DEVICE=/dev/input/event2` to pin) |
| SSL | Enabled (static OpenSSL 1.1.1w) |
| Platform defines | `HELIX_PLATFORM_MIPS` + `HELIX_PLATFORM_CREATOR5` |
| Font tiers | `medium large` (800×480, same as AD5M/AD5X) |
| Sound | Disabled (MIPS/musl path, no ALSA) |
| Output | `build/creator5/bin/helix-screen`, `helix-splash`; CA bundle in `build/creator5/certs/` |

## On-device bring-up (open work)

1. **Sanity on the printer** — done: `bin/helix-screen --version` runs. (`readelf -h` on
   the binary must list `nan2008` in Flags — without it the kernel answers ENOEXEC.)
   The installer's `detect_platform()` (`scripts/lib/installer/platform.sh`) still
   classifies any MIPS box with `/usr/data` + `/usr/prog` as **AD5X**, so `install.sh`
   cannot be used on this printer until it gets a Creator 5 Pro fingerprint; unpack the
   tarball by hand for now.
2. **Stock UI coexistence** — unlike the K1's `display-server`, `firmwareExe` is not just a
   UI: stopping it also kills the 8898 REST API, the cloud link and the stock print
   orchestration (tool grab/release for UI-started prints). Two processes drawing to
   `/dev/fb0` will fight. Options, to be evaluated: run HelixScreen only while `firmwareExe`
   is stopped (Mainsail-driven workflow), or find a way to blank/park the stock UI
   (`firmwareExe` has screen-off paths reachable via its API).
3. **Touch** — `/dev/input/event2`; if auto-detect picks another device, pin it with
   `HELIX_TOUCH_DEVICE`. Capacitive panel, no calibration expected.
4. **Moonraker** — HelixScreen talks to Moonraker (not `/tmp/uds`). The Moonraker instance
   that Mainsail uses is the one to point at (port 7125 unless `moonraker.conf` says otherwise).
5. **Detection + preset** — `printer_database.json` entry `flashforge_creator_5_pro`
   (fingerprint: `ff_toolchange` / `gcode_button extruder_grab1`, 4 extruders) and preset
   `creator5.json` (4 hotends, chamber heater, part/chamber fans, LED, `fd_ex*` runout
   switches, rotate 90). Without it the detector picked the AD5X (same hostname, MIPS,
   4 tools); the AD5X entry now excludes on `MOTOR_GRAB`. Uses the `generic-corexy` image.
6. **Memory** — 128–256 MB shared with Klipper, Moonraker, `firmwareExe`. HelixScreen's
   ~15 MB footprint is fine, but check `free` with the stock stack running.
7. **Init** — no systemd; the stock stack is started from BusyBox init scripts. An init.d
   script modeled on the AD5X/ZMOD `S80guppyscreen` pattern is the likely shape.
8. **Setup wizard calibration** — the preset sets
   `initial_resonance_compensation_run: false`, so the Input Shaper step runs once on
   first setup (the port's `SHAPER_CALIBRATE` wrapper grabs a tool first). The Tool
   Offsets step (`src/ui/ui_wizard_tool_offset.cpp`) is generic: it appears when the
   printer exposes klipper-toolchanger's `toolchanger` object plus a
   `CALIBRATE_TOOL_OFFSETS` macro (the name from klipper-toolchanger's
   `examples/calibrate-offsets.cfg`), runs it, mirrors the console, and offers
   `SAVE_CONFIG`. The macro's `description:` is the on-screen instruction. On the
   Creator 5 Pro the port defines it as `STATION_CALIBRATE` + `TOOL_OFFSET_CALIBRATE
   TOOL=ALL` with `PLATE_REMOVED=1`. Both steps skip themselves when the printer already
   holds the result: shaper frequencies in `[input_shaper]`
   (`PrinterDiscovery::has_input_shaper_config()`), or a non-zero gcode offset on every
   tool (`ToolState`). The port's `[ff_legacy] auto_import` loads firmwareExe's
   nozzle/station numbers at startup until something is saved, so a fresh install keeps
   the factory calibration and the wizard shows neither step.
