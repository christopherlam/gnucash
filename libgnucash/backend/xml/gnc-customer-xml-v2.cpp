/********************************************************************\
 * gnc-customer-xml-v2.c -- customer xml i/o implementation         *
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

#include "gncBillTermP.h"
#include "gncCustomerP.h"
#include "gncTaxTableP.h"
#include <guid.hpp>
#include <gnc-numeric.h>

#include "gnc-xml-helper.h"
#include "gnc-customer-xml-v2.h"
#include "gnc-address-xml-v2.h"
#include "gnc-bill-term-xml-v2.h"
#include "sixtp.h"
#include "sixtp-utils.h"
#include "sixtp-parsers.h"
#include "sixtp-utils.h"
#include "sixtp-dom-parsers.h"
#include "sixtp-dom-generators.h"

#include "gnc-xml.h"
#include "io-gncxml-gen.h"
#include "io-gncxml-v2.h"

#include "xml-helpers.h"

#define _GNC_MOD_NAME   GNC_ID_CUSTOMER

const gchar* customer_version_string = "2.0.0";

/* ids */
#define gnc_customer_string "gnc:GncCustomer"
#define cust_name_string "cust:name"
#define cust_guid_string "cust:guid"
#define cust_id_string "cust:id"
#define cust_addr_string "cust:addr"
#define cust_shipaddr_string "cust:shipaddr"
#define cust_notes_string "cust:notes"
#define cust_terms_string "cust:terms"
#define cust_taxincluded_string "cust:taxincluded"
#define cust_active_string "cust:active"
#define cust_discount_string "cust:discount"
#define cust_credit_string "cust:credit"
#define cust_currency_string "cust:currency"
#define cust_taxtable_string "cust:taxtable"
#define cust_taxtableoverride_string "cust:use-tt"
#define cust_slots_string "cust:slots"

static xmlNodePtr
customer_dom_tree_create (GncCustomer* cust)
{
    xmlNodePtr ret;
    gnc_numeric num;
    GncBillTerm* term;
    GncTaxTable* taxtable;

    ret = xmlNewNode (NULL, BAD_CAST gnc_customer_string);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST customer_version_string);

    xmlAddChild (ret, guid_to_dom_tree (cust_guid_string,
                                        qof_instance_get_guid (QOF_INSTANCE (cust))));

    xmlAddChild (ret, text_to_dom_tree (cust_name_string,
                                        gncCustomerGetName (cust)));

    xmlAddChild (ret, text_to_dom_tree (cust_id_string,
                                        gncCustomerGetID (cust)));

    xmlAddChild (ret, gnc_address_to_dom_tree (cust_addr_string,
                                               gncCustomerGetAddr (cust)));

    xmlAddChild (ret, gnc_address_to_dom_tree (cust_shipaddr_string,
                                               gncCustomerGetShipAddr (cust)));

    maybe_add_string (ret, cust_notes_string, gncCustomerGetNotes (cust));

    term = gncCustomerGetTerms (cust);
    if (term)
        xmlAddChild (ret, guid_to_dom_tree (cust_terms_string,
                                            qof_instance_get_guid (QOF_INSTANCE (term))));

    xmlAddChild (ret, text_to_dom_tree (cust_taxincluded_string,
                                        gncTaxIncludedTypeToString (
                                            gncCustomerGetTaxIncluded (cust))));

    xmlAddChild (ret, int_to_dom_tree (cust_active_string,
                                       gncCustomerGetActive (cust)));

    num = gncCustomerGetDiscount (cust);
    xmlAddChild (ret, gnc_numeric_to_dom_tree (cust_discount_string, &num));

    num = gncCustomerGetCredit (cust);
    xmlAddChild (ret, gnc_numeric_to_dom_tree (cust_credit_string, &num));

    xmlAddChild
    (ret,
     commodity_ref_to_dom_tree (cust_currency_string,
                                gncCustomerGetCurrency (cust)));

    xmlAddChild (ret, int_to_dom_tree (cust_taxtableoverride_string,
                                       gncCustomerGetTaxTableOverride (cust)));
    taxtable = gncCustomerGetTaxTable (cust);
    if (taxtable)
        xmlAddChild (ret, guid_to_dom_tree (cust_taxtable_string,
                                            qof_instance_get_guid (QOF_INSTANCE (taxtable))));

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (cust_slots_string,
                                                      QOF_INSTANCE (cust)));

    return ret;
}

/***********************************************************************/
/* SAX-direct (streaming) customer parser: reads a gnc:GncCustomer
   straight off the SAX character stream, with no intermediate
   xmlNodePtr built for any of its fields. Nothing else in the
   codebase uses the old DOM-based parser this replaces, so it's gone
   entirely. */

struct customer_sax_pdata
{
    GncCustomer* customer;
    QofBook* book;
};

static gboolean
sax_cust_guid_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                   gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;

        /* Adopt a customer that already exists by this guid (e.g. a
           placeholder created earlier by an owner or invoice
           reference) instead of the fresh one sax_customer_start()
           made. */
        GncCustomer* cust = gncCustomerLookup (pdata->book, &guid);
        if (cust)
        {
            gncCustomerDestroy (pdata->customer);
            pdata->customer = cust;
            gncCustomerBeginEdit (cust);
        }
        else
            gncCustomerSetGUID (pdata->customer, &guid);
        return TRUE;
    });
}

static gboolean
sax_cust_name_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                   gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncCustomerSetName (pdata->customer, txt); return TRUE; });
}

static gboolean
sax_cust_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                 gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncCustomerSetID (pdata->customer, txt); return TRUE; });
}

static gboolean
sax_cust_notes_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncCustomerSetNotes (pdata->customer, txt); return TRUE; });
}

static gboolean
sax_cust_terms_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        GncBillTerm* term = gnc_billterm_xml_find_or_create (pdata->book, &guid);
        g_assert (term);
        gncCustomerSetTerms (pdata->customer, term);
        return TRUE;
    });
}

static gboolean
sax_cust_taxincluded_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                          gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncTaxIncluded type;
        if (gncTaxIncludedStringToType (txt, &type))
            gncCustomerSetTaxIncluded (pdata->customer, type);
        return TRUE;
    });
}

static gboolean
sax_cust_active_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                     gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncCustomerSetActive (pdata->customer, (gboolean) val);
        return TRUE;
    });
}

static gboolean
sax_cust_discount_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gnc_numeric num = gnc_numeric_from_string (txt);
        gncCustomerSetDiscount (pdata->customer, gnc_numeric_check (num) ? gnc_numeric_zero () : num);
        return TRUE;
    });
}

static gboolean
sax_cust_credit_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                     gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gnc_numeric num = gnc_numeric_from_string (txt);
        gncCustomerSetCredit (pdata->customer, gnc_numeric_check (num) ? gnc_numeric_zero () : num);
        return TRUE;
    });
}

static gboolean
sax_cust_taxtable_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;

        GncTaxTable* taxtable = gncTaxTableLookup (pdata->book, &guid);
        if (!taxtable)
        {
            taxtable = gncTaxTableCreate (pdata->book);
            gncTaxTableBeginEdit (taxtable);
            gncTaxTableSetGUID (taxtable, &guid);
            gncTaxTableCommitEdit (taxtable);
        }
        else
            gncTaxTableDecRef (taxtable);

        gncCustomerSetTaxTable (pdata->customer, taxtable);
        return TRUE;
    });
}

static gboolean
sax_cust_taxtableoverride_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                               gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncCustomerSetTaxTableOverride (pdata->customer, (gboolean) val);
        return TRUE;
    });
}

static gboolean
sax_cust_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                        gpointer parent_data, gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ret = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->customer));
    xmlFreeNode (tree);
    return ret;
}

static gboolean
sax_cust_addr_start (GSList*, gpointer parent_data, gpointer,
                     gpointer* data_for_children, gpointer*, const gchar*, gchar**)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    *data_for_children = gncCustomerGetAddr (pdata->customer);
    return TRUE;
}

static gboolean
sax_cust_shipaddr_start (GSList*, gpointer parent_data, gpointer,
                         gpointer* data_for_children, gpointer*, const gchar*, gchar**)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
    *data_for_children = gncCustomerGetShipAddr (pdata->customer);
    return TRUE;
}

static gboolean
sax_cust_currency_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (parent_data);
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

    gnc_commodity* com = nullptr;
    if (space && id)
    {
        g_strstrip (space);
        g_strstrip (id);
        auto* table = gnc_commodity_table_get_table (pdata->book);
        if (table)
            com = gnc_commodity_table_lookup (table, space, id);
    }
    g_return_val_if_fail (com, FALSE);
    gncCustomerSetCurrency (pdata->customer, com);
    return TRUE;
}

static gboolean
sax_cust_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
                gpointer*, const gchar* tag, gchar**)
{
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }
    auto* gdata = static_cast<gxpf_data*> (global_data);
    QofBook* book = static_cast<QofBook*> (gdata->bookdata);
    auto* pdata = g_new (customer_sax_pdata, 1);
    pdata->customer = gncCustomerCreate (book);
    pdata->book = book;
    gncCustomerBeginEdit (pdata->customer);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_cust_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer global_data,
             gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<customer_sax_pdata*> (data_for_children);
    if (!pdata) return TRUE;
    if (!tag) { g_free (pdata); return TRUE; }

    GncCustomer* cust = pdata->customer;
    g_free (pdata);

    gncCustomerCommitEdit (cust);
    auto* gdata = static_cast<gxpf_data*> (global_data);
    gdata->cb (tag, gdata->parsedata, cust);
    return TRUE;
}

static void
sax_cust_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
              gpointer*, const gchar*)
{
    auto* pdata = static_cast<customer_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gncCustomerDestroy (pdata->customer);
    g_free (pdata);
}

static sixtp*
customer_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_cust_start,
        SIXTP_END_HANDLER_ID, sax_cust_end,
        SIXTP_FAIL_HANDLER_ID, sax_cust_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        cust_guid_string, restore_char_generator (sax_cust_guid_end),
        cust_name_string, restore_char_generator (sax_cust_name_end),
        cust_id_string, restore_char_generator (sax_cust_id_end),
        cust_notes_string, restore_char_generator (sax_cust_notes_end),
        cust_terms_string, restore_char_generator (sax_cust_terms_end),
        cust_taxincluded_string, restore_char_generator (sax_cust_taxincluded_end),
        cust_active_string, restore_char_generator (sax_cust_active_end),
        cust_discount_string, restore_char_generator (sax_cust_discount_end),
        cust_credit_string, restore_char_generator (sax_cust_credit_end),
        cust_taxtable_string, restore_char_generator (sax_cust_taxtable_end),
        cust_taxtableoverride_string, restore_char_generator (sax_cust_taxtableoverride_end),
        cust_slots_string, sixtp_dom_parser_new_rooted (sax_cust_slots_dom_end, NULL, NULL),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    sixtp_add_sub_parser (p, cust_addr_string, sax_address_parser_new (sax_cust_addr_start));
    sixtp_add_sub_parser (p, cust_shipaddr_string, sax_address_parser_new (sax_cust_shipaddr_start));

    {
        sixtp* cmdty = sax_commodity_ref_parser_new (sax_cust_currency_end);
        sixtp_add_sub_parser (p, cust_currency_string, cmdty);
        sixtp_add_sub_parser (p, "cust:commodity", cmdty);
    }

    return p;
}

static gboolean
customer_should_be_saved (GncCustomer* customer)
{
    const char* id;

    /* make sure this is a valid customer before we save it -- should have an ID */
    id = gncCustomerGetID (customer);
    if (id == NULL || *id == '\0')
        return FALSE;

    return TRUE;
}

static void
do_count (QofInstance* cust_p, gpointer count_p)
{
    int* count = static_cast<decltype (count)> (count_p);
    if (customer_should_be_saved ((GncCustomer*)cust_p))
        (*count)++;
}

static int
customer_get_count (QofBook* book)
{
    int count = 0;
    qof_object_foreach (_GNC_MOD_NAME, book, do_count, (gpointer) &count);
    return count;
}

static void
xml_add_customer (QofInstance* cust_p, gpointer out_p)
{
    xmlNodePtr node;
    GncCustomer* cust = (GncCustomer*) cust_p;
    FILE* out = static_cast<decltype (out)> (out_p);

    if (ferror (out))
        return;
    if (!customer_should_be_saved (cust))
        return;

    node = customer_dom_tree_create (cust);
    xmlElemDump (out, NULL, node);
    xmlFreeNode (node);
    if (ferror (out) || fprintf (out, "\n") < 0)
        return;
}

static gboolean
customer_write (FILE* out, QofBook* book)
{
    qof_object_foreach_sorted (_GNC_MOD_NAME, book, xml_add_customer,
                               (gpointer) out);
    return ferror (out) == 0;
}

static gboolean
customer_ns (FILE* out)
{
    g_return_val_if_fail (out, FALSE);
    return gnc_xml2_write_namespace_decl (out, "cust");
}

void
gnc_customer_xml_initialize (void)
{
    static GncXmlDataType_t be_data =
    {
        GNC_FILE_BACKEND_VERS,
        gnc_customer_string,
        customer_sixtp_parser_create,
        NULL,           /* add_item */
        customer_get_count,
        customer_write,
        NULL,           /* scrub */
        customer_ns,
    };

    gnc_xml_register_backend (be_data);
}
