/********************************************************************\
 * gnc-commodity-xml-v2.c -- commodity xml i/o implementation       *
 *                                                                  *
 * Copyright (C) 2001 James LewisMoss <dres@debian.org>             *
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
#include <string.h>
#include "AccountP.hpp"
#include "Account.h"

#include "gnc-xml-helper.h"
#include "sixtp.h"
#include "sixtp-utils.h"
#include "sixtp-parsers.h"
#include "sixtp-utils.h"
#include "sixtp-dom-parsers.h"
#include "sixtp-dom-generators.h"

#include "gnc-xml.h"
#include "io-gncxml-gen.h"

static QofLogModule log_module = GNC_MOD_IO;

const gchar* commodity_version_string = "2.0.0";

/* ids */
#define gnc_commodity_string "gnc:commodity"
#define cmdty_namespace      "cmdty:space"
#define cmdty_id             "cmdty:id"
#define cmdty_name           "cmdty:name"
#define cmdty_xcode          "cmdty:xcode"
#define cmdty_fraction       "cmdty:fraction"
#define cmdty_get_quotes     "cmdty:get_quotes"
#define cmdty_quote_source   "cmdty:quote_source"
#define cmdty_quote_tz       "cmdty:quote_tz"
#define cmdty_slots          "cmdty:slots"

xmlNodePtr
gnc_commodity_dom_tree_create (const gnc_commodity* com)
{
    gnc_quote_source* source;
    const char* string;
    xmlNodePtr ret;
    gboolean currency = gnc_commodity_is_iso (com);
    xmlNodePtr slotsnode =
        qof_instance_slots_to_dom_tree (cmdty_slots, QOF_INSTANCE (com));

    if (currency && !gnc_commodity_get_quote_flag (com) && !slotsnode)
        return NULL;

    ret = xmlNewNode (NULL, BAD_CAST gnc_commodity_string);

    xmlSetProp (ret, BAD_CAST "version", BAD_CAST commodity_version_string);

    xmlAddChild (ret, text_to_dom_tree (cmdty_namespace,
                                        gnc_commodity_get_namespace (com)));
    xmlAddChild (ret, text_to_dom_tree (cmdty_id,
                                        gnc_commodity_get_mnemonic (com)));

    if (!currency)
    {
        if (gnc_commodity_get_fullname (com))
        {
            xmlAddChild (ret, text_to_dom_tree (cmdty_name,
                                                gnc_commodity_get_fullname (com)));
        }

        const char* cusip = gnc_commodity_get_cusip (com);
        if (cusip && *cusip)
        {
            xmlAddChild (ret, text_to_dom_tree (cmdty_xcode, cusip));
        }

        xmlAddChild (ret, int_to_dom_tree (cmdty_fraction,
                                           gnc_commodity_get_fraction (com)));
    }

    if (gnc_commodity_get_quote_flag (com))
    {
        xmlNewChild (ret, NULL, BAD_CAST cmdty_get_quotes, NULL);
        source = gnc_commodity_get_quote_source (com);
        if (source)
            xmlAddChild (ret, text_to_dom_tree (cmdty_quote_source,
                                                gnc_quote_source_get_internal_name (source)));
        string = gnc_commodity_get_quote_tz (com);
        if (string)
            xmlAddChild (ret, text_to_dom_tree (cmdty_quote_tz, string));
    }

    if (slotsnode)
        xmlAddChild (ret, slotsnode);

    return ret;
}

/***********************************************************************/

static gboolean
valid_commodity (gnc_commodity* com)
{
    if (gnc_commodity_get_namespace (com) == NULL)
    {
        PWARN ("Invalid commodity: no namespace");
        return FALSE;
    }
    if (gnc_commodity_get_mnemonic (com) == NULL)
    {
        PWARN ("Invalid commodity: no mnemonic");
        return FALSE;
    }
    if (gnc_commodity_get_fraction (com) == 0)
    {
        PWARN ("Invalid commodity: 0 fraction");
        return FALSE;
    }
    return TRUE;
}

/***********************************************************************/
/* SAX-direct (streaming) commodity parser.
 *
 * Reads a gnc:commodity straight off the SAX character stream, with no
 * intermediate xmlNodePtr built for any of its fields. Only cmdty:slots
 * (a kvp frame) still goes through a narrowly scoped DOM sub-parser.
 *
 * If this element declares an ISO currency that already exists in the
 * book's commodity table, its fields should default to that existing
 * commodity's before this element's own fields are applied as
 * overrides. maybe_apply_currency_defaults() below runs that check the
 * moment both cmdty:space and cmdty:id have been seen; this writer
 * always emits namespace and id first, so nothing else has been
 * applied to the new commodity yet at that point.
 */

struct commodity_sax_pdata
{
    gnc_commodity* com;
    QofBook* book;
    gboolean has_namespace;
    gboolean has_id;
    gboolean checked_currency_defaults;
};

static void
maybe_apply_currency_defaults (commodity_sax_pdata* pdata)
{
    if (pdata->checked_currency_defaults ||
        !pdata->has_namespace || !pdata->has_id)
        return;
    pdata->checked_currency_defaults = TRUE;

    const char* exchange = gnc_commodity_get_namespace (pdata->com);
    const char* mnemonic = gnc_commodity_get_mnemonic (pdata->com);
    if (exchange && gnc_commodity_namespace_is_iso (exchange) && mnemonic)
    {
        auto* table = gnc_commodity_table_get_table (pdata->book);
        gnc_commodity* old_com = gnc_commodity_table_lookup (table, exchange, mnemonic);
        if (old_com)
            gnc_commodity_copy (pdata->com, old_com);
    }
}

static gboolean
sax_cmdty_namespace_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<commodity_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (char* txt) -> gboolean
    {
        gnc_commodity_set_namespace (pdata->com, g_strstrip (txt));
        pdata->has_namespace = TRUE;
        maybe_apply_currency_defaults (pdata);
        return TRUE;
    });
}

static gboolean
sax_cmdty_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                  gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<commodity_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (char* txt) -> gboolean
    {
        gnc_commodity_set_mnemonic (pdata->com, g_strstrip (txt));
        pdata->has_id = TRUE;
        maybe_apply_currency_defaults (pdata);
        return TRUE;
    });
}

static gboolean
sax_cmdty_name_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<commodity_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (char* txt) -> gboolean
    { gnc_commodity_set_fullname (pdata->com, g_strstrip (txt)); return TRUE; });
}

static gboolean
sax_cmdty_xcode_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                     gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<commodity_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (char* txt) -> gboolean
    { gnc_commodity_set_cusip (pdata->com, g_strstrip (txt)); return TRUE; });
}

static gboolean
sax_cmdty_fraction_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<commodity_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val;
        if (string_to_gint64 (txt, &val))
            gnc_commodity_set_fraction (pdata->com, val);
        return TRUE;
    });
}

static gboolean
sax_cmdty_get_quotes_end (gpointer, GSList*, GSList*, gpointer parent_data,
                          gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<commodity_sax_pdata*> (parent_data);
    gnc_commodity_set_quote_flag (pdata->com, TRUE);
    return TRUE;
}

static gboolean
sax_cmdty_quote_source_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                            gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<commodity_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gnc_quote_source* source = gnc_quote_source_lookup_by_internal (txt);
        if (!source)
            source = gnc_quote_source_add_new (txt, FALSE);
        gnc_commodity_set_quote_source (pdata->com, source);
        return TRUE;
    });
}

static gboolean
sax_cmdty_quote_tz_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<commodity_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (char* txt) -> gboolean
    { gnc_commodity_set_quote_tz (pdata->com, g_strstrip (txt)); return TRUE; });
}

static gboolean
sax_cmdty_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                         gpointer parent_data, gpointer, gpointer* result, const gchar*)
{
    auto* pdata = static_cast<commodity_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    if (tree)
    {
        /* Slot parsing errors aren't treated as fatal for the commodity. */
        dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->com));
        xmlFreeNode (tree);
    }
    *result = nullptr;
    return TRUE;
}

static gboolean
sax_cmdty_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
                 gpointer*, const gchar* tag, gchar**)
{
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }
    auto* gdata = static_cast<gxpf_data*> (global_data);
    auto* pdata = g_new0 (commodity_sax_pdata, 1);
    pdata->book = static_cast<QofBook*> (gdata->bookdata);
    pdata->com = gnc_commodity_new (pdata->book, NULL, NULL, NULL, NULL, 0);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_cmdty_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer global_data,
              gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<commodity_sax_pdata*> (data_for_children);
    auto* gdata = static_cast<gxpf_data*> (global_data);

    if (!tag)
        return TRUE;

    gnc_commodity* com = pdata->com;
    g_free (pdata);

    if (!valid_commodity (com))
    {
        PWARN ("Invalid commodity parsed");
        gnc_commodity_destroy (com);
        return FALSE;
    }

    gdata->cb (tag, gdata->parsedata, com);
    return TRUE;
}

static void
sax_cmdty_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
                gpointer*, const gchar*)
{
    auto* pdata = static_cast<commodity_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gnc_commodity_destroy (pdata->com);
    g_free (pdata);
}

sixtp*
gnc_commodity_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_cmdty_start,
        SIXTP_END_HANDLER_ID, sax_cmdty_end,
        SIXTP_FAIL_HANDLER_ID, sax_cmdty_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        cmdty_namespace, restore_char_generator (sax_cmdty_namespace_end),
        cmdty_id, restore_char_generator (sax_cmdty_id_end),
        cmdty_name, restore_char_generator (sax_cmdty_name_end),
        cmdty_xcode, restore_char_generator (sax_cmdty_xcode_end),
        cmdty_fraction, restore_char_generator (sax_cmdty_fraction_end),
        cmdty_get_quotes, restore_char_generator (sax_cmdty_get_quotes_end),
        cmdty_quote_source, restore_char_generator (sax_cmdty_quote_source_end),
        cmdty_quote_tz, restore_char_generator (sax_cmdty_quote_tz_end),
        cmdty_slots, sixtp_dom_parser_new_rooted (sax_cmdty_slots_dom_end, NULL, NULL),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    /* Self-reference under our own tag: see the equivalent comment in
       gnc_transaction_sixtp_parser_create() in gnc-transaction-xml-v2.cpp. */
    sixtp_add_sub_parser (p, gnc_commodity_string, p);

    return p;
}
