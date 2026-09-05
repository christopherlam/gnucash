/********************************************************************\
 * gnc-reconciled-balance.h -- Reconciled balances public interface.   *
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

/** @addtogroup Engine
    @{ */
/** @addtogroup ReconciledBalance Reconciled Balances
    @{ */
/** @file gnc-reconciled-balance.h
 *  @brief A record of what a reconciliation agreed, kept so it can be
 *  checked again
 *
 *  Finishing a reconciliation in GnuCash marks the splits it covered and
 *  stores the statement date, and then throws away the statement's
 *  ending balance -- the one number the user actually agreed with the
 *  bank.  Nothing afterwards can answer "is that statement still
 *  reconciled?".
 *
 *  A reconciled balance is that missing record: account @a A had
 *  reconciled balance @a B as of statement date @a D.  One is written
 *  automatically when a reconciliation finishes, and they can also be
 *  entered by hand for a statement reconciled elsewhere or before this
 *  feature existed.
 *
 *  It enforces nothing.  It marks no split, locks nothing, and never
 *  stops a transaction being entered, edited or deleted.  It is simply
 *  re-evaluated whenever the book changes, and the result is surfaced
 *  passively -- an icon in the account tree, a tooltip naming the
 *  discrepancy.  A broken record is information, not an error.
 *
 *  Semantics:
 *
 *  - The sum runs over the account's splits that are marked reconciled
 *    or frozen, and each split is placed by its own reconcile date
 *    rather than by its posting date.  That is what makes the figure
 *    durable: gnc_reconcile_view_commit() stamps every newly reconciled
 *    split with the statement date, so a cheque written in January but
 *    cleared on the February statement carries February's stamp and
 *    stays out of the January sum.  A January record therefore keeps
 *    holding as reconciling continues, and breaks only when a split
 *    belonging to that statement is later edited, deleted or
 *    un-reconciled -- which is the damage worth catching.
 *
 *  - Voided transactions are excluded.
 *
 *  - The amount is in the account's own commodity and covers that
 *    account alone; sub-account balances are not included.
 *
 *  - The amount is stored with the engine's internal sign.
 *    Presentation layers that show balances with the "reversed balance"
 *    convention (income, credit card, liability, equity ...) must apply
 *    gnc_reverse_balance() themselves on the way in and out, just as the
 *    reconcile dialog does with the statement ending balance.
 *
 *  - The date is stored day-neutral (like a transaction post date) and
 *    the balance is taken at the end of that day.
 */

#ifndef GNC_RECONCILED_BALANCE_H
#define GNC_RECONCILED_BALANCE_H

#include <glib.h>

/** The reconciled balance data. */
typedef struct reconciled_balance_s GncReconciledBalance;
typedef struct _GncReconciledBalanceClass GncReconciledBalanceClass;

#include "qof.h"
#include "Account.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --- type macros --- */
#define GNC_TYPE_RECONCILED_BALANCE            (gnc_reconciled_balance_get_type ())
#define GNC_RECONCILED_BALANCE(o)              \
     (G_TYPE_CHECK_INSTANCE_CAST ((o), GNC_TYPE_RECONCILED_BALANCE, GncReconciledBalance))
#define GNC_RECONCILED_BALANCE_CLASS(k)        \
     (G_TYPE_CHECK_CLASS_CAST((k), GNC_TYPE_RECONCILED_BALANCE, GncReconciledBalanceClass))
#define GNC_IS_RECONCILED_BALANCE(o)           \
     (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNC_TYPE_RECONCILED_BALANCE))
#define GNC_IS_RECONCILED_BALANCE_CLASS(k)     \
     (G_TYPE_CHECK_CLASS_TYPE ((k), GNC_TYPE_RECONCILED_BALANCE))
#define GNC_RECONCILED_BALANCE_GET_CLASS(o)    \
     (G_TYPE_INSTANCE_GET_CLASS ((o), GNC_TYPE_RECONCILED_BALANCE, GncReconciledBalanceClass))

GType gnc_reconciled_balance_get_type (void);

/** The outcome of checking a record against the book. */
typedef enum
{
    /** The record cannot be checked: it has no account, or the account
     *  it referred to has been deleted. */
    GNC_RECONCILED_BALANCE_UNKNOWN,
    /** The account still reconciles to the recorded figure. */
    GNC_RECONCILED_BALANCE_HOLDS,
    /** It no longer does: a split covered by that statement has been
     *  edited, deleted or un-reconciled since. */
    GNC_RECONCILED_BALANCE_BROKEN,
} GncReconciledBalanceStatus;

/** Register the reconciled balance object with the engine.  Called from
 *  cashobjects_register(). */
gboolean gnc_reconciled_balance_register (void);

/** Create a new, empty record in @a book.  The caller is expected to
 *  set an account, a date and an amount. */
GncReconciledBalance *gnc_reconciled_balance_new (QofBook *book);

/** Remove a record from its book and free it. */
void gnc_reconciled_balance_destroy (GncReconciledBalance *ba);

void gnc_reconciled_balance_begin_edit (GncReconciledBalance *ba);
void gnc_reconciled_balance_commit_edit (GncReconciledBalance *ba);

const GncGUID *gnc_reconciled_balance_get_guid (const GncReconciledBalance *ba);

/** The account the record is about, or NULL if it is unset or has
 *  since been deleted. */
Account *gnc_reconciled_balance_get_account (const GncReconciledBalance *ba);
void gnc_reconciled_balance_set_account (GncReconciledBalance *ba, Account *acc);

/** The statement date.  Stored day-neutral; the balance is taken at
 *  the end of that day. */
time64 gnc_reconciled_balance_get_date (const GncReconciledBalance *ba);
void gnc_reconciled_balance_set_date (GncReconciledBalance *ba, time64 date);

/** The recorded balance, in the account's commodity and with the
 *  engine's internal sign.  See the file comment. */
gnc_numeric gnc_reconciled_balance_get_amount (const GncReconciledBalance *ba);
void gnc_reconciled_balance_set_amount (GncReconciledBalance *ba, gnc_numeric amount);

/** A free-text note, e.g. the statement number the figure came from. */
const char *gnc_reconciled_balance_get_notes (const GncReconciledBalance *ba);
void gnc_reconciled_balance_set_notes (GncReconciledBalance *ba, const char *notes);

/** The reconciled balance the book actually has for the account as of
 *  the recorded date.  gnc_numeric_zero() if the account is gone. */
gnc_numeric gnc_reconciled_balance_get_actual (const GncReconciledBalance *ba);

/** actual - recorded.  Zero when the record still holds. */
gnc_numeric gnc_reconciled_balance_get_delta (const GncReconciledBalance *ba);

GncReconciledBalanceStatus gnc_reconciled_balance_get_status (const GncReconciledBalance *ba);

/** Convenience wrapper: TRUE only for GNC_RECONCILED_BALANCE_BROKEN, so
 *  a record whose account has been deleted does not raise an alarm. */
gboolean gnc_reconciled_balance_is_broken (const GncReconciledBalance *ba);

/** The reconciled balance @a acc actually has as of the end of the day
 *  of @a date, by the rule described in the file comment.  Exposed so
 *  that the GUI can propose a figure without duplicating the rule. */
gnc_numeric gnc_reconciled_balance_compute (Account *acc, time64 date);

GncReconciledBalance *gnc_reconciled_balance_lookup (const GncGUID *guid,
                                                   const QofBook *book);

/** All records in the book, oldest date first.  Free the list with
 *  g_list_free(); the records themselves belong to the book. */
GList *gnc_reconciled_balance_get_all (const QofBook *book);

/** The records for one account, oldest date first.  Free the list
 *  with g_list_free(). */
GList *gnc_reconciled_balance_get_for_account (const Account *acc);

/** The broken records in the book, oldest date first.  Free the list
 *  with g_list_free(). */
GList *gnc_reconciled_balance_get_broken (const QofBook *book);

/** The number of broken records in the book.  Each record that has not
 *  been checked since the last change to the book walks its account's
 *  splits, so this is not free after an edit; results are memoised until
 *  the next change. */
guint gnc_reconciled_balance_count_broken (const QofBook *book);

/** The number of records for @a acc that are broken.  Same cost note
 *  as gnc_reconciled_balance_count_broken(). */
guint gnc_reconciled_balance_count_broken_for_account (const Account *acc);

#ifdef __cplusplus
}
#endif

#endif /* GNC_RECONCILED_BALANCE_H */
/** @} */
/** @} */
