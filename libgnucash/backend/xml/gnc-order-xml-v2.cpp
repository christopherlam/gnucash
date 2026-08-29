/********************************************************************\
 * gnc-order-xml-v2.c -- order xml i/o implementation         *
 *                                                                  *
 * Copyright (C) 2002 Derek Atkins <warlord@MIT.EDU>                *
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
\********************************************************************/
#include <glib.h>

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include "gncOrderP.h"
#include <guid.hpp>

#include "gnc-xml-helper.h"
#include "sixtp.h"
#include "sixtp-utils.h"
#include "sixtp-parsers.h"
#include "sixtp-utils.h"
#include "sixtp-dom-parsers.h"
#include "sixtp-dom-generators.h"

#include "gnc-xml.h"
#include "io-gncxml-gen.h"
#include "io-gncxml-v2.h"

#include "gnc-order-xml-v2.h"
#include "gnc-owner-xml-v2.h"

#define _GNC_MOD_NAME   GNC_ID_ORDER

const gchar* order_version_string = "2.0.0";

/* ids */
#define gnc_order_string "gnc:GncOrder"
#define order_guid_string "order:guid"
#define order_id_string "order:id"
#define order_owner_string "order:owner"
#define order_opened_string "order:opened"
#define order_closed_string "order:closed"
#define order_notes_string "order:notes"
#define order_reference_string "order:reference"
#define order_active_string "order:active"
#define order_slots_string "order:slots"

static void
maybe_add_string (xmlNodePtr ptr, const char* tag, const char* str)
{
    if (str && *str)
        xmlAddChild (ptr, text_to_dom_tree (tag, str));
}

static xmlNodePtr
order_dom_tree_create (GncOrder* order)
{
    xmlNodePtr ret;
    time64 tt;

    ret = xmlNewNode (NULL, BAD_CAST gnc_order_string);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST order_version_string);

    xmlAddChild (ret, guid_to_dom_tree (order_guid_string,
                                        qof_instance_get_guid (QOF_INSTANCE (order))));

    xmlAddChild (ret, text_to_dom_tree (order_id_string,
                                        gncOrderGetID (order)));

    xmlAddChild (ret, gnc_owner_to_dom_tree (order_owner_string,
                                             gncOrderGetOwner (order)));

    tt = gncOrderGetDateOpened (order);
    xmlAddChild (ret, time64_to_dom_tree (order_opened_string, tt));

    tt = gncOrderGetDateClosed (order);
    if (tt != INT64_MAX)
        xmlAddChild (ret, time64_to_dom_tree (order_closed_string, tt));

    maybe_add_string (ret, order_notes_string, gncOrderGetNotes (order));
    maybe_add_string (ret, order_reference_string, gncOrderGetReference (order));

    xmlAddChild (ret, int_to_dom_tree (order_active_string,
                                       gncOrderGetActive (order)));

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (order_slots_string,
                                                      QOF_INSTANCE (order)));

    return ret;
}

/***********************************************************************/
/* SAX-direct (streaming) order parser: reads a gnc:GncOrder straight
   off the SAX character stream, with no intermediate xmlNodePtr built
   for any of its fields. Nothing else in the codebase uses the old
   DOM-based parser this replaces, so it's gone entirely. */

struct order_sax_pdata
{
    GncOrder* order;
    GncOwner owner;
    QofBook* book;
};

static gboolean
sax_order_guid_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<order_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;

        /* Adopt an order that already exists by this guid instead of
           the fresh one sax_order_start() made. */
        GncOrder* order = gncOrderLookup (pdata->book, &guid);
        if (order)
        {
            gncOrderDestroy (pdata->order);
            pdata->order = order;
            gncOrderBeginEdit (order);
        }
        else
            gncOrderSetGUID (pdata->order, &guid);
        return TRUE;
    });
}

static gboolean
sax_order_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                  gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<order_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncOrderSetID (pdata->order, txt); return TRUE; });
}

static gboolean
sax_order_opened_ts_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<order_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        time64 t = gnc_iso8601_to_time64_gmt (txt);
        if (!dom_tree_valid_time64 (t, BAD_CAST order_opened_string)) t = 0;
        gncOrderSetDateOpened (pdata->order, t);
        return TRUE;
    });
}

static gboolean
sax_order_closed_ts_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<order_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        time64 t = gnc_iso8601_to_time64_gmt (txt);
        if (!dom_tree_valid_time64 (t, BAD_CAST order_closed_string)) t = 0;
        gncOrderSetDateClosed (pdata->order, t);
        return TRUE;
    });
}

static gboolean
sax_order_notes_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                     gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<order_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncOrderSetNotes (pdata->order, txt); return TRUE; });
}

static gboolean
sax_order_reference_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<order_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncOrderSetReference (pdata->order, txt); return TRUE; });
}

static gboolean
sax_order_active_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                      gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<order_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncOrderSetActive (pdata->order, (gboolean) val);
        return TRUE;
    });
}

static gboolean
sax_order_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                         gpointer parent_data, gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<order_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ret = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->order));
    xmlFreeNode (tree);
    return ret;
}

static gboolean
sax_order_owner_start (GSList*, gpointer parent_data, gpointer,
                       gpointer* data_for_children, gpointer*, const gchar*, gchar**)
{
    auto* pdata = static_cast<order_sax_pdata*> (parent_data);
    auto* ctx = g_new (owner_sax_ctx, 1);
    ctx->owner = &pdata->owner;
    ctx->book = pdata->book;
    *data_for_children = ctx;
    return TRUE;
}

static gboolean
sax_order_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
                 gpointer*, const gchar* tag, gchar**)
{
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }
    auto* gdata = static_cast<gxpf_data*> (global_data);
    QofBook* book = static_cast<QofBook*> (gdata->bookdata);
    auto* pdata = g_new0 (order_sax_pdata, 1);
    pdata->order = gncOrderCreate (book);
    pdata->book = book;
    gncOrderBeginEdit (pdata->order);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_order_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer global_data,
              gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<order_sax_pdata*> (data_for_children);
    if (!pdata) return TRUE;
    if (!tag) { g_free (pdata); return TRUE; }

    gncOrderSetOwner (pdata->order, &pdata->owner);
    gncOrderCommitEdit (pdata->order);

    auto* gdata = static_cast<gxpf_data*> (global_data);
    gdata->cb (tag, gdata->parsedata, pdata->order);

    g_free (pdata);
    return TRUE;
}

static void
sax_order_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
                gpointer*, const gchar*)
{
    auto* pdata = static_cast<order_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gncOrderDestroy (pdata->order);
    g_free (pdata);
}

static sixtp*
order_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_order_start,
        SIXTP_END_HANDLER_ID, sax_order_end,
        SIXTP_FAIL_HANDLER_ID, sax_order_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        order_guid_string, restore_char_generator (sax_order_guid_end),
        order_id_string, restore_char_generator (sax_order_id_end),
        order_notes_string, restore_char_generator (sax_order_notes_end),
        order_reference_string, restore_char_generator (sax_order_reference_end),
        order_active_string, restore_char_generator (sax_order_active_end),
        order_slots_string, sixtp_dom_parser_new_rooted (sax_order_slots_dom_end, NULL, NULL),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    sixtp_add_sub_parser (p, order_owner_string, sax_owner_parser_new (sax_order_owner_start));
    sixtp_add_sub_parser (p, order_opened_string, sax_time64_parser_new (sax_order_opened_ts_end));
    sixtp_add_sub_parser (p, order_closed_string, sax_time64_parser_new (sax_order_closed_ts_end));

    return p;
}

static gboolean
order_should_be_saved (GncOrder* order)
{
    const char* id;

    /* make sure this is a valid order before we save it -- should have an ID */
    id = gncOrderGetID (order);
    if (id == NULL || *id == '\0')
        return FALSE;

    return TRUE;
}

static void
do_count (QofInstance* order_p, gpointer count_p)
{
    int* count = static_cast<decltype (count)> (count_p);
    if (order_should_be_saved ((GncOrder*) order_p))
        (*count)++;
}

static int
order_get_count (QofBook* book)
{
    int count = 0;
    qof_object_foreach (_GNC_MOD_NAME, book, do_count, (gpointer) &count);
    return count;
}

static void
xml_add_order (QofInstance* order_p, gpointer out_p)
{
    xmlNodePtr node;
    GncOrder* order = (GncOrder*) order_p;
    FILE* out = static_cast<decltype (out)> (out_p);

    if (ferror (out))
        return;
    if (!order_should_be_saved (order))
        return;

    node = order_dom_tree_create (order);
    xmlElemDump (out, NULL, node);
    xmlFreeNode (node);
    if (ferror (out) || fprintf (out, "\n") < 0)
        return;
}

static gboolean
order_write (FILE* out, QofBook* book)
{
    qof_object_foreach_sorted (_GNC_MOD_NAME, book, xml_add_order, (gpointer) out);
    return ferror (out) == 0;
}

static gboolean
order_ns (FILE* out)
{
    g_return_val_if_fail (out, FALSE);
    return gnc_xml2_write_namespace_decl (out, "order");
}

void
gnc_order_xml_initialize (void)
{
    static GncXmlDataType_t be_data =
    {
        GNC_FILE_BACKEND_VERS,
        gnc_order_string,
        order_sixtp_parser_create,
        NULL,           /* add_item */
        order_get_count,
        order_write,
        NULL,           /* scrub */
        order_ns,
    };

    gnc_xml_register_backend(be_data);
}
