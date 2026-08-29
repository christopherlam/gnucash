/********************************************************************\
 * gnc-tax-table-xml-v2.c -- tax table xml i/o implementation       *
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
#include "gncEntry.h"
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

#include "gnc-tax-table-xml-v2.h"

#define _GNC_MOD_NAME   GNC_ID_TAXTABLE

static QofLogModule log_module = GNC_MOD_IO;

const gchar* taxtable_version_string = "2.0.0";

/* ids */
#define gnc_taxtable_string "gnc:GncTaxTable"
#define taxtable_guid_string "taxtable:guid"
#define taxtable_name_string "taxtable:name"
#define taxtable_refcount_string "taxtable:refcount"
#define taxtable_invisible_string "taxtable:invisible"
#define taxtable_parent_string "taxtable:parent"
#define taxtable_child_string "taxtable:child"
#define taxtable_entries_string "taxtable:entries"
#define taxtable_slots_string "taxtable:slots"

#define gnc_taxtableentry_string "gnc:GncTaxTableEntry"
#define ttentry_account_string "tte:acct"
#define ttentry_type_string "tte:type"
#define ttentry_amount_string "tte:amount"

static void
maybe_add_guid (xmlNodePtr ptr, const char* tag, GncTaxTable* table)
{
    if (table)
        xmlAddChild (ptr, guid_to_dom_tree (tag,
                                            qof_instance_get_guid (QOF_INSTANCE (table))));
}

static xmlNodePtr
ttentry_dom_tree_create (GncTaxTableEntry* entry)
{
    xmlNodePtr ret;
    Account* account;
    gnc_numeric amount;

    ret = xmlNewNode (NULL, BAD_CAST gnc_taxtableentry_string);

    account = gncTaxTableEntryGetAccount (entry);
    if (account)
        xmlAddChild (ret, guid_to_dom_tree (ttentry_account_string,
                                            qof_instance_get_guid (QOF_INSTANCE (account))));

    amount = gncTaxTableEntryGetAmount (entry);
    xmlAddChild (ret, gnc_numeric_to_dom_tree (ttentry_amount_string, &amount));

    xmlAddChild (ret, text_to_dom_tree (ttentry_type_string,
                                        gncAmountTypeToString (
                                            gncTaxTableEntryGetType (entry))));

    return ret;
}

static xmlNodePtr
taxtable_dom_tree_create (GncTaxTable* table)
{
    xmlNodePtr ret, entries;
    GList* list;

    ret = xmlNewNode (NULL, BAD_CAST gnc_taxtable_string);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST taxtable_version_string);

    maybe_add_guid (ret, taxtable_guid_string, table);
    xmlAddChild (ret, text_to_dom_tree (taxtable_name_string,
                                        gncTaxTableGetName (table)));

    xmlAddChild (ret, int_to_dom_tree (taxtable_refcount_string,
                                       gncTaxTableGetRefcount (table)));
    xmlAddChild (ret, int_to_dom_tree (taxtable_invisible_string,
                                       gncTaxTableGetInvisible (table)));

    /* We should not be our own child */
    if (gncTaxTableGetChild (table) != table)
        maybe_add_guid (ret, taxtable_child_string, gncTaxTableGetChild (table));

    maybe_add_guid (ret, taxtable_parent_string, gncTaxTableGetParent (table));

    entries = xmlNewChild (ret, NULL, BAD_CAST taxtable_entries_string, NULL);
    for (list = gncTaxTableGetEntries (table); list; list = list->next)
    {
        GncTaxTableEntry* entry = static_cast<decltype (entry)> (list->data);
        xmlAddChild (entries, ttentry_dom_tree_create (entry));
    }

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (taxtable_slots_string,
                                                      QOF_INSTANCE (table)));
    return ret;
}

/***********************************************************************/
/* SAX-direct (streaming) tax-table parser: reads a gnc:GncTaxTable and
 * its gnc:GncTaxTableEntry children straight off the SAX character
 * stream, with no intermediate xmlNodePtr built for any of their
 * fields. Nothing else in the codebase uses the old DOM-based parser
 * this replaces, so it's gone entirely rather than kept for reuse.
 */

struct taxtable_sax_pdata
{
    GncTaxTable* table;
    QofBook* book;
};

struct ttentry_sax_pdata
{
    GncTaxTableEntry* ttentry;
    QofBook* book;
};

static gboolean
sax_ttentry_acct_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                      gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<ttentry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        Account* acc = xaccAccountLookup (&guid, pdata->book);
        if (!acc) return FALSE;
        gncTaxTableEntrySetAccount (pdata->ttentry, acc);
        return TRUE;
    });
}

static gboolean
sax_ttentry_type_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                      gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<ttentry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncAmountType type;
        if (gncAmountStringToType (txt, &type))
            gncTaxTableEntrySetType (pdata->ttentry, type);
        return TRUE;
    });
}

static gboolean
sax_ttentry_amount_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<ttentry_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gnc_numeric num = gnc_numeric_from_string (txt);
        gncTaxTableEntrySetAmount (pdata->ttentry, gnc_numeric_check (num) ? gnc_numeric_zero () : num);
        return TRUE;
    });
}

static gboolean
sax_ttentry_start (GSList*, gpointer parent_data, gpointer, gpointer* data_for_children,
                   gpointer*, const gchar*, gchar**)
{
    auto* taxtable_pdata = static_cast<struct taxtable_sax_pdata*> (parent_data);
    auto* pdata = g_new (ttentry_sax_pdata, 1);
    pdata->book = taxtable_pdata->book;
    pdata->ttentry = gncTaxTableEntryCreate ();
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_ttentry_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
                 gpointer* result, const gchar*)
{
    auto* pdata = static_cast<ttentry_sax_pdata*> (data_for_children);
    *result = pdata->ttentry;
    g_free (pdata);
    return TRUE;
}

static void
sax_ttentry_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
                  gpointer*, const gchar*)
{
    auto* pdata = static_cast<ttentry_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gncTaxTableEntryDestroy (pdata->ttentry);
    g_free (pdata);
}

static sixtp*
sax_ttentry_parser_new (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_ttentry_start,
        SIXTP_END_HANDLER_ID, sax_ttentry_end,
        SIXTP_FAIL_HANDLER_ID, sax_ttentry_fail,
        SIXTP_NO_MORE_HANDLERS);

    return sixtp_add_some_sub_parsers (
        p, TRUE,
        ttentry_account_string, restore_char_generator (sax_ttentry_acct_end),
        ttentry_type_string, restore_char_generator (sax_ttentry_type_end),
        ttentry_amount_string, restore_char_generator (sax_ttentry_amount_end),
        NULL, NULL);
}

/***********************************************************************/

static gboolean
sax_taxtable_parent_child (GncTaxTable* table, QofBook* book, const char* txt,
                           void (*func) (GncTaxTable*, GncTaxTable*))
{
    GncGUID guid;
    if (!string_to_guid (txt, &guid)) return FALSE;

    GncTaxTable* other = gncTaxTableLookup (book, &guid);

    /* Ignore pointers to self. */
    if (other == table)
        return TRUE;

    if (!other)
    {
        other = gncTaxTableCreate (book);
        gncTaxTableBeginEdit (other);
        gncTaxTableSetGUID (other, &guid);
        gncTaxTableCommitEdit (other);
    }
    func (table, other);
    return TRUE;
}

static gboolean
sax_taxtable_guid_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<taxtable_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;

        /* Adopt a table that already exists by this guid -- e.g. a
           placeholder created earlier by another table's parent/child
           ref -- instead of the fresh one sax_taxtable_start() made. */
        GncTaxTable* table = gncTaxTableLookup (pdata->book, &guid);
        if (table)
        {
            gncTaxTableDestroy (pdata->table);
            pdata->table = table;
            gncTaxTableBeginEdit (table);
        }
        else
            gncTaxTableSetGUID (pdata->table, &guid);
        return TRUE;
    });
}

static gboolean
sax_taxtable_name_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<taxtable_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncTaxTableSetName (pdata->table, txt); return TRUE; });
}

static gboolean
sax_taxtable_refcount_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<taxtable_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncTaxTableSetRefcount (pdata->table, val);
        return TRUE;
    });
}

static gboolean
sax_taxtable_invisible_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                            gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<taxtable_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        if (val)
            gncTaxTableMakeInvisible (pdata->table);
        return TRUE;
    });
}

static gboolean
sax_taxtable_parent_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<taxtable_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { return sax_taxtable_parent_child (pdata->table, pdata->book, txt, gncTaxTableSetParent); });
}

static gboolean
sax_taxtable_child_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<taxtable_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { return sax_taxtable_parent_child (pdata->table, pdata->book, txt, gncTaxTableSetChild); });
}

static gboolean
sax_taxtable_entries_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                          gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<taxtable_sax_pdata*> (parent_data);
    GSList* ordered = g_slist_reverse (g_slist_copy (dfc));
    for (GSList* lp = ordered; lp; lp = lp->next)
    {
        auto* cr = static_cast<sixtp_child_result*> (lp->data);
        gncTaxTableAddEntry (pdata->table, static_cast<GncTaxTableEntry*> (cr->data));
    }
    g_slist_free (ordered);
    return TRUE;
}

static gboolean
sax_taxtable_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                            gpointer parent_data, gpointer, gpointer* result,
                            const gchar*)
{
    auto* pdata = static_cast<taxtable_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ok = TRUE;
    if (tree)
    {
        ok = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->table));
        xmlFreeNode (tree);
    }
    *result = nullptr;
    return ok;
}

static gboolean
sax_taxtable_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
                    gpointer*, const gchar* tag, gchar**)
{
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }
    auto* gdata = static_cast<gxpf_data*> (global_data);
    auto* pdata = g_new (taxtable_sax_pdata, 1);
    pdata->book = static_cast<QofBook*> (gdata->bookdata);
    pdata->table = gncTaxTableCreate (pdata->book);
    gncTaxTableBeginEdit (pdata->table);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_taxtable_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer global_data,
                  gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<taxtable_sax_pdata*> (data_for_children);
    auto* gdata = static_cast<gxpf_data*> (global_data);

    if (!tag)
        return TRUE;

    GncTaxTable* table = pdata->table;
    g_free (pdata);

    gncTaxTableCommitEdit (table);
    gdata->cb (tag, gdata->parsedata, table);
    return TRUE;
}

static void
sax_taxtable_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
                   gpointer*, const gchar*)
{
    auto* pdata = static_cast<taxtable_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gncTaxTableDestroy (pdata->table);
    g_free (pdata);
}

static sixtp*
taxtable_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_taxtable_start,
        SIXTP_END_HANDLER_ID, sax_taxtable_end,
        SIXTP_FAIL_HANDLER_ID, sax_taxtable_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        taxtable_guid_string, restore_char_generator (sax_taxtable_guid_end),
        taxtable_name_string, restore_char_generator (sax_taxtable_name_end),
        taxtable_refcount_string, restore_char_generator (sax_taxtable_refcount_end),
        taxtable_invisible_string, restore_char_generator (sax_taxtable_invisible_end),
        taxtable_parent_string, restore_char_generator (sax_taxtable_parent_end),
        taxtable_child_string, restore_char_generator (sax_taxtable_child_end),
        taxtable_slots_string, sixtp_dom_parser_new_rooted (sax_taxtable_slots_dom_end, NULL, NULL),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    {
        sixtp* entries = sixtp_set_any (
            sixtp_new (), FALSE,
            SIXTP_START_HANDLER_ID, sax_passthrough_start,
            SIXTP_END_HANDLER_ID, sax_taxtable_entries_end,
            SIXTP_NO_MORE_HANDLERS);
        sixtp_add_sub_parser (entries, gnc_taxtableentry_string, sax_ttentry_parser_new ());
        sixtp_add_sub_parser (p, taxtable_entries_string, entries);
    }

    return p;
}

static void
do_count (QofInstance* table_p, gpointer count_p)
{
    int* count = static_cast<decltype (count)> (count_p);
    (*count)++;
}

static int
taxtable_get_count (QofBook* book)
{
    int count = 0;
    qof_object_foreach (_GNC_MOD_NAME, book, do_count, (gpointer) &count);
    return count;
}

static void
xml_add_taxtable (QofInstance* table_p, gpointer out_p)
{
    xmlNodePtr node;
    GncTaxTable* table = (GncTaxTable*) table_p;
    FILE* out = static_cast<decltype (out)> (out_p);

    if (ferror (out))
        return;

    node = taxtable_dom_tree_create (table);
    xmlElemDump (out, NULL, node);
    xmlFreeNode (node);
    if (ferror (out) || fprintf (out, "\n") < 0)
        return;
}

static gboolean
taxtable_write (FILE* out, QofBook* book)
{
    qof_object_foreach_sorted (_GNC_MOD_NAME, book, xml_add_taxtable,
                               (gpointer) out);
    return ferror (out) == 0;
}


static gboolean
taxtable_is_grandchild (GncTaxTable* table)
{
    return (gncTaxTableGetParent (gncTaxTableGetParent (table)) != NULL);
}

static GncTaxTable*
taxtable_find_senior (GncTaxTable* table)
{
    GncTaxTable* temp, *parent, *gp = NULL;

    temp = table;
    do
    {
        /* See if "temp" is a grandchild */
        parent = gncTaxTableGetParent (temp);
        if (!parent)
            break;
        gp = gncTaxTableGetParent (parent);
        if (!gp)
            break;

        /* Yep, this is a grandchild.  Move up one generation and try again */
        temp = parent;
    }
    while (TRUE);

    /* Ok, at this point temp points to the most senior child and parent
     * should point to the top taxtable (and gp should be NULL).  If
     * parent is NULL then we are the most senior child (and have no
     * children), so do nothing.  If temp == table then there is no
     * grandparent, so do nothing.
     *
     * Do something if parent != NULL && temp != table
     */
    g_assert (gp == NULL);

    /* return the most senior table */
    return temp;
}

/* build a list of tax tables that are grandchildren or bogus (empty entry list). */
static void
taxtable_scrub_cb (QofInstance* table_p, gpointer list_p)
{
    GncTaxTable* table = GNC_TAXTABLE (table_p);
    GList** list = static_cast<decltype (list)> (list_p);

    if (taxtable_is_grandchild (table) || gncTaxTableGetEntries (table) == NULL)
        *list = g_list_prepend (*list, table);
}

/* for each entry, check the tax tables.  If the tax tables are
 * grandchildren, then fix them to point to the most senior child
 */
static void
taxtable_scrub_entries (QofInstance* entry_p, gpointer ht_p)
{
    GHashTable* ht = static_cast<decltype (ht)> (ht_p);
    GncEntry* entry = GNC_ENTRY (entry_p);
    GncTaxTable* table, *new_tt;
    gint32 count;

    table = gncEntryGetInvTaxTable (entry);
    if (table)
    {
        if (taxtable_is_grandchild (table))
        {
            gchar guidstr[GUID_ENCODING_LENGTH + 1];
            guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (entry)), guidstr);
            PINFO ("Fixing i-taxtable on entry %s\n", guidstr);
            new_tt = taxtable_find_senior (table);
            gncEntryBeginEdit (entry);
            gncEntrySetInvTaxTable (entry, new_tt);
            gncEntryCommitEdit (entry);
            table = new_tt;
        }
        if (table)
        {
            count = GPOINTER_TO_INT (g_hash_table_lookup (ht, table));
            count++;
            g_hash_table_insert (ht, table, GINT_TO_POINTER (count));
        }
    }

    table = gncEntryGetBillTaxTable (entry);
    if (table)
    {
        if (taxtable_is_grandchild (table))
        {
            gchar guidstr[GUID_ENCODING_LENGTH + 1];
            guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (entry)), guidstr);
            PINFO ("Fixing b-taxtable on entry %s\n", guidstr);
            new_tt = taxtable_find_senior (table);
            gncEntryBeginEdit (entry);
            gncEntrySetBillTaxTable (entry, new_tt);
            gncEntryCommitEdit (entry);
            table = new_tt;
        }
        if (table)
        {
            count = GPOINTER_TO_INT (g_hash_table_lookup (ht, table));
            count++;
            g_hash_table_insert (ht, table, GINT_TO_POINTER (count));
        }
    }
}

static void
taxtable_scrub_cust (QofInstance* cust_p, gpointer ht_p)
{
    GHashTable* ht = static_cast<decltype (ht)> (ht_p);
    GncCustomer* cust = GNC_CUSTOMER (cust_p);
    GncTaxTable* table;
    gint32 count;

    table = gncCustomerGetTaxTable (cust);
    if (table)
    {
        count = GPOINTER_TO_INT (g_hash_table_lookup (ht, table));
        count++;
        g_hash_table_insert (ht, table, GINT_TO_POINTER (count));
    }
}

static void
taxtable_scrub_vendor (QofInstance* vendor_p, gpointer ht_p)
{
    GHashTable* ht = static_cast<decltype (ht)> (ht_p);
    GncVendor* vendor = GNC_VENDOR (vendor_p);
    GncTaxTable* table;
    gint32 count;

    table = gncVendorGetTaxTable (vendor);
    if (table)
    {
        count = GPOINTER_TO_INT (g_hash_table_lookup (ht, table));
        count++;
        g_hash_table_insert (ht, table, GINT_TO_POINTER (count));
    }
}

static void
taxtable_reset_refcount (gpointer key, gpointer value, gpointer notused)
{
    GncTaxTable* table = static_cast<decltype (table)> (key);
    gint32 count = GPOINTER_TO_INT (value);

    if (count != gncTaxTableGetRefcount (table) &&
        !gncTaxTableGetInvisible (table))
    {
        gchar guidstr[GUID_ENCODING_LENGTH + 1];
        guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (table)), guidstr);
        PWARN ("Fixing refcount on taxtable %s (%" G_GINT64_FORMAT " -> %d)\n",
               guidstr, gncTaxTableGetRefcount (table), count);
        gncTaxTableSetRefcount (table, count);
    }
}

static void
taxtable_scrub (QofBook* book)
{
    GList* list = NULL;
    GList* node;
    GncTaxTable* parent, *table;
    GHashTable* ht = g_hash_table_new (g_direct_hash, g_direct_equal);

    qof_object_foreach (GNC_ID_ENTRY, book, taxtable_scrub_entries, ht);
    qof_object_foreach (GNC_ID_CUSTOMER, book, taxtable_scrub_cust, ht);
    qof_object_foreach (GNC_ID_VENDOR, book, taxtable_scrub_vendor, ht);
    qof_object_foreach (GNC_ID_TAXTABLE, book, taxtable_scrub_cb, &list);

    /* destroy the list of "grandchildren" tax tables */
    for (node = list; node; node = node->next)
    {
        gchar guidstr[GUID_ENCODING_LENGTH + 1];
        table = static_cast<decltype (table)> (node->data);

        guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (table)), guidstr);
        PINFO ("deleting grandchild taxtable: %s\n", guidstr);

        /* Make sure the parent has no children */
        parent = gncTaxTableGetParent (table);
        gncTaxTableSetChild (parent, NULL);

        /* Destroy this tax table */
        gncTaxTableBeginEdit (table);
        gncTaxTableDestroy (table);
    }

    /* reset the refcounts as necessary */
    g_hash_table_foreach (ht, taxtable_reset_refcount, NULL);

    g_list_free (list);
    g_hash_table_destroy (ht);
}

static gboolean
taxtable_ns (FILE* out)
{
    g_return_val_if_fail (out, FALSE);
    return
        gnc_xml2_write_namespace_decl (out, "taxtable")
        && gnc_xml2_write_namespace_decl (out, "tte");
}

void
gnc_taxtable_xml_initialize (void)
{
    static GncXmlDataType_t be_data =
    {
        GNC_FILE_BACKEND_VERS,
        gnc_taxtable_string,
        taxtable_sixtp_parser_create,
        NULL,           /* add_item */
        taxtable_get_count,
        taxtable_write,
        taxtable_scrub,
        taxtable_ns,
    };

    gnc_xml_register_backend(be_data);
}
