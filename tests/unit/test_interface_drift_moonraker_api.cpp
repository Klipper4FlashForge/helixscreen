// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compile-only drift protection: if IMoonrakerAPI gains a pure-virtual method
// and neither MoonrakerAPI (the real implementation) nor MoonrakerAPIMock
// provides it, MoonrakerAPIMock becomes abstract and this fails to build.
//
// Also pins the ten Moonraker sub-API interfaces (i_moonraker_sub_apis.h):
// each concrete sub-API class must derive from its matching interface and
// implement every pure virtual (non-abstract), or this fails to build.

#include "i_moonraker_api.h"
#include "i_moonraker_sub_apis.h"
#include "moonraker_advanced_api.h"
#include "moonraker_file_api.h"
#include "moonraker_file_transfer_api.h"
#include "moonraker_history_api.h"
#include "moonraker_job_api.h"
#include "moonraker_motion_api.h"
#include "moonraker_queue_api.h"
#include "moonraker_rest_api.h"
#include "moonraker_spoolman_api.h"
#include "moonraker_timelapse_api.h"

#include "../catch_amalgamated.hpp"

#include <type_traits>

TEST_CASE("Moonraker sub-API classes satisfy their interfaces", "[compile][drift]") {
    static_assert(std::is_base_of_v<IMotionAPI, MoonrakerMotionAPI>,
                  "MoonrakerMotionAPI must derive from IMotionAPI");
    static_assert(!std::is_abstract_v<MoonrakerMotionAPI>,
                  "MoonrakerMotionAPI must implement every pure virtual from IMotionAPI");

    static_assert(std::is_base_of_v<IJobAPI, MoonrakerJobAPI>,
                  "MoonrakerJobAPI must derive from IJobAPI");
    static_assert(!std::is_abstract_v<MoonrakerJobAPI>,
                  "MoonrakerJobAPI must implement every pure virtual from IJobAPI");

    static_assert(std::is_base_of_v<IFilesAPI, MoonrakerFileAPI>,
                  "MoonrakerFileAPI must derive from IFilesAPI");
    static_assert(!std::is_abstract_v<MoonrakerFileAPI>,
                  "MoonrakerFileAPI must implement every pure virtual from IFilesAPI");

    static_assert(std::is_base_of_v<IQueueAPI, MoonrakerQueueAPI>,
                  "MoonrakerQueueAPI must derive from IQueueAPI");
    static_assert(!std::is_abstract_v<MoonrakerQueueAPI>,
                  "MoonrakerQueueAPI must implement every pure virtual from IQueueAPI");

    static_assert(std::is_base_of_v<IHistoryAPI, MoonrakerHistoryAPI>,
                  "MoonrakerHistoryAPI must derive from IHistoryAPI");
    static_assert(!std::is_abstract_v<MoonrakerHistoryAPI>,
                  "MoonrakerHistoryAPI must implement every pure virtual from IHistoryAPI");

    static_assert(std::is_base_of_v<IAdvancedAPI, MoonrakerAdvancedAPI>,
                  "MoonrakerAdvancedAPI must derive from IAdvancedAPI");
    static_assert(!std::is_abstract_v<MoonrakerAdvancedAPI>,
                  "MoonrakerAdvancedAPI must implement every pure virtual from IAdvancedAPI");

    static_assert(std::is_base_of_v<IRestAPI, MoonrakerRestAPI>,
                  "MoonrakerRestAPI must derive from IRestAPI");
    static_assert(!std::is_abstract_v<MoonrakerRestAPI>,
                  "MoonrakerRestAPI must implement every pure virtual from IRestAPI");

    static_assert(std::is_base_of_v<ITransfersAPI, MoonrakerFileTransferAPI>,
                  "MoonrakerFileTransferAPI must derive from ITransfersAPI");
    static_assert(!std::is_abstract_v<MoonrakerFileTransferAPI>,
                  "MoonrakerFileTransferAPI must implement every pure virtual from ITransfersAPI");

    static_assert(std::is_base_of_v<ISpoolmanAPI, MoonrakerSpoolmanAPI>,
                  "MoonrakerSpoolmanAPI must derive from ISpoolmanAPI");
    static_assert(!std::is_abstract_v<MoonrakerSpoolmanAPI>,
                  "MoonrakerSpoolmanAPI must implement every pure virtual from ISpoolmanAPI");

    static_assert(std::is_base_of_v<ITimelapseAPI, MoonrakerTimelapseAPI>,
                  "MoonrakerTimelapseAPI must derive from ITimelapseAPI");
    static_assert(!std::is_abstract_v<MoonrakerTimelapseAPI>,
                  "MoonrakerTimelapseAPI must implement every pure virtual from ITimelapseAPI");

    SUCCEED("All ten Moonraker sub-API interfaces ↔ concrete class pairs verified at compile time");
}

#ifdef HELIX_ENABLE_MOCKS
#include "moonraker_api_mock.h"

TEST_CASE("MoonrakerAPIMock satisfies IMoonrakerAPI interface", "[compile][drift]") {
    static_assert(std::is_base_of_v<IMoonrakerAPI, MoonrakerAPIMock>,
                  "MoonrakerAPIMock must derive from IMoonrakerAPI");
    static_assert(!std::is_abstract_v<MoonrakerAPIMock>,
                  "MoonrakerAPIMock must implement every pure virtual from IMoonrakerAPI");
    SUCCEED("IMoonrakerAPI ↔ MoonrakerAPIMock parity verified at compile time");
}
#endif
