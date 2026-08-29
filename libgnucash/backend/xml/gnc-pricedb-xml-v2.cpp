/********************************************************************
 * gnc-pricedb-xml-v2.c -- xml routines for price db                *
 * Copyright (C) 2001 Gnumatic, Inc.                                *
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
 *******************************************************************/
#include <config.h>

#include <string.h>
#include "gnc-pricedb.h"
#include "gnc-pricedb-p.h"

#include "gnc-xml.h"
#include "sixtp.h"
#include "sixtp-utils.h"
#include "sixtp-parsers.h"
#include "sixtp-dom-parsers.h"
#include "sixtp-dom-generators.h"
#include "io-gncxml-gen.h"
#include "io-gncxml-v2.h"
#include <guid.hpp>
#include <gnc-numeric.h>
#include <gnc-date.h>
#include <gnc-commodity.h>

/* This static indicates the debugging module that this .o belongs to.  */
static QofLogModule log_module = GNC_MOD_IO;

/* Read and Write the pricedb as XML -- something like this:

  <pricedb>
    price-1
    price-2
    ...
  </pricedb>

  where each price should look roughly like this:

  <price>
    <price:id>
      00000000111111112222222233333333
    </price:id>
    <price:commodity>
      <cmdty:space>NASDAQ</cmdty:space>
      <cmdty:id>RHAT</cmdty:id>
    </price:commodity>
    <price:currency>
      <cmdty:space>ISO?</cmdty:space>
      <cmdty:id>USD</cmdty:id>
    </price:currency>
    <price:time><ts:date>Mon ...</ts:date><ts:ns>12</ts:ns></price:time>
    <price:source>Finance::Quote</price:source>
    <price:type>bid</price:type>
    <price:value>11011/100</price:value>
  </price>

*/

/***********************************************************************/
/* READING */
/***********************************************************************/

/****************************************************************************/
/* <price>

  restores a price.  Does so via a walk of the XML tree in memory.
  Returns a GNCPrice * in result.

  Right now, a price is legitimate even if all of it's fields are not
  set.  We may need to change that later, but at the moment.

*/

/* SAX-direct (streaming) price parser: reads a <price> straight off the
 * SAX character stream, with no intermediate xmlNodePtr built for any
 * of its fields. */

struct price_sax_pdata
{
    GNCPrice* price;
    QofBook* book;
};

static gboolean
sax_price_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                  gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<price_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        gnc_price_set_guid (pdata->price, &guid);
        return TRUE;
    });
}

/* Unlike trn:currency/act:commodity, an unresolvable commodity/currency
   ref fails the whole price rather than being left NULL. */
static gboolean
sax_price_commodity_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<price_sax_pdata*> (parent_data);
    gchar* space = nullptr;
    gchar* id = nullptr;
    for (GSList* lp = dfc; lp; lp = lp->next)
    {
        auto* cr = static_cast<sixtp_child_result*> (lp->data);
        if (is_child_result_from_node_named (cr, "cmdty:space"))
            space = static_cast<gchar*> (cr->data);
        else if (is_child_result_from_node_named (cr, "cmdty:id"))
            id = static_cast<gchar*> (cr->data);
    }
    if (!space || !id) return FALSE;
    g_strstrip (space);
    g_strstrip (id);
    auto* table = gnc_commodity_table_get_table (pdata->book);
    if (!table) return FALSE;
    gnc_commodity* ref = gnc_commodity_table_lookup (table, space, id);
    if (!ref) return FALSE;
    gnc_price_set_commodity (pdata->price, ref);
    return TRUE;
}

static gboolean
sax_price_currency_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<price_sax_pdata*> (parent_data);
    gchar* space = nullptr;
    gchar* id = nullptr;
    for (GSList* lp = dfc; lp; lp = lp->next)
    {
        auto* cr = static_cast<sixtp_child_result*> (lp->data);
        if (is_child_result_from_node_named (cr, "cmdty:space"))
            space = static_cast<gchar*> (cr->data);
        else if (is_child_result_from_node_named (cr, "cmdty:id"))
            id = static_cast<gchar*> (cr->data);
    }
    if (!space || !id) return FALSE;
    g_strstrip (space);
    g_strstrip (id);
    auto* table = gnc_commodity_table_get_table (pdata->book);
    if (!table) return FALSE;
    gnc_commodity* ref = gnc_commodity_table_lookup (table, space, id);
    if (!ref) return FALSE;
    gnc_price_set_currency (pdata->price, ref);
    return TRUE;
}

static gboolean
sax_price_time_ts_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<price_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        time64 t = gnc_iso8601_to_time64_gmt (txt);
        if (!dom_tree_valid_time64 (t, BAD_CAST "price:time")) t = 0;
        gnc_price_set_time64 (pdata->price, t);
        return TRUE;
    });
}

static gboolean
sax_price_source_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                      gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<price_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gnc_price_set_source_string (pdata->price, txt); return TRUE; });
}

static gboolean
sax_price_type_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<price_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gnc_price_set_typestr (pdata->price, txt); return TRUE; });
}

static gboolean
sax_price_value_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                     gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<price_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gnc_numeric num = gnc_numeric_from_string (txt);
        gnc_price_set_value (pdata->price, gnc_numeric_check (num) ? gnc_numeric_zero () : num);
        return TRUE;
    });
}

static gboolean
sax_price_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
                 gpointer*, const gchar* tag, gchar**)
{
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }
    auto* gdata = static_cast<gxpf_data*> (global_data);
    auto* pdata = g_new (price_sax_pdata, 1);
    pdata->book = static_cast<QofBook*> (gdata->bookdata);
    pdata->price = gnc_price_create (pdata->book);
    gnc_price_begin_edit (pdata->price);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_price_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
              gpointer* result, const gchar* tag)
{
    auto* pdata = static_cast<price_sax_pdata*> (data_for_children);
    if (!tag)
        return TRUE;
    gnc_price_commit_edit (pdata->price);
    *result = pdata->price;
    g_free (pdata);
    return TRUE;
}

static void
sax_price_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
                gpointer*, const gchar*)
{
    auto* pdata = static_cast<price_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gnc_price_unref (pdata->price);
    g_free (pdata);
}

static void
cleanup_gnc_price (sixtp_child_result* result)
{
    if (result->data) gnc_price_unref ((GNCPrice*) result->data);
}

static sixtp*
gnc_price_parser_new (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_price_start,
        SIXTP_END_HANDLER_ID, sax_price_end,
        SIXTP_FAIL_HANDLER_ID, sax_price_fail,
        SIXTP_CLEANUP_RESULT_ID, cleanup_gnc_price,
        SIXTP_RESULT_FAIL_ID, cleanup_gnc_price,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        "price:id", restore_char_generator (sax_price_id_end),
        "price:source", restore_char_generator (sax_price_source_end),
        "price:type", restore_char_generator (sax_price_type_end),
        "price:value", restore_char_generator (sax_price_value_end),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    sixtp_add_sub_parser (p, "price:commodity",
                          sax_commodity_ref_parser_new (sax_price_commodity_end));
    sixtp_add_sub_parser (p, "price:currency",
                          sax_commodity_ref_parser_new (sax_price_currency_end));
    sixtp_add_sub_parser (p, "price:time", sax_time64_parser_new (sax_price_time_ts_end));

    return p;
}


/****************************************************************************/
/* <pricedb> (lineage <ledger-data>)

   restores a pricedb.  We allocate the new db in the start block, the
   children add to it, and it gets returned in result.  Note that the
   cleanup handler will destroy the pricedb, so the parent needs to
   stop that if desired.

   result: GNCPriceDB*

   start: create new GNCPriceDB*, and leave in *data_for_children.
   cleanup-result: destroy GNCPriceDB*
   result-fail: destroy GNCPriceDB*

*/

static gboolean
pricedb_start_handler (GSList* sibling_data,
                       gpointer parent_data,
                       gpointer global_data,
                       gpointer* data_for_children,
                       gpointer* result,
                       const gchar* tag,
                       gchar** attrs)
{
    gxpf_data* gdata = static_cast<decltype (gdata)> (global_data);
    QofBook* book = static_cast<decltype (book)> (gdata->bookdata);
    GNCPriceDB* db = gnc_pricedb_get_db (book);
    g_return_val_if_fail (db, FALSE);
    gnc_pricedb_set_bulk_update (db, TRUE);
    *result = db;
    return (TRUE);
}

static gboolean
pricedb_after_child_handler (gpointer data_for_children,
                             GSList* data_from_children,
                             GSList* sibling_data,
                             gpointer parent_data,
                             gpointer global_data,
                             gpointer* result,
                             const gchar* tag,
                             const gchar* child_tag,
                             sixtp_child_result* child_result)
{
    gxpf_data* gdata = static_cast<decltype (gdata)> (global_data);
    sixtp_gdv2* gd = static_cast<decltype (gd)> (gdata->parsedata);
    GNCPriceDB* db = (GNCPriceDB*) * result;

    g_return_val_if_fail (db, FALSE);

    /* right now children have to produce results :> */
    if (!child_result) return (FALSE);
    if (child_result->type != SIXTP_CHILD_RESULT_NODE) return (FALSE);

    if (strcmp (child_result->tag, "price") == 0)
    {
        GNCPrice* p = (GNCPrice*) child_result->data;

        g_return_val_if_fail (p, FALSE);
        gnc_pricedb_add_price (db, p);
        gd->counter.prices_loaded++;
        sixtp_run_callback (gd, "prices");
        return TRUE;
    }
    else
    {
        PERR ("unexpected tag %s\n", child_result->tag);
        return FALSE;
    }
    return FALSE;
}

static void
pricedb_cleanup_result_handler (sixtp_child_result* result)
{
    if (result->data)
    {
        GNCPriceDB* db = (GNCPriceDB*) result->data;
        if (db) gnc_pricedb_destroy (db);
        result->data = NULL;
    }
}

static gboolean
pricedb_v2_end_handler (
    gpointer data_for_children, GSList* data_from_children,
    GSList* sibling_data, gpointer parent_data, gpointer global_data,
    gpointer* result, const gchar* tag)
{
    GNCPriceDB* db = static_cast<decltype (db)> (*result);
    gxpf_data* gdata = (gxpf_data*)global_data;

    if (parent_data)
    {
        return TRUE;
    }

    if (!tag)
    {
        return TRUE;
    }

    gdata->cb (tag, gdata->parsedata, db);
    *result = NULL;

    gnc_pricedb_set_bulk_update (db, FALSE);

    return TRUE;
}

static sixtp*
gnc_pricedb_parser_new (void)
{
    sixtp* top_level;
    sixtp* price_parser;

    top_level =
        sixtp_set_any (sixtp_new (), TRUE,
                       SIXTP_START_HANDLER_ID, pricedb_start_handler,
                       SIXTP_AFTER_CHILD_HANDLER_ID, pricedb_after_child_handler,
                       SIXTP_CHARACTERS_HANDLER_ID,
                       allow_and_ignore_only_whitespace,
                       SIXTP_RESULT_FAIL_ID, pricedb_cleanup_result_handler,
                       SIXTP_CLEANUP_RESULT_ID, pricedb_cleanup_result_handler,
                       SIXTP_NO_MORE_HANDLERS);

    if (!top_level) return NULL;

    price_parser = gnc_price_parser_new ();

    if (!price_parser)
    {
        sixtp_destroy (top_level);
        return NULL;
    }

    sixtp_add_sub_parser (top_level, "price", price_parser);

    return top_level;
}

sixtp*
gnc_pricedb_sixtp_parser_create (void)
{
    sixtp* ret;
    ret = gnc_pricedb_parser_new ();
    sixtp_set_end (ret, pricedb_v2_end_handler);
    return ret;
}


/***********************************************************************/
/* WRITING */
/***********************************************************************/

static gboolean
add_child_or_kill_parent (xmlNodePtr parent, xmlNodePtr child)
{
    if (!child)
    {
        xmlFreeNode (parent);
        return FALSE;
    }
    xmlAddChild (parent, child);
    return TRUE;
}

static xmlNodePtr
gnc_price_to_dom_tree (const xmlChar* tag, GNCPrice* price)
{
    xmlNodePtr price_xml;
    const gchar* typestr, *sourcestr;
    xmlNodePtr tmpnode;
    gnc_commodity* commodity;
    gnc_commodity* currency;
    time64 time;
    gnc_numeric value;

    if (! (tag && price)) return NULL;

    price_xml = xmlNewNode (NULL, tag);
    if (!price_xml) return NULL;

    commodity = gnc_price_get_commodity (price);
    currency = gnc_price_get_currency (price);

    if (! (commodity && currency)) return NULL;

    tmpnode = guid_to_dom_tree ("price:id", gnc_price_get_guid (price));
    if (!add_child_or_kill_parent (price_xml, tmpnode)) return NULL;

    tmpnode = commodity_ref_to_dom_tree ("price:commodity", commodity);
    if (!add_child_or_kill_parent (price_xml, tmpnode)) return NULL;

    tmpnode = commodity_ref_to_dom_tree ("price:currency", currency);
    if (!add_child_or_kill_parent (price_xml, tmpnode)) return NULL;

    time = gnc_price_get_time64 (price);
    tmpnode = time64_to_dom_tree ("price:time", time);
    if (!add_child_or_kill_parent (price_xml, tmpnode)) return NULL;

    sourcestr = gnc_price_get_source_string (price);
    if (sourcestr && *sourcestr)
    {
        tmpnode = text_to_dom_tree ("price:source", sourcestr);
        if (!add_child_or_kill_parent (price_xml, tmpnode)) return NULL;
    }

    typestr = gnc_price_get_typestr (price);
    if (typestr && *typestr)
    {
        tmpnode = text_to_dom_tree ("price:type", typestr);
        if (!add_child_or_kill_parent (price_xml, tmpnode)) return NULL;
    }

    value = gnc_price_get_value (price);
    tmpnode = gnc_numeric_to_dom_tree ("price:value", &value);
    if (!add_child_or_kill_parent (price_xml, tmpnode)) return NULL;

    return price_xml;
}

static gboolean
xml_add_gnc_price_adapter (GNCPrice* p, gpointer data)
{
    xmlNodePtr xml_node = (xmlNodePtr) data;

    if (p)
    {
        xmlNodePtr price_xml = gnc_price_to_dom_tree (BAD_CAST "price", p);
        if (!price_xml) return FALSE;
        xmlAddChild (xml_node, price_xml);
        return TRUE;
    }
    else
    {
        return TRUE;
    }
}

static xmlNodePtr
gnc_pricedb_to_dom_tree (const xmlChar* tag, GNCPriceDB* db)
{
    xmlNodePtr db_xml = NULL;

    if (!tag) return NULL;

    db_xml = xmlNewNode (NULL, tag);
    if (!db_xml) return NULL;

    xmlSetProp (db_xml, BAD_CAST "version", BAD_CAST "1");

    if (!gnc_pricedb_foreach_price (db, xml_add_gnc_price_adapter, db_xml, TRUE))
    {
        xmlFreeNode (db_xml);
        return NULL;
    }

    /* if no children have been added just return NULL */
    if (!db_xml->xmlChildrenNode)
    {
        xmlFreeNode (db_xml);
        return NULL;
    }

    return db_xml;
}

xmlNodePtr
gnc_pricedb_dom_tree_create (GNCPriceDB* db)
{
    return gnc_pricedb_to_dom_tree (BAD_CAST "gnc:pricedb", db);
}
