# Plan: Seed temperature graphs from `server.temperature_store` (#944)

## Goal
Temperature graphs (home widget + full-screen overlay) start empty for the first
5–20 min after every WS connect because `TemperatureHistoryManager` only records
in-process samples. Moonraker keeps ~20 min of 1 Hz history in `server.temperature_store`.
Fetch it once per connect and seed the manager so graphs are populated immediately.

## Key sequencing insight (simplifies the issue's "race" open-question)
`TempGraphController::backfill_history()` runs only when a graph is constructed /
resumed / rebuilt (i.e. when the temp screen opens), **not** at connect time. It reads
the global `TemperatureHistoryManager`. So we only need the seed to land in that global
manager before the user opens a graph — which the discovery-complete path guarantees for
cold launch. No graph-gating state machine is needed.

The only real hazard: the seed RPC is async and may return *after* the first live
sample has been appended for a key. The ring buffer assumes insertion order == time
order, so `seed_from_store` must **merge existing + seed samples, sort by timestamp,
keep newest HISTORY_SIZE** — not blind-append.

## Design decisions (locked)
1. **Client layer only.** New RPC `get_temperature_store` on `IMoonrakerClient` +
   `MoonrakerClient` (template: `get_gcode_store`). No API-layer wrapper (not needed by
   the connect path; keeps it to one drift boundary).
2. **New types** (next to `GcodeStoreEntry`):
   - `struct TemperatureStoreSeries { std::vector<float> temperatures, targets, powers; }`
   - `using TemperatureStore = std::map<std::string, TemperatureStoreSeries>;`
3. **Seed entrypoint:** `TemperatureHistoryManager::seed_from_store(const TemperatureStore&, int64_t now_ms)`.
   - Per key: synthesize timestamps backward from `now_ms` at `SAMPLE_INTERVAL_MS` (1000 ms),
     last sample == now, oldest first.
   - Convert temps/targets to **decidegrees** (`×10`, round). Sensor-only keys (no
     `targets`) → `target_deci = 0`.
   - Merge with any samples already present for that key, sort by `timestamp_ms`, keep
     the newest `HISTORY_SIZE`. Race-safe regardless of live-sample interleave.
   - Bypasses the `SAMPLE_INTERVAL_MS` write throttle (seed is authoritative bulk load).
   - `powers` ignored (`TempSample` has no power field).
4. **Wiring:** in the discovery-complete UI handler (`application.cpp` ~2492, after
   `init_fans`, before/around `dispatch_status_update`), fire
   `client->get_temperature_store(...)`. Success cb → `queue_update` →
   `get_temperature_history_manager()->seed_from_store(store, now_ms)`. Re-runs naturally
   on reconnect because discovery re-runs.
5. **Mock:** `server.temperature_store` handler in `register_server_handlers` returns a
   realistic heat/hold/cool curve (extruder 25→215°C, bed 25→60°C, ~10 min @ 1 Hz,
   ending near ambient) via `MoonrakerClientMock::build_historical_temperature_store()`.
   This makes `--test` a real end-to-end verification: opening the temp graph shows a
   populated curve. (Supersedes the original "empty store" plan — a mock printer that's
   been running should have history; empty was less representative.)

## Known v1 gap (documented, acceptable)
If a temp graph is **already open** when a reconnect seed lands, it won't re-backfill
until its next natural open/resume/rebuild. Cold-launch (primary acceptance) is fully
covered because the seed lands before any graph opens. Revisit only if field feedback
shows the reconnect-with-open-graph case matters.

## Files
- `include/moonraker_types.h` (or wherever `GcodeStoreEntry` lives) — new structs.
- `include/i_moonraker_client.h` — pure-virtual `get_temperature_store`.
- `include/moonraker_client.h` + `src/api/moonraker_client.cpp` — concrete impl (parse).
- `include/temperature_history_manager.h` + `src/temperature/temperature_history_manager.cpp`
  — `seed_from_store`.
- `src/api/moonraker_client_mock_server.cpp` — empty-store handler (+ bump count log).
- `src/application/application.cpp` — connect-path fetch + seed wiring.
- Tests (test-first):
  - `tests/unit/` — `seed_from_store`: fill-empty, decidegree conversion, 1 Hz backfill
    timestamps, sensor-only (target 0), merge-with-existing out-of-order, HISTORY_SIZE cap.
  - `tests/unit/` — RPC parse: canned JSON → `TemperatureStore` via mock handler.
  - Interface drift stays green (concrete impl inherited by mock); no edit needed.

## Test-first order
1. Write failing unit tests for `seed_from_store` (pure, no LVGL/network) + RPC parse.
2. Implement types + `seed_from_store` → green.
3. Implement RPC binding + mock handler → green.
4. Wire connect path.
5. Build program + tests, run `[temp]`/interface-drift tags.
6. Verify in mock UI: `--test` cold launch, confirm graph seeding path logs.
