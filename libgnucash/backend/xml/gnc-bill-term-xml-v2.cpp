/********************************************************************\
 * gnc-bill-term-xml-v2.c -- billing term xml i/o implementation    *
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
#include <config.h>
#include <stdlib.h>
#include <string.h>

#include "gncBillTermP.h"
#include "gncInvoice.h"
#include "qof.h"
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
#include "gnc-bill-term-xml-v2.h"

#include "xml-helpers.h"

#define _GNC_MOD_NAME   GNC_ID_BILLTERM

static QofLogModule log_module = GNC_MOD_IO;

const gchar* billterm_version_string = "2.0.0";

/* ids */
#define gnc_billterm_string "gnc:GncBillTerm"
#define billterm_guid_string "billterm:guid"
#define billterm_name_string "billterm:name"
#define billterm_desc_string "billterm:desc"
#define billterm_refcount_string "billterm:refcount"
#define billterm_invisible_string "billterm:invisible"
#define billterm_parent_string "billterm:parent"
#define billterm_child_string "billterm:child"
#define billterm_slots_string "billterm:slots"

#define gnc_daystype_string "billterm:days"
#define days_duedays_string "bt-days:due-days"
#define days_discdays_string "bt-days:disc-days"
#define days_discount_string "bt-days:discount"

#define gnc_proximotype_string "billterm:proximo"
#define prox_dueday_string "bt-prox:due-day"
#define prox_discday_string "bt-prox:disc-day"
#define prox_discount_string "bt-prox:discount"
#define prox_cutoff_string "bt-prox:cutoff-day"

static xmlNodePtr
billterm_dom_tree_create (GncBillTerm* term)
{
    xmlNodePtr ret, data;

    ret = xmlNewNode (NULL, BAD_CAST gnc_billterm_string);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST billterm_version_string);

    maybe_add_guid (ret, billterm_guid_string, QOF_INSTANCE (term));
    xmlAddChild (ret, text_to_dom_tree (billterm_name_string,
                                        gncBillTermGetName (term)));
    xmlAddChild (ret, text_to_dom_tree (billterm_desc_string,
                                        gncBillTermGetDescription (term)));

    xmlAddChild (ret, int_to_dom_tree (billterm_refcount_string,
                                       gncBillTermGetRefcount (term)));
    xmlAddChild (ret, int_to_dom_tree (billterm_invisible_string,
                                       gncBillTermGetInvisible (term)));

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (billterm_slots_string,
                                                      QOF_INSTANCE (term)));

    /* We should not be our own child */
    if (gncBillTermGetChild (term) != term)
        maybe_add_guid (ret, billterm_child_string,
                        QOF_INSTANCE (gncBillTermGetChild (term)));

    maybe_add_guid (ret, billterm_parent_string,
                    QOF_INSTANCE (gncBillTermGetParent (term)));

    switch (gncBillTermGetType (term))
    {
    case GNC_TERM_TYPE_DAYS:
        data = xmlNewChild (ret, NULL, BAD_CAST gnc_daystype_string, NULL);
        maybe_add_int (data, days_duedays_string, gncBillTermGetDueDays (term));
        maybe_add_int (data, days_discdays_string,
                       gncBillTermGetDiscountDays (term));
        maybe_add_numeric (data, days_discount_string,
                           gncBillTermGetDiscount (term));
        break;

    case GNC_TERM_TYPE_PROXIMO:
        data = xmlNewChild (ret, NULL, BAD_CAST gnc_proximotype_string, NULL);
        maybe_add_int (data, prox_dueday_string, gncBillTermGetDueDays (term));
        maybe_add_int (data, prox_discday_string,
                       gncBillTermGetDiscountDays (term));
        maybe_add_numeric (data, prox_discount_string,
                           gncBillTermGetDiscount (term));
        maybe_add_int (data, prox_cutoff_string, gncBillTermGetCutoff (term));
        break;
    }

    return ret;
}

/***********************************************************************/
/* SAX-direct (streaming) bill-term parser: reads a gnc:GncBillTerm
 * straight off the SAX character stream, with no intermediate
 * xmlNodePtr built for any of its fields. Nothing else in the codebase
 * uses the old DOM-based parser this replaces, so it's gone entirely
 * rather than kept around for reuse.
 */

struct billterm_sax_pdata
{
    GncBillTerm* term;
    QofBook* book;
};

static gboolean
sax_billterm_guid_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;

        /* If a term with this guid already exists -- e.g. created as a
           placeholder by an earlier billterm:parent/billterm:child ref
           elsewhere in the file -- adopt it instead of the fresh one
           sax_billterm_start() allocated, and keep filling in fields on
           that one. */
        GncBillTerm* term = gncBillTermLookup (pdata->book, &guid);
        if (term)
        {
            gncBillTermDestroy (pdata->term);
            pdata->term = term;
            gncBillTermBeginEdit (term);
        }
        else
            gncBillTermSetGUID (pdata->term, &guid);
        return TRUE;
    });
}

static gboolean
sax_billterm_name_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncBillTermSetName (pdata->term, txt); return TRUE; });
}

static gboolean
sax_billterm_desc_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncBillTermSetDescription (pdata->term, txt); return TRUE; });
}

static gboolean
sax_billterm_refcount_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncBillTermSetRefcount (pdata->term, val);
        return TRUE;
    });
}

static gboolean
sax_billterm_invisible_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                            gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        if (val)
            gncBillTermMakeInvisible (pdata->term);
        return TRUE;
    });
}

static gboolean
sax_billterm_parent_child_end (GncBillTerm* term, QofBook* book, const char* txt,
                               void (*func) (GncBillTerm*, GncBillTerm*))
{
    GncGUID guid;
    if (!string_to_guid (txt, &guid)) return FALSE;

    GncBillTerm* other = gncBillTermLookup (book, &guid);
    if (!other)
    {
        other = gncBillTermCreate (book);
        gncBillTermBeginEdit (other);
        gncBillTermSetGUID (other, &guid);
        gncBillTermCommitEdit (other);
    }
    func (term, other);
    return TRUE;
}

static gboolean
sax_billterm_parent_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { return sax_billterm_parent_child_end (pdata->term, pdata->book, txt, gncBillTermSetParent); });
}

static gboolean
sax_billterm_child_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { return sax_billterm_parent_child_end (pdata->term, pdata->book, txt, gncBillTermSetChild); });
}

static gboolean
sax_billterm_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                            gpointer parent_data, gpointer, gpointer* result,
                            const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ok = TRUE;
    if (tree)
    {
        ok = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->term));
        xmlFreeNode (tree);
    }
    *result = nullptr;
    return ok;
}

/* billterm:days and billterm:proximo are mutually exclusive (a term is
   either type); their own start handler enforces that the same way the
   original's g_return_val_if_fail(gncBillTermGetType(term) == 0, ...)
   did, then sets the type immediately so it's in place before any of
   the wrapper's own children run. */

static gboolean
sax_billterm_days_start (GSList*, gpointer parent_data, gpointer,
                         gpointer* data_for_children, gpointer*,
                         const gchar*, gchar**)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    if (gncBillTermGetType (pdata->term) != 0) return FALSE;
    gncBillTermSetType (pdata->term, GNC_TERM_TYPE_DAYS);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_billterm_prox_start (GSList*, gpointer parent_data, gpointer,
                         gpointer* data_for_children, gpointer*,
                         const gchar*, gchar**)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    if (gncBillTermGetType (pdata->term) != 0) return FALSE;
    gncBillTermSetType (pdata->term, GNC_TERM_TYPE_PROXIMO);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_billterm_duedays_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                          gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncBillTermSetDueDays (pdata->term, val);
        return TRUE;
    });
}

static gboolean
sax_billterm_discdays_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncBillTermSetDiscountDays (pdata->term, val);
        return TRUE;
    });
}

static gboolean
sax_billterm_discount_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gnc_numeric num = gnc_numeric_from_string (txt);
        gncBillTermSetDiscount (pdata->term, gnc_numeric_check (num) ? gnc_numeric_zero () : num);
        return TRUE;
    });
}

static gboolean
sax_billterm_cutoff_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncBillTermSetCutoff (pdata->term, val);
        return TRUE;
    });
}

static gboolean
sax_billterm_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
                    gpointer*, const gchar* tag, gchar**)
{
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }
    auto* gdata = static_cast<gxpf_data*> (global_data);
    auto* pdata = g_new (billterm_sax_pdata, 1);
    pdata->book = static_cast<QofBook*> (gdata->bookdata);
    pdata->term = gncBillTermCreate (pdata->book);
    gncBillTermBeginEdit (pdata->term);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_billterm_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer global_data,
                  gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (data_for_children);
    auto* gdata = static_cast<gxpf_data*> (global_data);

    if (!tag)
        return TRUE;

    GncBillTerm* term = pdata->term;
    g_free (pdata);

    gncBillTermCommitEdit (term);
    gdata->cb (tag, gdata->parsedata, term);
    return TRUE;
}

static void
sax_billterm_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
                   gpointer*, const gchar*)
{
    auto* pdata = static_cast<billterm_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gncBillTermDestroy (pdata->term);
    g_free (pdata);
}

static sixtp*
billterm_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_billterm_start,
        SIXTP_END_HANDLER_ID, sax_billterm_end,
        SIXTP_FAIL_HANDLER_ID, sax_billterm_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        billterm_guid_string, restore_char_generator (sax_billterm_guid_end),
        billterm_name_string, restore_char_generator (sax_billterm_name_end),
        billterm_desc_string, restore_char_generator (sax_billterm_desc_end),
        billterm_refcount_string, restore_char_generator (sax_billterm_refcount_end),
        billterm_invisible_string, restore_char_generator (sax_billterm_invisible_end),
        billterm_parent_string, restore_char_generator (sax_billterm_parent_end),
        billterm_child_string, restore_char_generator (sax_billterm_child_end),
        billterm_slots_string, sixtp_dom_parser_new_rooted (sax_billterm_slots_dom_end, NULL, NULL),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    {
        sixtp* days = sixtp_set_any (
            sixtp_new (), FALSE,
            SIXTP_START_HANDLER_ID, sax_billterm_days_start,
            SIXTP_NO_MORE_HANDLERS);
        sixtp_add_some_sub_parsers (
            days, TRUE,
            days_duedays_string, restore_char_generator (sax_billterm_duedays_end),
            days_discdays_string, restore_char_generator (sax_billterm_discdays_end),
            days_discount_string, restore_char_generator (sax_billterm_discount_end),
            NULL, NULL);
        sixtp_add_sub_parser (p, gnc_daystype_string, days);
    }
    {
        sixtp* prox = sixtp_set_any (
            sixtp_new (), FALSE,
            SIXTP_START_HANDLER_ID, sax_billterm_prox_start,
            SIXTP_NO_MORE_HANDLERS);
        sixtp_add_some_sub_parsers (
            prox, TRUE,
            prox_dueday_string, restore_char_generator (sax_billterm_duedays_end),
            prox_discday_string, restore_char_generator (sax_billterm_discdays_end),
            prox_discount_string, restore_char_generator (sax_billterm_discount_end),
            prox_cutoff_string, restore_char_generator (sax_billterm_cutoff_end),
            NULL, NULL);
        sixtp_add_sub_parser (p, gnc_proximotype_string, prox);
    }

    return p;
}

static void
do_count (QofInstance* term_p, gpointer count_p)
{
    int* count = static_cast<decltype (count)> (count_p);
    (*count)++;
}

static int
billterm_get_count (QofBook* book)
{
    int count = 0;
    qof_object_foreach (_GNC_MOD_NAME, book, do_count, (gpointer) &count);
    return count;
}

static void
xml_add_billterm (QofInstance* term_p, gpointer out_p)
{
    xmlNodePtr node;
    GncBillTerm* term = (GncBillTerm*) term_p;
    FILE* out = static_cast<decltype (out)> (out_p);

    if (ferror (out))
        return;

    node = billterm_dom_tree_create (term);
    xmlElemDump (out, NULL, node);
    xmlFreeNode (node);
    if (ferror (out) || fprintf (out, "\n") < 0)
        return;
}

static gboolean
billterm_write (FILE* out, QofBook* book)
{
    qof_object_foreach_sorted (_GNC_MOD_NAME, book, xml_add_billterm,
                               (gpointer) out);
    return ferror (out) == 0;
}

static gboolean
billterm_is_grandchild (GncBillTerm* term)
{
    return (gncBillTermGetParent (gncBillTermGetParent (term)) != NULL);
}

static GncBillTerm*
billterm_find_senior (GncBillTerm* term)
{
    GncBillTerm* temp, *parent, *gp = NULL;

    temp = term;
    do
    {
        /* See if "temp" is a grandchild */
        parent = gncBillTermGetParent (temp);
        if (!parent)
            break;
        gp = gncBillTermGetParent (parent);
        if (!gp)
            break;

        /* Yep, this is a grandchild.  Move up one generation and try again */
        temp = parent;
    }
    while (TRUE);

    /* Ok, at this point temp points to the most senior child and parent
     * should point to the top billterm (and gp should be NULL).  If
     * parent is NULL then we are the most senior child (and have no
     * children), so do nothing.  If temp == term then there is no
     * grandparent, so do nothing.
     *
     * Do something if parent != NULL && temp != term
     */
    g_assert (gp == NULL);

    /* return the most senior term */
    return temp;
}

/* build a list of bill terms that are grandchildren or bogus (empty entry list). */
static void
billterm_scrub_cb (QofInstance* term_p, gpointer list_p)
{
    GncBillTerm* term = GNC_BILLTERM (term_p);
    GList** list = static_cast<decltype (list)> (list_p);

    if (billterm_is_grandchild (term))
    {
        *list = g_list_prepend (*list, term);

    }
    else if (!gncBillTermGetType (term))
    {
        GncBillTerm* t = gncBillTermGetParent (term);
        if (t)
        {
            /* Fix up the broken "copy" function */
            gchar guidstr[GUID_ENCODING_LENGTH + 1];
            guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (term)), guidstr);
            PWARN ("Fixing broken child billterm: %s", guidstr);

            gncBillTermBeginEdit (term);
            gncBillTermSetType (term, gncBillTermGetType (t));
            gncBillTermSetDueDays (term, gncBillTermGetDueDays (t));
            gncBillTermSetDiscountDays (term, gncBillTermGetDiscountDays (t));
            gncBillTermSetDiscount (term, gncBillTermGetDiscount (t));
            gncBillTermSetCutoff (term, gncBillTermGetCutoff (t));
            gncBillTermCommitEdit (term);

        }
        else
        {
            /* No parent?  Must be a standalone */
            *list = g_list_prepend (*list, term);
        }
    }
}

/* for each invoice, check the bill terms.  If the bill terms are
 * grandchildren, then fix them to point to the most senior child
 */
static void
billterm_scrub_invoices (QofInstance* invoice_p, gpointer ht_p)
{
    GHashTable* ht = static_cast<decltype (ht)> (ht_p);
    GncInvoice* invoice = GNC_INVOICE (invoice_p);
    GncBillTerm* term, *new_bt;
    gint32 count;

    term = gncInvoiceGetTerms (invoice);
    if (term)
    {
        if (billterm_is_grandchild (term))
        {
            gchar guidstr[GUID_ENCODING_LENGTH + 1];
            guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (invoice)), guidstr);
            PWARN ("Fixing i-billterm on invoice %s\n", guidstr);
            new_bt = billterm_find_senior (term);
            gncInvoiceBeginEdit (invoice);
            gncInvoiceSetTerms (invoice, new_bt);
            gncInvoiceCommitEdit (invoice);
            term = new_bt;
        }
        if (term)
        {
            count = GPOINTER_TO_INT (g_hash_table_lookup (ht, term));
            count++;
            g_hash_table_insert (ht, term, GINT_TO_POINTER (count));
        }
    }
}

static void
billterm_scrub_cust (QofInstance* cust_p, gpointer ht_p)
{
    GHashTable* ht = static_cast<decltype (ht)> (ht_p);
    GncCustomer* cust = GNC_CUSTOMER (cust_p);
    GncBillTerm* term;
    gint32 count;

    term = gncCustomerGetTerms (cust);
    if (term)
    {
        count = GPOINTER_TO_INT (g_hash_table_lookup (ht, term));
        count++;
        g_hash_table_insert (ht, term, GINT_TO_POINTER (count));
        if (billterm_is_grandchild (term))
        {
            gchar custstr[GUID_ENCODING_LENGTH + 1];
            gchar termstr[GUID_ENCODING_LENGTH + 1];
            guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (cust)), custstr);
            guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (term)), termstr);
            PWARN ("customer %s has grandchild billterm %s\n", custstr, termstr);
        }
    }
}

static void
billterm_scrub_vendor (QofInstance* vendor_p, gpointer ht_p)
{
    GHashTable* ht = static_cast<decltype (ht)> (ht_p);
    GncVendor* vendor = GNC_VENDOR (vendor_p);
    GncBillTerm* term;
    gint32 count;

    term = gncVendorGetTerms (vendor);
    if (term)
    {
        count = GPOINTER_TO_INT (g_hash_table_lookup (ht, term));
        count++;
        g_hash_table_insert (ht, term, GINT_TO_POINTER (count));
        if (billterm_is_grandchild (term))
        {
            gchar vendstr[GUID_ENCODING_LENGTH + 1];
            gchar termstr[GUID_ENCODING_LENGTH + 1];
            guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (vendor)), vendstr);
            guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (term)), termstr);
            PWARN ("vendor %s has grandchild billterm %s\n", vendstr, termstr);
        }
    }
}

static void
billterm_reset_refcount (gpointer key, gpointer value, gpointer notused)
{
    GncBillTerm* term = static_cast<decltype (term)> (key);
    gint32 count = GPOINTER_TO_INT (value);

    if (count != gncBillTermGetRefcount (term) && !gncBillTermGetInvisible (term))
    {
        gchar termstr[GUID_ENCODING_LENGTH + 1];
        guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (term)), termstr);
        PWARN ("Fixing refcount on billterm %s (%" G_GINT64_FORMAT " -> %d)\n",
               termstr, gncBillTermGetRefcount (term), count);
        gncBillTermSetRefcount (term, count);
    }
}

static void
billterm_scrub (QofBook* book)
{
    GList* list = NULL;
    GList* node;
    GncBillTerm* parent, *term;
    GHashTable* ht = g_hash_table_new (g_direct_hash, g_direct_equal);

    DEBUG ("scrubbing billterms...");
    qof_object_foreach (GNC_ID_INVOICE,  book, billterm_scrub_invoices, ht);
    qof_object_foreach (GNC_ID_CUSTOMER, book, billterm_scrub_cust, ht);
    qof_object_foreach (GNC_ID_VENDOR,   book, billterm_scrub_vendor, ht);
    qof_object_foreach (GNC_ID_BILLTERM, book, billterm_scrub_cb, &list);

    /* destroy the list of "grandchildren" bill terms */
    for (node = list; node; node = node->next)
    {
        gchar termstr[GUID_ENCODING_LENGTH + 1];
        term = static_cast<decltype (term)> (node->data);

        guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (term)), termstr);
        PWARN ("deleting grandchild billterm: %s\n", termstr);

        /* Make sure the parent has no children */
        parent = gncBillTermGetParent (term);
        gncBillTermSetChild (parent, NULL);

        /* Destroy this bill term */
        gncBillTermBeginEdit (term);
        gncBillTermDestroy (term);
    }

    /* reset the refcounts as necessary */
    g_hash_table_foreach (ht, billterm_reset_refcount, NULL);

    g_list_free (list);
    g_hash_table_destroy (ht);
}

static gboolean
billterm_ns (FILE* out)
{
    g_return_val_if_fail (out, FALSE);
    return
        gnc_xml2_write_namespace_decl (out, "billterm")
        && gnc_xml2_write_namespace_decl (out, "bt-days")
        && gnc_xml2_write_namespace_decl (out, "bt-prox");
}

void
gnc_billterm_xml_initialize (void)
{
    static GncXmlDataType_t be_data =
    {
        GNC_FILE_BACKEND_VERS,
        gnc_billterm_string,
        billterm_sixtp_parser_create,
        NULL,           /* add_item */
        billterm_get_count,
        billterm_write,
        billterm_scrub,
        billterm_ns,
    };

    gnc_xml_register_backend(be_data);
}

GncBillTerm*
gnc_billterm_xml_find_or_create (QofBook* book, GncGUID* guid)
{
    GncBillTerm* term;
    gchar guidstr[GUID_ENCODING_LENGTH + 1];

    guid_to_string_buff (guid, guidstr);
    g_return_val_if_fail (book, NULL);
    g_return_val_if_fail (guid, NULL);
    term = gncBillTermLookup (book, guid);
    DEBUG ("looking for billterm %s, found %p", guidstr, term);
    if (!term)
    {
        term = gncBillTermCreate (book);
        gncBillTermBeginEdit (term);
        gncBillTermSetGUID (term, guid);
        gncBillTermCommitEdit (term);
        DEBUG ("Created term: %p", term);
    }
    else
        gncBillTermDecRef (term);

    return term;
}
