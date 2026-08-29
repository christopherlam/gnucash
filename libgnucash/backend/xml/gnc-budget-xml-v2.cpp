/*
 * gnc-budget-xml-v2.c -- budget xml i/o implementation
 *
 * Copyright (C) 2005 Chris Shoemaker <c.shoemaker@cox.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, contact:
 *
 * Free Software Foundation           Voice:  +1-617-542-5942
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652
 * Boston, MA  02110-1301,  USA       gnu@gnu.org
 */
#include <glib.h>

#include <config.h>
#include <stdlib.h>
#include <string.h>

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
#include <guid.hpp>

static QofLogModule log_module = GNC_MOD_IO;

const gchar* budget_version_string = "2.0.0";

/* ids */
#define gnc_budget_string       "gnc:budget"
#define bgt_id_string           "bgt:id"
#define bgt_name_string         "bgt:name"
#define bgt_description_string  "bgt:description"
#define bgt_num_periods_string  "bgt:num-periods"
#define bgt_recurrence_string   "bgt:recurrence"
#define bgt_slots_string        "bgt:slots"

xmlNodePtr
gnc_budget_dom_tree_create (GncBudget* bgt)
{
    xmlNodePtr ret;

    ENTER ("(budget=%p)", bgt);

    ret = xmlNewNode (NULL, BAD_CAST gnc_budget_string);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST budget_version_string);

    /* field: GncGUID */
    xmlAddChild (ret, guid_to_dom_tree (bgt_id_string,
                                        gnc_budget_get_guid (bgt)));
    /* field: char* name */
    xmlAddChild (ret, text_to_dom_tree (bgt_name_string,
                                        gnc_budget_get_name (bgt)));
    /* field: char* description */
    xmlAddChild (ret, text_to_dom_tree (bgt_description_string,
                                        gnc_budget_get_description (bgt)));
    /* field: guint num_periods */
    xmlAddChild (ret, guint_to_dom_tree (bgt_num_periods_string,
                                         gnc_budget_get_num_periods (bgt)));
    /* field: Recurrence*  */
    xmlAddChild (ret, recurrence_to_dom_tree (bgt_recurrence_string,
                                              gnc_budget_get_recurrence (bgt)));
    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (bgt_slots_string,
                                                      QOF_INSTANCE (bgt)));

    LEAVE (" ");
    return ret;
}

/***********************************************************************/
/* SAX-direct (streaming) budget parser: reads a gnc:budget straight off
 * the SAX character stream, with no intermediate xmlNodePtr built for
 * its scalar fields (id, name, description, num-periods). bgt:recurrence
 * and bgt:slots stay on the (low-volume -- one per budget) DOM path via
 * dom_tree_to_recurrence()/dom_tree_create_instance_slots().
 */

struct budget_sax_pdata
{
    GncBudget* bgt;
    QofBook* book;
};

static gboolean
sax_bgt_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<budget_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        qof_instance_set_guid (QOF_INSTANCE (pdata->bgt), &guid);
        return TRUE;
    });
}

static gboolean
sax_bgt_name_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                  gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<budget_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gnc_budget_set_name (pdata->bgt, txt); return TRUE; });
}

static gboolean
sax_bgt_description_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<budget_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gnc_budget_set_description (pdata->bgt, txt); return TRUE; });
}

static gboolean
sax_bgt_num_periods_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<budget_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        guint num_periods;
        if (!string_to_guint (txt, &num_periods)) return FALSE;
        gnc_budget_set_num_periods (pdata->bgt, num_periods);
        return TRUE;
    });
}

static gboolean
sax_bgt_recurrence_dom_end (gpointer data_for_children, GSList*, GSList*,
                            gpointer parent_data, gpointer, gpointer* result,
                            const gchar*)
{
    auto* pdata = static_cast<budget_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ok = TRUE;
    if (tree)
    {
        Recurrence* r = dom_tree_to_recurrence (tree);
        if (r)
        {
            gnc_budget_set_recurrence (pdata->bgt, r);
            g_free (r);
        }
        else
            ok = FALSE;
        xmlFreeNode (tree);
    }
    *result = nullptr;
    return ok;
}

static gboolean
sax_bgt_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                       gpointer parent_data, gpointer, gpointer* result,
                       const gchar*)
{
    auto* pdata = static_cast<budget_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ok = TRUE;
    if (tree)
    {
        ok = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->bgt));
        xmlFreeNode (tree);
    }
    *result = nullptr;
    return ok;
}

static gboolean
sax_bgt_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
              gpointer*, const gchar* tag, gchar**)
{
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }
    auto* gdata = static_cast<gxpf_data*> (global_data);
    auto* pdata = g_new (budget_sax_pdata, 1);
    pdata->book = static_cast<QofBook*> (gdata->bookdata);
    pdata->bgt = gnc_budget_new (pdata->book);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_bgt_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer global_data,
            gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<budget_sax_pdata*> (data_for_children);
    auto* gdata = static_cast<gxpf_data*> (global_data);

    if (!tag)
        return TRUE;

    GncBudget* bgt = pdata->bgt;
    g_free (pdata);

    /* ends up calling book_callback, which does nothing for gnc:budget:
       gnc_budget_new() already registered it with the book. */
    gdata->cb (tag, gdata->parsedata, bgt);
    return TRUE;
}

static void
sax_bgt_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
             gpointer*, const gchar*)
{
    auto* pdata = static_cast<budget_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gnc_budget_destroy (pdata->bgt);
    g_free (pdata);
}

sixtp*
gnc_budget_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_bgt_start,
        SIXTP_END_HANDLER_ID, sax_bgt_end,
        SIXTP_FAIL_HANDLER_ID, sax_bgt_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        bgt_id_string, restore_char_generator (sax_bgt_id_end),
        bgt_name_string, restore_char_generator (sax_bgt_name_end),
        bgt_description_string, restore_char_generator (sax_bgt_description_end),
        bgt_num_periods_string, restore_char_generator (sax_bgt_num_periods_end),
        bgt_recurrence_string, sixtp_dom_parser_new_rooted (sax_bgt_recurrence_dom_end, NULL, NULL),
        bgt_slots_string, sixtp_dom_parser_new_rooted (sax_bgt_slots_dom_end, NULL, NULL),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    /* Self-reference under our own tag: see the equivalent comment in
       gnc_transaction_sixtp_parser_create() in gnc-transaction-xml-v2.cpp. */
    sixtp_add_sub_parser (p, gnc_budget_string, p);

    return p;
}
/* ======================  END OF FILE ===================*/
