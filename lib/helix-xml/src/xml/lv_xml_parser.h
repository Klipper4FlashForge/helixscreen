/**
 * @file lv_xml_parser.h
 *
 */

#ifndef LV_XML_PARSER_H
#define LV_XML_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../misc/lv_types.h"
#include "lv_xml_types.h"
#include "../misc/lv_style.h"
#if LV_USE_XML

#include "lv_xml_component.h"
#include "lv_xml_component_private.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

typedef enum {
    LV_XML_PARSER_SECTION_NONE,
    LV_XML_PARSER_SECTION_API,
    LV_XML_PARSER_SECTION_CONSTS,
    LV_XML_PARSER_SECTION_GRAD,
    LV_XML_PARSER_SECTION_GRAD_STOP,
    LV_XML_PARSER_SECTION_STYLES,
    LV_XML_PARSER_SECTION_FONTS,
    LV_XML_PARSER_SECTION_IMAGES,
    LV_XML_PARSER_SECTION_SUBJECTS,
    LV_XML_PARSER_SECTION_ANIMATION,
    LV_XML_PARSER_SECTION_INCLUDE_TIMELINE,
    LV_XML_PARSER_SECTION_TIMELINE,
    LV_XML_PARSER_SECTION_VIEW
} lv_xml_parser_section_t;

/** One entry per open element during view parsing; accumulates PCDATA so
 *  inline text (`<text_muted>Foo</text_muted>`) can be applied at end-tag. */
typedef struct {
    lv_obj_t * item;    /**< widget the element created (NULL if none) */
    char * buf;         /**< accumulated character data (lv_malloc'd) */
    size_t len;
    size_t cap;
    bool has_conflict;  /**< element already had text=/bind_text=/translation_tag= */
} lv_xml_pcdata_entry_t;

struct _lv_xml_parser_state_t {
    const char * tag_name;
    lv_xml_component_scope_t scope;
    lv_ll_t parent_ll;
    lv_ll_t pcdata_ll;  /*Stack of lv_xml_pcdata_entry_t mirroring open elements*/
    lv_obj_t * parent;
    lv_obj_t * item;
    lv_obj_t * view;    /*Pointer to the created view during component creation*/
    void * context;     /*Custom data that can be stored during parsing*/
    const char ** parent_attrs;
    lv_xml_component_scope_t * parent_scope;
    lv_xml_parser_section_t section;
};

/**********************
 * GLOBAL PROTOTYPES
 **********************/

void lv_xml_parser_state_init(lv_xml_parser_state_t * state);

void lv_xml_parser_start_section(lv_xml_parser_state_t * state, const char * name);

void lv_xml_parser_end_section(lv_xml_parser_state_t * state, const char * name);

void * lv_xml_state_get_parent(lv_xml_parser_state_t * state);

void * lv_xml_state_get_item(lv_xml_parser_state_t * state);

/**********************
 *      MACROS
 **********************/

#endif /* LV_USE_XML */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_XML_PARSER_H*/
