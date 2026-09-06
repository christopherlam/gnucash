/********************************************************************
 * sixtp.h -- header file for XML parsing                           *
 * Copyright 2001 Gnumatic, Inc.                                    *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
 *                                                                  *
 ********************************************************************/

#ifndef SIXTP_H
#define SIXTP_H

#include <glib.h>

#include <stdio.h>

#include <stdarg.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gnc-engine.h"

#include "gnc-xml-helper.h"
#include "gnc-backend-xml.h"

typedef struct sixtp_gdv2 sixtp_gdv2;
typedef void (*countCallbackFn) (sixtp_gdv2* gd, const char* type);

typedef struct
{
    int accounts_total;
    int accounts_loaded;

    int books_total;
    int books_loaded;

    int commodities_total;
    int commodities_loaded;

    int transactions_total;
    int transactions_loaded;

    int prices_total;
    int prices_loaded;

    int schedXactions_total;
    int schedXactions_loaded;

    int budgets_total;
    int budgets_loaded;
} load_counter;

struct sixtp_gdv2
{
    QofBook* book;
    load_counter counter;
    countCallbackFn countCallback;
    QofBePercentageFunc gui_display_fn;
    gboolean exporting;
};
typedef struct _sixtp_child_result sixtp_child_result;

typedef gboolean (*sixtp_start_handler) (GSList* sibling_data,
                                         gpointer parent_data,
                                         gpointer global_data,
                                         gpointer* data_for_children,
                                         gpointer* result,
                                         const gchar* tag,
                                         gchar** attrs);

typedef gboolean (*sixtp_before_child_handler) (gpointer data_for_children,
                                                GSList* data_from_children,
                                                GSList* sibling_data,
                                                gpointer parent_data,
                                                gpointer global_data,
                                                gpointer* result,
                                                const gchar* tag,
                                                const gchar* child_tag);

typedef gboolean (*sixtp_after_child_handler) (gpointer data_for_children,
                                               GSList* data_from_children,
                                               GSList* sibling_data,
                                               gpointer parent_data,
                                               gpointer global_data,
                                               gpointer* result,
                                               const gchar* tag,
                                               const gchar* child_tag,
                                               sixtp_child_result* child_result);

typedef gboolean (*sixtp_end_handler) (gpointer data_for_children,
                                       GSList* data_from_children,
                                       GSList* sibling_data,
                                       gpointer parent_data,
                                       gpointer global_data,
                                       gpointer* result,
                                       const gchar* tag);

typedef gboolean (*sixtp_characters_handler) (GSList* sibling_data,
                                              gpointer parent_data,
                                              gpointer global_data,
                                              gpointer* result,
                                              const char* text,
                                              int length);

typedef void (*sixtp_result_handler) (sixtp_child_result* result);

typedef void (*sixtp_fail_handler) (gpointer data_for_children,
                                    GSList* data_from_children,
                                    GSList* sibling_data,
                                    gpointer parent_data,
                                    gpointer global_data,
                                    gpointer* result,
                                    const gchar* tag);

typedef void (*sixtp_push_handler) (xmlParserCtxtPtr xml_context,
                                    gpointer user_data);

typedef struct sixtp
{
    /* If you change this, don't forget to modify all the copy/etc. functions */
    sixtp_start_handler start_handler = nullptr;
    sixtp_before_child_handler before_child = nullptr;
    sixtp_after_child_handler after_child = nullptr;
    sixtp_end_handler end_handler = nullptr;
    sixtp_characters_handler characters_handler = nullptr;

    sixtp_fail_handler fail_handler = nullptr;
    /* called for failures before the close tag */

    sixtp_result_handler cleanup_result = nullptr; /* called unless failure */
    sixtp_result_handler cleanup_chars = nullptr; /* called unless failure */

    sixtp_result_handler result_fail_handler = nullptr;
    /* called to cleanup results from this node on failure */

    sixtp_result_handler chars_fail_handler = nullptr;
    /* called to cleanup character results when cleaning up this node's
       children. */

    std::map<std::string, sixtp*> child_parsers;
} sixtp;

typedef enum
{
    SIXTP_NO_MORE_HANDLERS,

    SIXTP_START_HANDLER_ID,
    SIXTP_BEFORE_CHILD_HANDLER_ID,
    SIXTP_AFTER_CHILD_HANDLER_ID,
    SIXTP_END_HANDLER_ID,
    SIXTP_CHARACTERS_HANDLER_ID,

    SIXTP_FAIL_HANDLER_ID,

    SIXTP_CLEANUP_RESULT_ID,
    SIXTP_CLEANUP_CHARS_ID,

    SIXTP_RESULT_FAIL_ID,

    SIXTP_CHARS_FAIL_ID,
} sixtp_handler_type;

/* completely invalid tag for xml */
#define SIXTP_MAGIC_CATCHER "&MAGIX&"

typedef enum
{
    SIXTP_CHILD_RESULT_CHARS,
    SIXTP_CHILD_RESULT_NODE
} sixtp_child_result_type;

struct _sixtp_child_result
{
    sixtp_child_result_type type = SIXTP_CHILD_RESULT_CHARS;
    gchar* tag = nullptr; /* NULL for a CHARS node. */
    gpointer data = nullptr;
    gboolean should_cleanup = FALSE;
    sixtp_result_handler cleanup_handler = nullptr;
    sixtp_result_handler fail_handler = nullptr;
};

/* Full definition lives in sixtp-stack.h; the parser's frame stack owns
   its frames, so a stack is just a vector of them. */
struct sixtp_stack_frame;
using sixtp_frame_stack = std::vector<std::unique_ptr<sixtp_stack_frame>>;

typedef struct sixtp_sax_data
{
    gboolean parsing_ok = TRUE;
    sixtp_frame_stack stack;
    gpointer global_data = nullptr;
    xmlParserCtxtPtr saxParserCtxt = nullptr;
    sixtp* bad_xml_parser = nullptr;
} sixtp_sax_data;

gboolean is_child_result_from_node_named (sixtp_child_result* cr,
                                          const char* tag);
void sixtp_child_free_data (sixtp_child_result* result);
void sixtp_child_result_destroy (sixtp_child_result* r);
void sixtp_child_result_print (sixtp_child_result* cr, FILE* f);

void sixtp_sax_start_handler (void* user_data, const xmlChar* name,
                              const xmlChar** attrs);
void sixtp_sax_characters_handler (void* user_data, const xmlChar* text,
                                   int len);
void sixtp_sax_end_handler (void* user_data, const xmlChar* name);

sixtp* sixtp_new (void);
void sixtp_destroy (sixtp* sp);

void sixtp_handle_catastrophe (sixtp_sax_data* sax_data);
xmlEntityPtr sixtp_sax_get_entity_handler (void* user_data,
                                           const xmlChar* name);

gboolean sixtp_parse_file (sixtp* sixtp, const char* filename,
                           gpointer data_for_top_level, gpointer global_data,
                           gpointer* parse_result);
gboolean sixtp_parse_fd (sixtp* sixtp, FILE* fd,
                         gpointer data_for_top_level, gpointer global_data,
                         gpointer* parse_result);
gboolean sixtp_parse_buffer (sixtp* sixtp, char* bufp, int bufsz,
                             gpointer data_for_top_level, gpointer global_data,
                             gpointer* parse_result);
gboolean sixtp_parse_push (sixtp* sixtp, sixtp_push_handler push_handler,
                           gpointer push_user_data, gpointer data_for_top_level,
                           gpointer global_data, gpointer* parse_result);

void sixtp_set_start (sixtp* parser, sixtp_start_handler start_handler);
void sixtp_set_before_child (sixtp* parser,
                             sixtp_before_child_handler handler);
void sixtp_set_after_child (sixtp* parser, sixtp_after_child_handler handler);
void sixtp_set_end (sixtp* parser, sixtp_end_handler end_handler);
void sixtp_set_chars (sixtp* parser, sixtp_characters_handler char_handler);
void sixtp_set_cleanup_result (sixtp* parser, sixtp_result_handler handler);
void sixtp_set_cleanup_chars (sixtp* parser, sixtp_result_handler handler);
void sixtp_set_fail (sixtp* parser, sixtp_fail_handler handler);
void sixtp_set_result_fail (sixtp* parser, sixtp_result_handler handler);
void sixtp_set_chars_fail (sixtp* parser, sixtp_result_handler handler);

sixtp* sixtp_set_any (sixtp* tochange, gboolean cleanup, ...);
sixtp* sixtp_add_some_sub_parsers (sixtp* tochange, gboolean cleanup, ...);

gboolean sixtp_add_sub_parser (sixtp* parser, const gchar* tag,
                               sixtp* sub_parser);

QofBookFileType gnc_is_our_xml_file (const char* filename,
                                     gboolean* with_encoding);

QofBookFileType gnc_is_our_first_xml_chunk (char* chunk,
                                            gboolean* with_encoding);
/** Call after loading each record */
void sixtp_run_callback (sixtp_gdv2* data, const char* type);

#endif /* _SIXTP_H_ */
