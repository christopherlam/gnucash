/********************************************************************\
 * gnc-invoice-xml-v2.c -- invoice xml i/o implementation         *
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
#include "gncInvoiceP.h"
#include <guid.hpp>
#include <gnc-numeric.h>

#include "gnc-xml-helper.h"
#include "sixtp.h"
#include "sixtp-utils.h"
#include "sixtp-parsers.h"
#include "sixtp-utils.h"
#include "sixtp-dom-parsers.h"
#include "sixtp-dom-generators.h"
#include "gnc-invoice-xml-v2.h"
#include "gnc-owner-xml-v2.h"
#include "gnc-bill-term-xml-v2.h"

#include "gnc-xml.h"
#include "io-gncxml-gen.h"
#include "io-gncxml-v2.h"

#define _GNC_MOD_NAME   GNC_ID_INVOICE

const gchar* invoice_version_string = "2.0.0";

/* ids */
#define gnc_invoice_string "gnc:GncInvoice"
#define invoice_guid_string "invoice:guid"
#define invoice_id_string "invoice:id"
#define invoice_owner_string "invoice:owner"
#define invoice_opened_string "invoice:opened"
#define invoice_posted_string "invoice:posted"
#define invoice_terms_string "invoice:terms"
#define invoice_billing_id_string "invoice:billing_id"
#define invoice_notes_string "invoice:notes"
#define invoice_active_string "invoice:active"
#define invoice_posttxn_string "invoice:posttxn"
#define invoice_postlot_string "invoice:postlot"
#define invoice_postacc_string "invoice:postacc"
#define invoice_currency_string "invoice:currency"
#define invoice_billto_string "invoice:billto"
#define invoice_tochargeamt_string "invoice:charge-amt"
#define invoice_slots_string "invoice:slots"

static void
maybe_add_string (xmlNodePtr ptr, const char* tag, const char* str)
{
    if (str && *str)
        xmlAddChild (ptr, text_to_dom_tree (tag, str));
}

static void
maybe_add_time64 (xmlNodePtr ptr, const char* tag, time64 time)
{
    if (time != INT64_MAX)
        xmlAddChild (ptr, time64_to_dom_tree (tag, time));
}

static xmlNodePtr
invoice_dom_tree_create (GncInvoice* invoice)
{
    xmlNodePtr ret;
    time64 time;
    Transaction* txn;
    GNCLot* lot;
    Account* acc;
    GncBillTerm* term;
    GncOwner* billto;
    gnc_numeric amt;

    ret = xmlNewNode (NULL, BAD_CAST gnc_invoice_string);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST invoice_version_string);

    xmlAddChild (ret, guid_to_dom_tree (invoice_guid_string,
                                        qof_instance_get_guid (QOF_INSTANCE (invoice))));

    xmlAddChild (ret, text_to_dom_tree (invoice_id_string,
                                        gncInvoiceGetID (invoice)));

    xmlAddChild (ret, gnc_owner_to_dom_tree (invoice_owner_string,
                                             gncInvoiceGetOwner (invoice)));

    time = gncInvoiceGetDateOpened (invoice);
    xmlAddChild (ret, time64_to_dom_tree (invoice_opened_string, time));

    maybe_add_time64 (ret, invoice_posted_string, gncInvoiceGetDatePosted (invoice));

    term = gncInvoiceGetTerms (invoice);
    if (term)
        xmlAddChild (ret, guid_to_dom_tree (invoice_terms_string,
                                            qof_instance_get_guid (QOF_INSTANCE (term))));

    maybe_add_string (ret, invoice_billing_id_string,
                      gncInvoiceGetBillingID (invoice));
    maybe_add_string (ret, invoice_notes_string, gncInvoiceGetNotes (invoice));

    xmlAddChild (ret, int_to_dom_tree (invoice_active_string,
                                       gncInvoiceGetActive (invoice)));

    txn = gncInvoiceGetPostedTxn (invoice);
    if (txn)
        xmlAddChild (ret, guid_to_dom_tree (invoice_posttxn_string,
                                            xaccTransGetGUID (txn)));

    lot = gncInvoiceGetPostedLot (invoice);
    if (lot)
        xmlAddChild (ret, guid_to_dom_tree (invoice_postlot_string,
                                            gnc_lot_get_guid (lot)));

    acc = gncInvoiceGetPostedAcc (invoice);
    if (acc)
        xmlAddChild (ret, guid_to_dom_tree (invoice_postacc_string,
                                            qof_instance_get_guid (QOF_INSTANCE (acc))));

    xmlAddChild
    (ret,
     commodity_ref_to_dom_tree (invoice_currency_string,
                                gncInvoiceGetCurrency (invoice)));

    billto = gncInvoiceGetBillTo (invoice);
    if (billto && billto->owner.undefined != NULL)
        xmlAddChild (ret, gnc_owner_to_dom_tree (invoice_billto_string, billto));

    amt = gncInvoiceGetToChargeAmount (invoice);
    if (! gnc_numeric_zero_p (amt))
        xmlAddChild (ret, gnc_numeric_to_dom_tree (invoice_tochargeamt_string, &amt));

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (invoice_slots_string,
                                                      QOF_INSTANCE (invoice)));
    return ret;
}

/***********************************************************************/
/* SAX-direct (streaming) invoice parser: reads a gnc:GncInvoice
   straight off the SAX character stream, with no intermediate
   xmlNodePtr built for any of its fields. Nothing else in the
   codebase uses the old DOM-based parser this replaces, so it's gone
   entirely. */

struct invoice_sax_pdata
{
    GncInvoice* invoice;
    GncOwner owner;
    GncOwner billto;
    QofBook* book;
};

static gboolean
sax_invoice_guid_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                      gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;

        /* Adopt an invoice that already exists by this guid instead
           of the fresh one sax_invoice_start() made. */
        GncInvoice* invoice = gncInvoiceLookup (pdata->book, &guid);
        if (invoice)
        {
            gncInvoiceDestroy (pdata->invoice);
            pdata->invoice = invoice;
            gncInvoiceBeginEdit (invoice);
        }
        else
            gncInvoiceSetGUID (pdata->invoice, &guid);
        return TRUE;
    });
}

static gboolean
sax_invoice_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncInvoiceSetID (pdata->invoice, txt); return TRUE; });
}

static gboolean
sax_invoice_opened_ts_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        time64 t = gnc_iso8601_to_time64_gmt (txt);
        if (!dom_tree_valid_time64 (t, BAD_CAST invoice_opened_string)) t = 0;
        gncInvoiceSetDateOpened (pdata->invoice, t);
        return TRUE;
    });
}

static gboolean
sax_invoice_posted_ts_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        time64 t = gnc_iso8601_to_time64_gmt (txt);
        if (!dom_tree_valid_time64 (t, BAD_CAST invoice_posted_string)) t = 0;
        gncInvoiceSetDatePosted (pdata->invoice, t);
        return TRUE;
    });
}

static gboolean
sax_invoice_billing_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                            gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncInvoiceSetBillingID (pdata->invoice, txt); return TRUE; });
}

static gboolean
sax_invoice_notes_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncInvoiceSetNotes (pdata->invoice, txt); return TRUE; });
}

static gboolean
sax_invoice_active_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncInvoiceSetActive (pdata->invoice, (gboolean) val);
        return TRUE;
    });
}

static gboolean
sax_invoice_terms_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        GncBillTerm* term = gnc_billterm_xml_find_or_create (pdata->book, &guid);
        g_assert (term);
        gncInvoiceSetTerms (pdata->invoice, term);
        return TRUE;
    });
}

static gboolean
sax_invoice_posttxn_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        Transaction* txn = xaccTransLookup (&guid, pdata->book);
        g_return_val_if_fail (txn, FALSE);
        gncInvoiceSetPostedTxn (pdata->invoice, txn);
        return TRUE;
    });
}

static gboolean
sax_invoice_postlot_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        GNCLot* lot = gnc_lot_lookup (&guid, pdata->book);
        g_return_val_if_fail (lot, FALSE);
        gncInvoiceSetPostedLot (pdata->invoice, lot);
        return TRUE;
    });
}

static gboolean
sax_invoice_postacc_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        Account* acc = xaccAccountLookup (&guid, pdata->book);
        g_return_val_if_fail (acc, FALSE);
        gncInvoiceSetPostedAcc (pdata->invoice, acc);
        return TRUE;
    });
}

static gboolean
sax_invoice_tochargeamt_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                             gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gnc_numeric num = gnc_numeric_from_string (txt);
        gncInvoiceSetToChargeAmount (pdata->invoice, gnc_numeric_check (num) ? gnc_numeric_zero () : num);
        return TRUE;
    });
}

static gboolean
sax_invoice_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                           gpointer parent_data, gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ret = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->invoice));
    xmlFreeNode (tree);
    return ret;
}

static gboolean
sax_invoice_owner_start (GSList*, gpointer parent_data, gpointer,
                         gpointer* data_for_children, gpointer*, const gchar*, gchar**)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    auto* ctx = g_new (owner_sax_ctx, 1);
    ctx->owner = &pdata->owner;
    ctx->book = pdata->book;
    *data_for_children = ctx;
    return TRUE;
}

static gboolean
sax_invoice_billto_start (GSList*, gpointer parent_data, gpointer,
                          gpointer* data_for_children, gpointer*, const gchar*, gchar**)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
    auto* ctx = g_new (owner_sax_ctx, 1);
    ctx->owner = &pdata->billto;
    ctx->book = pdata->book;
    *data_for_children = ctx;
    return TRUE;
}

static gboolean
sax_invoice_currency_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                          gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (parent_data);
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
    gncInvoiceSetCurrency (pdata->invoice, com);
    return TRUE;
}

static gboolean
sax_invoice_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
                   gpointer*, const gchar* tag, gchar**)
{
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }
    auto* gdata = static_cast<gxpf_data*> (global_data);
    QofBook* book = static_cast<QofBook*> (gdata->bookdata);
    auto* pdata = g_new0 (invoice_sax_pdata, 1);
    pdata->invoice = gncInvoiceCreate (book);
    pdata->book = book;
    gncInvoiceBeginEdit (pdata->invoice);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_invoice_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer global_data,
                 gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (data_for_children);
    if (!pdata) return TRUE;
    if (!tag) { g_free (pdata); return TRUE; }

    gncInvoiceSetOwner (pdata->invoice, &pdata->owner);
    if (pdata->billto.owner.undefined != NULL)
        gncInvoiceSetBillTo (pdata->invoice, &pdata->billto);
    gncInvoiceCommitEdit (pdata->invoice);

    auto* gdata = static_cast<gxpf_data*> (global_data);
    gdata->cb (tag, gdata->parsedata, pdata->invoice);

    g_free (pdata);
    return TRUE;
}

static void
sax_invoice_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
                  gpointer*, const gchar*)
{
    auto* pdata = static_cast<invoice_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gncInvoiceDestroy (pdata->invoice);
    g_free (pdata);
}

static sixtp*
invoice_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_invoice_start,
        SIXTP_END_HANDLER_ID, sax_invoice_end,
        SIXTP_FAIL_HANDLER_ID, sax_invoice_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        invoice_guid_string, restore_char_generator (sax_invoice_guid_end),
        invoice_id_string, restore_char_generator (sax_invoice_id_end),
        invoice_billing_id_string, restore_char_generator (sax_invoice_billing_id_end),
        invoice_notes_string, restore_char_generator (sax_invoice_notes_end),
        invoice_active_string, restore_char_generator (sax_invoice_active_end),
        invoice_terms_string, restore_char_generator (sax_invoice_terms_end),
        invoice_posttxn_string, restore_char_generator (sax_invoice_posttxn_end),
        invoice_postlot_string, restore_char_generator (sax_invoice_postlot_end),
        invoice_postacc_string, restore_char_generator (sax_invoice_postacc_end),
        invoice_tochargeamt_string, restore_char_generator (sax_invoice_tochargeamt_end),
        invoice_slots_string, sixtp_dom_parser_new_rooted (sax_invoice_slots_dom_end, NULL, NULL),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    sixtp_add_sub_parser (p, invoice_owner_string, sax_owner_parser_new (sax_invoice_owner_start));
    sixtp_add_sub_parser (p, invoice_billto_string, sax_owner_parser_new (sax_invoice_billto_start));
    sixtp_add_sub_parser (p, invoice_opened_string, sax_time64_parser_new (sax_invoice_opened_ts_end));
    sixtp_add_sub_parser (p, invoice_posted_string, sax_time64_parser_new (sax_invoice_posted_ts_end));

    {
        sixtp* cmdty = sax_commodity_ref_parser_new (sax_invoice_currency_end);
        sixtp_add_sub_parser (p, invoice_currency_string, cmdty);
        sixtp_add_sub_parser (p, "invoice:commodity", cmdty);
    }

    return p;
}

static gboolean
invoice_should_be_saved (GncInvoice* invoice)
{
    const char* id;

    /* make sure this is a valid invoice before we save it -- should have an ID */
    id = gncInvoiceGetID (invoice);
    if (id == NULL || *id == '\0')
        return FALSE;

    return TRUE;
}

static void
do_count (QofInstance* invoice_p, gpointer count_p)
{
    int* count = static_cast<decltype (count)> (count_p);
    if (invoice_should_be_saved ((GncInvoice*)invoice_p))
        (*count)++;
}

static int
invoice_get_count (QofBook* book)
{
    int count = 0;
    qof_object_foreach (_GNC_MOD_NAME, book, do_count, (gpointer) &count);
    return count;
}

static void
xml_add_invoice (QofInstance* invoice_p, gpointer out_p)
{
    xmlNodePtr node;
    GncInvoice* invoice = (GncInvoice*) invoice_p;
    FILE* out = static_cast<decltype (out)> (out_p);

    if (ferror (out))
        return;
    if (!invoice_should_be_saved (invoice))
        return;

    node = invoice_dom_tree_create (invoice);
    xmlElemDump (out, NULL, node);
    xmlFreeNode (node);
    if (ferror (out) || fprintf (out, "\n") < 0)
        return;
}

static gboolean
invoice_write (FILE* out, QofBook* book)
{
    qof_object_foreach_sorted (_GNC_MOD_NAME, book, xml_add_invoice,
                               (gpointer) out);
    return ferror (out) == 0;
}

static gboolean
invoice_ns (FILE* out)
{
    g_return_val_if_fail (out, FALSE);
    return gnc_xml2_write_namespace_decl (out, "invoice");
}

void
gnc_invoice_xml_initialize (void)
{
    static GncXmlDataType_t be_data =
    {
        GNC_FILE_BACKEND_VERS,
        gnc_invoice_string,
        invoice_sixtp_parser_create,
        NULL,           /* add_item */
        invoice_get_count,
        invoice_write,
        NULL,           /* scrub */
        invoice_ns,
    };

    gnc_xml_register_backend(be_data);
}
