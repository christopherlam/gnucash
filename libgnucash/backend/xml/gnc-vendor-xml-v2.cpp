/********************************************************************\
 * gnc-vendor-xml-v2.c -- vendor xml i/o implementation         *
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
#include "gncVendorP.h"
#include "gncTaxTableP.h"
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

#include "gnc-vendor-xml-v2.h"
#include "gnc-address-xml-v2.h"
#include "xml-helpers.h"
#include "gnc-bill-term-xml-v2.h"

#define _GNC_MOD_NAME   GNC_ID_VENDOR

const gchar* vendor_version_string = "2.0.0";

/* ids */
#define gnc_vendor_string "gnc:GncVendor"
#define vendor_name_string "vendor:name"
#define vendor_guid_string "vendor:guid"
#define vendor_id_string "vendor:id"
#define vendor_addr_string "vendor:addr"
#define vendor_notes_string "vendor:notes"
#define vendor_terms_string "vendor:terms"
#define vendor_taxincluded_string "vendor:taxincluded"
#define vendor_active_string "vendor:active"
#define vendor_currency_string "vendor:currency"
#define vendor_taxtable_string "vendor:taxtable"
#define vendor_taxtableoverride_string "vendor:use-tt"
#define vendor_slots_string "vendor:slots"

static xmlNodePtr
vendor_dom_tree_create (GncVendor* vendor)
{
    xmlNodePtr ret;
    GncBillTerm* term;
    GncTaxTable* taxtable;

    ret = xmlNewNode (NULL, BAD_CAST gnc_vendor_string);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST vendor_version_string);

    xmlAddChild (ret, guid_to_dom_tree (vendor_guid_string,
                                        qof_instance_get_guid (QOF_INSTANCE (vendor))));

    xmlAddChild (ret, text_to_dom_tree (vendor_name_string,
                                        gncVendorGetName (vendor)));

    xmlAddChild (ret, text_to_dom_tree (vendor_id_string,
                                        gncVendorGetID (vendor)));

    xmlAddChild (ret, gnc_address_to_dom_tree (vendor_addr_string,
                                               gncVendorGetAddr (vendor)));

    maybe_add_string (ret, vendor_notes_string, gncVendorGetNotes (vendor));

    term = gncVendorGetTerms (vendor);
    if (term)
        xmlAddChild (ret, guid_to_dom_tree (vendor_terms_string,
                                            qof_instance_get_guid (QOF_INSTANCE (term))));

    xmlAddChild (ret, text_to_dom_tree (vendor_taxincluded_string,
                                        gncTaxIncludedTypeToString (
                                            gncVendorGetTaxIncluded (vendor))));

    xmlAddChild (ret, int_to_dom_tree (vendor_active_string,
                                       gncVendorGetActive (vendor)));

    xmlAddChild
    (ret,
     commodity_ref_to_dom_tree (vendor_currency_string,
                                gncVendorGetCurrency (vendor)));

    xmlAddChild (ret, int_to_dom_tree (vendor_taxtableoverride_string,
                                       gncVendorGetTaxTableOverride (vendor)));
    taxtable = gncVendorGetTaxTable (vendor);
    if (taxtable)
        xmlAddChild (ret, guid_to_dom_tree (vendor_taxtable_string,
                                            qof_instance_get_guid (QOF_INSTANCE (taxtable))));

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (vendor_slots_string,
                                                      QOF_INSTANCE (vendor)));
    return ret;
}

/***********************************************************************/
/* SAX-direct (streaming) vendor parser: reads a gnc:GncVendor straight
   off the SAX character stream, with no intermediate xmlNodePtr built
   for any of its fields. Nothing else in the codebase uses the old
   DOM-based parser this replaces, so it's gone entirely. */

struct vendor_sax_pdata
{
    GncVendor* vendor;
    QofBook* book;
};

static gboolean
sax_vendor_guid_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                     gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;

        /* Adopt a vendor that already exists by this guid (e.g. a
           placeholder created earlier by an owner or invoice
           reference) instead of the fresh one sax_vendor_start()
           made. */
        GncVendor* vendor = gncVendorLookup (pdata->book, &guid);
        if (vendor)
        {
            gncVendorDestroy (pdata->vendor);
            pdata->vendor = vendor;
            gncVendorBeginEdit (vendor);
        }
        else
            gncVendorSetGUID (pdata->vendor, &guid);
        return TRUE;
    });
}

static gboolean
sax_vendor_name_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                     gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncVendorSetName (pdata->vendor, txt); return TRUE; });
}

static gboolean
sax_vendor_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                   gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncVendorSetID (pdata->vendor, txt); return TRUE; });
}

static gboolean
sax_vendor_notes_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                      gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncVendorSetNotes (pdata->vendor, txt); return TRUE; });
}

static gboolean
sax_vendor_terms_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                      gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        GncBillTerm* term = gnc_billterm_xml_find_or_create (pdata->book, &guid);
        g_assert (term);
        gncVendorSetTerms (pdata->vendor, term);
        return TRUE;
    });
}

static gboolean
sax_vendor_taxincluded_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                            gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncTaxIncluded type;
        if (gncTaxIncludedStringToType (txt, &type))
            gncVendorSetTaxIncluded (pdata->vendor, type);
        return TRUE;
    });
}

static gboolean
sax_vendor_active_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncVendorSetActive (pdata->vendor, (gboolean) val);
        return TRUE;
    });
}

static gboolean
sax_vendor_taxtable_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (parent_data);
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

        gncVendorSetTaxTable (pdata->vendor, taxtable);
        return TRUE;
    });
}

static gboolean
sax_vendor_taxtableoverride_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                                 gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncVendorSetTaxTableOverride (pdata->vendor, (gboolean) val);
        return TRUE;
    });
}

static gboolean
sax_vendor_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                          gpointer parent_data, gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ret = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->vendor));
    xmlFreeNode (tree);
    return ret;
}

static gboolean
sax_vendor_addr_start (GSList*, gpointer parent_data, gpointer,
                       gpointer* data_for_children, gpointer*, const gchar*, gchar**)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (parent_data);
    *data_for_children = gncVendorGetAddr (pdata->vendor);
    return TRUE;
}

static gboolean
sax_vendor_currency_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (parent_data);
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
    gncVendorSetCurrency (pdata->vendor, com);
    return TRUE;
}

static gboolean
sax_vendor_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
                  gpointer*, const gchar* tag, gchar**)
{
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }
    auto* gdata = static_cast<gxpf_data*> (global_data);
    QofBook* book = static_cast<QofBook*> (gdata->bookdata);
    auto* pdata = g_new (vendor_sax_pdata, 1);
    pdata->vendor = gncVendorCreate (book);
    pdata->book = book;
    gncVendorBeginEdit (pdata->vendor);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_vendor_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer global_data,
                gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (data_for_children);
    if (!pdata) return TRUE;
    if (!tag) { g_free (pdata); return TRUE; }

    GncVendor* vendor = pdata->vendor;
    g_free (pdata);

    gncVendorCommitEdit (vendor);
    auto* gdata = static_cast<gxpf_data*> (global_data);
    gdata->cb (tag, gdata->parsedata, vendor);
    return TRUE;
}

static void
sax_vendor_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
                 gpointer*, const gchar*)
{
    auto* pdata = static_cast<vendor_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gncVendorDestroy (pdata->vendor);
    g_free (pdata);
}

static sixtp*
vendor_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_vendor_start,
        SIXTP_END_HANDLER_ID, sax_vendor_end,
        SIXTP_FAIL_HANDLER_ID, sax_vendor_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        vendor_guid_string, restore_char_generator (sax_vendor_guid_end),
        vendor_name_string, restore_char_generator (sax_vendor_name_end),
        vendor_id_string, restore_char_generator (sax_vendor_id_end),
        vendor_notes_string, restore_char_generator (sax_vendor_notes_end),
        vendor_terms_string, restore_char_generator (sax_vendor_terms_end),
        vendor_taxincluded_string, restore_char_generator (sax_vendor_taxincluded_end),
        vendor_active_string, restore_char_generator (sax_vendor_active_end),
        vendor_taxtable_string, restore_char_generator (sax_vendor_taxtable_end),
        vendor_taxtableoverride_string, restore_char_generator (sax_vendor_taxtableoverride_end),
        vendor_slots_string, sixtp_dom_parser_new_rooted (sax_vendor_slots_dom_end, NULL, NULL),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    sixtp_add_sub_parser (p, vendor_addr_string, sax_address_parser_new (sax_vendor_addr_start));

    {
        sixtp* cmdty = sax_commodity_ref_parser_new (sax_vendor_currency_end);
        sixtp_add_sub_parser (p, vendor_currency_string, cmdty);
        sixtp_add_sub_parser (p, "vendor:commodity", cmdty);
    }

    return p;
}

static gboolean
vendor_should_be_saved (GncVendor* vendor)
{
    const char* id;

    /* make sure this is a valid vendor before we save it -- should have an ID */
    id = gncVendorGetID (vendor);
    if (id == NULL || *id == '\0')
        return FALSE;

    return TRUE;
}

static void
do_count (QofInstance* vendor_p, gpointer count_p)
{
    int* count = static_cast<decltype (count)> (count_p);
    if (vendor_should_be_saved ((GncVendor*)vendor_p))
        (*count)++;
}

static int
vendor_get_count (QofBook* book)
{
    int count = 0;
    qof_object_foreach (_GNC_MOD_NAME, book, do_count, (gpointer) &count);
    return count;
}

static void
xml_add_vendor (QofInstance* vendor_p, gpointer out_p)
{
    xmlNodePtr node;
    GncVendor* vendor = (GncVendor*) vendor_p;
    FILE* out = static_cast<decltype (out)> (out_p);

    if (ferror (out))
        return;
    if (!vendor_should_be_saved (vendor))
        return;

    node = vendor_dom_tree_create (vendor);
    xmlElemDump (out, NULL, node);
    xmlFreeNode (node);
    if (ferror (out) || fprintf (out, "\n") < 0)
        return;
}

static gboolean
vendor_write (FILE* out, QofBook* book)
{
    qof_object_foreach_sorted (_GNC_MOD_NAME, book, xml_add_vendor,
                               (gpointer) out);
    return ferror (out) == 0;
}

static gboolean
vendor_ns (FILE* out)
{
    g_return_val_if_fail (out, FALSE);
    return gnc_xml2_write_namespace_decl (out, "vendor");
}

void
gnc_vendor_xml_initialize (void)
{
    static GncXmlDataType_t be_data =
    {
        GNC_FILE_BACKEND_VERS,
        gnc_vendor_string,
        vendor_sixtp_parser_create,
        NULL,           /* add_item */
        vendor_get_count,
        vendor_write,
        NULL,           /* scrub */
        vendor_ns,
    };

    gnc_xml_register_backend(be_data);
}
