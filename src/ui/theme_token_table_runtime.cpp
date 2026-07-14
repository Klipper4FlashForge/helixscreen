// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme_token_table.h"

#include <cstdlib>
#include <cstring>

namespace helix::theme_tokens {

bool enabled() {
    static const bool on = [] {
        if (const char* env = std::getenv("HELIX_TOKEN_TABLE")) {
            return env[0] == '1';
        }
#if defined(HELIX_RELEASE_BUILD) || defined(HELIX_TOKEN_TABLE_DEFAULT_ON)
        return true; // immutable ui_xml: table is safe and fast
#else
        return false; // dev build: preserve edit-XML-and-relaunch
#endif
    }();
    return on;
}

std::unordered_map<std::string, std::string> for_element(const char* element_type) {
    std::unordered_map<std::string, std::string> out;
    for (size_t i = 0; i < k_token_table_count; ++i) {
        const TokenEntry& e = k_token_table[i];
        if (std::strcmp(e.type, element_type) == 0) {
            out[e.name] = e.value;
        }
    }
    return out;
}

std::unordered_map<std::string, std::string> for_suffix(const char* element_type,
                                                        const char* suffix) {
    std::unordered_map<std::string, std::string> out;
    const size_t slen = std::strlen(suffix);
    for (size_t i = 0; i < k_token_table_count; ++i) {
        const TokenEntry& e = k_token_table[i];
        if (std::strcmp(e.type, element_type) != 0) {
            continue;
        }
        const size_t nlen = std::strlen(e.name);
        // Same predicate as the runtime scanner: name must be strictly
        // longer than the suffix and end with it; key is the stripped base.
        if (nlen > slen && std::strcmp(e.name + nlen - slen, suffix) == 0) {
            out[std::string(e.name, nlen - slen)] = e.value;
        }
    }
    return out;
}

} // namespace helix::theme_tokens
