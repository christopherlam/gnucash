/********************************************************************
 * gnc-pricedb-p.h -- a simple price database for gnucash.          *
 * Copyright (C) 2001 Rob Browning                                  *
 * Copyright (C) 2003 Linas Vepstas <linas@linas.org>               *
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
 *******************************************************************/

#ifndef GNC_PRICEDB_P_H
#define GNC_PRICEDB_P_H

#include <glib.h>
#include "qof.h"
#include "gnc-engine.h"
#include "gnc-pricedb.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gnc_price_s
{
    /* 'public' data fields */
    QofInstance inst;              /* globally unique object identifier */

    GNCPriceDB *db;
    gnc_commodity *commodity;
    gnc_commodity *currency;
    time64 tmspec;
    PriceSource source;
    const char *type;
    gnc_numeric value;

    /* 'private' object management fields */
    guint32  refcount;             /* garbage collection reference count */
};

struct _GncPriceClass
{
    QofInstanceClass parent_class;
};

/* The actual storage for the price database is implemented with C++ STL
 * containers (see GncPriceDBImpl in gnc-pricedb.cpp). It is kept behind
 * this opaque pointer, rather than as direct members of this struct,
 * because this header (via typedef GNCPriceDB in gnc-engine.h) is included
 * by plain C translation units (e.g. libgnucash/engine/test/utest-gnc-pricedb.c)
 * that cannot parse C++-only member types. Only gnc-pricedb.cpp may
 * dereference impl. */
struct gnc_price_db_s
{
    QofInstance inst;              /* globally unique object identifier */
    struct GncPriceDBImpl *impl;   /* opaque pointer to private C++ implementation */
    gboolean bulk_update;		 /* TRUE while reading XML file, etc. */
    gboolean reset_nth_price_cache;
};

struct _GncPriceDBClass
{
    QofInstanceClass parent_class;
};

/* These structs define the kind of price lookup being done
 * so that it can be passed to the backend.  This is a rather
 * cheesy, low-brow interface.  It could stand improvement.
 */
typedef enum
{
    LOOKUP_LATEST = 1,
    LOOKUP_ALL,
    LOOKUP_AT_TIME,
    LOOKUP_NEAREST_IN_TIME,
    LOOKUP_LATEST_BEFORE,
    LOOKUP_EARLIEST_AFTER
} PriceLookupType;

typedef struct gnc_price_lookup_helper_s
{
    GList    **return_list;
    gnc_commodity *key;
    time64 time;
} GNCPriceLookupHelper;

#define  gnc_price_set_guid(P,G)  qof_instance_set_guid(QOF_INSTANCE(P),(G))

/** register the pricedb object with the gncObject system */
gboolean gnc_pricedb_register (void);

QofBackend * xaccPriceDBGetBackend (GNCPriceDB *prdb);

#ifdef __cplusplus
}
#endif

#endif
