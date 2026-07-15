// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../catch_amalgamated.hpp"
#include <algorithm>
#include <cstring>
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

static lv_subject_t s_a, s_b;
static lv_subject_t * mock_resolver(void *, const char * name) {
    if (!strcmp(name, "a")) return &s_a;
    if (!strcmp(name, "b")) return &s_b;
    return nullptr;
}

TEST_CASE("compile: valid expression returns non-null", "[xml_expr]") {
    lv_subject_init_int(&s_a, 0); lv_subject_init_int(&s_b, 0);
    lv_xml_expr_t * e = lv_xml_expr_compile("a && b > 3", mock_resolver, nullptr);
    REQUIRE(e != nullptr);
    lv_xml_expr_free(e);
}

TEST_CASE("compile: unknown subject fails (returns null)", "[xml_expr]") {
    lv_xml_expr_t * e = lv_xml_expr_compile("a && zzz", mock_resolver, nullptr);
    REQUIRE(e == nullptr);
}

TEST_CASE("compile: repeated subject collapses to one distinct", "[xml_expr]") {
    lv_subject_init_int(&s_a, 1); lv_subject_init_int(&s_b, 0);
    lv_xml_expr_t * e = lv_xml_expr_compile("a && a", mock_resolver, nullptr);
    REQUIRE(e != nullptr);
    REQUIRE(lv_xml_expr_subject_count(e) == 1);
    lv_xml_expr_free(e);
}

TEST_CASE("compile: malformed expressions fail cleanly", "[xml_expr]") {
    REQUIRE(lv_xml_expr_compile("", mock_resolver, nullptr) == nullptr);
    REQUIRE(lv_xml_expr_compile("(a && b", mock_resolver, nullptr) == nullptr);
    REQUIRE(lv_xml_expr_compile("a &&", mock_resolver, nullptr) == nullptr);
    REQUIRE(lv_xml_expr_compile("a @ b", mock_resolver, nullptr) == nullptr);
    REQUIRE(lv_xml_expr_compile("a && b)", mock_resolver, nullptr) == nullptr);
}

static int32_t eval(const char * src) {
    lv_xml_expr_t * e = lv_xml_expr_compile(src, mock_resolver, nullptr);
    REQUIRE(e != nullptr);
    int32_t v = lv_xml_expr_eval(e);
    lv_xml_expr_free(e);
    return v;
}

TEST_CASE("eval: boolean + comparison over subjects", "[xml_expr]") {
    lv_subject_init_int(&s_a, 1); lv_subject_init_int(&s_b, 5);
    REQUIRE(eval("a && b > 3")   == 1);
    REQUIRE(eval("a and b gt 3") == 1);   // word form identical
    lv_subject_set_int(&s_b, 2);
    REQUIRE(eval("a && b > 3")   == 0);
    REQUIRE(eval("a || b > 3")   == 1);
    REQUIRE(eval("!a")           == 0);
    REQUIRE(eval("not a")        == 0);
}

TEST_CASE("eval: precedence and grouping", "[xml_expr]") {
    lv_subject_init_int(&s_a, 2); lv_subject_init_int(&s_b, 3);
    REQUIRE(eval("a + b * 2")   == 8);
    REQUIRE(eval("(a + b) * 2") == 10);
    REQUIRE(eval("a % b")       == 2);
}

TEST_CASE("eval: divide by zero yields 0, no crash", "[xml_expr]") {
    lv_subject_init_int(&s_a, 4); lv_subject_init_int(&s_b, 0);
    REQUIRE(eval("a / b") == 0);
    REQUIRE(eval("a % b") == 0);
}
