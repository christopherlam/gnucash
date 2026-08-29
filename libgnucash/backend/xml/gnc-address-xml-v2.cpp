/********************************************************************\
 * gnc-address-xml-v2.c -- address xml i/o implementation           *
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

#include "gnc-address-xml-v2.h"

const gchar* address_version_string = "2.0.0";

/* ids */
#define addr_name_string    "addr:name"
#define addr_addr1_string   "addr:addr1"
#define addr_addr2_string   "addr:addr2"
#define addr_addr3_string   "addr:addr3"
#define addr_addr4_string   "addr:addr4"
#define addr_phone_string   "addr:phone"
#define addr_fax_string     "addr:fax"
#define addr_email_string   "addr:email"
#define addr_slots_string   "addr:slots"

static void
maybe_add_string (xmlNodePtr ptr, const char* tag, const char* str)
{
    if (str && *str)
        xmlAddChild (ptr, text_to_dom_tree (tag, str));
}

xmlNodePtr
gnc_address_to_dom_tree (const char* tag, GncAddress* addr)
{
    xmlNodePtr ret;

    ret = xmlNewNode (NULL, BAD_CAST tag);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST address_version_string);

    maybe_add_string (ret, addr_name_string, gncAddressGetName (addr));

    maybe_add_string (ret, addr_addr1_string, gncAddressGetAddr1 (addr));
    maybe_add_string (ret, addr_addr2_string, gncAddressGetAddr2 (addr));
    maybe_add_string (ret, addr_addr3_string, gncAddressGetAddr3 (addr));
    maybe_add_string (ret, addr_addr4_string, gncAddressGetAddr4 (addr));

    maybe_add_string (ret, addr_phone_string, gncAddressGetPhone (addr));
    maybe_add_string (ret, addr_fax_string, gncAddressGetFax (addr));
    maybe_add_string (ret, addr_email_string, gncAddressGetEmail (addr));

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (addr_slots_string,
                                                      QOF_INSTANCE (addr)));
    return ret;
}

/***********************************************************************/
/* SAX-direct (streaming) address sub-parser: see gnc-address-xml-v2.h. */

static gboolean
sax_addr_name_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                   gpointer, gpointer*, const gchar*)
{
    auto* addr = static_cast<GncAddress*> (parent_data);
    return sax_apply_chars (dfc, [addr] (const char* txt) -> gboolean
    { gncAddressSetName (addr, txt); return TRUE; });
}

static gboolean
sax_addr_addr1_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* addr = static_cast<GncAddress*> (parent_data);
    return sax_apply_chars (dfc, [addr] (const char* txt) -> gboolean
    { gncAddressSetAddr1 (addr, txt); return TRUE; });
}

static gboolean
sax_addr_addr2_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* addr = static_cast<GncAddress*> (parent_data);
    return sax_apply_chars (dfc, [addr] (const char* txt) -> gboolean
    { gncAddressSetAddr2 (addr, txt); return TRUE; });
}

static gboolean
sax_addr_addr3_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* addr = static_cast<GncAddress*> (parent_data);
    return sax_apply_chars (dfc, [addr] (const char* txt) -> gboolean
    { gncAddressSetAddr3 (addr, txt); return TRUE; });
}

static gboolean
sax_addr_addr4_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* addr = static_cast<GncAddress*> (parent_data);
    return sax_apply_chars (dfc, [addr] (const char* txt) -> gboolean
    { gncAddressSetAddr4 (addr, txt); return TRUE; });
}

static gboolean
sax_addr_phone_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* addr = static_cast<GncAddress*> (parent_data);
    return sax_apply_chars (dfc, [addr] (const char* txt) -> gboolean
    { gncAddressSetPhone (addr, txt); return TRUE; });
}

static gboolean
sax_addr_fax_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                  gpointer, gpointer*, const gchar*)
{
    auto* addr = static_cast<GncAddress*> (parent_data);
    return sax_apply_chars (dfc, [addr] (const char* txt) -> gboolean
    { gncAddressSetFax (addr, txt); return TRUE; });
}

static gboolean
sax_addr_email_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* addr = static_cast<GncAddress*> (parent_data);
    return sax_apply_chars (dfc, [addr] (const char* txt) -> gboolean
    { gncAddressSetEmail (addr, txt); return TRUE; });
}

static gboolean
sax_addr_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                        gpointer parent_data, gpointer, gpointer* result,
                        const gchar*)
{
    auto* addr = static_cast<GncAddress*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ok = TRUE;
    if (tree)
    {
        ok = dom_tree_create_instance_slots (tree, QOF_INSTANCE (addr));
        xmlFreeNode (tree);
    }
    *result = nullptr;
    return ok;
}

sixtp*
sax_address_parser_new (sixtp_start_handler start)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, start,
        SIXTP_NO_MORE_HANDLERS);

    return sixtp_add_some_sub_parsers (
        p, TRUE,
        addr_name_string, restore_char_generator (sax_addr_name_end),
        addr_addr1_string, restore_char_generator (sax_addr_addr1_end),
        addr_addr2_string, restore_char_generator (sax_addr_addr2_end),
        addr_addr3_string, restore_char_generator (sax_addr_addr3_end),
        addr_addr4_string, restore_char_generator (sax_addr_addr4_end),
        addr_phone_string, restore_char_generator (sax_addr_phone_end),
        addr_fax_string, restore_char_generator (sax_addr_fax_end),
        addr_email_string, restore_char_generator (sax_addr_email_end),
        addr_slots_string, sixtp_dom_parser_new_rooted (sax_addr_slots_dom_end, NULL, NULL),
        NULL, NULL);
}

static gboolean
address_ns (FILE* out)
{
    g_return_val_if_fail (out, FALSE);
    return gnc_xml2_write_namespace_decl (out, "addr");
}

void
gnc_address_xml_initialize (void)
{
    static GncXmlDataType_t be_data =
    {
        GNC_FILE_BACKEND_VERS,
        "gnc:Address",
        NULL,           /* parser_create */
        NULL,           /* add_item */
        NULL,           /* get_count */
        NULL,           /* write */
        NULL,           /* scrub */
        address_ns,
    };

    gnc_xml_register_backend (be_data);
}
