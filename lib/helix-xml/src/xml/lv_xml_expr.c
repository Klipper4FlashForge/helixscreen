// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "lv_xml_expr.h"
#if LV_USE_XML
#include <string.h>
#include <stdbool.h>

typedef struct { lv_xml_expr_tok_kind_t kind; const char * start; size_t len; int32_t ival; } tok_t;

static bool is_ident_start(char c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static bool is_ident_char(char c){ return is_ident_start(c)||(c>='0'&&c<='9'); }

/* Fold a word operator to its symbolic kind, else IDENT. */
static lv_xml_expr_tok_kind_t word_kind(const char * s, size_t n){
    struct { const char * w; lv_xml_expr_tok_kind_t k; } tbl[] = {
        {"and",LV_XML_EXPR_TOK_AND},{"or",LV_XML_EXPR_TOK_OR},{"not",LV_XML_EXPR_TOK_NOT},
        {"eq",LV_XML_EXPR_TOK_EQ},{"ne",LV_XML_EXPR_TOK_NE},{"lt",LV_XML_EXPR_TOK_LT},
        {"le",LV_XML_EXPR_TOK_LE},{"gt",LV_XML_EXPR_TOK_GT},{"ge",LV_XML_EXPR_TOK_GE},
    };
    for(size_t i=0;i<sizeof(tbl)/sizeof(tbl[0]);i++)
        if(strlen(tbl[i].w)==n && strncmp(tbl[i].w,s,n)==0) return tbl[i].k;
    return LV_XML_EXPR_TOK_IDENT;
}

/* Core tokenizer. Returns token count; last token is always EOF (or ERROR). */
static size_t tokenize(const char * s, tok_t * out, size_t cap){
    size_t n = 0;
    #define PUSH(K,ST,LN,IV) do{ if(n<cap){out[n].kind=(K);out[n].start=(ST);out[n].len=(LN);out[n].ival=(IV);} n++; }while(0)
    while(*s){
        if(*s==' '||*s=='\t'||*s=='\n'||*s=='\r'){ s++; continue; }
        char c=*s;
        if(c>='0'&&c<='9'){ int32_t v=0; const char*st=s; while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;} PUSH(LV_XML_EXPR_TOK_INT,st,(size_t)(s-st),v); continue; }
        if(is_ident_start(c)){ const char*st=s; while(is_ident_char(*s))s++; size_t ln=(size_t)(s-st); PUSH(word_kind(st,ln),st,ln,0); continue; }
        switch(c){
            case '(': PUSH(LV_XML_EXPR_TOK_LPAREN,s,1,0); s++; break;
            case ')': PUSH(LV_XML_EXPR_TOK_RPAREN,s,1,0); s++; break;
            case '+': PUSH(LV_XML_EXPR_TOK_PLUS,s,1,0); s++; break;
            case '-': PUSH(LV_XML_EXPR_TOK_MINUS,s,1,0); s++; break;
            case '*': PUSH(LV_XML_EXPR_TOK_STAR,s,1,0); s++; break;
            case '/': PUSH(LV_XML_EXPR_TOK_SLASH,s,1,0); s++; break;
            case '%': PUSH(LV_XML_EXPR_TOK_PERCENT,s,1,0); s++; break;
            case '&': if(s[1]=='&'){ PUSH(LV_XML_EXPR_TOK_AND,s,2,0); s+=2; } else { PUSH(LV_XML_EXPR_TOK_ERROR,s,1,0); s++; } break;
            case '|': if(s[1]=='|'){ PUSH(LV_XML_EXPR_TOK_OR,s,2,0); s+=2; } else { PUSH(LV_XML_EXPR_TOK_ERROR,s,1,0); s++; } break;
            case '!': if(s[1]=='='){ PUSH(LV_XML_EXPR_TOK_NE,s,2,0); s+=2; } else { PUSH(LV_XML_EXPR_TOK_NOT,s,1,0); s++; } break;
            case '=': if(s[1]=='='){ PUSH(LV_XML_EXPR_TOK_EQ,s,2,0); s+=2; } else { PUSH(LV_XML_EXPR_TOK_ERROR,s,1,0); s++; } break;
            case '<': if(s[1]=='='){ PUSH(LV_XML_EXPR_TOK_LE,s,2,0); s+=2; } else { PUSH(LV_XML_EXPR_TOK_LT,s,1,0); s++; } break;
            case '>': if(s[1]=='='){ PUSH(LV_XML_EXPR_TOK_GE,s,2,0); s+=2; } else { PUSH(LV_XML_EXPR_TOK_GT,s,1,0); s++; } break;
            default:  PUSH(LV_XML_EXPR_TOK_ERROR,s,1,0); s++; break;
        }
    }
    PUSH(LV_XML_EXPR_TOK_EOF,s,0,0);
    #undef PUSH
    return n;
}

#define LV_XML_EXPR_TEST_TOK_MAX 128
size_t lv_xml_expr_tokenize_for_test(const char * src, lv_xml_expr_tok_kind_t * out, size_t cap){
    tok_t buf[LV_XML_EXPR_TEST_TOK_MAX];
    size_t n = tokenize(src, buf, LV_XML_EXPR_TEST_TOK_MAX);
    size_t m = n < cap ? n : cap;
    if (m > LV_XML_EXPR_TEST_TOK_MAX) m = LV_XML_EXPR_TEST_TOK_MAX;
    for (size_t i = 0; i < m; i++) out[i] = buf[i].kind;
    return n;
}
#endif /* LV_USE_XML */
