// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstring>

namespace helix::gcode_viewer {

/// Why enhanced shading (SSAO) ended up on or off. Drives the startup log line.
enum class SsaoReason {
    DefaultOn,      ///< Unconstrained device, no override
    ConstrainedOff, ///< < 256MB RAM tier, no override
    EnvForcedOn,    ///< HELIX_SSAO=1
    EnvForcedOff,   ///< HELIX_SSAO=0
};

/// Outcome of the SSAO tiering decision.
struct SsaoDecision {
    bool enabled;
    SsaoReason reason;
};

/**
 * @brief Decide whether the viewer starts with enhanced shading enabled.
 *
 * Pure function. Enhanced shading is ON by default but OFF on constrained
 * devices: the SSAO cache is a full-canvas ARGB8888 buffer (~592KB at 368x402,
 * more at larger sizes) held for the life of the viewer, and it is the third
 * such buffer alongside the solid cache and the ghost buffer. On a 47MB AD5M
 * that is real money for a shading nicety.
 *
 * HELIX_SSAO overrides the tier in either direction so the effect can still be
 * forced on for comparison, or off on a machine that would otherwise get it.
 * Any other value of the variable (including empty) is ignored — only the exact
 * strings "0" and "1" are honored.
 *
 * @param constrained_device MemoryInfo::is_constrained_device() for this host.
 * @param ssao_env           Raw HELIX_SSAO value, or nullptr when unset.
 */
inline SsaoDecision decide_ssao_enabled(bool constrained_device, const char* ssao_env) {
    if (ssao_env && std::strcmp(ssao_env, "0") == 0) {
        return SsaoDecision{false, SsaoReason::EnvForcedOff};
    }
    if (ssao_env && std::strcmp(ssao_env, "1") == 0) {
        return SsaoDecision{true, SsaoReason::EnvForcedOn};
    }
    if (constrained_device) {
        return SsaoDecision{false, SsaoReason::ConstrainedOff};
    }
    return SsaoDecision{true, SsaoReason::DefaultOn};
}

} // namespace helix::gcode_viewer
