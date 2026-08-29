/********************************************************************\
 * gnc-entry-xml-v2.c -- entry xml i/o implementation         *
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

#include "gncEntryP.h"
#include "gncOrderP.h"
#include "gncInvoiceP.h"
#include "gncTaxTableP.h"
#include <guid.hpp>
#include <gnc-numeric.h>

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
#include "gnc-entry-xml-v2.h"
#include "gnc-owner-xml-v2.h"

#define _GNC_MOD_NAME   GNC_ID_ENTRY

const gchar* entry_version_string = "2.0.0";

/* ids */
#define gnc_entry_string "gnc:GncEntry"
#define entry_guid_string "entry:guid"
#define entry_date_string "entry:date"
#define entry_dateentered_string "entry:entered"
#define entry_description_string "entry:description"
#define entry_action_string "entry:action"
#define entry_notes_string "entry:notes"
#define entry_qty_string "entry:qty"

/* cust inv */
#define entry_invacct_string "entry:i-acct"
#define entry_iprice_string "entry:i-price"
#define entry_idiscount_string "entry:i-discount"
#define entry_idisctype_string "entry:i-disc-type"
#define entry_idischow_string "entry:i-disc-how"
#define entry_itaxable_string "entry:i-taxable"
#define entry_itaxincluded_string "entry:i-taxincluded"
#define entry_itaxtable_string "entry:i-taxtable"

/* vend bill */
#define entry_billacct_string "entry:b-acct"
#define entry_bprice_string "entry:b-price"
#define entry_btaxable_string "entry:b-taxable"
#define entry_btaxincluded_string "entry:b-taxincluded"
#define entry_btaxtable_string "entry:b-taxtable"
#define entry_billable_string "entry:billable"
#define entry_billto_string "entry:billto"

/* emp bill */
#define entry_billpayment_string "entry:b-pay"

/* other stuff */
#define entry_order_string "entry:order"
#define entry_invoice_string "entry:invoice"
#define entry_bill_string "entry:bill"
#define entry_slots_string "entry:slots"

static void
maybe_add_string (xmlNodePtr ptr, const char* tag, const char* str)
{
    if (str && *str)
        xmlAddChild (ptr, text_to_dom_tree (tag, str));
}

static void
maybe_add_numeric (xmlNodePtr ptr, const char* tag, gnc_numeric num)
{
    if (!gnc_numeric_zero_p (num))
        xmlAddChild (ptr, gnc_numeric_to_dom_tree (tag, &num));
}

static xmlNodePtr
entry_dom_tree_create (GncEntry* entry)
{
    xmlNodePtr ret;
    Account* acc;
    GncTaxTable* taxtable;
    GncOrder* order;
    GncInvoice* invoice;

    ret = xmlNewNode (NULL, BAD_CAST gnc_entry_string);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST entry_version_string);

    xmlAddChild (ret, guid_to_dom_tree (entry_guid_string,
                                        qof_instance_get_guid (QOF_INSTANCE (entry))));

    auto time = gncEntryGetDate (entry);
    xmlAddChild (ret, time64_to_dom_tree (entry_date_string, time));

    time = gncEntryGetDateEntered (entry);
    xmlAddChild (ret, time64_to_dom_tree (entry_dateentered_string, time));

    maybe_add_string (ret, entry_description_string,
                      gncEntryGetDescription (entry));
    maybe_add_string (ret, entry_action_string, gncEntryGetAction (entry));
    maybe_add_string (ret, entry_notes_string, gncEntryGetNotes (entry));

    maybe_add_numeric (ret, entry_qty_string, gncEntryGetQuantity (entry));

    /* cust invoice */

    acc = gncEntryGetInvAccount (entry);
    if (acc)
        xmlAddChild (ret, guid_to_dom_tree (entry_invacct_string,
                                            qof_instance_get_guid (QOF_INSTANCE (acc))));

    maybe_add_numeric (ret, entry_iprice_string, gncEntryGetInvPrice (entry));

    maybe_add_numeric (ret, entry_idiscount_string,
                       gncEntryGetInvDiscount (entry));

    invoice = gncEntryGetInvoice (entry);
    if (invoice)
    {
        xmlAddChild (ret, guid_to_dom_tree (entry_invoice_string,
                                            qof_instance_get_guid (QOF_INSTANCE (invoice))));

        xmlAddChild (ret, text_to_dom_tree (entry_idisctype_string,
                                            gncAmountTypeToString (
                                                gncEntryGetInvDiscountType (entry))));
        xmlAddChild (ret, text_to_dom_tree (entry_idischow_string,
                                            gncEntryDiscountHowToString (
                                                gncEntryGetInvDiscountHow (entry))));

        xmlAddChild (ret, int_to_dom_tree (entry_itaxable_string,
                                           gncEntryGetInvTaxable (entry)));
        xmlAddChild (ret, int_to_dom_tree (entry_itaxincluded_string,
                                           gncEntryGetInvTaxIncluded (entry)));
    }

    taxtable = gncEntryGetInvTaxTable (entry);
    if (taxtable)
        xmlAddChild (ret, guid_to_dom_tree (entry_itaxtable_string,
                                            qof_instance_get_guid (QOF_INSTANCE (taxtable))));

    /* vendor bills */

    acc = gncEntryGetBillAccount (entry);
    if (acc)
        xmlAddChild (ret, guid_to_dom_tree (entry_billacct_string,
                                            qof_instance_get_guid (QOF_INSTANCE (acc))));

    maybe_add_numeric (ret, entry_bprice_string, gncEntryGetBillPrice (entry));

    invoice = gncEntryGetBill (entry);
    if (invoice)
    {
        GncOwner* owner;
        xmlAddChild (ret, guid_to_dom_tree (entry_bill_string,
                                            qof_instance_get_guid (QOF_INSTANCE (invoice))));
        xmlAddChild (ret, int_to_dom_tree (entry_billable_string,
                                           gncEntryGetBillable (entry)));
        owner = gncEntryGetBillTo (entry);
        if (owner && owner->owner.undefined != NULL)
            xmlAddChild (ret, gnc_owner_to_dom_tree (entry_billto_string, owner));

        xmlAddChild (ret, int_to_dom_tree (entry_btaxable_string,
                                           gncEntryGetBillTaxable (entry)));
        xmlAddChild (ret, int_to_dom_tree (entry_btaxincluded_string,
                                           gncEntryGetBillTaxIncluded (entry)));
        maybe_add_string (ret, entry_billpayment_string,
                          gncEntryPaymentTypeToString (gncEntryGetBillPayment (entry)));
    }

    taxtable = gncEntryGetBillTaxTable (entry);
    if (taxtable)
        xmlAddChild (ret, guid_to_dom_tree (entry_btaxtable_string,
                                            qof_instance_get_guid (QOF_INSTANCE (taxtable))));

    /* Other stuff */

    order = gncEntryGetOrder (entry);
    if (order)
        xmlAddChild (ret, guid_to_dom_tree (entry_order_string,
                                            qof_instance_get_guid (QOF_INSTANCE (order))));

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (entry_slots_string,
                                                      QOF_INSTANCE (entry)));
    return ret;
}

/***********************************************************************/
/* SAX-direct (streaming) entry parser: reads a gnc:GncEntry straight
   off the SAX character stream, with no intermediate xmlNodePtr built
   for any of its fields. Nothing else in the codebase uses the old
   DOM-based parser this replaces, so it's gone entirely. */

struct entry_sax_pdata
{
    GncEntry* entry;
    QofBook* book;
    Account* acc;       /* legacy entry:acct: which side it belongs to
                            isn't known until entry:bill/invoice is seen */
    GncOwner billto;
};

static gboolean
sax_entry_guid_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;

        /* Adopt an entry that already exists by this guid (e.g. a
           placeholder created earlier by an order/invoice/bill
           reference) instead of the fresh one sax_entry_start()
           made. */
        GncEntry* entry = gncEntryLookup (pdata->book, &guid);
        if (entry)
        {
            gncEntryDestroy (pdata->entry);
            pdata->entry = entry;
            gncEntryBeginEdit (entry);
        }
        else
            gncEntrySetGUID (pdata->entry, &guid);
        return TRUE;
    });
}

static gboolean
sax_entry_date_ts_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        time64 t = gnc_iso8601_to_time64_gmt (txt);
        if (!dom_tree_valid_time64 (t, BAD_CAST entry_date_string)) t = 0;
        gncEntrySetDate (pdata->entry, t);
        return TRUE;
    });
}

static gboolean
sax_entry_dateentered_ts_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                              gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        time64 t = gnc_iso8601_to_time64_gmt (txt);
        if (!dom_tree_valid_time64 (t, BAD_CAST entry_dateentered_string)) t = 0;
        gncEntrySetDateEntered (pdata->entry, t);
        return TRUE;
    });
}

static gboolean
sax_entry_description_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncEntrySetDescription (pdata->entry, txt); return TRUE; });
}

static gboolean
sax_entry_action_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                      gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncEntrySetAction (pdata->entry, txt); return TRUE; });
}

static gboolean
sax_entry_notes_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                     gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncEntrySetNotes (pdata->entry, txt); return TRUE; });
}

static gboolean
sax_entry_numeric_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       void (*setter) (GncEntry*, gnc_numeric))
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata, setter] (const char* txt) -> gboolean
    {
        gnc_numeric num = gnc_numeric_from_string (txt);
        setter (pdata->entry, gnc_numeric_check (num) ? gnc_numeric_zero () : num);
        return TRUE;
    });
}

static gboolean
sax_entry_qty_end (gpointer dfc_holder, GSList* dfc, GSList* sib, gpointer parent_data,
                   gpointer gd, gpointer* res, const gchar* tag)
{ return sax_entry_numeric_end (dfc_holder, dfc, sib, parent_data, gncEntrySetQuantity); }

static gboolean
sax_entry_iprice_end (gpointer dfc_holder, GSList* dfc, GSList* sib, gpointer parent_data,
                      gpointer gd, gpointer* res, const gchar* tag)
{ return sax_entry_numeric_end (dfc_holder, dfc, sib, parent_data, gncEntrySetInvPrice); }

static gboolean
sax_entry_idiscount_end (gpointer dfc_holder, GSList* dfc, GSList* sib, gpointer parent_data,
                         gpointer gd, gpointer* res, const gchar* tag)
{ return sax_entry_numeric_end (dfc_holder, dfc, sib, parent_data, gncEntrySetInvDiscount); }

static gboolean
sax_entry_bprice_end (gpointer dfc_holder, GSList* dfc, GSList* sib, gpointer parent_data,
                      gpointer gd, gpointer* res, const gchar* tag)
{ return sax_entry_numeric_end (dfc_holder, dfc, sib, parent_data, gncEntrySetBillPrice); }

static gboolean
sax_entry_price_legacy_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                            gpointer, gpointer*, const gchar*)
{
    /* legacy entry:price: sets both inv-price and bill-price. */
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gnc_numeric num = gnc_numeric_from_string (txt);
        if (gnc_numeric_check (num)) num = gnc_numeric_zero ();
        gncEntrySetInvPrice (pdata->entry, num);
        gncEntrySetBillPrice (pdata->entry, num);
        return TRUE;
    });
}

static gboolean
sax_entry_idisctype_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncAmountType type;
        if (gncAmountStringToType (txt, &type))
            gncEntrySetInvDiscountType (pdata->entry, type);
        return TRUE;
    });
}

static gboolean
sax_entry_idischow_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncDiscountHow how;
        if (gncEntryDiscountStringToHow (txt, &how))
            gncEntrySetInvDiscountHow (pdata->entry, how);
        return TRUE;
    });
}

static gboolean
sax_entry_boolean_end (gpointer dfc_holder, GSList* dfc, gpointer parent_data,
                       void (*setter) (GncEntry*, gboolean))
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata, setter] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        setter (pdata->entry, (gboolean) val);
        return TRUE;
    });
}

static gboolean
sax_entry_itaxable_end (gpointer dh, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{ return sax_entry_boolean_end (dh, dfc, parent_data, gncEntrySetInvTaxable); }

static gboolean
sax_entry_itaxincluded_end (gpointer dh, GSList* dfc, GSList*, gpointer parent_data,
                            gpointer, gpointer*, const gchar*)
{ return sax_entry_boolean_end (dh, dfc, parent_data, gncEntrySetInvTaxIncluded); }

static gboolean
sax_entry_btaxable_end (gpointer dh, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{ return sax_entry_boolean_end (dh, dfc, parent_data, gncEntrySetBillTaxable); }

static gboolean
sax_entry_btaxincluded_end (gpointer dh, GSList* dfc, GSList*, gpointer parent_data,
                            gpointer, gpointer*, const gchar*)
{ return sax_entry_boolean_end (dh, dfc, parent_data, gncEntrySetBillTaxIncluded); }

static gboolean
sax_entry_billable_end (gpointer dh, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{ return sax_entry_boolean_end (dh, dfc, parent_data, gncEntrySetBillable); }

static gboolean
sax_entry_taxtable_end (gpointer parent_data, GSList* dfc,
                        void (*setter) (GncEntry*, GncTaxTable*))
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata, setter] (const char* txt) -> gboolean
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

        setter (pdata->entry, taxtable);
        return TRUE;
    });
}

static gboolean
sax_entry_itaxtable_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{ return sax_entry_taxtable_end (parent_data, dfc, gncEntrySetInvTaxTable); }

static gboolean
sax_entry_btaxtable_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{ return sax_entry_taxtable_end (parent_data, dfc, gncEntrySetBillTaxTable); }

static gboolean
sax_entry_account_end (gpointer parent_data, GSList* dfc,
                       void (*setter) (GncEntry*, Account*))
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata, setter] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        Account* acc = xaccAccountLookup (&guid, pdata->book);
        g_return_val_if_fail (acc, FALSE);

        if (setter)
            setter (pdata->entry, acc);
        else
            pdata->acc = acc;
        return TRUE;
    });
}

static gboolean
sax_entry_invacct_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{ return sax_entry_account_end (parent_data, dfc, gncEntrySetInvAccount); }

static gboolean
sax_entry_billacct_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{ return sax_entry_account_end (parent_data, dfc, gncEntrySetBillAccount); }

static gboolean
sax_entry_acct_legacy_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    /* legacy entry:acct: which side (invoice or bill) it belongs to
       isn't known until entry:bill/entry:invoice is seen, so stash it
       and resolve at the entry's own end. */
    return sax_entry_account_end (parent_data, dfc, nullptr);
}

static gboolean
sax_entry_billto_start (GSList*, gpointer parent_data, gpointer,
                        gpointer* data_for_children, gpointer*, const gchar*, gchar**)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    auto* ctx = g_new (owner_sax_ctx, 1);
    ctx->owner = &pdata->billto;
    ctx->book = pdata->book;
    *data_for_children = ctx;
    return TRUE;
}

static gboolean
sax_entry_billpayment_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncEntryPaymentType type;
        if (gncEntryPaymentStringToType (txt, &type))
            gncEntrySetBillPayment (pdata->entry, type);
        return TRUE;
    });
}

static gboolean
sax_entry_order_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                     gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        GncOrder* order = gncOrderLookup (pdata->book, &guid);
        if (!order)
        {
            order = gncOrderCreate (pdata->book);
            gncOrderBeginEdit (order);
            gncOrderSetGUID (order, &guid);
            gncOrderCommitEdit (order);
        }
        gncOrderBeginEdit (order);
        gncOrderAddEntry (order, pdata->entry);
        gncOrderCommitEdit (order);
        return TRUE;
    });
}

static gboolean
sax_entry_invoice_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        GncInvoice* invoice = gncInvoiceLookup (pdata->book, &guid);
        if (!invoice)
        {
            invoice = gncInvoiceCreate (pdata->book);
            gncInvoiceBeginEdit (invoice);
            gncInvoiceSetGUID (invoice, &guid);
            gncInvoiceCommitEdit (invoice);
        }
        gncInvoiceBeginEdit (invoice);
        gncInvoiceAddEntry (invoice, pdata->entry);
        gncInvoiceCommitEdit (invoice);
        return TRUE;
    });
}

static gboolean
sax_entry_bill_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        GncInvoice* invoice = gncInvoiceLookup (pdata->book, &guid);
        if (!invoice)
        {
            invoice = gncInvoiceCreate (pdata->book);
            gncInvoiceBeginEdit (invoice);
            gncInvoiceSetGUID (invoice, &guid);
            gncInvoiceCommitEdit (invoice);
        }
        gncInvoiceBeginEdit (invoice);
        gncBillAddEntry (invoice, pdata->entry);
        gncInvoiceCommitEdit (invoice);
        return TRUE;
    });
}

static gboolean
sax_entry_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                         gpointer parent_data, gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ret = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->entry));
    xmlFreeNode (tree);
    return ret;
}

static gboolean
sax_entry_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
                 gpointer*, const gchar* tag, gchar**)
{
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }
    auto* gdata = static_cast<gxpf_data*> (global_data);
    QofBook* book = static_cast<QofBook*> (gdata->bookdata);
    auto* pdata = g_new0 (entry_sax_pdata, 1);
    pdata->entry = gncEntryCreate (book);
    pdata->book = book;
    gncEntryBeginEdit (pdata->entry);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_entry_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer global_data,
              gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<entry_sax_pdata*> (data_for_children);
    if (!pdata) return TRUE;
    if (!tag) { g_free (pdata); return TRUE; }

    if (pdata->billto.owner.undefined != NULL)
        gncEntrySetBillTo (pdata->entry, &pdata->billto);

    /* legacy entry:acct: which side it belongs to wasn't known until
       now -- matches the original's post-parse resolution. */
    if (pdata->acc != NULL)
    {
        if (gncEntryGetBill (pdata->entry))
            gncEntrySetBillAccount (pdata->entry, pdata->acc);
        else
            gncEntrySetInvAccount (pdata->entry, pdata->acc);
    }

    gncEntryCommitEdit (pdata->entry);

    auto* gdata = static_cast<gxpf_data*> (global_data);
    gdata->cb (tag, gdata->parsedata, pdata->entry);

    g_free (pdata);
    return TRUE;
}

static void
sax_entry_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
                gpointer*, const gchar*)
{
    auto* pdata = static_cast<entry_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gncEntryDestroy (pdata->entry);
    g_free (pdata);
}

static sixtp*
entry_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_entry_start,
        SIXTP_END_HANDLER_ID, sax_entry_end,
        SIXTP_FAIL_HANDLER_ID, sax_entry_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        entry_guid_string, restore_char_generator (sax_entry_guid_end),
        entry_description_string, restore_char_generator (sax_entry_description_end),
        entry_action_string, restore_char_generator (sax_entry_action_end),
        entry_notes_string, restore_char_generator (sax_entry_notes_end),
        entry_qty_string, restore_char_generator (sax_entry_qty_end),

        /* cust invoice */
        entry_invacct_string, restore_char_generator (sax_entry_invacct_end),
        entry_iprice_string, restore_char_generator (sax_entry_iprice_end),
        entry_idiscount_string, restore_char_generator (sax_entry_idiscount_end),
        entry_idisctype_string, restore_char_generator (sax_entry_idisctype_end),
        entry_idischow_string, restore_char_generator (sax_entry_idischow_end),
        entry_itaxable_string, restore_char_generator (sax_entry_itaxable_end),
        entry_itaxincluded_string, restore_char_generator (sax_entry_itaxincluded_end),
        entry_itaxtable_string, restore_char_generator (sax_entry_itaxtable_end),

        /* vendor bills */
        entry_billacct_string, restore_char_generator (sax_entry_billacct_end),
        entry_bprice_string, restore_char_generator (sax_entry_bprice_end),
        entry_btaxable_string, restore_char_generator (sax_entry_btaxable_end),
        entry_btaxincluded_string, restore_char_generator (sax_entry_btaxincluded_end),
        entry_btaxtable_string, restore_char_generator (sax_entry_btaxtable_end),
        entry_billable_string, restore_char_generator (sax_entry_billable_end),

        /* employee stuff */
        entry_billpayment_string, restore_char_generator (sax_entry_billpayment_end),

        /* other stuff */
        entry_order_string, restore_char_generator (sax_entry_order_end),
        entry_invoice_string, restore_char_generator (sax_entry_invoice_end),
        entry_bill_string, restore_char_generator (sax_entry_bill_end),
        entry_slots_string, sixtp_dom_parser_new_rooted (sax_entry_slots_dom_end, NULL, NULL),

        /* old XML support */
        "entry:acct", restore_char_generator (sax_entry_acct_legacy_end),
        "entry:price", restore_char_generator (sax_entry_price_legacy_end),
        "entry:discount", restore_char_generator (sax_entry_idiscount_end),
        "entry:disc-type", restore_char_generator (sax_entry_idisctype_end),
        "entry:disc-how", restore_char_generator (sax_entry_idischow_end),
        "entry:taxable", restore_char_generator (sax_entry_itaxable_end),
        "entry:taxincluded", restore_char_generator (sax_entry_itaxincluded_end),
        "entry:taxtable", restore_char_generator (sax_entry_itaxtable_end),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    sixtp_add_sub_parser (p, entry_billto_string, sax_owner_parser_new (sax_entry_billto_start));
    sixtp_add_sub_parser (p, entry_date_string, sax_time64_parser_new (sax_entry_date_ts_end));
    sixtp_add_sub_parser (p, entry_dateentered_string, sax_time64_parser_new (sax_entry_dateentered_ts_end));

    return p;
}

static void
do_count (QofInstance* entry_p, gpointer count_p)
{
    int* count = static_cast<decltype (count)> (count_p);
    (*count)++;
}

static int
entry_get_count (QofBook* book)
{
    int count = 0;
    qof_object_foreach (_GNC_MOD_NAME, book, do_count, (gpointer) &count);
    return count;
}

static void
xml_add_entry (QofInstance* entry_p, gpointer out_p)
{
    xmlNodePtr node;
    GncEntry* entry = (GncEntry*) entry_p;
    FILE* out = static_cast<decltype (out)> (out_p);

    if (ferror (out))
        return;

    /* Don't save non-attached entries! */
    if (! (gncEntryGetOrder (entry) || gncEntryGetInvoice (entry) ||
           gncEntryGetBill (entry)))
        return;

    node = entry_dom_tree_create (entry);
    xmlElemDump (out, NULL, node);
    xmlFreeNode (node);
    if (ferror (out) || fprintf (out, "\n") < 0)
        return;
}

static gboolean
entry_write (FILE* out, QofBook* book)
{
    qof_object_foreach_sorted (_GNC_MOD_NAME, book, xml_add_entry, (gpointer) out);
    return ferror (out) == 0;
}

static gboolean
entry_ns (FILE* out)
{
    g_return_val_if_fail (out, FALSE);
    return gnc_xml2_write_namespace_decl (out, "entry");
}

void
gnc_entry_xml_initialize (void)
{
    static GncXmlDataType_t be_data =
    {
        GNC_FILE_BACKEND_VERS,
        gnc_entry_string,
        entry_sixtp_parser_create,
        NULL,           /* add_item */
        entry_get_count,
        entry_write,
        NULL,           /* scrub */
        entry_ns,
    };

    gnc_xml_register_backend (be_data);
}
