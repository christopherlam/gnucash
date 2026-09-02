/*
 * gnc-balance-assertion-xml-v2.cpp -- balance assertion xml i/o
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
#include "sixtp-dom-parsers.h"
#include "sixtp-dom-generators.h"

#include "gnc-xml.h"
#include "io-gncxml-gen.h"
#include "io-gncxml-v2.h"

static QofLogModule log_module = GNC_MOD_IO;

const gchar* balance_assertion_version_string = "2.0.0";

/* ids */
#define gnc_balance_assertion_string  "gnc:balance-assertion"
#define ba_id_string                  "bassert:id"
#define ba_account_string             "bassert:account"
#define ba_date_string                "bassert:date"
#define ba_amount_string              "bassert:amount"
#define ba_notes_string               "bassert:notes"
#define ba_slots_string               "bassert:slots"

xmlNodePtr
gnc_balance_assertion_dom_tree_create (GncBalanceAssertion* ba)
{
    xmlNodePtr ret;
    Account* acc;
    const char* notes;

    ENTER ("(balance assertion=%p)", ba);

    ret = xmlNewNode (NULL, BAD_CAST gnc_balance_assertion_string);
    xmlSetProp (ret, BAD_CAST "version",
                BAD_CAST balance_assertion_version_string);

    xmlAddChild (ret, guid_to_dom_tree (ba_id_string,
                                        gnc_balance_assertion_get_guid (ba)));

    acc = gnc_balance_assertion_get_account (ba);
    if (acc)
        xmlAddChild (ret, guid_to_dom_tree (ba_account_string,
                                            xaccAccountGetGUID (acc)));

    xmlAddChild (ret, time64_to_dom_tree (ba_date_string,
                                          gnc_balance_assertion_get_date (ba)));

    gnc_numeric amount = gnc_balance_assertion_get_amount (ba);
    xmlAddChild (ret, gnc_numeric_to_dom_tree (ba_amount_string, &amount));

    notes = gnc_balance_assertion_get_notes (ba);
    if (notes && *notes)
        xmlAddChild (ret, text_to_dom_tree (ba_notes_string, notes));

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (ba_slots_string,
                                                      QOF_INSTANCE (ba)));

    LEAVE (" ");
    return ret;
}

/***********************************************************************/

static gboolean
ba_id_handler (xmlNodePtr node, gpointer ba)
{
    auto guid = dom_tree_to_guid (node);
    g_return_val_if_fail (guid, FALSE);
    qof_instance_set_guid (QOF_INSTANCE (ba), &*guid);
    return TRUE;
}

static gboolean
ba_account_handler (xmlNodePtr node, gpointer p)
{
    auto ba = GNC_BALANCE_ASSERTION (p);
    auto guid = dom_tree_to_guid (node);
    g_return_val_if_fail (guid, FALSE);

    auto acc = xaccAccountLookup (&*guid,
                                  qof_instance_get_book (QOF_INSTANCE (ba)));
    /* Accounts are written before balance assertions, so a missing
     * account means a damaged file rather than an ordering problem. Keep
     * the assertion -- it will simply report as unevaluatable -- but say
     * so in the log. */
    if (!acc)
    {
        PWARN ("balance assertion refers to an unknown account");
        return TRUE;
    }

    gnc_balance_assertion_set_account (ba, acc);
    return TRUE;
}

static gboolean
ba_date_handler (xmlNodePtr node, gpointer ba)
{
    time64 date = dom_tree_to_time64 (node);

    if (!dom_tree_valid_time64 (date, node->name))
        return FALSE;

    gnc_balance_assertion_set_date (GNC_BALANCE_ASSERTION (ba), date);
    return TRUE;
}

static gboolean
ba_amount_handler (xmlNodePtr node, gpointer ba)
{
    gnc_balance_assertion_set_amount (GNC_BALANCE_ASSERTION (ba),
                                      dom_tree_to_gnc_numeric (node));
    return TRUE;
}

static gboolean
ba_notes_handler (xmlNodePtr node, gpointer ba)
{
    return apply_xmlnode_text (gnc_balance_assertion_set_notes,
                               GNC_BALANCE_ASSERTION (ba), node);
}

static gboolean
ba_slots_handler (xmlNodePtr node, gpointer ba)
{
    return dom_tree_create_instance_slots (node, QOF_INSTANCE (ba));
}

static struct dom_tree_handler balance_assertion_handlers[] =
{
    { ba_id_string, ba_id_handler, 1, 0 },
    { ba_account_string, ba_account_handler, 0, 0 },
    { ba_date_string, ba_date_handler, 1, 0 },
    { ba_amount_string, ba_amount_handler, 1, 0 },
    { ba_notes_string, ba_notes_handler, 0, 0 },
    { ba_slots_string, ba_slots_handler, 0, 0 },
    { NULL, 0, 0, 0 }
};

static gboolean
gnc_balance_assertion_end_handler (gpointer data_for_children,
                                   GSList* data_from_children,
                                   GSList* sibling_data,
                                   gpointer parent_data, gpointer global_data,
                                   gpointer* result, const gchar* tag)
{
    GncBalanceAssertion* ba;
    xmlNodePtr tree = (xmlNodePtr)data_for_children;
    gxpf_data* gdata = (gxpf_data*)global_data;
    QofBook* book = static_cast<decltype (book)> (gdata->bookdata);

    if (parent_data)
        return TRUE;

    /* the parser calls us again with a NULL tag; ignore those */
    if (!tag)
        return TRUE;

    g_return_val_if_fail (tree, FALSE);

    ba = dom_tree_to_balance_assertion (tree, book);
    xmlFreeNode (tree);
    if (ba != NULL)
        gdata->cb (tag, gdata->parsedata, ba);

    return ba != NULL;
}

GncBalanceAssertion*
dom_tree_to_balance_assertion (xmlNodePtr node, QofBook* book)
{
    GncBalanceAssertion* ba;

    ba = gnc_balance_assertion_new (book);
    if (!dom_tree_generic_parse (node, balance_assertion_handlers, ba))
    {
        PERR ("failed to parse balance assertion tree");
        gnc_balance_assertion_destroy (ba);
        ba = NULL;
    }
    return ba;
}

sixtp*
gnc_balance_assertion_sixtp_parser_create (void)
{
    return sixtp_dom_parser_new (gnc_balance_assertion_end_handler, NULL, NULL);
}
/* ======================  END OF FILE ===================*/
