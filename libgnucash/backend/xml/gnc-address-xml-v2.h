/* gnc-address-xml-v2.h -- Address XML header
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

#ifndef GNC_ADDRESS_XML_V2_H
#define GNC_ADDRESS_XML_V2_H

#include "gncAddress.h"
#include "sixtp.h"

gboolean   gnc_dom_tree_to_address (xmlNodePtr node, GncAddress* address);
xmlNodePtr gnc_address_to_dom_tree (const char* tag, GncAddress* addr);
void gnc_address_xml_initialize (void);

/* A SAX-direct (streaming) sub-parser for a nested address element
 * (e.g. cust:addr, vendor:addr): reads addr:name/addr1..4/phone/fax/
 * email straight off the SAX character stream, with addr:slots (a kvp
 * frame) on the scoped DOM path. start's job is to resolve the
 * caller's own parent_data down to the target GncAddress* -- already
 * allocated and owned by the customer/vendor/employee/... being
 * parsed -- and pass it on as data_for_children; every address field
 * handler then receives that GncAddress* directly as its own
 * parent_data. */
sixtp* sax_address_parser_new (sixtp_start_handler start);

#endif /* GNC_ADDRESS_XML_V2_H */
