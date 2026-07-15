// SPDX-License-Identifier: GPL-3.0-or-later
//
// hv/json.hpp → the repo's own vendored nlohmann/json. lib/libhv/include/hv/json.hpp
// IS nlohmann/json single-header, verbatim, with zero libhv platform deps — so this
// is purely an include-path alias to the exact header the Linux build compiles.

#pragma once

#include <libhv/include/hv/json.hpp> // via -isystem <repo>/lib
