/********************************************************************\
 * gnc-account-xml-v2.c -- account xml i/o implementation           *
 *                                                                  *
 * Copyright (C) 2001 James LewisMoss <dres@debian.org>             *
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
\********************************************************************/
#include <glib.h>

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <AccountP.hpp>
#include <Account.h>
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

#include "sixtp-dom-parsers.h"

static QofLogModule log_module = GNC_MOD_IO;

const gchar* account_version_string = "2.0.0";

/* ids */
#define gnc_account_string "gnc:account"
#define act_name_string "act:name"
#define act_id_string "act:id"
#define act_type_string "act:type"
#define act_commodity_string "act:commodity"
#define act_commodity_scu_string "act:commodity-scu"
#define act_non_standard_scu_string "act:non-standard-scu"
#define act_code_string "act:code"
#define act_description_string "act:description"
#define act_slots_string "act:slots"
#define act_parent_string "act:parent"
#define act_lots_string "act:lots"
/* The currency and security strings should not appear in newer
 * xml files (anything post-gnucash-1.6) */
#define act_currency_string "act:currency"
#define act_currency_scu_string "act:currency-scu"
#define act_security_string "act:security"
#define act_security_scu_string "act:security-scu"
#define act_hidden_string "act:hidden"
#define act_placeholder_string "act:placeholder"

xmlNodePtr
gnc_account_dom_tree_create (Account* act,
                             gboolean exporting,
                             gboolean allow_incompat)
{
    const char* str;
    xmlNodePtr ret;
    GList* lots, *n;
    Account* parent;
    gnc_commodity* acct_commodity;

    ENTER ("(account=%p)", act);

    ret = xmlNewNode (NULL, BAD_CAST gnc_account_string);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST account_version_string);

    xmlAddChild (ret, text_to_dom_tree (act_name_string,
                                        xaccAccountGetName (act)));

    xmlAddChild (ret, guid_to_dom_tree (act_id_string, xaccAccountGetGUID (act)));

    xmlAddChild (ret, text_to_dom_tree (
                     act_type_string,
                     xaccAccountTypeEnumAsString (xaccAccountGetType (act))));

    /* Don't write new XML tags in version 2.3.x and 2.4.x because it
       would mean 2.2.x cannot read those files again. But we can
       enable writing these tags in 2.5.x or late in 2.4.x. */
    /*
    xmlAddChild(ret, boolean_to_dom_tree(
                            act_hidden_string,
                            xaccAccountGetHidden(act)));
    xmlAddChild(ret, boolean_to_dom_tree(
                            act_placeholder_string,
                            xaccAccountGetPlaceholder(act)));
    */

    acct_commodity = xaccAccountGetCommodity (act);
    if (acct_commodity != NULL)
    {
        xmlAddChild (ret, commodity_ref_to_dom_tree (act_commodity_string,
                                                     acct_commodity));

        xmlAddChild (ret, int_to_dom_tree (act_commodity_scu_string,
                                           xaccAccountGetCommoditySCUi (act)));

        if (xaccAccountGetNonStdSCU (act))
            xmlNewChild (ret, NULL, BAD_CAST act_non_standard_scu_string, NULL);
    }

    str = xaccAccountGetCode (act);
    if (str && *str)
    {
        xmlAddChild (ret, text_to_dom_tree (act_code_string, str));
    }

    str = xaccAccountGetDescription (act);
    if (str && *str)
    {
        xmlAddChild (ret, text_to_dom_tree (act_description_string, str));
    }

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (act_slots_string,
                                                      QOF_INSTANCE (act)));
    parent = gnc_account_get_parent (act);
    if (parent)
    {
        if (!gnc_account_is_root (parent) || allow_incompat)
            xmlAddChild (ret, guid_to_dom_tree (act_parent_string,
                                                xaccAccountGetGUID (parent)));
    }

    lots = xaccAccountGetLotList (act);
    PINFO ("lot list=%p", lots);
    if (lots && !exporting)
    {
        xmlNodePtr toaddto = xmlNewChild (ret, NULL, BAD_CAST act_lots_string, NULL);

        lots = g_list_sort (lots, qof_instance_guid_compare);

        for (n = lots; n; n = n->next)
        {
            GNCLot* lot = static_cast<decltype (lot)> (n->data);
            xmlAddChild (toaddto, gnc_lot_dom_tree_create (lot));
        }
    }
    g_list_free (lots);

    LEAVE ("");
    return ret;
}

/***********************************************************************/

struct account_pdata
{
    Account* account;
    QofBook* book;
};

static gboolean
account_name_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);

    return apply_xmlnode_text (xaccAccountSetName, pdata->account, node);
}

static gboolean
account_id_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);

    auto guid = dom_tree_to_guid (node);
    g_return_val_if_fail (guid, FALSE);

    xaccAccountSetGUID (pdata->account, &*guid);

    return TRUE;
}

static gboolean
account_type_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);
    GNCAccountType type = ACCT_TYPE_INVALID;
    char* string;

    string = (char*) xmlNodeGetContent (node->xmlChildrenNode);
    xaccAccountStringToType (string, &type);
    xmlFree (string);

    xaccAccountSetType (pdata->account, type);

    return TRUE;
}

static gboolean
account_commodity_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);
    gnc_commodity* ref;

//    ref = dom_tree_to_commodity_ref_no_engine(node, pdata->book);
    ref = dom_tree_to_commodity_ref (node, pdata->book);
    xaccAccountSetCommodity (pdata->account, ref);

    return TRUE;
}

static gboolean
account_commodity_scu_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);
    gint64 val;

    dom_tree_to_integer (node, &val);
    xaccAccountSetCommoditySCU (pdata->account, val);

    return TRUE;
}

static gboolean
account_hidden_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);
    gboolean val;

    dom_tree_to_boolean (node, &val);
    xaccAccountSetHidden (pdata->account, val);

    return TRUE;
}

static gboolean
account_placeholder_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);
    gboolean val;

    dom_tree_to_boolean (node, &val);
    xaccAccountSetPlaceholder (pdata->account, val);

    return TRUE;
}

static gboolean
account_non_standard_scu_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);

    xaccAccountSetNonStdSCU (pdata->account, TRUE);

    return TRUE;
}

/* ============================================================== */
/* The following deprecated routines are here only to service
 * older XML files. */

static gboolean
deprecated_account_currency_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);
    gnc_commodity* ref;

    PWARN ("Account %s: Obsolete xml tag 'act:currency' will not be preserved.",
           xaccAccountGetName (pdata->account));
    ref = dom_tree_to_commodity_ref_no_engine (node, pdata->book);
    DxaccAccountSetCurrency (pdata->account, ref);

    return TRUE;
}

static gboolean
deprecated_account_currency_scu_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);
    PWARN ("Account %s: Obsolete xml tag 'act:currency-scu' will not be preserved.",
           xaccAccountGetName (pdata->account));
    return TRUE;
}

static gboolean
deprecated_account_security_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);
    gnc_commodity* ref, *orig = xaccAccountGetCommodity (pdata->account);

    PWARN ("Account %s: Obsolete xml tag 'act:security' will not be preserved.",
           xaccAccountGetName (pdata->account));
    /* If the account has both a commodity and a security element, and
       the commodity is a currency, then the commodity is probably
       wrong. In that case we want to replace it with the
       security. jralls 2010-11-02 */
    if (!orig || gnc_commodity_is_currency (orig))
    {
        ref = dom_tree_to_commodity_ref_no_engine (node, pdata->book);
        xaccAccountSetCommodity (pdata->account, ref);
        /* If the SCU was set, it was probably wrong, so zero it out
           so that the SCU handler can fix it if there's a
           security-scu element. jralls 2010-11-02 */
        xaccAccountSetCommoditySCU (pdata->account, 0);
    }

    return TRUE;
}

static gboolean
deprecated_account_security_scu_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);
    gint64 val;

    PWARN ("Account %s: Obsolete xml tag 'act:security-scu' will not be preserved.",
           xaccAccountGetName (pdata->account));
    if (!xaccAccountGetCommoditySCU (pdata->account))
    {
        dom_tree_to_integer (node, &val);
        xaccAccountSetCommoditySCU (pdata->account, val);
    }

    return TRUE;
}

/* ============================================================== */

static gboolean
account_slots_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);
    return dom_tree_create_instance_slots (node, QOF_INSTANCE (pdata->account));
}

static gboolean
account_parent_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);
    Account* parent;

    auto gid = dom_tree_to_guid (node);
    g_return_val_if_fail (gid, FALSE);

    parent = xaccAccountLookup (&*gid, pdata->book);
    if (!parent)
    {
        g_return_val_if_fail (parent, FALSE);
    }

    gnc_account_append_child (parent, pdata->account);

    return TRUE;
}

static gboolean
account_code_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);

    return apply_xmlnode_text (xaccAccountSetCode, pdata->account, node);
}

static gboolean
account_description_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);

    return apply_xmlnode_text (xaccAccountSetDescription, pdata->account, node);
}

static gboolean
account_lots_handler (xmlNodePtr node, gpointer act_pdata)
{
    struct account_pdata* pdata = static_cast<decltype (pdata)> (act_pdata);
    xmlNodePtr mark;

    g_return_val_if_fail (node, FALSE);
    g_return_val_if_fail (node->xmlChildrenNode, FALSE);

    for (mark = node->xmlChildrenNode; mark; mark = mark->next)
    {
        GNCLot* lot;

        if (g_strcmp0 ("text", (char*) mark->name) == 0)
            continue;

        lot = dom_tree_to_lot (mark, pdata->book);

        if (lot)
        {
            xaccAccountInsertLot (pdata->account, lot);
        }
        else
        {
            return FALSE;
        }
    }
    return TRUE;
}

static struct dom_tree_handler account_handlers_v2[] =
{
    { act_name_string, account_name_handler, 1, 0 },
    { act_id_string, account_id_handler, 1, 0 },
    { act_type_string, account_type_handler, 1, 0 },
    { act_commodity_string, account_commodity_handler, 0, 0 },
    { act_commodity_scu_string, account_commodity_scu_handler, 0, 0 },
    { act_non_standard_scu_string, account_non_standard_scu_handler, 0, 0 },
    { act_code_string, account_code_handler, 0, 0 },
    { act_description_string, account_description_handler, 0, 0},
    { act_slots_string, account_slots_handler, 0, 0 },
    { act_parent_string, account_parent_handler, 0, 0 },
    { act_lots_string, account_lots_handler, 0, 0 },
    { act_hidden_string, account_hidden_handler, 0, 0 },
    { act_placeholder_string, account_placeholder_handler, 0, 0 },

    /* These should not appear in  newer xml files; only in old
     * (circa gnucash-1.6) xml files. We maintain them for backward
     * compatibility. */
    { act_currency_string, deprecated_account_currency_handler, 0, 0 },
    { act_currency_scu_string, deprecated_account_currency_scu_handler, 0, 0 },
    { act_security_string, deprecated_account_security_handler, 0, 0 },
    { act_security_scu_string, deprecated_account_security_scu_handler, 0, 0 },
    { NULL, 0, 0, 0 }
};

Account*
dom_tree_to_account (xmlNodePtr node, QofBook* book)
{
    struct account_pdata act_pdata;
    Account* accToRet;
    gboolean successful;

    accToRet = xaccMallocAccount (book);
    xaccAccountBeginEdit (accToRet);

    act_pdata.account = accToRet;
    act_pdata.book = book;

    successful = dom_tree_generic_parse (node, account_handlers_v2,
                                         &act_pdata);
    if (successful)
    {
        xaccAccountCommitEdit (accToRet);
    }
    else
    {
        PERR ("failed to parse account tree");
        xaccAccountDestroy (accToRet);
        accToRet = NULL;
    }

    return accToRet;
}

/***********************************************************************/
/* SAX-direct (streaming) account parser.
 *
 * dom_tree_to_account() above is unchanged and still used for template
 * accounts under scheduled transactions, which hand it an already-built
 * DOM subtree. gnc_account_sixtp_parser_create() below builds a real
 * sixtp parser tree instead, so an ordinary gnc:account element -- the
 * second-highest-volume element in a real book, after gnc:transaction --
 * is consumed straight off the SAX character stream: no intermediate
 * xmlNodePtr for any scalar field (name, id, type, commodity, code,
 * description, parent, hidden, placeholder, commodity-scu). Only
 * act:slots (a kvp frame) and act:lots (each a nested gnc:lot, itself
 * containing kvp) still go through a narrowly scoped DOM sub-parser via
 * sixtp_dom_parser_new_rooted().
 */

struct account_sax_pdata
{
    Account* account;
    QofBook* book;
};

static gboolean
sax_act_name_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                  gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { xaccAccountSetName (pdata->account, txt); return TRUE; });
}

static gboolean
sax_act_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        xaccAccountSetGUID (pdata->account, &guid);
        return TRUE;
    });
}

static gboolean
sax_act_type_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                  gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GNCAccountType type = ACCT_TYPE_INVALID;
        xaccAccountStringToType (txt, &type);
        xaccAccountSetType (pdata->account, type);
        return TRUE;
    });
}

static gboolean
sax_act_code_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                  gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { xaccAccountSetCode (pdata->account, txt); return TRUE; });
}

static gboolean
sax_act_description_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { xaccAccountSetDescription (pdata->account, txt); return TRUE; });
}

static gboolean
sax_act_commodity_scu_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    /* dom_tree_to_integer()-based account_commodity_scu_handler() never
       failed the parse over a malformed value either; match that. */
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        xaccAccountSetCommoditySCU (pdata->account, val);
        return TRUE;
    });
}

static gboolean
sax_act_hidden_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        xaccAccountSetHidden (pdata->account,
                              g_ascii_strncasecmp (txt, "true", 4) == 0);
        return TRUE;
    });
}

static gboolean
sax_act_placeholder_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        xaccAccountSetPlaceholder (pdata->account,
                                   g_ascii_strncasecmp (txt, "true", 4) == 0);
        return TRUE;
    });
}

static gboolean
sax_act_non_standard_scu_end (gpointer, GSList*, GSList*, gpointer parent_data,
                              gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    xaccAccountSetNonStdSCU (pdata->account, TRUE);
    return TRUE;
}

static gboolean
sax_act_parent_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        Account* parent = xaccAccountLookup (&guid, pdata->book);
        if (!parent) return FALSE;
        gnc_account_append_child (parent, pdata->account);
        return TRUE;
    });
}

static gboolean
sax_act_commodity_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
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
    /* account_commodity_handler() always calls xaccAccountSetCommodity()
       with whatever dom_tree_to_commodity_ref() returned (NULL included)
       and always returns TRUE; match that instead of failing the parse. */
    gnc_commodity* ref = nullptr;
    if (space && id)
    {
        g_strstrip (space);
        g_strstrip (id);
        auto* table = gnc_commodity_table_get_table (pdata->book);
        if (table)
            ref = gnc_commodity_table_lookup (table, space, id);
    }

    xaccAccountSetCommodity (pdata->account, ref);
    return TRUE;
}

static gboolean
sax_act_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                       gpointer parent_data, gpointer, gpointer* result, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ok = TRUE;
    if (tree)
    {
        ok = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->account));
        xmlFreeNode (tree);
    }
    *result = nullptr;
    return ok;
}

static gboolean
sax_act_lots_dom_end (gpointer data_for_children, GSList*, GSList*,
                      gpointer parent_data, gpointer, gpointer* result, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ok = TRUE;

    if (tree)
    {
        for (xmlNodePtr mark = tree->xmlChildrenNode; mark && ok; mark = mark->next)
        {
            if (g_strcmp0 ("text", (char*) mark->name) == 0)
                continue;

            GNCLot* lot = dom_tree_to_lot (mark, pdata->book);
            if (lot)
                xaccAccountInsertLot (pdata->account, lot);
            else
                ok = FALSE;
        }
        xmlFreeNode (tree);
    }
    *result = nullptr;
    return ok;
}

/* Deprecated, pre-1.6 fallback tags. Rare in practice, so these stay on
   the (narrowly scoped) DOM path rather than earning their own SAX-direct
   handlers. */

static gboolean
sax_act_deprecated_currency_end (gpointer data_for_children, GSList*, GSList*,
                                 gpointer parent_data, gpointer, gpointer* result,
                                 const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    if (tree)
    {
        gnc_commodity* ref = dom_tree_to_commodity_ref_no_engine (tree, pdata->book);
        DxaccAccountSetCurrency (pdata->account, ref);
        xmlFreeNode (tree);
    }
    *result = nullptr;
    return TRUE;
}

static gboolean
sax_act_deprecated_security_end (gpointer data_for_children, GSList*, GSList*,
                                 gpointer parent_data, gpointer, gpointer* result,
                                 const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    if (tree)
    {
        gnc_commodity* orig = xaccAccountGetCommodity (pdata->account);
        if (!orig || gnc_commodity_is_currency (orig))
        {
            gnc_commodity* ref = dom_tree_to_commodity_ref_no_engine (tree, pdata->book);
            xaccAccountSetCommodity (pdata->account, ref);
            xaccAccountSetCommoditySCU (pdata->account, 0);
        }
        xmlFreeNode (tree);
    }
    *result = nullptr;
    return TRUE;
}

static gboolean
sax_act_deprecated_security_scu_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                                     gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        if (!xaccAccountGetCommoditySCU (pdata->account))
        {
            gint64 val;
            if (string_to_gint64 (txt, &val))
                xaccAccountSetCommoditySCU (pdata->account, val);
        }
        return TRUE;
    });
}

static gboolean
sax_act_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
              gpointer*, const gchar* tag, gchar**)
{
    /* See sax_trn_start() in gnc-transaction-xml-v2.cpp for why the NULL
       tag case (the synthetic top-frame priming call) must not allocate. */
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }

    auto* gdata = static_cast<gxpf_data*> (global_data);
    auto* pdata = g_new (account_sax_pdata, 1);
    pdata->book = static_cast<QofBook*> (gdata->bookdata);
    pdata->account = xaccMallocAccount (pdata->book);
    xaccAccountBeginEdit (pdata->account);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_act_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer global_data,
            gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<account_sax_pdata*> (data_for_children);
    auto* gdata = static_cast<gxpf_data*> (global_data);

    if (!tag)
        return TRUE;

    Account* acc = pdata->account;
    QofBook* book = pdata->book;
    g_free (pdata);

    /* Close out the BeginEdit() opened in sax_act_start() -- the account
       must be fully committed before the callback runs, matching
       dom_tree_to_account()'s own begin/commit pair -- then immediately
       reopen it below, exactly as the DOM-based gnc_account_end_handler()
       used to. */
    xaccAccountCommitEdit (acc);

    gdata->cb (tag, gdata->parsedata, acc);

    /* Now return the account to the "edit" state. At the end of reading
       all the transactions, we will Commit. This replaces #splits
       rebalances with #accounts rebalances at the end. A BIG win! */
    xaccAccountBeginEdit (acc);

    /* Backwards compatibility. If there's no parent, see if this account
       is of type ROOT. If not, find or create a ROOT account and make
       that the parent. */
    Account* parent = gnc_account_get_parent (acc);
    if (parent == NULL && xaccAccountGetType (acc) != ACCT_TYPE_ROOT)
    {
        Account* root = gnc_book_get_root_account (book);
        if (root == NULL)
            root = gnc_account_create_root (book);
        gnc_account_append_child (root, acc);
    }

    return TRUE;
}

static void
sax_act_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
             gpointer*, const gchar*)
{
    auto* pdata = static_cast<account_sax_pdata*> (data_for_children);
    if (!pdata) return;
    xaccAccountDestroy (pdata->account);
    g_free (pdata);
}

sixtp*
gnc_account_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_act_start,
        SIXTP_END_HANDLER_ID, sax_act_end,
        SIXTP_FAIL_HANDLER_ID, sax_act_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        act_name_string, restore_char_generator (sax_act_name_end),
        act_id_string, restore_char_generator (sax_act_id_end),
        act_type_string, restore_char_generator (sax_act_type_end),
        act_commodity_scu_string, restore_char_generator (sax_act_commodity_scu_end),
        act_non_standard_scu_string, restore_char_generator (sax_act_non_standard_scu_end),
        act_code_string, restore_char_generator (sax_act_code_end),
        act_description_string, restore_char_generator (sax_act_description_end),
        act_parent_string, restore_char_generator (sax_act_parent_end),
        act_hidden_string, restore_char_generator (sax_act_hidden_end),
        act_placeholder_string, restore_char_generator (sax_act_placeholder_end),
        act_slots_string, sixtp_dom_parser_new_rooted (sax_act_slots_dom_end, NULL, NULL),
        act_lots_string, sixtp_dom_parser_new_rooted (sax_act_lots_dom_end, NULL, NULL),
        act_currency_string, sixtp_dom_parser_new_rooted (sax_act_deprecated_currency_end, NULL, NULL),
        act_security_string, sixtp_dom_parser_new_rooted (sax_act_deprecated_security_end, NULL, NULL),
        act_security_scu_string, restore_char_generator (sax_act_deprecated_security_scu_end),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    sixtp_add_sub_parser (p, act_commodity_string,
                          sax_commodity_ref_parser_new (sax_act_commodity_end));

    /* act:currency-scu is deprecated and always ignored (see
       deprecated_account_currency_scu_handler() above); accept and
       discard it so files that still carry it keep parsing. */
    sixtp_add_sub_parser (p, act_currency_scu_string,
                          restore_char_generator (
                              [] (gpointer, GSList* dfc, GSList*, gpointer, gpointer,
                                  gpointer*, const gchar*) -> gboolean
                              {
                                  gchar* txt = concatenate_child_result_chars (dfc);
                                  g_free (txt);
                                  return TRUE;
                              }));

    /* Self-reference under our own tag: see the equivalent comment in
       gnc_transaction_sixtp_parser_create() in gnc-transaction-xml-v2.cpp. */
    sixtp_add_sub_parser (p, gnc_account_string, p);

    return p;
}

/* ======================  END OF FILE ===================*/
