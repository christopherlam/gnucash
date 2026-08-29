/********************************************************************\
 * gnc-employee-xml-v2.c -- employee xml i/o implementation         *
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
#include "gncEmployeeP.h"
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

#include "gnc-employee-xml-v2.h"
#include "gnc-address-xml-v2.h"

#define _GNC_MOD_NAME   GNC_ID_EMPLOYEE

const gchar* employee_version_string = "2.0.0";

/* ids */
#define gnc_employee_string "gnc:GncEmployee"
#define employee_username_string "employee:username"
#define employee_guid_string "employee:guid"
#define employee_id_string "employee:id"
#define employee_addr_string "employee:addr"
#define employee_language_string "employee:language"
#define employee_acl_string "employee:acl"
#define employee_active_string "employee:active"
#define employee_workday_string "employee:workday"
#define employee_rate_string "employee:rate"
#define employee_currency_string "employee:currency"
#define employee_ccard_string "employee:ccard"
#define employee_slots_string "employee:slots"

static void
maybe_add_string (xmlNodePtr ptr, const char* tag, const char* str)
{
    if (str && *str)
        xmlAddChild (ptr, text_to_dom_tree (tag, str));
}

static xmlNodePtr
employee_dom_tree_create (GncEmployee* employee)
{
    xmlNodePtr ret;
    gnc_numeric num;
    Account* ccard_acc;

    ret = xmlNewNode (NULL, BAD_CAST gnc_employee_string);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST employee_version_string);

    xmlAddChild (ret, guid_to_dom_tree (employee_guid_string,
                                        qof_instance_get_guid (QOF_INSTANCE (employee))));

    xmlAddChild (ret, text_to_dom_tree (employee_username_string,
                                        gncEmployeeGetUsername (employee)));

    xmlAddChild (ret, text_to_dom_tree (employee_id_string,
                                        gncEmployeeGetID (employee)));

    xmlAddChild (ret, gnc_address_to_dom_tree (employee_addr_string,
                                               gncEmployeeGetAddr (employee)));

    maybe_add_string (ret, employee_language_string,
                      gncEmployeeGetLanguage (employee));
    maybe_add_string (ret, employee_acl_string, gncEmployeeGetAcl (employee));

    xmlAddChild (ret, int_to_dom_tree (employee_active_string,
                                       gncEmployeeGetActive (employee)));

    num = gncEmployeeGetWorkday (employee);
    xmlAddChild (ret, gnc_numeric_to_dom_tree (employee_workday_string, &num));

    num = gncEmployeeGetRate (employee);
    xmlAddChild (ret, gnc_numeric_to_dom_tree (employee_rate_string, &num));

    xmlAddChild
    (ret,
     commodity_ref_to_dom_tree (employee_currency_string,
                                gncEmployeeGetCurrency (employee)));

    ccard_acc = gncEmployeeGetCCard (employee);
    if (ccard_acc)
        xmlAddChild (ret, guid_to_dom_tree (employee_ccard_string,
                                            qof_instance_get_guid (QOF_INSTANCE (ccard_acc))));

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (employee_slots_string,
                                                      QOF_INSTANCE (employee)));
    return ret;
}

/***********************************************************************/
/* SAX-direct (streaming) employee parser: reads a gnc:GncEmployee
   straight off the SAX character stream, with no intermediate
   xmlNodePtr built for any of its fields. Nothing else in the
   codebase uses the old DOM-based parser this replaces, so it's gone
   entirely. */

struct employee_sax_pdata
{
    GncEmployee* employee;
    QofBook* book;
};

static gboolean
sax_employee_guid_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<employee_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;

        /* Adopt an employee that already exists by this guid instead
           of the fresh one sax_employee_start() made. */
        GncEmployee* employee = gncEmployeeLookup (pdata->book, &guid);
        if (employee)
        {
            gncEmployeeDestroy (pdata->employee);
            pdata->employee = employee;
            gncEmployeeBeginEdit (employee);
        }
        else
            gncEmployeeSetGUID (pdata->employee, &guid);
        return TRUE;
    });
}

static gboolean
sax_employee_username_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<employee_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncEmployeeSetUsername (pdata->employee, txt); return TRUE; });
}

static gboolean
sax_employee_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                     gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<employee_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncEmployeeSetID (pdata->employee, txt); return TRUE; });
}

static gboolean
sax_employee_language_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<employee_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncEmployeeSetLanguage (pdata->employee, txt); return TRUE; });
}

static gboolean
sax_employee_acl_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                      gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<employee_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncEmployeeSetAcl (pdata->employee, txt); return TRUE; });
}

static gboolean
sax_employee_active_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                         gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<employee_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncEmployeeSetActive (pdata->employee, (gboolean) val);
        return TRUE;
    });
}

static gboolean
sax_employee_workday_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                          gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<employee_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gnc_numeric num = gnc_numeric_from_string (txt);
        gncEmployeeSetWorkday (pdata->employee, gnc_numeric_check (num) ? gnc_numeric_zero () : num);
        return TRUE;
    });
}

static gboolean
sax_employee_rate_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<employee_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gnc_numeric num = gnc_numeric_from_string (txt);
        gncEmployeeSetRate (pdata->employee, gnc_numeric_check (num) ? gnc_numeric_zero () : num);
        return TRUE;
    });
}

static gboolean
sax_employee_ccard_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                        gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<employee_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;
        Account* ccard_acc = xaccAccountLookup (&guid, pdata->book);
        g_return_val_if_fail (ccard_acc, FALSE);
        gncEmployeeSetCCard (pdata->employee, ccard_acc);
        return TRUE;
    });
}

static gboolean
sax_employee_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                            gpointer parent_data, gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<employee_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ret = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->employee));
    xmlFreeNode (tree);
    return ret;
}

static gboolean
sax_employee_addr_start (GSList*, gpointer parent_data, gpointer,
                         gpointer* data_for_children, gpointer*, const gchar*, gchar**)
{
    auto* pdata = static_cast<employee_sax_pdata*> (parent_data);
    *data_for_children = gncEmployeeGetAddr (pdata->employee);
    return TRUE;
}

static gboolean
sax_employee_currency_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                           gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<employee_sax_pdata*> (parent_data);
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
    gncEmployeeSetCurrency (pdata->employee, com);
    return TRUE;
}

static gboolean
sax_employee_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
                    gpointer*, const gchar* tag, gchar**)
{
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }
    auto* gdata = static_cast<gxpf_data*> (global_data);
    QofBook* book = static_cast<QofBook*> (gdata->bookdata);
    auto* pdata = g_new (employee_sax_pdata, 1);
    pdata->employee = gncEmployeeCreate (book);
    pdata->book = book;
    gncEmployeeBeginEdit (pdata->employee);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_employee_end (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer global_data,
                  gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<employee_sax_pdata*> (data_for_children);
    if (!pdata) return TRUE;
    if (!tag) { g_free (pdata); return TRUE; }

    GncEmployee* employee = pdata->employee;
    g_free (pdata);

    gncEmployeeCommitEdit (employee);
    auto* gdata = static_cast<gxpf_data*> (global_data);
    gdata->cb (tag, gdata->parsedata, employee);
    return TRUE;
}

static void
sax_employee_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
                   gpointer*, const gchar*)
{
    auto* pdata = static_cast<employee_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gncEmployeeDestroy (pdata->employee);
    g_free (pdata);
}

static sixtp*
employee_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_employee_start,
        SIXTP_END_HANDLER_ID, sax_employee_end,
        SIXTP_FAIL_HANDLER_ID, sax_employee_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        employee_guid_string, restore_char_generator (sax_employee_guid_end),
        employee_username_string, restore_char_generator (sax_employee_username_end),
        employee_id_string, restore_char_generator (sax_employee_id_end),
        employee_language_string, restore_char_generator (sax_employee_language_end),
        employee_acl_string, restore_char_generator (sax_employee_acl_end),
        employee_active_string, restore_char_generator (sax_employee_active_end),
        employee_workday_string, restore_char_generator (sax_employee_workday_end),
        employee_rate_string, restore_char_generator (sax_employee_rate_end),
        employee_ccard_string, restore_char_generator (sax_employee_ccard_end),
        employee_slots_string, sixtp_dom_parser_new_rooted (sax_employee_slots_dom_end, NULL, NULL),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    sixtp_add_sub_parser (p, employee_addr_string, sax_address_parser_new (sax_employee_addr_start));

    {
        sixtp* cmdty = sax_commodity_ref_parser_new (sax_employee_currency_end);
        sixtp_add_sub_parser (p, employee_currency_string, cmdty);
        sixtp_add_sub_parser (p, "employee:commodity", cmdty);
    }

    return p;
}

static gboolean
employee_should_be_saved (GncEmployee* employee)
{
    const char* id;

    /* make sure this is a valid employee before we save it -- should have an ID */
    id = gncEmployeeGetID (employee);
    if (id == NULL || *id == '\0')
        return FALSE;

    return TRUE;
}

static void
do_count (QofInstance* employee_p, gpointer count_p)
{
    int* count = static_cast<decltype (count)> (count_p);
    if (employee_should_be_saved ((GncEmployee*) employee_p))
        (*count)++;
}

static int
employee_get_count (QofBook* book)
{
    int count = 0;
    qof_object_foreach (_GNC_MOD_NAME, book, do_count, (gpointer) &count);
    return count;
}

static void
xml_add_employee (QofInstance* employee_p, gpointer out_p)
{
    xmlNodePtr node;
    GncEmployee* employee = (GncEmployee*) employee_p;
    FILE* out = static_cast<decltype (out)> (out_p);

    if (ferror (out))
        return;
    if (!employee_should_be_saved (employee))
        return;

    node = employee_dom_tree_create (employee);
    xmlElemDump (out, NULL, node);
    xmlFreeNode (node);
    if (ferror (out) || fprintf (out, "\n") < 0)
        return;
}

static gboolean
employee_write (FILE* out, QofBook* book)
{
    qof_object_foreach_sorted (_GNC_MOD_NAME, book, xml_add_employee,
                               (gpointer) out);
    return ferror (out) == 0;
}

static gboolean
employee_ns (FILE* out)
{
    g_return_val_if_fail (out, FALSE);
    return gnc_xml2_write_namespace_decl (out, "employee");
}

void
gnc_employee_xml_initialize (void)
{
    static GncXmlDataType_t be_data =
    {
        GNC_FILE_BACKEND_VERS,
        gnc_employee_string,
        employee_sixtp_parser_create,
        NULL,           /* add_item */
        employee_get_count,
        employee_write,
        NULL,           /* scrub */
        employee_ns,
    };

    gnc_xml_register_backend (be_data);
}
