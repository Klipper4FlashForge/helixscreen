// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../catch_amalgamated.hpp"
#include <algorithm>
#include <vector>
extern "C" {
#include "helix-xml/src/xml/lv_xml_expr.h"
}

static std::vector<lv_xml_expr_tok_kind_t> lex(const char * s) {
    lv_xml_expr_tok_kind_t buf[64];
    size_t n = lv_xml_expr_tokenize_for_test(s, buf, 64);
    return {buf, buf + n};
}

TEST_CASE("lexer: symbolic and word operators fold to same tokens", "[xml_expr]") {
    using K = lv_xml_expr_tok_kind_t;
    REQUIRE(lex("a && b") == std::vector<K>{
        LV_XML_EXPR_TOK_IDENT, LV_XML_EXPR_TOK_AND, LV_XML_EXPR_TOK_IDENT, LV_XML_EXPR_TOK_EOF});
    REQUIRE(lex("a and b") == lex("a && b"));
    REQUIRE(lex("x >= 3")  == lex("x ge 3"));
    REQUIRE(lex("!x")      == lex("not x"));
}

TEST_CASE("lexer: integers, parens, arithmetic", "[xml_expr]") {
    using K = lv_xml_expr_tok_kind_t;
    REQUIRE(lex("(1 + 2) % 3") == std::vector<K>{
        LV_XML_EXPR_TOK_LPAREN, LV_XML_EXPR_TOK_INT, LV_XML_EXPR_TOK_PLUS,
        LV_XML_EXPR_TOK_INT, LV_XML_EXPR_TOK_RPAREN, LV_XML_EXPR_TOK_PERCENT,
        LV_XML_EXPR_TOK_INT, LV_XML_EXPR_TOK_EOF});
}

TEST_CASE("lexer: stray character is an error token", "[xml_expr]") {
    auto t = lex("a @ b");
    REQUIRE(std::find(t.begin(), t.end(), LV_XML_EXPR_TOK_ERROR) != t.end());
}
