// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Memory probe for the gcode rendering stack. Hidden ([.] prefix) — it is a
// measurement tool, not an assertion test, and it needs a real gcode file.
//
//   MEM_PROBE_FILE=<path> ./build/bin/helix-tests "[.memprobe]"
//
// Reports peak (VmHWM) and current (VmRSS) resident set at each stage of a
// full-load 3D build plus a streaming sweep, so a memory change can be shown
// rather than asserted. malloc_trim(0) runs before every sample so glibc has
// actually returned freed pages before the reading is taken — without it the
// arena holds them and every "after free" number is meaningless.
//
// Linux/glibc only: the readings come from /proc/self/status and the arena is
// released with glibc's malloc_trim(). Neither exists on macOS, so the probe
// compiles away there rather than reporting numbers it cannot measure.

// __GLIBC__ comes from <features.h>, not the compiler, so a libc header has to
// be seen before the guard can test it — without this include the guard reads
// false on glibc too and the probe silently vanishes from the Linux build.
#include <cstdlib>

#if defined(__linux__) && defined(__GLIBC__)

#include "gcode_geometry_builder.h"
#include "gcode_layer_index.h"
#include "gcode_parser.h"
#include "gcode_streaming_controller.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <malloc.h>
#include <memory>
#include <string>

#include "../catch_amalgamated.hpp"

using namespace helix::gcode;

namespace {

struct Rss {
    double hwm_mb = 0.0;
    double rss_mb = 0.0;
};

Rss read_rss() {
    Rss out;
    std::ifstream st("/proc/self/status");
    std::string line;
    while (std::getline(st, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            out.hwm_mb = std::strtod(line.c_str() + 6, nullptr) / 1024.0;
        } else if (line.rfind("VmRSS:", 0) == 0) {
            out.rss_mb = std::strtod(line.c_str() + 6, nullptr) / 1024.0;
        }
    }
    return out;
}

class Probe {
  public:
    void header(const std::string& path, size_t bytes) {
        std::printf("\n================================================================\n");
        std::printf("MEM PROBE  %s\n           %zu bytes (%.2f MB)\n", path.c_str(), bytes,
                    static_cast<double>(bytes) / (1024.0 * 1024.0));
        std::printf("           malloc_trim(0) before every sample\n");
        std::printf("================================================================\n");
        std::printf("%-44s %9s %9s %9s %9s\n", "phase", "VmHWM(MB)", "VmRSS(MB)", "dRSS(MB)", "ms");
        std::printf("---------------------------------------------------------------"
                    "-------------------------\n");
        start_ = std::chrono::steady_clock::now();
        last_ = start_;
        sample("0 baseline");
    }

    void sample(const char* label) {
        malloc_trim(0);
        auto now = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(now - last_).count();
        last_ = now;
        Rss r = read_rss();
        std::printf("%-44s %9.1f %9.1f %+9.1f %9.1f\n", label, r.hwm_mb, r.rss_mb,
                    r.rss_mb - prev_rss_, ms);
        prev_rss_ = r.rss_mb;
        peak_ = r.hwm_mb;
    }

    double peak() const {
        return peak_;
    }

  private:
    std::chrono::steady_clock::time_point start_{}, last_{};
    double prev_rss_ = 0.0;
    double peak_ = 0.0;
};

} // namespace

TEST_CASE("gcode memory probe", "[.memprobe]") {
    const char* env_path = std::getenv("MEM_PROBE_FILE");
    if (!env_path || !*env_path) {
        WARN("Set MEM_PROBE_FILE=<path to .gcode> to run the probe");
        return;
    }
    const std::string path = env_path;

    std::ifstream probe_file(path, std::ios::binary | std::ios::ate);
    REQUIRE(probe_file.good());
    const size_t file_bytes = static_cast<size_t>(probe_file.tellg());
    probe_file.close();

    Probe p;
    p.header(path, file_bytes);

    // ---- full-load parse -------------------------------------------------
    auto parsed = std::make_unique<ParsedGCodeFile>();
    size_t seg_capacity = 0;
    {
        GCodeParser parser;
        std::ifstream f(path);
        std::string line;
        while (std::getline(f, line)) {
            parser.parse_line(line);
        }
        *parsed = parser.finalize();
    }
    size_t parsed_layers = parsed->layers.size();
    size_t parsed_segments = 0;
    for (const auto& l : parsed->layers) {
        seg_capacity += l.segments.capacity();
        parsed_segments += l.segments.size();
    }
    p.sample("1 parse + finalize");

    // ---- 3D geometry -----------------------------------------------------
    auto geometry = std::make_unique<RibbonGeometry>();
    size_t verts = 0, strips = 0, strip_colors = 0;
    {
        GeometryBuilder builder;
        SimplificationOptions opts; // defaults: merging on, 0.01mm tolerance
        *geometry = builder.build(*parsed, opts);
        verts = geometry->vertices.size();
        strips = geometry->strips.size();
        strip_colors = geometry->strip_color_index.size();
    }
    p.sample("2 GeometryBuilder::build");
    const size_t mem_after_build = geometry->memory_usage();

    geometry->prepare_interleaved_buffers();
    p.sample("3 prepare_interleaved_buffers");
    const size_t mem_after_prepare = geometry->memory_usage();

    geometry->clear();
    p.sample("4 geometry.clear()");

    parsed.reset();
    p.sample("5 ParsedGCodeFile destroyed");

    // ---- streaming sweep -------------------------------------------------
    GCodeLayerIndex index;
    REQUIRE(index.build_from_file(path));
    const size_t index_bytes = index.memory_usage_bytes();
    p.sample("6 layer index built");

    size_t streamed_layers = 0, streamed_segments = 0;
    {
        GCodeStreamingController ctl;
        if (ctl.open_file(path)) {
            for (size_t i = 0; i < ctl.get_layer_count(); ++i) {
                auto segs = ctl.get_layer_segments(i);
                if (segs) {
                    streamed_layers++;
                    streamed_segments += segs->size();
                }
            }
        }
        p.sample("7 streaming sweep (all layers)");
    }
    p.sample("8 streaming controller destroyed");

    std::printf("\n-- counts ------------------------------------------------------\n");
    std::printf("parse        : layers=%zu segments=%zu capacity=%zu\n", parsed_layers,
                parsed_segments, seg_capacity);
    std::printf("geometry     : vertices=%zu strips=%zu strip_colors=%zu\n", verts, strips,
                strip_colors);
    std::printf("memory_usage : after_build=%.2f MB after_prepare=%.2f MB\n",
                mem_after_build / 1048576.0, mem_after_prepare / 1048576.0);
    std::printf("layer index  : bytes=%zu\n", index_bytes);
    std::printf("streaming    : layers=%zu segments=%zu\n", streamed_layers, streamed_segments);
    std::printf("PEAK VmHWM   : %.1f MB\n", p.peak());
    std::printf("sizeof        : RibbonVertex=%zu PackedVertex=%zu ToolpathSegment=%zu\n",
                sizeof(RibbonVertex), sizeof(PackedVertex), sizeof(ToolpathSegment));
}

#endif // __linux__ && __GLIBC__
