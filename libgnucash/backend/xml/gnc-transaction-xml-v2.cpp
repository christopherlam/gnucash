/********************************************************************
 * gnc-transactions-xml-v2.c -- xml routines for transactions       *
 * Copyright (C) 2001 Rob Browning                                  *
 * Copyright (C) 2002 Linas Vepstas <linas@linas.org>               *
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
#include <glib.h>

#include <config.h>
#include <string.h>
#include "AccountP.hpp"
#include "Transaction.h"
#include "TransactionP.hpp"
#include "gnc-lot.h"
#include "gnc-lot-p.h"
#include <gnc-numeric.h>
#include <gnc-date.h>
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

#include "sixtp-dom-parsers.h"

[[maybe_unused]] static const QofLogModule log_module = G_LOG_DOMAIN;
const gchar* transaction_version_string = "2.0.0";

static void
add_gnc_num (xmlNodePtr node, const gchar* tag, gnc_numeric num)
{
    xmlAddChild (node, gnc_numeric_to_dom_tree (tag, &num));
}

static void
add_time64 (xmlNodePtr node, const gchar * tag, time64 time, gboolean always)
{
    if (always || time)
        xmlAddChild (node, time64_to_dom_tree (tag, time));
}

static xmlNodePtr
split_to_dom_tree (const gchar* tag, Split* spl)
{
    xmlNodePtr ret;

    ret = xmlNewNode (NULL, BAD_CAST tag);

    xmlAddChild (ret, guid_to_dom_tree ("split:id", xaccSplitGetGUID (spl)));

    {
        char* memo = g_strdup (xaccSplitGetMemo (spl));

        if (memo && g_strcmp0 (memo, "") != 0)
        {
            xmlNewTextChild (ret, NULL, BAD_CAST "split:memo",
                             checked_char_cast (memo));
        }
        g_free (memo);
    }

    {
        char* action = g_strdup (xaccSplitGetAction (spl));

        if (action && g_strcmp0 (action, "") != 0)
        {
            xmlNewTextChild (ret, NULL, BAD_CAST "split:action",
                             checked_char_cast (action));
        }
        g_free (action);
    }

    {
        char tmp[2];

        tmp[0] = xaccSplitGetReconcile (spl);
        tmp[1] = '\0';

        xmlNewTextChild (ret, NULL, BAD_CAST "split:reconciled-state",
                         BAD_CAST tmp);
    }

    add_time64 (ret, "split:reconcile-date",
                xaccSplitGetDateReconciled (spl), FALSE);

    add_gnc_num (ret, "split:value", xaccSplitGetValue (spl));

    add_gnc_num (ret, "split:quantity", xaccSplitGetAmount (spl));

    {
        Account* account = xaccSplitGetAccount (spl);

        xmlAddChild (ret, guid_to_dom_tree ("split:account",
                                            xaccAccountGetGUID (account)));
    }
    {
        GNCLot* lot = xaccSplitGetLot (spl);

        if (lot)
        {
            xmlAddChild (ret, guid_to_dom_tree ("split:lot",
                                                gnc_lot_get_guid (lot)));
        }
    }
    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree ("split:slots",
                                                      QOF_INSTANCE (spl)));
    return ret;
}

static void
add_trans_splits (xmlNodePtr node, Transaction* trn)
{
    GList* n;
    xmlNodePtr toaddto;

    toaddto = xmlNewChild (node, NULL, BAD_CAST "trn:splits", NULL);

    for (n = xaccTransGetSplitList (trn); n; n = n->next)
    {
        Split* s = static_cast<decltype (s)> (n->data);
        xmlAddChild (toaddto, split_to_dom_tree ("trn:split", s));
    }
}

xmlNodePtr
gnc_transaction_dom_tree_create (Transaction* trn)
{
    xmlNodePtr ret;
    gchar* str = NULL;

    ret = xmlNewNode (NULL, BAD_CAST "gnc:transaction");

    xmlSetProp (ret, BAD_CAST "version",
                BAD_CAST transaction_version_string);

    xmlAddChild (ret, guid_to_dom_tree ("trn:id", xaccTransGetGUID (trn)));

    xmlAddChild (ret, commodity_ref_to_dom_tree ("trn:currency",
                                                 xaccTransGetCurrency (trn)));
    str = g_strdup (xaccTransGetNum (trn));
    if (str && (g_strcmp0 (str, "") != 0))
    {
        xmlNewTextChild (ret, NULL, BAD_CAST "trn:num",
                         checked_char_cast (str));
    }
    g_free (str);

    add_time64 (ret, "trn:date-posted", xaccTransRetDatePosted (trn), TRUE);

    add_time64 (ret, "trn:date-entered",
                  xaccTransRetDateEntered (trn), TRUE);

    str = g_strdup (xaccTransGetDescription (trn));
    if (str)
    {
        xmlNewTextChild (ret, NULL, BAD_CAST "trn:description",
                         checked_char_cast (str));
    }
    g_free (str);

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree ("trn:slots",
                                                      QOF_INSTANCE (trn)));

    add_trans_splits (ret, trn);

    return ret;
}

/***********************************************************************/

struct split_pdata
{
    Split* split;
    QofBook* book;
};

static inline gboolean
set_spl_gnc_num (xmlNodePtr node, Split* spl,
                 void (*func) (Split* spl, gnc_numeric gn))
{
    func (spl, dom_tree_to_gnc_numeric (node));
    return TRUE;
}

static gboolean
spl_id_handler (xmlNodePtr node, gpointer data)
{
    struct split_pdata* pdata = static_cast<decltype (pdata)> (data);
    auto tmp = dom_tree_to_guid (node);
    g_return_val_if_fail (tmp, FALSE);

    xaccSplitSetGUID (pdata->split, &*tmp);

    return TRUE;
}

static gboolean
spl_memo_handler (xmlNodePtr node, gpointer data)
{
    struct split_pdata* pdata = static_cast<decltype (pdata)> (data);
    return apply_xmlnode_text (xaccSplitSetMemo, pdata->split, node);
}

static gboolean
spl_action_handler (xmlNodePtr node, gpointer data)
{
    struct split_pdata* pdata = static_cast<decltype (pdata)> (data);
    return apply_xmlnode_text (xaccSplitSetAction, pdata->split, node);
}

static gboolean
spl_reconciled_state_handler (xmlNodePtr node, gpointer data)
{
    struct split_pdata* pdata = static_cast<decltype (pdata)> (data);
    auto set_reconciled = [](Split* s, const char *txt)
    {
        xaccSplitSetReconcile(s, txt[0]);
    };
    return apply_xmlnode_text (set_reconciled, pdata->split, node);
}

static gboolean
spl_reconcile_date_handler (xmlNodePtr node, gpointer data)
{
    struct split_pdata* pdata = static_cast<decltype (pdata)> (data);
    time64 time  = dom_tree_to_time64 (node);
    if (!dom_tree_valid_time64 (time, node->name)) time = 0;
    xaccSplitSetDateReconciledSecs (pdata->split, time);
    return TRUE;
}

static gboolean
spl_value_handler (xmlNodePtr node, gpointer data)
{
    struct split_pdata* pdata = static_cast<decltype (pdata)> (data);
    return set_spl_gnc_num (node, pdata->split, xaccSplitSetValue);
}

static gboolean
spl_quantity_handler (xmlNodePtr node, gpointer data)
{
    struct split_pdata* pdata = static_cast<decltype (pdata)> (data);
    return set_spl_gnc_num (node, pdata->split, xaccSplitSetAmount);
}

gboolean gnc_transaction_xml_v2_testing = FALSE;

static gboolean
spl_account_handler (xmlNodePtr node, gpointer data)
{
    struct split_pdata* pdata = static_cast<decltype (pdata)> (data);
    auto id = dom_tree_to_guid (node);
    Account* account;

    g_return_val_if_fail (id, FALSE);

    account = xaccAccountLookup (&*id, pdata->book);
    if (!account && gnc_transaction_xml_v2_testing &&
        !guid_equal (&*id, guid_null ()))
    {
        account = xaccMallocAccount (pdata->book);
        xaccAccountSetGUID (account, &*id);
        xaccAccountSetCommoditySCU (account,
                                    xaccSplitGetAmount (pdata->split).denom);
    }

    xaccAccountInsertSplit (account, pdata->split);

    return TRUE;
}

static gboolean
spl_lot_handler (xmlNodePtr node, gpointer data)
{
    struct split_pdata* pdata = static_cast<decltype (pdata)> (data);
    auto id = dom_tree_to_guid (node);
    GNCLot* lot;

    g_return_val_if_fail (id, FALSE);

    lot = gnc_lot_lookup (&*id, pdata->book);
    if (!lot && gnc_transaction_xml_v2_testing &&
        !guid_equal (&*id, guid_null ()))
    {
        lot = gnc_lot_new (pdata->book);
        gnc_lot_set_guid (lot, *id);
    }

    gnc_lot_add_split (lot, pdata->split);

    return TRUE;
}

static gboolean
spl_slots_handler (xmlNodePtr node, gpointer data)
{
    struct split_pdata* pdata = static_cast<decltype (pdata)> (data);
    gboolean successful;

    successful = dom_tree_create_instance_slots (node,
                                                 QOF_INSTANCE (pdata->split));
    g_return_val_if_fail (successful, FALSE);

    return TRUE;
}

struct dom_tree_handler spl_dom_handlers[] =
{
    { "split:id", spl_id_handler, 1, 0 },
    { "split:memo", spl_memo_handler, 0, 0 },
    { "split:action", spl_action_handler, 0, 0 },
    { "split:reconciled-state", spl_reconciled_state_handler, 1, 0 },
    { "split:reconcile-date", spl_reconcile_date_handler, 0, 0 },
    { "split:value", spl_value_handler, 1, 0 },
    { "split:quantity", spl_quantity_handler, 1, 0 },
    { "split:account", spl_account_handler, 1, 0 },
    { "split:lot", spl_lot_handler, 0, 0 },
    { "split:slots", spl_slots_handler, 0, 0 },
    { NULL, NULL, 0, 0 },
};

static Split*
dom_tree_to_split (xmlNodePtr node, QofBook* book)
{
    struct split_pdata pdata;
    Split* ret;

    g_return_val_if_fail (book, NULL);

    ret = xaccMallocSplit (book);
    g_return_val_if_fail (ret, NULL);

    pdata.split = ret;
    pdata.book = book;

    /* this isn't going to work in a testing setup */
    if (dom_tree_generic_parse (node, spl_dom_handlers, &pdata))
    {
        return ret;
    }
    else
    {
        xaccSplitDestroy (ret);
        return NULL;
    }
}

/***********************************************************************/

struct trans_pdata
{
    Transaction* trans;
    QofBook* book;
};

static gboolean
set_tran_time64 (xmlNodePtr node, Transaction * trn,
        void (*func) (Transaction *, time64))
{
    time64 time = dom_tree_to_time64 (node);
    if (!dom_tree_valid_time64 (time, node->name)) time = 0;
    func (trn, time);
    return TRUE;
}

static gboolean
trn_id_handler (xmlNodePtr node, gpointer trans_pdata)
{
    struct trans_pdata* pdata = static_cast<decltype (pdata)> (trans_pdata);
    Transaction* trn = pdata->trans;
    auto tmp = dom_tree_to_guid (node);

    g_return_val_if_fail (tmp, FALSE);

    xaccTransSetGUID ((Transaction*)trn, &*tmp);

    return TRUE;
}

static gboolean
trn_currency_handler (xmlNodePtr node, gpointer trans_pdata)
{
    struct trans_pdata* pdata = static_cast<decltype (pdata)> (trans_pdata);
    Transaction* trn = pdata->trans;
    gnc_commodity* ref;

    ref = dom_tree_to_commodity_ref (node, pdata->book);
    xaccTransSetCurrency (trn, ref);

    return TRUE;
}

static gboolean
trn_num_handler (xmlNodePtr node, gpointer trans_pdata)
{
    struct trans_pdata* pdata = static_cast<decltype (pdata)> (trans_pdata);
    Transaction* trn = pdata->trans;

    return apply_xmlnode_text (xaccTransSetNum, trn, node);
}

static gboolean
trn_date_posted_handler (xmlNodePtr node, gpointer trans_pdata)
{
    struct trans_pdata* pdata = static_cast<decltype (pdata)> (trans_pdata);
    Transaction* trn = pdata->trans;

    return set_tran_time64 (node, trn, xaccTransSetDatePostedSecs);
}

static gboolean
trn_date_entered_handler (xmlNodePtr node, gpointer trans_pdata)
{
    struct trans_pdata* pdata = static_cast<decltype (pdata)> (trans_pdata);
    Transaction* trn = pdata->trans;

    return set_tran_time64 (node, trn, xaccTransSetDateEnteredSecs);
}

static gboolean
trn_description_handler (xmlNodePtr node, gpointer trans_pdata)
{
    struct trans_pdata* pdata = static_cast<decltype (pdata)> (trans_pdata);
    Transaction* trn = pdata->trans;

    return apply_xmlnode_text (xaccTransSetDescription, trn, node);
}

static gboolean
trn_slots_handler (xmlNodePtr node, gpointer trans_pdata)
{
    struct trans_pdata* pdata = static_cast<decltype (pdata)> (trans_pdata);
    Transaction* trn = pdata->trans;
    gboolean successful;

    successful = dom_tree_create_instance_slots (node, QOF_INSTANCE (trn));

    g_return_val_if_fail (successful, FALSE);

    return TRUE;
}

static gboolean
trn_splits_handler (xmlNodePtr node, gpointer trans_pdata)
{
    struct trans_pdata* pdata = static_cast<decltype (pdata)> (trans_pdata);
    Transaction* trn = pdata->trans;
    xmlNodePtr mark;

    g_return_val_if_fail (node, FALSE);
    g_return_val_if_fail (node->xmlChildrenNode, FALSE);

    for (mark = node->xmlChildrenNode; mark; mark = mark->next)
    {
        Split* spl;

        if (g_strcmp0 ("text", (char*)mark->name) == 0)
            continue;

        if (g_strcmp0 ("trn:split", (char*)mark->name))
        {
            return FALSE;
        }

        spl = dom_tree_to_split (mark, pdata->book);

        if (spl)
        {
            xaccTransAppendSplit (trn, spl);
        }
        else
        {
            return FALSE;
        }
    }
    return TRUE;
}

struct dom_tree_handler trn_dom_handlers[] =
{
    { "trn:id", trn_id_handler, 1, 0 },
    { "trn:currency", trn_currency_handler, 0, 0},
    { "trn:num", trn_num_handler, 0, 0 },
    { "trn:date-posted", trn_date_posted_handler, 1, 0 },
    { "trn:date-entered", trn_date_entered_handler, 1, 0 },
    { "trn:description", trn_description_handler, 0, 0 },
    { "trn:slots", trn_slots_handler, 0, 0 },
    { "trn:splits", trn_splits_handler, 1, 0 },
    { NULL, NULL, 0, 0 },
};

Transaction*
dom_tree_to_transaction (xmlNodePtr node, QofBook* book)
{
    Transaction* trn;
    gboolean successful;
    struct trans_pdata pdata;

    g_return_val_if_fail (node, NULL);
    g_return_val_if_fail (book, NULL);

    trn = xaccMallocTransaction (book);
    g_return_val_if_fail (trn, NULL);
    xaccTransBeginEdit (trn);

    pdata.trans = trn;
    pdata.book = book;

    successful = dom_tree_generic_parse (node, trn_dom_handlers, &pdata);

    /* A transaction with no splits is implicitly destroyed by
       xaccTransCommitEdit() itself (was_trans_emptied() in
       Transaction.cpp); using trn afterward would be a use-after-free.
       Check before committing, since the object may not exist to
       check afterward. */
    gboolean emptied = (xaccTransGetSplitList (trn) == NULL);
    xaccTransCommitEdit (trn);

    if (emptied)
        return NULL;

    if (!successful)
    {
        xmlElemDump (stdout, NULL, node);
        xaccTransBeginEdit (trn);
        xaccTransDestroy (trn);
        xaccTransCommitEdit (trn);
        trn = NULL;
    }

    return trn;
}

/***********************************************************************/
/* SAX-direct (streaming) transaction/split parser.
 *
 * dom_tree_to_transaction()/dom_tree_to_split() above are unchanged and
 * still used for gnc:template-transactions (scheduled transactions),
 * which hand them an already-built DOM subtree. But gnc:transaction
 * itself -- by far the highest-volume element in a real data file --
 * no longer needs sixtp_dom_parser_new() at all: every scalar field
 * (id, currency, num, dates, description, and every split's id/memo/
 * action/reconciled-state/value/quantity/account/lot) is applied
 * straight off the SAX character stream below, with no intermediate
 * xmlNodePtr ever built for any of it. Only trn:slots/split:slots (kvp
 * frames, which nest arbitrarily) still go through a narrowly scoped
 * DOM sub-parser via sixtp_dom_parser_new_rooted().
 */

struct trans_sax_pdata
{
    Transaction* trans;
    QofBook* book;
};

struct split_sax_pdata
{
    Split* split;
    QofBook* book;
};

/* sax_apply_chars() and sax_passthrough_start() are declared in
   sixtp-utils.h and shared with the other SAX-direct v2 parsers. */

/* dom_tree_to_time64()/parse_commodity_ref() (used by the still-DOM-based
   dom_tree_to_transaction() path) silently ignore any element they don't
   recognize inside a date or commodity-ref wrapper -- notably a lone
   ts:ns (nanoseconds) sibling of ts:date that some older files carry,
   even though the current writer never emits one. sax_time64_parser_new()
   (shared, sixtp-utils.h) already tolerates and discards it. */

/* ---- split leaf handlers ------------------------------------------ */

static gboolean
sax_spl_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<split_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        xaccSplitSetGUID (pdata->split, &guid);
        return TRUE;
    });
}

static gboolean
sax_spl_memo_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                  gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<split_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { xaccSplitSetMemo (pdata->split, txt); return TRUE; });
}

static gboolean
sax_spl_action_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<split_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { xaccSplitSetAction (pdata->split, txt); return TRUE; });
}

static gboolean
sax_spl_reconciled_state_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                              gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<split_sax_pdata*> (parent_data);
    /* Matches the original: passes txt[0] straight through, including
       '\0' for empty content, which xaccSplitSetReconcile() logs as
       "Bad reconciled flag" and otherwise ignores (the split keeps its
       prior reconciled state). */
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        xaccSplitSetReconcile (pdata->split, txt[0]);
        return TRUE;
    });
}

static gboolean
sax_spl_reconcile_date_ts_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                               gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<split_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        time64 t = gnc_iso8601_to_time64_gmt (txt);
        if (!dom_tree_valid_time64 (t, BAD_CAST "split:reconcile-date")) t = 0;
        xaccSplitSetDateReconciledSecs (pdata->split, t);
        return TRUE;
    });
}

static gboolean
sax_spl_value_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                   gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<split_sax_pdata*> (parent_data);
    /* Malformed or empty content falls back to zero rather than failing
       the whole split -- some legacy/test data has empty value fields. */
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gnc_numeric num = gnc_numeric_from_string (txt);
        xaccSplitSetValue (pdata->split, gnc_numeric_check (num) ? gnc_numeric_zero () : num);
        return TRUE;
    });
}

static gboolean
sax_spl_quantity_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                      gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<split_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gnc_numeric num = gnc_numeric_from_string (txt);
        xaccSplitSetAmount (pdata->split, gnc_numeric_check (num) ? gnc_numeric_zero () : num);
        return TRUE;
    });
}

static gboolean
sax_spl_account_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                     gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<split_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID id;
        if (!string_to_guid (txt, &id)) return FALSE;

        Account* account = xaccAccountLookup (&id, pdata->book);
        if (!account && gnc_transaction_xml_v2_testing &&
            !guid_equal (&id, guid_null ()))
        {
            account = xaccMallocAccount (pdata->book);
            xaccAccountSetGUID (account, &id);
            xaccAccountSetCommoditySCU (account,
                                        xaccSplitGetAmount (pdata->split).denom);
        }
        xaccAccountInsertSplit (account, pdata->split);
        return TRUE;
    });
}

static gboolean
sax_spl_lot_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                 gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<split_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID id;
        if (!string_to_guid (txt, &id)) return FALSE;

        GNCLot* lot = gnc_lot_lookup (&id, pdata->book);
        if (!lot && gnc_transaction_xml_v2_testing &&
            !guid_equal (&id, guid_null ()))
        {
            lot = gnc_lot_new (pdata->book);
            gnc_lot_set_guid (lot, id);
        }
        gnc_lot_add_split (lot, pdata->split);
        return TRUE;
    });
}

static gboolean
sax_spl_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                       gpointer parent_data, gpointer, gpointer* result, const gchar*)
{
    auto* pdata = static_cast<split_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ok = TRUE;
    if (tree)
    {
        ok = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->split));
        xmlFreeNode (tree);
    }
    *result = nullptr;
    return ok;
}

static gboolean
sax_spl_start (GSList*, gpointer parent_data, gpointer, gpointer* data_for_children,
              gpointer*, const gchar*, gchar**)
{
    auto* trn_pdata = static_cast<trans_sax_pdata*> (parent_data);
    auto* pdata = g_new (split_sax_pdata, 1);
    pdata->book = trn_pdata->book;
    pdata->split = xaccMallocSplit (pdata->book);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_spl_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
            gpointer* result, const gchar*)
{
    auto* pdata = static_cast<split_sax_pdata*> (data_for_children);
    *result = pdata->split;
    g_free (pdata);
    return TRUE;
}

static void
sax_spl_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
             gpointer*, const gchar*)
{
    auto* pdata = static_cast<split_sax_pdata*> (data_for_children);
    if (!pdata) return;
    xaccSplitDestroy (pdata->split);
    g_free (pdata);
}

static sixtp*
sax_split_parser_new (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_spl_start,
        SIXTP_END_HANDLER_ID, sax_spl_end,
        SIXTP_FAIL_HANDLER_ID, sax_spl_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        "split:id", restore_char_generator (sax_spl_id_end),
        "split:memo", restore_char_generator (sax_spl_memo_end),
        "split:action", restore_char_generator (sax_spl_action_end),
        "split:reconciled-state", restore_char_generator (sax_spl_reconciled_state_end),
        "split:value", restore_char_generator (sax_spl_value_end),
        "split:quantity", restore_char_generator (sax_spl_quantity_end),
        "split:account", restore_char_generator (sax_spl_account_end),
        "split:lot", restore_char_generator (sax_spl_lot_end),
        "split:slots", sixtp_dom_parser_new_rooted (sax_spl_slots_dom_end, NULL, NULL),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    sixtp_add_sub_parser (p, "split:reconcile-date",
                          sax_time64_parser_new (sax_spl_reconcile_date_ts_end));

    return p;
}

/* ---- transaction leaf handlers ------------------------------------ */

static gboolean
sax_trn_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<trans_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        xaccTransSetGUID (pdata->trans, &guid);
        return TRUE;
    });
}

static gboolean
sax_trn_num_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                 gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<trans_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { xaccTransSetNum (pdata->trans, txt); return TRUE; });
}

static gboolean
sax_trn_description_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<trans_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { xaccTransSetDescription (pdata->trans, txt); return TRUE; });
}

static gboolean
sax_trn_date_posted_ts_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                            gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<trans_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        time64 t = gnc_iso8601_to_time64_gmt (txt);
        if (!dom_tree_valid_time64 (t, BAD_CAST "trn:date-posted")) t = 0;
        xaccTransSetDatePostedSecs (pdata->trans, t);
        return TRUE;
    });
}

static gboolean
sax_trn_date_entered_ts_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                             gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<trans_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        time64 t = gnc_iso8601_to_time64_gmt (txt);
        if (!dom_tree_valid_time64 (t, BAD_CAST "trn:date-entered")) t = 0;
        xaccTransSetDateEnteredSecs (pdata->trans, t);
        return TRUE;
    });
}

static gboolean
sax_trn_currency_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                      gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<trans_sax_pdata*> (parent_data);
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
    /* An unresolvable currency ref sets a NULL currency rather than
       failing the whole transaction parse. */
    gnc_commodity* ref = nullptr;
    if (space && id)
    {
        /* trim in place; these buffers are owned by the child results
           and freed automatically once this end handler returns */
        g_strstrip (space);
        g_strstrip (id);
        auto* table = gnc_commodity_table_get_table (pdata->book);
        if (table)
            ref = gnc_commodity_table_lookup (table, space, id);
    }

    xaccTransSetCurrency (pdata->trans, ref);
    return TRUE;
}

static gboolean
sax_trn_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                       gpointer parent_data, gpointer, gpointer* result, const gchar*)
{
    auto* pdata = static_cast<trans_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ok = TRUE;
    if (tree)
    {
        ok = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->trans));
        xmlFreeNode (tree);
    }
    *result = nullptr;
    return ok;
}

static gboolean
sax_trn_splits_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<trans_sax_pdata*> (parent_data);
    /* data_from_children is youngest-first; reverse it to restore
       document (and thus split) order. */
    GSList* ordered = g_slist_reverse (g_slist_copy (dfc));
    for (GSList* lp = ordered; lp; lp = lp->next)
    {
        auto* cr = static_cast<sixtp_child_result*> (lp->data);
        xaccTransAppendSplit (pdata->trans, static_cast<Split*> (cr->data));
    }
    g_slist_free (ordered);
    return TRUE;
}

static gboolean
sax_trn_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
              gpointer*, const gchar* tag, gchar**)
{
    /* When this parser is used as sixtp_parse_file()'s top-level parser
       (as in gnc_transaction_sixtp_parser_create()'s standalone-file test
       usage below), sixtp calls the start handler an extra, harmless
       first time for the synthetic top frame, with tag == NULL. Don't
       allocate a Transaction for that call -- only for the real
       gnc:transaction element. */
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }

    auto* gdata = static_cast<gxpf_data*> (global_data);
    auto* pdata = g_new (trans_sax_pdata, 1);
    pdata->book = static_cast<QofBook*> (gdata->bookdata);
    pdata->trans = xaccMallocTransaction (pdata->book);
    xaccTransBeginEdit (pdata->trans);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_trn_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer global_data,
            gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<trans_sax_pdata*> (data_for_children);
    auto* gdata = static_cast<gxpf_data*> (global_data);

    /* Called an extra, harmless time with a NULL tag for the synthetic
       top frame; see sax_trn_start() above. */
    if (!tag)
        return TRUE;

    /* A transaction with no splits (an empty trn:splits, or none at
       all) is implicitly destroyed by xaccTransCommitEdit() itself
       (was_trans_emptied() in Transaction.cpp); using pdata->trans
       afterward would be a use-after-free. Check before committing,
       since the object may not exist to check afterward. */
    gboolean emptied = (xaccTransGetSplitList (pdata->trans) == NULL);
    xaccTransCommitEdit (pdata->trans);
    if (!emptied)
        gdata->cb (tag, gdata->parsedata, pdata->trans);
    g_free (pdata);
    return TRUE;
}

static void
sax_trn_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
             gpointer*, const gchar*)
{
    auto* pdata = static_cast<trans_sax_pdata*> (data_for_children);
    if (!pdata) return;
    xaccTransDestroy (pdata->trans);
    xaccTransCommitEdit (pdata->trans);
    g_free (pdata);
}

sixtp*
gnc_transaction_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_trn_start,
        SIXTP_END_HANDLER_ID, sax_trn_end,
        SIXTP_FAIL_HANDLER_ID, sax_trn_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        "trn:id", restore_char_generator (sax_trn_id_end),
        "trn:num", restore_char_generator (sax_trn_num_end),
        "trn:description", restore_char_generator (sax_trn_description_end),
        "trn:slots", sixtp_dom_parser_new_rooted (sax_trn_slots_dom_end, NULL, NULL),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    sixtp_add_sub_parser (p, "trn:currency",
                          sax_commodity_ref_parser_new (sax_trn_currency_end));
    sixtp_add_sub_parser (p, "trn:date-posted",
                          sax_time64_parser_new (sax_trn_date_posted_ts_end));
    sixtp_add_sub_parser (p, "trn:date-entered",
                          sax_time64_parser_new (sax_trn_date_entered_ts_end));
    {
        sixtp* splits = sixtp_set_any (
            sixtp_new (), FALSE,
            SIXTP_START_HANDLER_ID, sax_passthrough_start,
            SIXTP_END_HANDLER_ID, sax_trn_splits_end,
            SIXTP_NO_MORE_HANDLERS);
        sixtp_add_sub_parser (splits, "trn:split", sax_split_parser_new ());
        sixtp_add_sub_parser (p, "trn:splits", splits);
    }

    /* Self-reference under our own tag: sixtp always looks up an
       element's parser as a *child* of whatever parser is current when
       the element's start tag is seen -- including the outermost
       element, looked up as a child of the synthetic top frame. When p
       is nested under book_parser/main_parser (the normal book-loading
       path), that lookup is book_parser's to do and this registration
       is never consulted. But p is also used standalone, as the
       top-level parser passed straight to gnc_xml_parse_file() (see
       test-xml-transaction.cpp), where p itself must already recognize
       "gnc:transaction" as its own child for that first lookup to
       succeed. */
    sixtp_add_sub_parser (p, "gnc:transaction", p);

    return p;
}
