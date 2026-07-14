// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "config_storage.h"

#include <optional>
#include <string>

namespace helix::test {

class MockConfigStorage : public helix::ConfigStorage {
  public:
    std::optional<std::string> doc;
    std::string corrupt_stash;
    bool ro = false;
    int store_calls = 0;

    explicit MockConfigStorage(std::optional<std::string> initial = std::nullopt)
        : doc(std::move(initial)) {}

    std::optional<std::string> load() override { return doc; }
    bool store(const std::string& bytes) override {
        if (ro) return false;
        doc = bytes;
        store_calls++;
        return true;
    }
    void preserve_corrupt() override {
        if (doc) corrupt_stash = *doc;
        doc.reset();
    }
    bool read_only() override { return ro; }
    std::string describe() const override { return "mock://config"; }
};

} // namespace helix::test
