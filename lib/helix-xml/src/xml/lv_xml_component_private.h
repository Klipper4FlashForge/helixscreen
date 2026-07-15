/**
 * @file lv_xml_component_private.h
 *
 */

#ifndef LV_XML_COMPONENT_PRIVATE_H
#define LV_XML_COMPONENT_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lv_xml.h"
#if LV_USE_XML

#include "lv_xml_utils.h"
#include "../misc/lv_ll.h"
#include "../misc/lv_style.h"
#include "../core/lv_observer.h"
#include "lv_xml_expr.h"

/**********************
 *      TYPEDEFS
 **********************/

typedef  void * (*lv_xml_component_process_cb_t)(lv_obj_t * parent, const char * data, const char ** attrs);

struct _lv_xml_component_scope_t {
    const char * name;
    lv_ll_t style_ll;
    lv_ll_t const_ll;
    lv_ll_t param_ll;
    lv_ll_t gradient_ll;
    lv_ll_t subjects_ll;
    lv_ll_t subject_expr_ll;   /**< <subject_expr> derived subjects: (expr, ctx) pairs freed at scope teardown */
    lv_ll_t timeline_ll;
    lv_ll_t font_ll;
    lv_ll_t image_ll;
    lv_ll_t event_ll;
    const char * view_def;
    const char * extends;
    uint32_t is_widget : 1;
    uint32_t is_screen : 1;
    struct _lv_xml_component_scope_t * next;
};

typedef struct {
    const char * name;
    const char * value;
} lv_xml_const_t;

typedef struct {
    const char * name;
    lv_subject_t * subject;
} lv_xml_subject_t;

/**
 * A `<subject_expr>` derived-subject record: the compiled expression and its
 * shared observer context, kept only so scope teardown can free them. The
 * derived `lv_subject_t*` itself lives in `subjects_ll` (via lv_xml_subject_t)
 * like any other subject and is freed by the existing subjects_ll cleanup.
 */
typedef struct {
    lv_xml_expr_t * expr;
    void * ctx;                  /* subject_expr_ctx_t*, defined in lv_xml_component.c */
    lv_observer_t ** observers;  /* one per distinct input subject, detached at teardown */
    uint32_t observer_count;
} lv_xml_subject_expr_t;

typedef struct {
    const char * name;
    lv_ll_t anims_ll;
} lv_xml_timeline_t;

typedef struct {
    const char * name;
    const char * def;
    const char * type;
} lv_xml_param_t;

typedef struct {
    const char * name;
    lv_grad_dsc_t grad_dsc;
} lv_xml_grad_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Initialize the components system.
 */
void lv_xml_component_init(void);

/**
 * Initialize the linked lists of a component context
 * @param scope     pointer to a component contexts
 */
void lv_xml_component_scope_init(lv_xml_component_scope_t * scope);

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_XML_COMPONENT_PRIVATE_H*/
