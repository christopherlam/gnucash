/* gnc-owner-xml-v2.h -- Owner XML header
 *
 * Copyright (C) 2002 Derek Atkins <warlord@MIT.EDU>
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

#ifndef GNC_OWNER_XML_V2_H
#define GNC_OWNER_XML_V2_H
#include "gncOwner.h"
#include "qof.h"
#include "sixtp.h"
xmlNodePtr gnc_owner_to_dom_tree (const char* tag, const GncOwner* addr);
void gnc_owner_xml_initialize (void);

/* A SAX-direct (streaming) sub-parser for a nested owner element (a
 * type tag plus a guid reference to the customer/job/vendor/employee
 * of that type). start's job is to resolve the caller's own
 * parent_data down to the target GncOwner and QofBook, g_new() an
 * owner_sax_ctx holding them, and pass it on as data_for_children;
 * sax_owner_parser_new() takes care of freeing it once the owner
 * element closes. */
struct owner_sax_ctx
{
    GncOwner* owner;
    QofBook* book;
};

sixtp* sax_owner_parser_new (sixtp_start_handler start);

#endif /* GNC_OWNER_XML_V2_H */
