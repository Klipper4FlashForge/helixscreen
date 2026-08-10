#!/usr/bin/env bats
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Code lint tests: enforce architectural rules on the codebase.

setup() {
    cd "$BATS_TEST_DIRNAME/../.." || return 1
}

# --- No _for_testing methods in production code ---
# Test-only methods belong in test files via friend class TestAccess pattern.
# See commit removing these for the migration pattern.
#
# *_mock.h files are explicitly excluded: mocks ARE test infrastructure (the
# whole class exists only for tests), so a `_for_testing` setter on a mock
# carries no risk of shipping test code to users — the mock itself is gated
# by HELIX_ENABLE_MOCKS and never enters production builds.

@test "no _for_testing methods declared in headers" {
    run grep -rn '_for_testing' include/ --include='*.h' --exclude='*_mock.h'
    [ "$status" -eq 1 ]  # grep returns 1 when no matches found
}

@test "no _for_testing methods defined in source files" {
    run grep -rn '_for_testing' src/ --include='*.cpp'
    [ "$status" -eq 1 ]  # grep returns 1 when no matches found
}

# --- Migrated temperature VIEW files must route sends through the controller ---
# ui_overlay_temp_graph.cpp and ui_panel_controls.cpp were migrated to delegate
# temperature commands to helix::TemperatureController. They must NOT call the
# raw send API directly again — that would reintroduce the duplication the
# refactor removed. A direct send is either `api_->set_temperature(` or any
# `->set_temperature(` whose receiver is not a `controller`.

@test "migrated temp view files do not call the raw set_temperature send API" {
    local files="src/ui/ui_overlay_temp_graph.cpp src/ui/ui_panel_controls.cpp src/system/post_op_cooldown_manager.cpp src/ui/panel_widgets/preheat_widget.cpp src/ui/ui_panel_bed_mesh.cpp src/ui/ui_panel_filament.cpp src/ui/temperature_service.cpp src/ui/ui_ams_sidebar.cpp"

    # Direct API send on the cached MoonrakerAPI pointer.
    run grep -n 'api_->set_temperature' $files
    [ "$status" -eq 1 ]  # grep returns 1 when no matches found

    # Any ->set_temperature( call whose receiver is not `controller`. The
    # controller's own ->set_temperature() is the sanctioned path, so exclude it.
    run bash -c "grep -nE '\->set_temperature\(' $files | grep -v 'controller'"
    [ "$status" -ne 0 ]  # non-zero == no disallowed direct send found
}

# --- Chamber temp_display must use the maintain-aware effective target ---
# The raw `chamber_target` subject is the heater target only — it reads 0 during
# M141 "maintain" (cooling-ceiling) mode, so a display bound to it shows "—/Off"
# while the chamber is actually holding a setpoint. All chamber temp_display
# instances must bind to `chamber_effective_target` (+ `chamber_mode`), never the
# raw subject. (Drip-fixed missed-spot class; see chamber M141 routing work.)

@test "no temp_display binds chamber target to the raw chamber_target subject" {
    run grep -rn 'bind_target="chamber_target"' ui_xml/
    [ "$status" -eq 1 ]  # grep returns 1 when no matches found
}

# --- Temperature "decidegrees" misnomer must not creep back ---
# Subject temperatures are stored as degrees × 10 = DECIdegrees (1 unit = 0.1°C).
# The codebase was historically (and wrongly) calling these "centidegrees" — a
# centidegree would be degrees × 100. The Phase-1 rename swept the misnomer out;
# this gate keeps it from reappearing in code or developer docs.
#
# Excluded paths are legitimately historical or out-of-scope:
#   CHANGELOG.md          - records the old name as part of release history
#   docs/superpowers/     - archived planning docs
#   src/generated/        - generated code (regenerated from templates)
#   lib/                  - vendored submodules (LVGL, libhv, etc.)
#   */translations/       - translation catalogs (mirror upstream wording)

@test "no 'centidegree' misnomer in code or docs" {
    run bash -c "git grep -iIln 'centidegree' -- \
        ':!CHANGELOG.md' \
        ':(glob)!docs/superpowers/**' \
        ':(glob)!src/generated/**' \
        ':(glob)!lib/**' \
        ':(glob)!translations/**' \
        ':(glob)!ui_xml/translations/**' \
        ':(glob)!scripts/translations/**'"
    # git grep exits 0 when it finds matches, 1 when it finds none.
    [ "$status" -ne 0 ]  # non-zero == no misnomer found
}

# --- Temperature subject conversions must route through the unit helpers ---
# Subjects store decidegrees; converting to/from degrees inline (`x / 10`,
# `x * 10`) bypasses helix::units / helix::ui::temperature and silently risks a
# truncation/rounding mismatch (int trunc vs float). These files were migrated to
# the helpers (deci_to_degrees / deci_to_degrees_f / degrees_to_deci /
# to_decidegrees / from_decidegrees); they must not reintroduce a raw multiply or
# divide by 10 on a temperature-named value.
#
# The regex matches a temperature identifier (target/temp/deci/nozzle/bed/chamber/
# heater) or a bare keypad `value`, optionally closing a paren, then `* 10` or
# `/ 10` — the (\.0?f?)?([^0-9.]|$) tail rejects a trailing digit. `//` comments
# are stripped first so the "value * 10" explanatory comments don't trip the gate.
#
# A SECOND pattern catches the x100 form. Decidegrees are degrees x10, so a
# temperature never converts by 100 — `decidegrees / 100` is always the
# controls-panel class of bug (secondary sensors rendered at 1/10 scale: 45°C
# shown as "4°C"). That form slipped past the x10 gate precisely because the tail
# rejects a trailing digit, which is what keeps genuine centimillimetre `/ 100`
# conversions from being flagged.
#
# The x100 pattern therefore uses a NARROWER identifier set than the x10 one:
#   - `value` is dropped: the keypad legitimately converts centimm via `value / 100`.
#   - `bed` is dropped: bed-mesh Z values are distances, not temperatures.
# Everything left (target/temp/deci/nozzle/chamber/heater) is unambiguously a
# temperature in these files. Verified to produce zero hits on the clean tree.

@test "migrated temp files do not convert decidegrees inline (use unit helpers)" {
    local files="src/print/print_start_collector.cpp \
        src/api/moonraker_api_controls.cpp \
        src/api/moonraker_discovery_sequence.cpp \
        src/printer/ams_backend_ad5x_ifs.cpp \
        src/printer/ams_backend_cfs.cpp \
        src/system/telemetry_manager.cpp \
        src/ui/panel_widgets/nozzle_temps_widget.cpp \
        src/ui/ui_ams_sidebar.cpp \
        src/ui/temperature_service.cpp \
        src/ui/ui_overlay_temp_graph.cpp \
        src/ui/ui_panel_bed_mesh.cpp \
        src/ui/ui_panel_calibration_pid.cpp \
        src/ui/ui_panel_controls.cpp \
        src/ui/ui_panel_filament.cpp \
        src/ui/ui_print_preparation_manager.cpp \
        src/ui/ui_temp_display.cpp"
    local pat='(target|temp|deci|nozzle|bed|chamber|heater|value)[A-Za-z_]*(\s*\))?\s*[*/]\s*10(\.0?f?)?([^0-9.]|$)'
    local pat100='(target|temp|deci|nozzle|chamber|heater)[A-Za-z_]*(\s*\))?\s*[*/]\s*100(\.0?f?)?([^0-9.]|$)'
    run bash -c "sed -E 's@//.*@@' $files | grep -nE '$pat|$pat100'"
    [ "$status" -ne 0 ]  # non-zero == no inline decidegree conversion found
}

# --- Concrete Moonraker types must not leak outside the network layer (Plan 3) ---
# Every consumer of the Moonraker network layer now depends on the interfaces
# (helix::IMoonrakerClient, IMoonrakerAPI, and the ten IXxxAPI sub-API
# interfaces in include/i_moonraker_sub_apis.h), not the concrete classes. The
# concretes live behind MoonrakerManager, which owns them via
# std::unique_ptr<helix::IMoonrakerClient> / std::unique_ptr<IMoonrakerAPI> and
# constructs them in create_client()/create_api(). Naming a concrete type
# outside the allowlist below reintroduces a hard dependency the interface
# split was meant to remove (mock-parity, and — for the ESP32 port — a
# non-libhv client swapped in behind the same interface).
#
# The allowlist covers the network-layer implementation files themselves
# (moonraker_client, moonraker_manager, moonraker_api + its split translation
# units, the ten sub-API pairs, moonraker_request_tracker,
# moonraker_discovery_sequence) and *_mock.{h,cpp} (mocks legitimately inherit
# the concretes). It intentionally has no bare "test_" entry: this lint only
# scans src/ and include/, so a substring that broad would silently exempt any
# non-test path containing "test_" (e.g. a hypothetical src/foo/test_helpers.cpp)
# — narrower and cheaper to just not have it.
#
# Compile-time-only exceptions NOT covered by this lint (by design, not by
# gap): a few consumers reference concrete-class static constexpr timeouts
# (MoonrakerAdvancedAPI::PROBING_TIMEOUT_MS, ::LEVELING_TIMEOUT_MS,
# MoonrakerJobAPI::CANCEL_TIMEOUT_MS) and the MoonrakerAdvancedAPI::MPCResult
# qualified-name alias. These aren't runtime polymorphism — MPCResult is
# actually defined on IAdvancedAPI with the concrete class providing a `using`
# alias purely so old qualified references keep resolving (see
# include/i_moonraker_sub_apis.h and include/moonraker_advanced_api.h). The
# ten sub-API concrete class names are deliberately left out of the grep
# pattern below rather than allowlisting each of those consumer files, which
# would blur the "outside the network layer" invariant this test communicates.

@test "no concrete Moonraker types outside the network layer (Plan 3: interfaces are the consumer contract)" {
    local allowlist='moonraker_client|moonraker_manager|moonraker_api|moonraker_rest_api|moonraker_file_api|moonraker_file_transfer_api|moonraker_advanced_api|moonraker_history_api|moonraker_job_api|moonraker_motion_api|moonraker_queue_api|moonraker_spoolman_api|moonraker_timelapse_api|moonraker_request_tracker|moonraker_discovery_sequence|_mock'
    run bash -c "grep -rlE 'helix::MoonrakerClient|\bMoonrakerAPI\b' src/ include/ | grep -v -E \"$allowlist\""
    [ "$status" -ne 0 ]  # non-zero == no concrete-type reference found outside the allowlist
}

# --- switch_printer must invalidate every per-printer cache ---
# Per-printer state lives under /printers/<id>/ and is reached via Config::df().
# Any component that memoizes a df()-derived value serves the PREVIOUS printer's
# data after a switch — PanelWidgetConfig was the first case (#804), and the fix
# used to be a single hardcoded clear_all_panel_configs() call here. Components
# now self-register with PrinterCacheRegistry and switch_printer() fires them all,
# so this gate pins the registry walk rather than any one component.
#
# The registry's own behavior is pinned by tests/unit/test_printer_cache_registry.cpp,
# and clear_all_panel_configs() by the unit test "PanelWidgetManager:
# clear_all_panel_configs reloads after printer switch". What no unit test can
# reach is the success path: switch_printer() ends in a full teardown and
# display + Moonraker rebuild, which cannot run inside a shared Catch2 shard
# without handing every later test rebuilt global singletons.
# tests/unit/application/test_application_printer_switch.cpp covers the branches
# on the near side of teardown. This gate pins the wiring beyond them — that
# switch_printer() actually makes the call, and makes it BEFORE teardown, while
# Config::df() has already moved to the new printer.

# Print the body of Application::switch_printer() from the file given in $1.
switch_printer_body() {
    awk '
        /^void Application::switch_printer\(/ { inside = 1 }
        inside { print }
        inside && /^\}/ { exit }
    ' "$1"
}

# Emit a diagnostic and return non-zero if $1 does not wire up the invalidation.
check_switch_printer_clears_caches() {
    local body clear_line teardown_line
    body=$(switch_printer_body "$1")
    if [ -z "$body" ]; then
        echo "could not locate Application::switch_printer() in $1"
        return 1
    fi

    clear_line=$(printf '%s\n' "$body" | grep -n 'PrinterCacheRegistry::instance().invalidate_all()' | head -1 | cut -d: -f1)
    if [ -z "$clear_line" ]; then
        echo "switch_printer() does not call PrinterCacheRegistry::instance().invalidate_all() (#804)"
        return 1
    fi

    teardown_line=$(printf '%s\n' "$body" | grep -n 'tear_down_printer_state()' | head -1 | cut -d: -f1)
    if [ -z "$teardown_line" ]; then
        echo "switch_printer() does not call tear_down_printer_state()"
        return 1
    fi

    if [ "$clear_line" -ge "$teardown_line" ]; then
        echo "PrinterCacheRegistry::invalidate_all() must precede tear_down_printer_state()"
        return 1
    fi
    return 0
}

@test "switch_printer invalidates every registered per-printer cache before teardown" {
    run check_switch_printer_clears_caches src/application/application.cpp
    [ "$status" -eq 0 ]
}

@test "the switch_printer cache-invalidation gate fails when the call is removed" {
    # Meta-test: a gate that cannot fail is not a gate. Strip the call from a
    # copy and confirm the check reports the #804 regression.
    local mutated="${BATS_TEST_TMPDIR}/application_no_clear.cpp"
    grep -v 'PrinterCacheRegistry::instance().invalidate_all()' src/application/application.cpp > "$mutated"

    run check_switch_printer_clears_caches "$mutated"
    [ "$status" -eq 1 ]
    [[ "$output" == *"#804"* ]]
}

@test "the switch_printer cache-invalidation gate fails when the call moves after teardown" {
    # The ordering half: invalidating after teardown re-reads the OLD printer's
    # values on the way down, so position matters as much as presence.
    local mutated="${BATS_TEST_TMPDIR}/application_late_clear.cpp"
    sed -e 's@^    helix::PrinterCacheRegistry::instance().invalidate_all();@@' \
        -e 's@^    tear_down_printer_state();@    tear_down_printer_state();\n    helix::PrinterCacheRegistry::instance().invalidate_all();@' \
        src/application/application.cpp > "$mutated"

    run check_switch_printer_clears_caches "$mutated"
    [ "$status" -eq 1 ]
    [[ "$output" == *"must precede"* ]]
}

@test "the switch_printer cache-invalidation gate fails when the function cannot be located" {
    # Fail-closed: a rename or signature change must break the gate loudly rather
    # than silently pass on an empty body.
    local mutated="${BATS_TEST_TMPDIR}/application_no_fn.cpp"
    sed -e 's@^void Application::switch_printer(@void Application::switch_printer_renamed(@' \
        src/application/application.cpp > "$mutated"

    run check_switch_printer_clears_caches "$mutated"
    [ "$status" -eq 1 ]
    [[ "$output" == *"could not locate"* ]]
}

@test "the switch_printer cache-invalidation gate fails when teardown is missing" {
    local mutated="${BATS_TEST_TMPDIR}/application_no_teardown.cpp"
    grep -v '^    tear_down_printer_state();' src/application/application.cpp > "$mutated"

    run check_switch_printer_clears_caches "$mutated"
    [ "$status" -eq 1 ]
    [[ "$output" == *"tear_down_printer_state"* ]]
}

@test "filaments.json android mirror matches when present" {
  # The Android mirror is gitignored (generated by `make regen-filaments`, not
  # tracked), so it may be absent on a fresh checkout / CI clone. Only assert
  # byte-identity when it exists — catches on-disk drift without breaking CI.
  if [ ! -f android/app/src/main/assets/assets/filaments.json ]; then
    skip "android mirror not generated (gitignored)"
  fi
  diff assets/filaments.json android/app/src/main/assets/assets/filaments.json
}
