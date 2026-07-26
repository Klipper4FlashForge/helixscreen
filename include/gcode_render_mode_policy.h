// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui_gcode_viewer.h"

#include <cstring>

namespace helix::gcode_viewer {

/// Why the viewer ended up in a given render mode. Drives the startup log line.
enum class RenderModeReason {
    DefaultAuto,      ///< HELIX_GCODE_MODE unset — auto-detect at draw time
    EnvForced3D,      ///< HELIX_GCODE_MODE=3D, 3D renderer compiled in
    Env3DUnavailable, ///< HELIX_GCODE_MODE=3D on a build without ENABLE_3D_RENDERER
    EnvForced2D,      ///< HELIX_GCODE_MODE=2D
    EnvUnrecognized,  ///< Any other value, including empty
};

/// Outcome of the render-mode override decision.
struct RenderModeDecision {
    GcodeViewerRenderMode mode;
    RenderModeReason reason;
};

/**
 * @brief Resolve the viewer's startup render mode from HELIX_GCODE_MODE.
 *
 * Pure function. Only the exact strings "3D" and "2D" are honored — matching is
 * case-sensitive, so "3d" is an unrecognized value, not a request for 3D.
 *
 * Note the asymmetry with decide_ssao_enabled(): there, an unrecognized value
 * falls through to the memory tier, because the tier is the safe answer. Here an
 * unrecognized value resolves to Layer2D rather than Auto, so a typo'd override
 * lands on the renderer that works everywhere instead of silently behaving as if
 * nothing was set. Unset is the only path to Auto.
 *
 * @param mode_env          Raw HELIX_GCODE_MODE value, or nullptr when unset.
 * @param have_3d_renderer  Whether this build has ENABLE_3D_RENDERER. Passed in
 *                          rather than read from the macro so the unavailable
 *                          branch is reachable from a 3D-enabled test build.
 */
inline RenderModeDecision decide_render_mode(const char* mode_env, bool have_3d_renderer) {
    if (mode_env == nullptr) {
        return RenderModeDecision{GcodeViewerRenderMode::Auto, RenderModeReason::DefaultAuto};
    }
    if (std::strcmp(mode_env, "3D") == 0) {
        if (have_3d_renderer) {
            return RenderModeDecision{GcodeViewerRenderMode::Render3D,
                                      RenderModeReason::EnvForced3D};
        }
        return RenderModeDecision{GcodeViewerRenderMode::Layer2D,
                                  RenderModeReason::Env3DUnavailable};
    }
    if (std::strcmp(mode_env, "2D") == 0) {
        return RenderModeDecision{GcodeViewerRenderMode::Layer2D, RenderModeReason::EnvForced2D};
    }
    return RenderModeDecision{GcodeViewerRenderMode::Layer2D, RenderModeReason::EnvUnrecognized};
}

} // namespace helix::gcode_viewer
