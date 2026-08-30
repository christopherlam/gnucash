/********************************************************************\
 * gncOwner.c -- Business Interface:  Object OWNERs                 *
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

/*
 * Copyright (C) 2001, 2002 Derek Atkins
 * Copyright (C) 2003 Linas Vepstas <linas@linas.org>
 * Copyright (c) 2005 Neil Williams <linux@codehelp.co.uk>
 * Copyright (c) 2006 David Hampton <hampton@employees.org>
 * Copyright (c) 2011 Geert Janssens <geert@kobaltwit.be>
 * Author: Derek Atkins <warlord@MIT.EDU>
 */

#include <config.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <qofinstance-p.h>

#include "gncCustomerP.h"
#include "gncEmployeeP.h"
#include "gncJobP.h"
#include "gncOwner.h"
#include "gncOwnerP.h"
#include "gncVendorP.h"
#include "gncInvoice.h"
#include "gnc-commodity.h"
#include "Scrub2.h"
#include "Split.h"
#include "Transaction.h"
#include "engine-helpers.h"

#include <variant>

#define _GNC_MOD_NAME   GNC_ID_OWNER

#define GNC_OWNER_ID    "gncOwner"

static QofLogModule log_module = GNC_MOD_ENGINE;

/* GncOwner's public struct (gncOwner.h) keeps a plain tagged union so that
 * the many plain-C callers across the codebase (and SWIG) keep working
 * unchanged. Internally here, though, dispatching on owner->type via
 * std::variant/std::visit is safer than the hand-written switches this file
 * used to have: a visitor that fails to handle one of the alternatives is a
 * compile error, not a silently-missing case. to_variant() below is the
 * bridge between the two representations. */
namespace
{
using OwnerVariant = std::variant<std::monostate, gpointer, GncCustomer*,
                                   GncJob*, GncVendor*, GncEmployee*>;

OwnerVariant
to_variant (const GncOwner *owner)
{
    if (!owner)
        return {};
    switch (owner->type)
    {
    case GNC_OWNER_NONE:
        return {};
    case GNC_OWNER_UNDEFINED:
        return OwnerVariant{std::in_place_type<gpointer>, owner->owner.undefined};
    case GNC_OWNER_CUSTOMER:
        return OwnerVariant{std::in_place_type<GncCustomer*>, owner->owner.customer};
    case GNC_OWNER_JOB:
        return OwnerVariant{std::in_place_type<GncJob*>, owner->owner.job};
    case GNC_OWNER_VENDOR:
        return OwnerVariant{std::in_place_type<GncVendor*>, owner->owner.vendor};
    case GNC_OWNER_EMPLOYEE:
        return OwnerVariant{std::in_place_type<GncEmployee*>, owner->owner.employee};
    }
    return {};
}

/* Standard "overload set from lambdas" helper for std::visit. */
template <typename... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template <typename... Ts> overloaded (Ts...) -> overloaded<Ts...>;

} // anonymous namespace

GncOwner * gncOwnerNew (void)
{
    GncOwner *o;

    o = g_new0 (GncOwner, 1);
    o->type = GNC_OWNER_NONE;
    return o;
}

void gncOwnerFree (GncOwner *owner)
{
    if (!owner) return;
    g_free (owner);
}

void gncOwnerBeginEdit (GncOwner *owner)
{
    if (!owner) return;
    std::visit (overloaded{
        [](std::monostate) {},
        [](gpointer) {},
        [](GncCustomer *c) { gncCustomerBeginEdit (c); },
        [](GncJob *j) { gncJobBeginEdit (j); },
        [](GncVendor *v) { gncVendorBeginEdit (v); },
        [](GncEmployee *e) { gncEmployeeBeginEdit (e); },
    }, to_variant (owner));
}

void gncOwnerCommitEdit (GncOwner *owner)
{
    if (!owner) return;
    std::visit (overloaded{
        [](std::monostate) {},
        [](gpointer) {},
        [](GncCustomer *c) { gncCustomerCommitEdit (c); },
        [](GncJob *j) { gncJobCommitEdit (j); },
        [](GncVendor *v) { gncVendorCommitEdit (v); },
        [](GncEmployee *e) { gncEmployeeCommitEdit (e); },
    }, to_variant (owner));
}

void gncOwnerDestroy (GncOwner *owner)
{
    if (!owner) return;
    std::visit (overloaded{
        [](std::monostate) {},
        [](gpointer) {},
        [](GncCustomer *c) { gncCustomerDestroy (c); },
        [](GncJob *j) { gncJobDestroy (j); },
        [](GncVendor *v) { gncVendorDestroy (v); },
        [](GncEmployee *e) { gncEmployeeDestroy (e); },
    }, to_variant (owner));
}

void gncOwnerInitUndefined (GncOwner *owner, gpointer obj)
{
    if (!owner) return;
    owner->type = GNC_OWNER_UNDEFINED;
    owner->owner.undefined = obj;
}

void gncOwnerInitCustomer (GncOwner *owner, GncCustomer *customer)
{
    if (!owner) return;
    owner->type = GNC_OWNER_CUSTOMER;
    owner->owner.customer = customer;
}

void gncOwnerInitJob (GncOwner *owner, GncJob *job)
{
    if (!owner) return;
    owner->type = GNC_OWNER_JOB;
    owner->owner.job = job;
}

void gncOwnerInitVendor (GncOwner *owner, GncVendor *vendor)
{
    if (!owner) return;
    owner->type = GNC_OWNER_VENDOR;
    owner->owner.vendor = vendor;
}

void gncOwnerInitEmployee (GncOwner *owner, GncEmployee *employee)
{
    if (!owner) return;
    owner->type = GNC_OWNER_EMPLOYEE;
    owner->owner.employee = employee;
}

GncOwnerType gncOwnerGetType (const GncOwner *owner)
{
    if (!owner) return GNC_OWNER_NONE;
    return owner->type;
}

const char * gncOwnerGetTypeString (const GncOwner *owner)
{
    return std::visit (overloaded{
        [](std::monostate) { return N_("None"); },
        [](gpointer) { return N_("Undefined"); },
        [](GncCustomer*) { return N_("Customer"); },
        [](GncJob*) { return N_("Job"); },
        [](GncVendor*) { return N_("Vendor"); },
        [](GncEmployee*) { return N_("Employee"); },
    }, to_variant (owner));
}

QofIdTypeConst
qofOwnerGetType(const GncOwner *owner)
{
    return gncOwnerTypeToQofIdType(owner->type);
}

QofIdTypeConst gncOwnerTypeToQofIdType(GncOwnerType t)
{
    QofIdTypeConst type = NULL;
    switch (t)
    {
    case GNC_OWNER_NONE :
    {
        type = NULL;
        break;
    }
    case GNC_OWNER_UNDEFINED :
    {
        type = NULL;
        break;
    }
    case GNC_OWNER_CUSTOMER :
    {
        type = GNC_ID_CUSTOMER;
        break;
    }
    case GNC_OWNER_JOB :
    {
        type = GNC_ID_JOB;
        break;
    }
    case GNC_OWNER_VENDOR :
    {
        type = GNC_ID_VENDOR;
        break;
    }
    case GNC_OWNER_EMPLOYEE :
    {
        type = GNC_ID_EMPLOYEE;
        break;
    }
    }
    return type;
}

QofInstance*
qofOwnerGetOwner (const GncOwner *owner)
{
    if (!owner)
        return NULL;

    return std::visit (overloaded{
        [](std::monostate) -> QofInstance* { return NULL; },
        [](gpointer) -> QofInstance* { return NULL; },
        [](GncCustomer *c) { return QOF_INSTANCE (c); },
        [](GncJob *j) { return QOF_INSTANCE (j); },
        [](GncVendor *v) { return QOF_INSTANCE (v); },
        [](GncEmployee *e) { return QOF_INSTANCE (e); },
    }, to_variant (owner));
}

void
qofOwnerSetEntity (GncOwner *owner, QofInstance *ent)
{
    if (!owner || !ent)
    {
        return;
    }
    if (0 == g_strcmp0(ent->e_type, GNC_ID_CUSTOMER))
    {
        owner->type = GNC_OWNER_CUSTOMER;
        gncOwnerInitCustomer(owner, (GncCustomer*)ent);
    }
    else if (0 == g_strcmp0(ent->e_type, GNC_ID_JOB))
    {
        owner->type = GNC_OWNER_JOB;
        gncOwnerInitJob(owner, (GncJob*)ent);
    }
    else if (0 == g_strcmp0(ent->e_type, GNC_ID_VENDOR))
    {
        owner->type = GNC_OWNER_VENDOR;
        gncOwnerInitVendor(owner, (GncVendor*)ent);
    }
    else if (0 == g_strcmp0(ent->e_type, GNC_ID_EMPLOYEE))
    {
        owner->type = GNC_OWNER_EMPLOYEE;
        gncOwnerInitEmployee(owner, (GncEmployee*)ent);
    }
    else
    {
        owner->type = GNC_OWNER_NONE;
        owner->owner.undefined = NULL;
    }
}

gboolean GNC_IS_OWNER (QofInstance *ent)
{
    if (!ent)
        return FALSE;

    return (GNC_IS_VENDOR(ent) ||
            GNC_IS_CUSTOMER(ent) ||
            GNC_IS_EMPLOYEE(ent) ||
            GNC_IS_JOB(ent));
}
gpointer gncOwnerGetUndefined (const GncOwner *owner)
{
    if (!owner) return NULL;
    if (owner->type != GNC_OWNER_UNDEFINED) return NULL;
    return owner->owner.undefined;
}

GncCustomer * gncOwnerGetCustomer (const GncOwner *owner)
{
    if (!owner) return NULL;
    if (owner->type != GNC_OWNER_CUSTOMER) return NULL;
    return owner->owner.customer;
}

GncJob * gncOwnerGetJob (const GncOwner *owner)
{
    if (!owner) return NULL;
    if (owner->type != GNC_OWNER_JOB) return NULL;
    return owner->owner.job;
}

GncVendor * gncOwnerGetVendor (const GncOwner *owner)
{
    if (!owner) return NULL;
    if (owner->type != GNC_OWNER_VENDOR) return NULL;
    return owner->owner.vendor;
}

GncEmployee * gncOwnerGetEmployee (const GncOwner *owner)
{
    if (!owner) return NULL;
    if (owner->type != GNC_OWNER_EMPLOYEE) return NULL;
    return owner->owner.employee;
}

gpointer gncOwnerGetObject (const GncOwner *owner)
{
    if (!owner) return NULL;
    return std::visit (overloaded{
        [](std::monostate) -> gpointer { return NULL; },
        [](gpointer p) -> gpointer { return p; },
        [](GncCustomer *c) -> gpointer { return c; },
        [](GncJob *j) -> gpointer { return j; },
        [](GncVendor *v) -> gpointer { return v; },
        [](GncEmployee *e) -> gpointer { return e; },
    }, to_variant (owner));
}

void gncOwnerCopy (const GncOwner *src, GncOwner *dest)
{
    if (!src || !dest) return;
    if (src == dest) return;
    *dest = *src;
}

gboolean gncOwnerEqual (const GncOwner *a, const GncOwner *b)
{
    if (!a || !b) return FALSE;
    if (gncOwnerGetType (a) != gncOwnerGetType (b)) return FALSE;
    return (a->owner.undefined == b->owner.undefined);
}

int gncOwnerGCompareFunc (const GncOwner *a, const GncOwner *b)
{
    if (gncOwnerEqual (a, b))
        return 0;
    else
        return 1;
}

const char * gncOwnerGetID (const GncOwner *owner)
{
    if (!owner) return NULL;
    return std::visit (overloaded{
        [](std::monostate) -> const char* { return NULL; },
        [](gpointer) -> const char* { return NULL; },
        [](GncCustomer *c) { return gncCustomerGetID (c); },
        [](GncJob *j) { return gncJobGetID (j); },
        [](GncVendor *v) { return gncVendorGetID (v); },
        [](GncEmployee *e) { return gncEmployeeGetID (e); },
    }, to_variant (owner));
}

const char * gncOwnerGetName (const GncOwner *owner)
{
    if (!owner) return NULL;
    return std::visit (overloaded{
        [](std::monostate) -> const char* { return NULL; },
        [](gpointer) -> const char* { return NULL; },
        [](GncCustomer *c) { return gncCustomerGetName (c); },
        [](GncJob *j) { return gncJobGetName (j); },
        [](GncVendor *v) { return gncVendorGetName (v); },
        [](GncEmployee *e) { return gncEmployeeGetName (e); },
    }, to_variant (owner));
}

GncAddress * gncOwnerGetAddr (const GncOwner *owner)
{
    if (!owner) return NULL;
    return std::visit (overloaded{
        [](std::monostate) -> GncAddress* { return NULL; },
        [](gpointer) -> GncAddress* { return NULL; },
        [](GncCustomer *c) { return gncCustomerGetAddr (c); },
        [](GncJob*) -> GncAddress* { return NULL; },
        [](GncVendor *v) { return gncVendorGetAddr (v); },
        [](GncEmployee *e) { return gncEmployeeGetAddr (e); },
    }, to_variant (owner));
}

gnc_commodity * gncOwnerGetCurrency (const GncOwner *owner)
{
    if (!owner) return NULL;
    return std::visit (overloaded{
        [](std::monostate) -> gnc_commodity* { return NULL; },
        [](gpointer) -> gnc_commodity* { return NULL; },
        [](GncCustomer *c) { return gncCustomerGetCurrency (c); },
        [](GncJob *j) { return gncOwnerGetCurrency (gncJobGetOwner (j)); },
        [](GncVendor *v) { return gncVendorGetCurrency (v); },
        [](GncEmployee *e) { return gncEmployeeGetCurrency (e); },
    }, to_variant (owner));
}

gboolean gncOwnerGetActive (const GncOwner *owner)
{
    if (!owner) return FALSE;
    return std::visit (overloaded{
        [](std::monostate) -> gboolean { return FALSE; },
        [](gpointer) -> gboolean { return FALSE; },
        [](GncCustomer *c) { return gncCustomerGetActive (c); },
        [](GncJob *j) { return gncJobGetActive (j); },
        [](GncVendor *v) { return gncVendorGetActive (v); },
        [](GncEmployee *e) { return gncEmployeeGetActive (e); },
    }, to_variant (owner));
}

const GncGUID * gncOwnerGetGUID (const GncOwner *owner)
{
    if (!owner) return NULL;

    return std::visit (overloaded{
        [](std::monostate) -> const GncGUID* { return NULL; },
        [](gpointer) -> const GncGUID* { return NULL; },
        [](GncCustomer *c) { return qof_instance_get_guid (QOF_INSTANCE (c)); },
        [](GncJob *j) { return qof_instance_get_guid (QOF_INSTANCE (j)); },
        [](GncVendor *v) { return qof_instance_get_guid (QOF_INSTANCE (v)); },
        [](GncEmployee *e) { return qof_instance_get_guid (QOF_INSTANCE (e)); },
    }, to_variant (owner));
}

void
gncOwnerSetActive (const GncOwner *owner, gboolean active)
{
    if (!owner) return;
    std::visit (overloaded{
        [](std::monostate) {},
        [](gpointer) {},
        [active](GncCustomer *c) { gncCustomerSetActive (c, active); },
        [active](GncJob *j) { gncJobSetActive (j, active); },
        [active](GncVendor *v) { gncVendorSetActive (v, active); },
        [active](GncEmployee *e) { gncEmployeeSetActive (e, active); },
    }, to_variant (owner));
}

GncGUID gncOwnerRetGUID (GncOwner *owner)
{
    const GncGUID *guid = gncOwnerGetGUID (owner);
    if (guid)
        return *guid;
    return *guid_null ();
}

const GncOwner * gncOwnerGetEndOwner (const GncOwner *owner)
{
    if (!owner) return NULL;
    return std::visit (overloaded{
        [](std::monostate) -> const GncOwner* { return NULL; },
        [](gpointer) -> const GncOwner* { return NULL; },
        [owner](GncCustomer*) -> const GncOwner* { return owner; },
        [owner](GncVendor*) -> const GncOwner* { return owner; },
        [owner](GncEmployee*) -> const GncOwner* { return owner; },
        [](GncJob *j) -> const GncOwner* { return gncJobGetOwner (j); },
    }, to_variant (owner));
}

int gncOwnerCompare (const GncOwner *a, const GncOwner *b)
{
    if (!a && !b) return 0;
    if (!a && b) return 1;
    if (a && !b) return -1;

    if (a->type != b->type)
        return (a->type - b->type);

    /* Types match, so `b`'s variant necessarily holds the same
     * alternative as `a`'s -- std::get is safe here. */
    return std::visit (overloaded{
        [](std::monostate) { return 0; },
        [](gpointer) { return 0; },
        [&b](GncCustomer *ac) { return gncCustomerCompare (ac, std::get<GncCustomer*> (to_variant (b))); },
        [&b](GncJob *aj) { return gncJobCompare (aj, std::get<GncJob*> (to_variant (b))); },
        [&b](GncVendor *av) { return gncVendorCompare (av, std::get<GncVendor*> (to_variant (b))); },
        [&b](GncEmployee *ae) { return gncEmployeeCompare (ae, std::get<GncEmployee*> (to_variant (b))); },
    }, to_variant (a));
}

const GncGUID * gncOwnerGetEndGUID (const GncOwner *owner)
{
    if (!owner) return NULL;
    return gncOwnerGetGUID (gncOwnerGetEndOwner (owner));
}

void gncOwnerAttachToLot (const GncOwner *owner, GNCLot *lot)
{
     if (!owner || !lot)
        return;

    gnc_lot_begin_edit (lot);

    qof_instance_set (QOF_INSTANCE (lot),
		      GNC_OWNER_TYPE, (gint64)gncOwnerGetType (owner),
		      GNC_OWNER_GUID, gncOwnerGetGUID (owner),
		      NULL);
    gnc_lot_commit_edit (lot);
}

gboolean gncOwnerGetOwnerFromLot (GNCLot *lot, GncOwner *owner)
{
    GncGUID *guid = NULL;
    QofBook *book;
    GncOwnerType type = GNC_OWNER_NONE;
    guint64 type64 = 0;

    if (!lot || !owner) return FALSE;

    book = gnc_lot_get_book (lot);
    qof_instance_get (QOF_INSTANCE (lot),
		      GNC_OWNER_TYPE, &type64,
		      GNC_OWNER_GUID, &guid,
		      NULL);
    type = (GncOwnerType) type64;
    switch (type)
    {
    case GNC_OWNER_CUSTOMER:
        gncOwnerInitCustomer (owner, gncCustomerLookup (book, guid));
        break;
    case GNC_OWNER_VENDOR:
        gncOwnerInitVendor (owner, gncVendorLookup (book, guid));
        break;
    case GNC_OWNER_EMPLOYEE:
        gncOwnerInitEmployee (owner, gncEmployeeLookup (book, guid));
        break;
    case GNC_OWNER_JOB:
        gncOwnerInitJob (owner, gncJobLookup (book, guid));
        break;
    default:
        guid_free (guid);
        return FALSE;
    }

    guid_free (guid);
    return (owner->owner.undefined != NULL);
}

gboolean gncOwnerGetOwnerFromTxn (Transaction *txn, GncOwner *owner)
{
    Split *apar_split = NULL;

    if (!txn || !owner) return FALSE;

    if (xaccTransGetTxnType (txn) == TXN_TYPE_NONE)
        return FALSE;

    apar_split = xaccTransGetFirstAPARAcctSplit (txn, TRUE);
    if (apar_split)
    {
        GNCLot *lot = xaccSplitGetLot (apar_split);
        GncInvoice *invoice = gncInvoiceGetInvoiceFromLot (lot);
        if (invoice)
            gncOwnerCopy (gncInvoiceGetOwner (invoice), owner);
        else if (!gncOwnerGetOwnerFromLot (lot, owner))
                return FALSE;

        return TRUE; // Got owner from either invoice or lot
    }

    return FALSE;
}

gboolean gncOwnerIsValid (const GncOwner *owner)
{
    if (!owner) return FALSE;
    return (owner->owner.undefined != NULL);
}

gboolean
gncOwnerLotMatchOwnerFunc (GNCLot *lot, gpointer user_data)
{
    const GncOwner *req_owner = static_cast<const GncOwner*> (user_data);
    GncOwner lot_owner;
    const GncOwner *end_owner;
    GncInvoice *invoice = gncInvoiceGetInvoiceFromLot (lot);

    /* Determine the owner associated to the lot */
    if (invoice)
        /* Invoice lots */
        end_owner = gncOwnerGetEndOwner (gncInvoiceGetOwner (invoice));
    else if (gncOwnerGetOwnerFromLot (lot, &lot_owner))
        /* Pre-payment lots */
        end_owner = gncOwnerGetEndOwner (&lot_owner);
    else
        return FALSE;

    /* Is this a lot for the requested owner ? */
    return gncOwnerEqual (end_owner, req_owner);
}

gint
gncOwnerLotsSortFunc (GNCLot *lotA, GNCLot *lotB)
{
    GncInvoice *ia, *ib;
    time64 da, db;

    ia = gncInvoiceGetInvoiceFromLot (lotA);
    ib = gncInvoiceGetInvoiceFromLot (lotB);

    if (ia)
        da = gncInvoiceGetDateDue (ia);
    else
        da = xaccTransRetDatePosted (xaccSplitGetParent (gnc_lot_get_earliest_split (lotA)));

    if (ib)
        db = gncInvoiceGetDateDue (ib);
    else
        db = xaccTransRetDatePosted (xaccSplitGetParent (gnc_lot_get_earliest_split (lotB)));

    return (da > db) - (da < db);
}

GNCLot *
gncOwnerCreatePaymentLotSecs (const GncOwner *owner, Transaction **preset_txn,
                              Account *posted_acc, Account *xfer_acc,
                              gnc_numeric amount, gnc_numeric exch, time64 date,
                              const char *memo, const char *num)
{
    QofBook *book;
    Split *split;
    const char *name;
    gnc_commodity *post_comm, *xfer_comm;
    Split *xfer_split = NULL;
    Transaction *txn = NULL;
    GNCLot *payment_lot;
    gnc_numeric xfer_amount = gnc_numeric_zero();
    gnc_numeric txn_value = gnc_numeric_zero();

    /* Verify our arguments */
    if (!owner || !posted_acc || !xfer_acc) return NULL;
    g_return_val_if_fail (owner->owner.undefined != NULL, NULL);

    /* Compute the ancillary data */
    book = gnc_account_get_book (posted_acc);
    name = gncOwnerGetName (gncOwnerGetEndOwner ((GncOwner*)owner));
    post_comm = xaccAccountGetCommodity (posted_acc);
    xfer_comm = xaccAccountGetCommodity (xfer_acc);

//    reverse = use_reversed_payment_amounts(owner);

    if (preset_txn && *preset_txn)
        txn = *preset_txn;

    if (txn)
    {
        int i = 0;

        /* Pre-existing transaction was specified. We completely clear it,
         * except for a pre-existing transfer split. We're very conservative
         * in preserving that one as it may have been reconciled already. */
        xfer_split = xaccTransFindSplitByAccount(txn, xfer_acc);
        xaccTransBeginEdit (txn);
        while (i < xaccTransCountSplits(txn))
        {
            Split *split = xaccTransGetSplit (txn, i);
            if (split == xfer_split)
                ++i;
            else
                xaccSplitDestroy(split);
        }
        /* Note: don't commit transaction now - that would insert an imbalance split.*/
    }
    else
    {
        txn = xaccMallocTransaction (book);
        xaccTransBeginEdit (txn);
    }

    /* Complete transaction setup */
    xaccTransSetDescription (txn, name ? name : "");
    if (!gnc_commodity_equal(xaccTransGetCurrency (txn), post_comm) &&
        !gnc_commodity_equal (xaccTransGetCurrency (txn), xfer_comm))
        xaccTransSetCurrency (txn, xfer_comm);

    /* With all commodities involved known, define split amounts and txn value.
     * - post amount (amount passed in as parameter) is always in post_acct commodity,
     * - xfer amount requires conversion if the xfer account has a different
     *   commodity than the post account.
     * - txn value requires conversion if the post account has a different
     *   commodity than the transaction */
    if (gnc_commodity_equal(post_comm, xfer_comm))
        xfer_amount = amount;
    else
        xfer_amount = gnc_numeric_mul (amount, exch, GNC_DENOM_AUTO,
                                       GNC_HOW_RND_ROUND_HALF_UP);

    if (gnc_commodity_equal(post_comm, xaccTransGetCurrency (txn)))
        txn_value = amount;
    else
        txn_value = gnc_numeric_mul (amount, exch, GNC_DENOM_AUTO,
                                      GNC_HOW_RND_ROUND_HALF_UP);

    /* Insert a split for the transfer account if we don't have one yet */
    if (!xfer_split)
    {
        /* The split for the transfer account */
        xfer_split = xaccMallocSplit (book);
        xaccSplitSetMemo (xfer_split, memo);
        /* set per book option */
        gnc_set_num_action (NULL, xfer_split, num, _("Payment"));
        xaccAccountBeginEdit (xfer_acc);
        xaccAccountInsertSplit (xfer_acc, xfer_split);
        xaccAccountCommitEdit (xfer_acc);
        xaccTransAppendSplit (txn, xfer_split);

        xaccSplitSetAmount(xfer_split, xfer_amount); /* Payment in xfer account currency */
        xaccSplitSetValue(xfer_split, txn_value); /* Payment in transaction currency */
    }
    /* For a pre-existing xfer split, let's check if the amount and value
     * have changed. If so, update them and unreconcile. */
    else if (!gnc_numeric_equal (xaccSplitGetAmount (xfer_split), xfer_amount) ||
             !gnc_numeric_equal (xaccSplitGetValue (xfer_split), txn_value))
    {
        xaccSplitSetAmount (xfer_split, xfer_amount);
        xaccSplitSetValue (xfer_split, txn_value);
        xaccSplitSetReconcile (xfer_split, NREC);
    }

    /* Add a split in the post account */
    split = xaccMallocSplit (book);
    xaccSplitSetMemo (split, memo);
    /* set per book option */
    xaccSplitSetAction (split, _("Payment"));
    xaccAccountBeginEdit (posted_acc);
    xaccAccountInsertSplit (posted_acc, split);
    xaccAccountCommitEdit (posted_acc);
    xaccTransAppendSplit (txn, split);
    xaccSplitSetAmount (split, gnc_numeric_neg (amount));
    xaccSplitSetValue (split, gnc_numeric_neg (txn_value));

    /* Create a new lot for the payment */
    payment_lot = gnc_lot_new (book);
    gncOwnerAttachToLot (owner, payment_lot);
    gnc_lot_add_split (payment_lot, split);

    /* Mark the transaction as a payment */
    xaccTransSetNum (txn, num);
    xaccTransSetTxnType (txn, TXN_TYPE_PAYMENT);

    /* Set date for transaction */
    xaccTransSetDateEnteredSecs (txn, gnc_time (NULL));
    xaccTransSetDatePostedSecs (txn, date);

    /* Commit this new transaction */
    xaccTransCommitEdit (txn);
    if (preset_txn)
        *preset_txn = txn;

    return payment_lot;
}

typedef enum
{
    is_equal     = 8,
    is_more      = 4,
    is_less      = 2,
    is_pay_split = 1
} split_flags;

Split *gncOwnerFindOffsettingSplit (GNCLot *lot, gnc_numeric target_amount)
{
    SplitList *ls_iter = NULL;
    Split *best_split = NULL;
    gnc_numeric best_amt = { 0, 1};
    gint best_flags = 0;

    if (!lot)
        return NULL;

    for (ls_iter = gnc_lot_get_split_list (lot); ls_iter; ls_iter = ls_iter->next)
    {
        Split *split = static_cast<Split*> (ls_iter->data);

        if (!split)
            continue;


        Transaction *txn = xaccSplitGetParent (split);
        if (!txn)
        {
            // Ooops - the split doesn't belong to any transaction !
            // This is not expected so issue a warning and continue with next split
            PWARN("Encountered a split in a payment lot that's not part of any transaction. "
                  "This is unexpected! Skipping split %p.", split);
            continue;
        }

        // Check if this split has the opposite sign of the target amount we want to offset
        gnc_numeric split_amount = xaccSplitGetAmount (split);
        if (gnc_numeric_positive_p (target_amount) == gnc_numeric_positive_p (split_amount))
            continue;

        // Ok we have found a split that potentially can offset the target value
        // Let's see if it's better than what we have found already.
        gint amt_cmp = gnc_numeric_compare (gnc_numeric_abs (split_amount),
                                            gnc_numeric_abs (target_amount));
        gint new_flags = 0;
        if (amt_cmp == 0)
            new_flags += is_equal;
        else if (amt_cmp > 0)
            new_flags += is_more;
        else
            new_flags += is_less;

        if (xaccTransGetTxnType (txn) != TXN_TYPE_LINK)
            new_flags += is_pay_split;

        if ((new_flags >= best_flags) &&
            (gnc_numeric_compare (gnc_numeric_abs (split_amount),
                                  gnc_numeric_abs (best_amt)) > 0))
        {
            // The new split is a better match than what we found so far
            best_split = split;
            best_flags = new_flags;
            best_amt   = split_amount;
        }
    }

    return best_split;
}

gboolean
gncOwnerReduceSplitTo (Split *split, gnc_numeric target_amount)
{
    gnc_numeric split_amt = xaccSplitGetAmount (split);
    if (gnc_numeric_positive_p (split_amt) != gnc_numeric_positive_p (target_amount))
        return FALSE; // Split and target amount have to be of the same sign

    if (gnc_numeric_equal (split_amt, target_amount))
        return FALSE; // Split already has the target amount

    if (gnc_numeric_zero_p (split_amt))
        return FALSE; // We can't reduce a split that already has zero amount

    Transaction *txn = xaccSplitGetParent (split);
    xaccTransBeginEdit (txn);

    /* Calculate new value for reduced split. This can be different from
     * the reduced split's new amount (when account and transaction
     * commodity differ) */
    gnc_numeric split_val = xaccSplitGetValue (split);
    gnc_numeric exch = gnc_numeric_div (split_val, split_amt,
                                        GNC_DENOM_AUTO, GNC_HOW_DENOM_LCD);

    gint64 txn_comm_fraction = gnc_commodity_get_fraction (xaccTransGetCurrency (txn));
    gnc_numeric target_val = gnc_numeric_mul (target_amount, exch,
                                              txn_comm_fraction,
                                              GNC_HOW_RND_ROUND_HALF_UP);
    xaccSplitSetAmount (split, target_amount);
    xaccSplitSetValue (split, target_val);

    /* Calculate amount and value for remainder split.
     * Note we calculate the remaining value by subtracting the new target value
     * from the original split's value rather than multiplying the remaining
     * amount with the exchange rate to avoid imbalances due to rounding errors. */
    gnc_numeric rem_amt = gnc_numeric_sub (split_amt, target_amount, GNC_DENOM_AUTO, GNC_HOW_DENOM_FIXED);
    gnc_numeric rem_val = gnc_numeric_sub (split_val, target_val, GNC_DENOM_AUTO, GNC_HOW_DENOM_FIXED);

    Split *rem_split = xaccMallocSplit (xaccSplitGetBook (split));
    xaccSplitCopyOnto (split, rem_split);
    xaccSplitSetAmount (rem_split, rem_amt);
    xaccSplitSetValue (rem_split, rem_val);
    xaccSplitSetParent (rem_split, txn);

    xaccTransCommitEdit (txn);

    GNCLot *lot = xaccSplitGetLot (split);
    gnc_lot_add_split (lot, rem_split);

    return TRUE;
}

void
gncOwnerSetLotLinkMemo (Transaction *ll_txn)
{
    gchar *memo_prefix = _("Offset between business items: ");
    gchar *new_memo;
    SplitList *lts_iter;
    SplitList *splits = NULL, *siter;
    GList *titles = NULL, *titer;

    if (!ll_txn)
        return;

    if (xaccTransGetTxnType (ll_txn) != TXN_TYPE_LINK)
        return;

    // Find all splits in the lot link transaction that are also in a document lot
    for (lts_iter = xaccTransGetSplitList (ll_txn); lts_iter; lts_iter = lts_iter->next)
    {
        Split *split = static_cast<Split*> (lts_iter->data);
        GNCLot *lot;
        GncInvoice *invoice;
        gchar *title;

        if (!split)
            continue;

        lot = xaccSplitGetLot (split);
        if (!lot)
            continue;

        invoice = gncInvoiceGetInvoiceFromLot (lot);
        if (!invoice)
            continue;

        title = g_strdup_printf ("%s %s", gncInvoiceGetTypeString (invoice), gncInvoiceGetID (invoice));

        titles = g_list_prepend (titles, title);
        splits = g_list_prepend (splits, split); // splits don't need to be sorted
    }

    if (!titles)
        return; // We didn't find document lots

    titles = g_list_sort (titles, (GCompareFunc)g_strcmp0);

    // Create the memo as we'd want it to be
    new_memo = g_strconcat (memo_prefix, titles->data, NULL);
    for (titer = titles->next; titer; titer = titer->next)
    {
        gchar *tmp_memo = g_strconcat (new_memo, " - ", titer->data, NULL);
        g_free (new_memo);
        new_memo = tmp_memo;
    }
    g_list_free_full (titles, g_free);

    // Update the memos of all the splits we found previously (if needed)
    for (siter = splits; siter; siter = siter->next)
    {
        Split *memo_split = static_cast<Split*> (siter->data);
        if (g_strcmp0 (xaccSplitGetMemo (memo_split), new_memo) != 0)
            xaccSplitSetMemo (memo_split, new_memo);
    }

    g_list_free (splits);
    g_free (new_memo);
}

/* Find an existing lot link transaction in the given lot
 * Only use a lot link that already links at least two
 * documents (to avoid perpetuating the lot link proliferation
 * that happened in 2.6.0-2.6.3).
 */
static Transaction *
get_ll_transaction_from_lot (GNCLot *lot)
{
    SplitList *ls_iter;

    /* This should really only be called on a document lot */
    if (!gncInvoiceGetInvoiceFromLot (lot))
        return NULL;

    /* The given lot is a valid document lot. Now iterate over all
     * other lot links in this lot to find one more document lot.
     */
    for (ls_iter = gnc_lot_get_split_list (lot); ls_iter; ls_iter = ls_iter->next)
    {
        Split *ls = static_cast<Split*> (ls_iter->data);
        Transaction *ll_txn = xaccSplitGetParent (ls);
        SplitList *ts_iter;

        if (xaccTransGetTxnType (ll_txn) != TXN_TYPE_LINK)
            continue;

        for (ts_iter = xaccTransGetSplitList (ll_txn); ts_iter; ts_iter = ts_iter->next)
        {
            Split *ts = static_cast<Split*> (ts_iter->data);
            GNCLot *tslot = xaccSplitGetLot (ts);

            if (!tslot)
                continue;

            if (tslot == lot)
                continue;

            if (gncInvoiceGetInvoiceFromLot (lot))
                return ll_txn; /* Got one more document lot - mission accomplished */
        }
    }

    /* The lot doesn't have an ll_txn with the requested criteria... */
    return NULL;
}

static void
gncOwnerCreateLotLink (GNCLot *from_lot, GNCLot *to_lot, const GncOwner *owner)
{
    const gchar *action = _("Lot Link");
    Account *acct = gnc_lot_get_account (from_lot);
    const gchar *name = gncOwnerGetName (gncOwnerGetEndOwner (owner));
    Transaction *ll_txn = NULL;
    gnc_numeric from_lot_bal, to_lot_bal;
    time64 from_time, to_time;
    time64 time_posted;
    Split *split;

    /* Sanity check */
    if (!gncInvoiceGetInvoiceFromLot (from_lot) ||
        !gncInvoiceGetInvoiceFromLot (to_lot))
        return;

    /* Determine transaction date based on lot splits */
    from_time = xaccTransRetDatePosted (xaccSplitGetParent (gnc_lot_get_latest_split (from_lot)));
    to_time   = xaccTransRetDatePosted (xaccSplitGetParent (gnc_lot_get_latest_split (to_lot)));
    if (from_time >= to_time)
        time_posted = from_time;
    else
        time_posted = to_time;

    /* Figure out how much we can offset between the lots */
    from_lot_bal = gnc_lot_get_balance (from_lot);
    to_lot_bal = gnc_lot_get_balance (to_lot);
    if (gnc_numeric_compare (gnc_numeric_abs (from_lot_bal),
                             gnc_numeric_abs (to_lot_bal)) > 0)
        from_lot_bal = gnc_numeric_neg (to_lot_bal);
    else
        to_lot_bal = gnc_numeric_neg (from_lot_bal);

    xaccAccountBeginEdit (acct);

    /* Look for a pre-existing lot link we can extend */
    ll_txn = get_ll_transaction_from_lot (from_lot);

    if (!ll_txn)
        ll_txn = get_ll_transaction_from_lot (to_lot);

    if (!ll_txn)
    {
        /* No pre-existing lot link. Create one. */
        ll_txn = xaccMallocTransaction (gnc_lot_get_book (from_lot));
        xaccTransBeginEdit (ll_txn);
        xaccTransSetDescription (ll_txn, name ? name : "(Unknown)");
        xaccTransSetCurrency (ll_txn, xaccAccountGetCommodity(acct));
        xaccTransSetDateEnteredSecs (ll_txn, gnc_time (NULL));
        xaccTransSetDatePostedSecs (ll_txn, time_posted);
        xaccTransSetTxnType (ll_txn, TXN_TYPE_LINK);
    }
    else
    {
        time64 time = xaccTransRetDatePosted (ll_txn);
        xaccTransBeginEdit (ll_txn);

        /* Maybe we need to update the post date of the transaction ? */
        if (time_posted > time)
            xaccTransSetDatePostedSecs (ll_txn, time_posted);
    }

    /* Create a split for the from_lot */
    split = xaccMallocSplit (gnc_lot_get_book (from_lot));
    /* set Action using utility function */
    gnc_set_num_action (NULL, split, NULL, action);
    xaccAccountInsertSplit (acct, split);
    xaccTransAppendSplit (ll_txn, split);
    /* To offset the lot balance, the split must be of the opposite sign */
    xaccSplitSetBaseValue (split, gnc_numeric_neg (from_lot_bal), xaccAccountGetCommodity(acct));
    gnc_lot_add_split (from_lot, split);

    /* Create a split for the to_lot */
    split = xaccMallocSplit (gnc_lot_get_book (to_lot));
    /* set Action using utility function */
    gnc_set_num_action (NULL, split, NULL, action);
    xaccAccountInsertSplit (acct, split);
    xaccTransAppendSplit (ll_txn, split);
    /* To offset the lot balance, the split must be of the opposite sign */
    xaccSplitSetBaseValue (split, gnc_numeric_neg (to_lot_bal), xaccAccountGetCommodity(acct));
    gnc_lot_add_split (to_lot, split);

    xaccTransCommitEdit (ll_txn);


    /* Do some post-cleaning on the lots
     * The above actions may have created splits that are
     * in the same transaction and lot. These can be merged.
     */
    xaccScrubMergeLotSubSplits (to_lot, FALSE);
    xaccScrubMergeLotSubSplits (from_lot, FALSE);
    /* And finally set the same memo for all remaining splits
     * It's a convenience for the users to identify all documents
     * involved in the link.
     */
    gncOwnerSetLotLinkMemo (ll_txn);
    xaccAccountCommitEdit (acct);
}

static void gncOwnerOffsetLots (GNCLot *from_lot, GNCLot *to_lot, const GncOwner *owner)
{
    gnc_numeric target_offset;
    Split *split;

    /* from lot should not be a document lot because we're removing a split from there ! */
    if (gncInvoiceGetInvoiceFromLot (from_lot))
    {
        PWARN ("from_lot %p is a document lot. That is not allowed in gncOwnerOffsetLots", from_lot);
        return;
    }

    /* Get best matching split from from_lot to offset to_lot */
    target_offset = gnc_lot_get_balance (to_lot);
    if (gnc_numeric_zero_p (target_offset))
        return; // to_lot is already balanced, nothing more to do

    split = gncOwnerFindOffsettingSplit (from_lot, target_offset);
    if (!split)
        return; // No suitable offsetting split found, nothing more to do

    /* If the offsetting split is bigger than the amount needed to balance
     * to_lot, reduce the split so its reduced amount closes to_lot exactly.
     * Note the negation in the reduction function. The split must be of
     * opposite sign of to_lot's balance in order to be able to close it.
     */
    if (gnc_numeric_compare (gnc_numeric_abs (xaccSplitGetAmount (split)),
                             gnc_numeric_abs (target_offset)) > 0)
        gncOwnerReduceSplitTo (split, gnc_numeric_neg (target_offset));

    /* Move the reduced split from from_lot to to_lot */
    gnc_lot_add_split (to_lot, split);

}

void gncOwnerAutoApplyPaymentsWithLots (const GncOwner *owner, GList *lots)
{
    GList *left_iter;

    /* General note: in the code below the term "payment" can
     * both mean a true payment or a document of
     * the opposite sign (invoice vs credit note) relative to
     * the lot being processed. In general this function will
     * perform a balancing action on a set of lots, so you
     * will also find frequent references to balancing instead. */

    /* Payments can only be applied when at least an owner
     * and a list of lots to use are given */
    if (!owner) return;
    if (!lots) return;

    for (left_iter = lots; left_iter; left_iter = left_iter->next)
    {
        GNCLot *left_lot = static_cast<GNCLot*> (left_iter->data);
        gnc_numeric left_lot_bal;
        gboolean left_lot_has_doc;
        gboolean left_modified = FALSE;
        Account *acct;
        GList *right_iter;

        /* Only attempt to apply payments to open lots.
         * Note that due to the iterative nature of this function lots
         * in the list may become empty/closed before they are evaluated as
         * base lot, so we should check this for each lot. */
        if (!left_lot)
            continue;
        if (gnc_lot_count_splits (left_lot) == 0)
        {
            gnc_lot_destroy (left_lot);
            left_iter->data = NULL;
            continue;
        }
        if (gnc_lot_is_closed (left_lot))
            continue;

        acct = gnc_lot_get_account (left_lot);
        xaccAccountBeginEdit (acct);

        left_lot_bal = gnc_lot_get_balance (left_lot);
        left_lot_has_doc = (gncInvoiceGetInvoiceFromLot (left_lot) != NULL);

        /* Attempt to offset left_lot with any of the remaining lots. To do so
         * iterate over the remaining lots adding lot links or moving payments
         * around.
         */
        for (right_iter = left_iter->next; right_iter; right_iter = right_iter->next)
        {
            GNCLot *right_lot = static_cast<GNCLot*> (right_iter->data);
            gnc_numeric right_lot_bal;
            gboolean right_lot_has_doc;

            /* Only attempt to use open lots to balance the base lot.
             * Note that due to the iterative nature of this function lots
             * in the list may become empty/closed before they are evaluated as
             * base lot, so we should check this for each lot. */
            if (!right_lot)
                continue;
            if (gnc_lot_count_splits (right_lot) == 0)
            {
                gnc_lot_destroy (right_lot);
                right_iter->data = NULL;
                continue;
            }
            if (gnc_lot_is_closed (right_lot))
                continue;

            /* Balancing transactions for invoice/payments can only happen
             * in the same account. */
            if (acct != gnc_lot_get_account (right_lot))
                continue;


            /* Only attempt to balance if the base lot and balancing lot are
             * of the opposite sign. (Otherwise we would increase the balance
             * of the lot - Duh */
            right_lot_bal = gnc_lot_get_balance (right_lot);
            if (gnc_numeric_positive_p (left_lot_bal) == gnc_numeric_positive_p (right_lot_bal))
                continue;

            /* Ok we found two lots than can (partly) offset each other.
             * Depending on the lot types, a different action is needed to accomplish this.
             * 1. Both lots are document lots (invoices/credit notes)
             *    -> Create a lot linking transaction between the lots
             * 2. Both lots are payment lots (lots without a document attached)
             *    -> Use part of the bigger lot to the close the smaller lot
             * 3. One document lot with one payment lot
             *    -> Use (part of) the payment to offset (part of) the document lot,
             *       Which one will be closed depends on which is the bigger one
             */
            right_lot_has_doc = (gncInvoiceGetInvoiceFromLot (right_lot) != NULL);
            if (left_lot_has_doc && right_lot_has_doc)
                gncOwnerCreateLotLink (left_lot, right_lot, owner);
            else if (!left_lot_has_doc && !right_lot_has_doc)
            {
                gint cmp = gnc_numeric_compare (gnc_numeric_abs (left_lot_bal),
                                                gnc_numeric_abs (right_lot_bal));
                if (cmp >= 0)
                    gncOwnerOffsetLots (left_lot, right_lot, owner);
                else
                    gncOwnerOffsetLots (right_lot, left_lot, owner);
            }
            else
            {
                GNCLot *doc_lot = left_lot_has_doc ? left_lot : right_lot;
                GNCLot *pay_lot = left_lot_has_doc ? right_lot : left_lot;
                // Ok, let's try to move a payment from pay_lot to doc_lot
                gncOwnerOffsetLots (pay_lot, doc_lot, owner);
            }

            /* If we get here, then right_lot was modified
             * If the lot has a document, send an event for send an event for it as well
             * so it gets potentially updated as paid */

            {
                GncInvoice *this_invoice = gncInvoiceGetInvoiceFromLot(right_lot);
                if (this_invoice)
                    qof_event_gen (QOF_INSTANCE(this_invoice), QOF_EVENT_MODIFY, NULL);
            }
            left_modified = TRUE;
        }

        /* If left_lot was modified and the lot has a document,
         * send an event for send an event for it as well
         * so it gets potentially updated as paid */
        if (left_modified)
        {
            GncInvoice *this_invoice = gncInvoiceGetInvoiceFromLot(left_lot);
            if (this_invoice)
                qof_event_gen (QOF_INSTANCE(this_invoice), QOF_EVENT_MODIFY, NULL);
        }
        xaccAccountCommitEdit (acct);

    }
}

/*
 * Create a payment of "amount" for the owner and match it with
 * the set of lots passed in.
 * If
 * - no lots were given
 * - auto_pay is true
 * then all open lots for the owner are considered.
 */
void
gncOwnerApplyPaymentSecs (const GncOwner *owner, Transaction **preset_txn,
                          GList *lots, Account *posted_acc, Account *xfer_acc,
                          gnc_numeric amount, gnc_numeric exch, time64 date,
                          const char *memo, const char *num, gboolean auto_pay)
{
    GNCLot *payment_lot = NULL;
    GList *selected_lots = NULL;

    /* Verify our arguments */
    if (!owner || !posted_acc
               || (!xfer_acc && !gnc_numeric_zero_p (amount)) ) return;
    g_return_if_fail (owner->owner.undefined);

    if (lots)
        selected_lots = lots;
    else if (auto_pay)
        selected_lots = xaccAccountFindOpenLots (posted_acc, gncOwnerLotMatchOwnerFunc,
                        (gpointer)owner, NULL);

    /* If there's a real amount to transfer create a lot for this payment */
    if (!gnc_numeric_zero_p (amount))
        payment_lot = gncOwnerCreatePaymentLotSecs (owner, preset_txn,
                                                    posted_acc, xfer_acc,
                                                    amount, exch, date, memo,
                                                    num);

    /* And link the selected lots and the payment lot together as well
     * as possible.  If the payment was bigger than the selected
     * documents/overpayments, only part of the payment will be
     * used. Similarly if more documents were selected than the
     * payment value set, not all documents will be marked as paid. */
    if (payment_lot)
        selected_lots = g_list_prepend (selected_lots, payment_lot);

    gncOwnerAutoApplyPaymentsWithLots (owner, selected_lots);
    g_list_free (selected_lots);
}

GList *
gncOwnerGetAccountTypesList (const GncOwner *owner)
{
    g_return_val_if_fail (owner, NULL);

    switch (gncOwnerGetType (owner))
    {
    case GNC_OWNER_CUSTOMER:
        return (g_list_prepend (NULL, (gpointer)ACCT_TYPE_RECEIVABLE));
    case GNC_OWNER_VENDOR:
    case GNC_OWNER_EMPLOYEE:
        return (g_list_prepend (NULL, (gpointer)ACCT_TYPE_PAYABLE));
        break;
    default:
        return (g_list_prepend (NULL, (gpointer)ACCT_TYPE_NONE));
    }
}

GList *
gncOwnerGetCommoditiesList (const GncOwner *owner)
{
    g_return_val_if_fail (owner, NULL);
    g_return_val_if_fail (gncOwnerGetCurrency(owner), NULL);

    return (g_list_prepend (NULL, gncOwnerGetCurrency(owner)));
}

/*********************************************************************/
/* Owner balance calculation routines                                */

/*
 * Given an owner, extract the open balance from the owner and then
 * convert it to the desired currency.
 */
gnc_numeric
gncOwnerGetBalanceInCurrency (const GncOwner *owner,
                              const gnc_commodity *report_currency)
{
    gnc_numeric balance = gnc_numeric_zero ();
    QofBook *book;
    gnc_commodity *owner_currency;
    GNCPriceDB *pdb;
    const gnc_numeric *cached_balance = NULL;

    g_return_val_if_fail (owner, gnc_numeric_zero ());

    book       = qof_instance_get_book (qofOwnerGetOwner (owner));
    owner_currency = gncOwnerGetCurrency (owner);

    cached_balance = gncOwnerGetCachedBalance (owner);
    if (cached_balance)
        balance = *cached_balance;
    else
    {
        /* No valid cache value found for balance. Let's recalculate */
        GList *acct_list  = gnc_account_get_descendants (gnc_book_get_root_account (book));
        GList *acct_types = gncOwnerGetAccountTypesList (owner);
        GList *acct_node;

        /* For each account */
        for (acct_node = acct_list; acct_node; acct_node = acct_node->next)
        {
            Account *account = static_cast<Account*> (acct_node->data);
            GList *lot_list = NULL, *lot_node;

            /* Check if this account can have lots for the owner, otherwise skip to next */
            if (g_list_index (acct_types, (gpointer)xaccAccountGetType (account))
                    == -1)
                continue;


            if (!gnc_commodity_equal (owner_currency, xaccAccountGetCommodity (account)))
                continue;

            /* Get a list of open lots for this owner and account */
            lot_list = xaccAccountFindOpenLots (account, gncOwnerLotMatchOwnerFunc,
                                                (gpointer)owner, NULL);
            /* For each lot */
            for (lot_node = lot_list; lot_node; lot_node = lot_node->next)
            {
                GNCLot *lot = static_cast<GNCLot*> (lot_node->data);
                gnc_numeric lot_balance = gnc_lot_get_balance (lot);
                GncInvoice *invoice = gncInvoiceGetInvoiceFromLot(lot);
                if (invoice)
                balance = gnc_numeric_add (balance, lot_balance,
                                            gnc_commodity_get_fraction (owner_currency), GNC_HOW_RND_ROUND_HALF_UP);
            }
            g_list_free (lot_list);
        }
        g_list_free (acct_list);
        g_list_free (acct_types);

        gncOwnerSetCachedBalance (owner, &balance);
    }

    pdb = gnc_pricedb_get_db (book);

    if (report_currency)
        balance = gnc_pricedb_convert_balance_latest_price (
                      pdb, balance, owner_currency, report_currency);

    return balance;
}


/* XXX: Yea, this is broken, but it should work fine for Queries.
 * We're single-threaded, right?
 */
static GncOwner *
owner_from_lot (GNCLot *lot)
{
    static GncOwner owner;

    if (!lot) return NULL;
    if (gncOwnerGetOwnerFromLot (lot, &owner))
        return &owner;

    return NULL;
}

static void
reg_lot (void)
{
    static QofParam params[] =
    {
        { OWNER_FROM_LOT, _GNC_MOD_NAME, (QofAccessFunc)owner_from_lot, NULL },
        { NULL },
    };

    qof_class_register (GNC_ID_LOT, NULL, params);
}

gboolean gncOwnerGetOwnerFromTypeGuid (QofBook *book, GncOwner *owner, QofIdType type, GncGUID *guid)
{
    if (!book || !owner || !type || !guid) return FALSE;

    if (0 == g_strcmp0(type, GNC_ID_CUSTOMER))
    {
        GncCustomer *customer = gncCustomerLookup(book, guid);
        gncOwnerInitCustomer(owner, customer);
        return (NULL != customer);
    }
    else if (0 == g_strcmp0(type, GNC_ID_JOB))
    {
        GncJob *job = gncJobLookup(book, guid);
        gncOwnerInitJob(owner, job);
        return (NULL != job);
    }
    else if (0 == g_strcmp0(type, GNC_ID_VENDOR))
    {
        GncVendor *vendor = gncVendorLookup(book, guid);
        gncOwnerInitVendor(owner, vendor);
        return (NULL != vendor);
    }
    else if (0 == g_strcmp0(type, GNC_ID_EMPLOYEE))
    {
        GncEmployee *employee = gncEmployeeLookup(book, guid);
        gncOwnerInitEmployee(owner, employee);
        return (NULL != employee);
    }
    return 0;
}

gboolean gncOwnerRegister (void)
{
    static QofParam params[] =
    {
        { OWNER_TYPE, QOF_TYPE_INT64,      (QofAccessFunc)gncOwnerGetType,          NULL },
        { OWNER_CUSTOMER, GNC_ID_CUSTOMER, (QofAccessFunc)gncOwnerGetCustomer,      NULL },
        { OWNER_JOB, GNC_ID_JOB,           (QofAccessFunc)gncOwnerGetJob,           NULL },
        { OWNER_VENDOR, GNC_ID_VENDOR,     (QofAccessFunc)gncOwnerGetVendor,        NULL },
        { OWNER_EMPLOYEE, GNC_ID_EMPLOYEE, (QofAccessFunc)gncOwnerGetEmployee,      NULL },
        { OWNER_PARENT, GNC_ID_OWNER,      (QofAccessFunc)gncOwnerGetEndOwner,      NULL },
        { OWNER_PARENTG, QOF_TYPE_GUID,    (QofAccessFunc)gncOwnerGetEndGUID,       NULL },
        { OWNER_NAME, QOF_TYPE_STRING,     (QofAccessFunc)gncOwnerGetName, NULL },
        { QOF_PARAM_GUID, QOF_TYPE_GUID,   (QofAccessFunc)gncOwnerGetGUID, NULL },
        { NULL },
    };

    qof_class_register (GNC_ID_OWNER, (QofSortFunc)gncOwnerCompare, params);
    reg_lot ();

    return TRUE;
}

const gnc_numeric*
gncOwnerGetCachedBalance (const GncOwner *owner)
{
    if (!owner) return NULL;

    if (gncOwnerGetType (owner) == GNC_OWNER_CUSTOMER)
        return gncCustomerGetCachedBalance (gncOwnerGetCustomer (owner));
    else if (gncOwnerGetType (owner) == GNC_OWNER_VENDOR)
        return gncVendorGetCachedBalance (gncOwnerGetVendor (owner));
    else if (gncOwnerGetType (owner) == GNC_OWNER_EMPLOYEE)
        return gncEmployeeGetCachedBalance (gncOwnerGetEmployee (owner));

    return NULL;
}

void gncOwnerSetCachedBalance (const GncOwner *owner, const gnc_numeric *new_bal)
{
    if (!owner) return;

    if (gncOwnerGetType (owner) == GNC_OWNER_CUSTOMER)
        gncCustomerSetCachedBalance (gncOwnerGetCustomer (owner), new_bal);
    else if (gncOwnerGetType (owner) == GNC_OWNER_VENDOR)
        gncVendorSetCachedBalance (gncOwnerGetVendor (owner), new_bal);
    else if (gncOwnerGetType (owner) == GNC_OWNER_EMPLOYEE)
        gncEmployeeSetCachedBalance (gncOwnerGetEmployee (owner), new_bal);
}
