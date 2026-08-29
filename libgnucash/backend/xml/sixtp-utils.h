/********************************************************************
 * sixtp-utils.h                                                    *
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

#ifndef SIXTP_UTILS_H
#define SIXTP_UTILS_H
#include "qof.h"
#include "sixtp.h"

typedef struct
{
    time64 time;
    guint s_block_count;
} Time64ParseInfo;

gboolean isspace_str (const gchar* str, int nomorethan);

gboolean allow_and_ignore_only_whitespace (GSList* sibling_data,
                                           gpointer parent_data,
                                           gpointer global_data,
                                           gpointer* result,
                                           const char* text,
                                           int length);

gboolean generic_accumulate_chars (GSList* sibling_data,
                                   gpointer parent_data,
                                   gpointer global_data,
                                   gpointer* result,
                                   const char* text,
                                   int length);


void generic_free_data_for_children (gpointer data_for_children,
                                     GSList* data_from_children,
                                     GSList* sibling_data,
                                     gpointer parent_data,
                                     gpointer global_data,
                                     gpointer* result,
                                     const gchar* tag);

gchar* concatenate_child_result_chars (GSList* data_from_children);

gboolean string_to_double (std::string_view, double* result);

gboolean string_to_gint64 (std::string_view, gint64* v);

gboolean string_to_guint16 (std::string_view, guint16* v);

gboolean string_to_guint (std::string_view, guint* v);

gboolean hex_string_to_binary (const gchar* str,  void** v, guint64* data_len);

gboolean generic_return_chars_end_handler (gpointer data_for_children,
                                           GSList* data_from_children,
                                           GSList* sibling_data,
                                           gpointer parent_data,
                                           gpointer global_data,
                                           gpointer* result,
                                           const gchar* tag);

sixtp* simple_chars_only_parser_new (sixtp_end_handler end_handler);

gboolean generic_timespec_start_handler (GSList* sibling_data,
                                         gpointer parent_data,
                                         gpointer global_data,
                                         gpointer* data_for_children,
                                         gpointer* result,
                                         const gchar* tag, gchar** attrs);

gboolean timespec_parse_ok (Time64ParseInfo* info);

gboolean generic_timespec_secs_end_handler (
    gpointer data_for_children,
    GSList*  data_from_children, GSList* sibling_data,
    gpointer parent_data, gpointer global_data,
    gpointer* result, const gchar* tag);

gboolean generic_timespec_nsecs_end_handler (
    gpointer data_for_children,
    GSList*  data_from_children, GSList* sibling_data,
    gpointer parent_data, gpointer global_data,
    gpointer* result, const gchar* tag);


sixtp* generic_timespec_parser_new (sixtp_end_handler end_handler);

gboolean generic_guid_end_handler (
    gpointer data_for_children,
    GSList*  data_from_children, GSList* sibling_data,
    gpointer parent_data, gpointer global_data,
    gpointer* result, const gchar* tag);

sixtp* generic_guid_parser_new (void);

gboolean generic_gnc_numeric_end_handler (
    gpointer data_for_children,
    GSList*  data_from_children, GSList* sibling_data,
    gpointer parent_data, gpointer global_data,
    gpointer* result, const gchar* tag);

sixtp* generic_gnc_numeric_parser_new (void);

sixtp* restore_char_generator (sixtp_end_handler ender);

/* ---- shared helpers for SAX-direct (non-DOM) v2 object parsers ----
 *
 * These back the streaming parsers in gnc-transaction-xml-v2.cpp,
 * gnc-account-xml-v2.cpp, and friends: reading a v2 record straight off
 * the SAX character stream into the engine object, with no per-record
 * xmlNodePtr ever built. See the "SAX-direct (streaming) parser"
 * comments in gnc-transaction-xml-v2.cpp for the full rationale.
 */

/* Concatenate a leaf element's accumulated character fragments (no DOM
   node is ever built for them) and hand the result to f; frees the
   temporary string itself. Use as the body of a sixtp_end_handler
   registered via restore_char_generator(). */
template <typename F>
inline gboolean
sax_apply_chars (GSList* data_from_children, F&& f)
{
    gchar* txt = concatenate_child_result_chars (data_from_children);
    if (!txt)
        return FALSE;
    gboolean ok = f (txt);
    g_free (txt);
    return ok;
}

/* For wrapper elements that have no text of their own (e.g.
   trn:date-posted, act:currency's <cmdty:.../> pair's container) but
   whose children need to see the same pdata pointer the wrapper itself
   received: passes parent_data straight through as data_for_children. */
gboolean sax_passthrough_start (GSList* sibling_data, gpointer parent_data,
                                gpointer global_data,
                                gpointer* data_for_children, gpointer* result,
                                const gchar* tag, gchar** attrs);

/* A <.../> wrapper with exactly a cmdty:space and cmdty:id child, e.g.

     <price:commodity>
       <cmdty:space>NASDAQ</cmdty:space>
       <cmdty:id>RHAT</cmdty:id>
     </price:commodity>

   ender receives the two leaf values as SIXTP_CHILD_RESULT_NODE entries
   (tags "cmdty:space"/"cmdty:id") in its data_from_children list --
   look them up with is_child_result_from_node_named() -- and is
   responsible for doing the book's commodity table lookup and applying
   the result; parent_data is passed through unchanged from the wrapper
   element's own parent, exactly as for any other sixtp_end_handler. */
sixtp* sax_commodity_ref_parser_new (sixtp_end_handler ender);

/* A <.../> wrapper containing exactly a ts:date child (v2's time64
   encoding), tolerating an optional, ignored ts:ns sibling that some
   older files still carry:

     <trn:date-posted>
       <ts:date>2020-01-01 10:59:00 +0000</ts:date>
     </trn:date-posted>

   ts_ender is the sixtp_end_handler for the inner ts:date leaf itself
   (not the wrapper): it receives the wrapper's own parent_data
   (via sax_passthrough_start()) and the accumulated ts:date text. */
sixtp* sax_time64_parser_new (sixtp_end_handler ts_ender);

#endif /* _SIXTP_UTILS_H_ */
