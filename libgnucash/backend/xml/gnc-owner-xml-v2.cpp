/********************************************************************\
 * gnc-owner-xml-v2.c -- owner xml i/o implementation           *
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
#include "gncCustomerP.h"
#include "gncJobP.h"
#include "gncVendorP.h"
#include "gncEmployeeP.h"
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

#include "gnc-owner-xml-v2.h"

static QofLogModule log_module = GNC_MOD_IO;

const gchar* owner_version_string = "2.0.0";

/* ids */
#define owner_type_string   "owner:type"
#define owner_id_string     "owner:id"

xmlNodePtr
gnc_owner_to_dom_tree (const char* tag, const GncOwner* owner)
{
    xmlNodePtr ret;
    const char* type_str;

    switch (gncOwnerGetType (owner))
    {
    case GNC_OWNER_CUSTOMER:
        type_str = GNC_ID_CUSTOMER;
        break;
    case GNC_OWNER_JOB:
        type_str = GNC_ID_JOB;
        break;
    case GNC_OWNER_VENDOR:
        type_str = GNC_ID_VENDOR;
        break;
    case GNC_OWNER_EMPLOYEE:
        type_str = GNC_ID_EMPLOYEE;
        break;
    default:
        PWARN ("Invalid owner type: %d", gncOwnerGetType (owner));
        return NULL;
    }

    ret = xmlNewNode (NULL, BAD_CAST tag);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST owner_version_string);

    xmlAddChild (ret, text_to_dom_tree (owner_type_string, type_str));
    xmlAddChild (ret, guid_to_dom_tree (owner_id_string,
                                        gncOwnerGetGUID (owner)));

    return ret;
}

/***********************************************************************/
/* SAX-direct (streaming) owner sub-parser: see gnc-owner-xml-v2.h. */

static gboolean
sax_owner_type_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* ctx = static_cast<owner_sax_ctx*> (parent_data);
    return sax_apply_chars (dfc, [ctx] (const char* txt) -> gboolean
    {
        if (!g_strcmp0 (txt, GNC_ID_CUSTOMER))
            gncOwnerInitCustomer (ctx->owner, NULL);
        else if (!g_strcmp0 (txt, GNC_ID_JOB))
            gncOwnerInitJob (ctx->owner, NULL);
        else if (!g_strcmp0 (txt, GNC_ID_VENDOR))
            gncOwnerInitVendor (ctx->owner, NULL);
        else if (!g_strcmp0 (txt, GNC_ID_EMPLOYEE))
            gncOwnerInitEmployee (ctx->owner, NULL);
        return TRUE;
    });
}

static gboolean
sax_owner_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                  gpointer, gpointer*, const gchar*)
{
    auto* ctx = static_cast<owner_sax_ctx*> (parent_data);
    return sax_apply_chars (dfc, [ctx] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;

        switch (gncOwnerGetType (ctx->owner))
        {
        case GNC_OWNER_CUSTOMER:
        {
            GncCustomer* cust = gncCustomerLookup (ctx->book, &guid);
            if (!cust)
            {
                cust = gncCustomerCreate (ctx->book);
                gncCustomerSetGUID (cust, &guid);
            }
            gncOwnerInitCustomer (ctx->owner, cust);
            break;
        }
        case GNC_OWNER_JOB:
        {
            GncJob* job = gncJobLookup (ctx->book, &guid);
            if (!job)
            {
                job = gncJobCreate (ctx->book);
                gncJobSetGUID (job, &guid);
            }
            gncOwnerInitJob (ctx->owner, job);
            break;
        }
        case GNC_OWNER_VENDOR:
        {
            GncVendor* vendor = gncVendorLookup (ctx->book, &guid);
            if (!vendor)
            {
                vendor = gncVendorCreate (ctx->book);
                gncVendorSetGUID (vendor, &guid);
            }
            gncOwnerInitVendor (ctx->owner, vendor);
            break;
        }
        case GNC_OWNER_EMPLOYEE:
        {
            GncEmployee* employee = gncEmployeeLookup (ctx->book, &guid);
            if (!employee)
            {
                employee = gncEmployeeCreate (ctx->book);
                gncEmployeeSetGUID (employee, &guid);
            }
            gncOwnerInitEmployee (ctx->owner, employee);
            break;
        }
        default:
            PWARN ("Invalid owner type: %d\n", gncOwnerGetType (ctx->owner));
            return FALSE;
        }
        return TRUE;
    });
}

static gboolean
sax_owner_wrapper_end (gpointer data_for_children, GSList*, GSList*, gpointer,
                       gpointer, gpointer*, const gchar*)
{
    g_free (static_cast<owner_sax_ctx*> (data_for_children));
    return TRUE;
}

sixtp*
sax_owner_parser_new (sixtp_start_handler start)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, start,
        SIXTP_END_HANDLER_ID, sax_owner_wrapper_end,
        SIXTP_NO_MORE_HANDLERS);

    return sixtp_add_some_sub_parsers (
        p, TRUE,
        owner_type_string, restore_char_generator (sax_owner_type_end),
        owner_id_string, restore_char_generator (sax_owner_id_end),
        NULL, NULL);
}

static gboolean
owner_ns (FILE* out)
{
    g_return_val_if_fail (out, FALSE);
    return gnc_xml2_write_namespace_decl (out, "owner");
}

void
gnc_owner_xml_initialize (void)
{
    static GncXmlDataType_t be_data =
    {
        GNC_FILE_BACKEND_VERS,
        "gnc:Owner",
        NULL,           /* parser_create */
        NULL,           /* add_item */
        NULL,           /* get_count */
        NULL,           /* write */
        NULL,           /* scrub */
        owner_ns,
    };

    gnc_xml_register_backend (be_data);
}
