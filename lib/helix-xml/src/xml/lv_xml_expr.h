// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LV_XML_EXPR_H
#define LV_XML_EXPR_H

#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lv_xml_expr_t lv_xml_expr_t;

/* Resolve a subject name to an lv_subject_t*. Returns NULL if unknown. */
typedef lv_subject_t * (*lv_xml_expr_resolver_t)(void * ctx, const char * name);

/* Token kinds — exposed only for the lexer test hook. */
typedef enum {
    LV_XML_EXPR_TOK_EOF,
    LV_XML_EXPR_TOK_INT,       /* integer literal            */
    LV_XML_EXPR_TOK_IDENT,     /* subject name / word-op     */
    LV_XML_EXPR_TOK_LPAREN, LV_XML_EXPR_TOK_RPAREN,
    LV_XML_EXPR_TOK_PLUS, LV_XML_EXPR_TOK_MINUS,
    LV_XML_EXPR_TOK_STAR, LV_XML_EXPR_TOK_SLASH, LV_XML_EXPR_TOK_PERCENT,
    LV_XML_EXPR_TOK_EQ, LV_XML_EXPR_TOK_NE,
    LV_XML_EXPR_TOK_LT, LV_XML_EXPR_TOK_LE, LV_XML_EXPR_TOK_GT, LV_XML_EXPR_TOK_GE,
    LV_XML_EXPR_TOK_AND, LV_XML_EXPR_TOK_OR, LV_XML_EXPR_TOK_NOT,
    LV_XML_EXPR_TOK_ERROR,
} lv_xml_expr_tok_kind_t;

/* Test-only: tokenize `src` into `out` (up to `cap`); returns token count.
 * Word operators (and/or/not/eq/ne/lt/le/gt/ge) are folded to their symbolic
 * kind here so the parser never sees the distinction. */
size_t lv_xml_expr_tokenize_for_test(const char * src,
                                     lv_xml_expr_tok_kind_t * out, size_t cap);

/* Compile / evaluate / introspect / free — implemented in later tasks. */
lv_xml_expr_t * lv_xml_expr_compile(const char * src,
                                    lv_xml_expr_resolver_t resolver, void * resolver_ctx);
int32_t lv_xml_expr_eval(const lv_xml_expr_t * expr);
size_t  lv_xml_expr_subject_count(const lv_xml_expr_t * expr);
lv_subject_t * lv_xml_expr_subject_at(const lv_xml_expr_t * expr, size_t i);
void    lv_xml_expr_free(lv_xml_expr_t * expr);

#ifdef __cplusplus
}
#endif
#endif /* LV_XML_EXPR_H */
