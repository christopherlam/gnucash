/********************************************************************\
 * gnc-balance-assertion.h -- Balance assertions public interface.   *
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
/** @addtogroup BalanceAssertion Balance Assertions
    @{ */
/** @file gnc-balance-assertion.h
 *  @brief Assert that an account's balance on a given date is a given amount
 *
 *  A balance assertion records a user's claim that account @a A had
 *  balance @a B at the end of day @a D -- typically copied off a bank
 *  or credit-card statement.  It is deliberately *not* a
 *  reconciliation:
 *
 *  - it does not mark any split as reconciled or cleared,
 *  - it does not lock anything, and
 *  - it never prevents a transaction from being entered or edited.
 *
 *  Instead it is re-evaluated whenever the book changes, and the
 *  result is surfaced passively in the GUI (an icon in the account
 *  tree, a tooltip explaining the discrepancy).  This is the "gentle
 *  enforcement" model: a failing assertion is information, not an
 *  error.
 *
 *  Semantics:
 *
 *  - The asserted amount is in the account's own commodity, and holds
 *    for that account alone -- sub-account balances are not included.
 *    This mirrors the ledger-style balance assertion and keeps the
 *    check free of any dependence on price quotes.
 *
 *  - The amount is stored with the engine's internal sign, exactly as
 *    xaccAccountGetBalanceAsOfDate() reports it.  Presentation layers
 *    that show balances to the user with the "reversed balance"
 *    convention (income, credit card, liability, equity ...) must
 *    apply gnc_reverse_balance() themselves on the way in and out,
 *    just as the reconcile dialog does with the statement ending
 *    balance.
 *
 *  - The date is stored day-neutral (like a transaction post date) and
 *    the balance is taken at the end of that day, so an assertion
 *    dated the 30th includes everything posted on the 30th.
 */

#ifndef GNC_BALANCE_ASSERTION_H
#define GNC_BALANCE_ASSERTION_H

#include <glib.h>

/** The balance assertion data. */
typedef struct balance_assertion_s GncBalanceAssertion;
typedef struct _GncBalanceAssertionClass GncBalanceAssertionClass;

#include "qof.h"
#include "Account.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --- type macros --- */
#define GNC_TYPE_BALANCE_ASSERTION            (gnc_balance_assertion_get_type ())
#define GNC_BALANCE_ASSERTION(o)              \
     (G_TYPE_CHECK_INSTANCE_CAST ((o), GNC_TYPE_BALANCE_ASSERTION, GncBalanceAssertion))
#define GNC_BALANCE_ASSERTION_CLASS(k)        \
     (G_TYPE_CHECK_CLASS_CAST((k), GNC_TYPE_BALANCE_ASSERTION, GncBalanceAssertionClass))
#define GNC_IS_BALANCE_ASSERTION(o)           \
     (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNC_TYPE_BALANCE_ASSERTION))
#define GNC_IS_BALANCE_ASSERTION_CLASS(k)     \
     (G_TYPE_CHECK_CLASS_TYPE ((k), GNC_TYPE_BALANCE_ASSERTION))
#define GNC_BALANCE_ASSERTION_GET_CLASS(o)    \
     (G_TYPE_INSTANCE_GET_CLASS ((o), GNC_TYPE_BALANCE_ASSERTION, GncBalanceAssertionClass))

GType gnc_balance_assertion_get_type (void);

/** The outcome of evaluating an assertion against the book. */
typedef enum
{
    /** The assertion cannot be evaluated: it has no account, or the
     *  account it referred to has been deleted. */
    GNC_BALANCE_ASSERTION_UNKNOWN,
    /** The account's balance on the asserted date matches. */
    GNC_BALANCE_ASSERTION_PASS,
    /** The account's balance on the asserted date does not match. */
    GNC_BALANCE_ASSERTION_FAIL,
} GncBalanceAssertionStatus;

/** Register the balance assertion object with the engine.  Called from
 *  cashobjects_register(). */
gboolean gnc_balance_assertion_register (void);

/** Create a new, empty balance assertion in @a book.  The caller is
 *  expected to set an account, a date and an amount. */
GncBalanceAssertion *gnc_balance_assertion_new (QofBook *book);

/** Remove an assertion from its book and free it. */
void gnc_balance_assertion_destroy (GncBalanceAssertion *ba);

void gnc_balance_assertion_begin_edit (GncBalanceAssertion *ba);
void gnc_balance_assertion_commit_edit (GncBalanceAssertion *ba);

const GncGUID *gnc_balance_assertion_get_guid (const GncBalanceAssertion *ba);

/** The account whose balance is asserted, or NULL if it is unset or
 *  has since been deleted. */
Account *gnc_balance_assertion_get_account (const GncBalanceAssertion *ba);
void gnc_balance_assertion_set_account (GncBalanceAssertion *ba, Account *acc);

/** The date the assertion is made for.  Stored day-neutral; the
 *  balance is taken at the end of that day. */
time64 gnc_balance_assertion_get_date (const GncBalanceAssertion *ba);
void gnc_balance_assertion_set_date (GncBalanceAssertion *ba, time64 date);

/** The asserted balance, in the account's commodity and with the
 *  engine's internal sign.  See the file comment. */
gnc_numeric gnc_balance_assertion_get_amount (const GncBalanceAssertion *ba);
void gnc_balance_assertion_set_amount (GncBalanceAssertion *ba, gnc_numeric amount);

/** A free-text note, e.g. the statement number the figure came from. */
const char *gnc_balance_assertion_get_notes (const GncBalanceAssertion *ba);
void gnc_balance_assertion_set_notes (GncBalanceAssertion *ba, const char *notes);

/** The balance the book actually has for the account at the end of the
 *  asserted date.  Returns gnc_numeric_zero() if the account is gone. */
gnc_numeric gnc_balance_assertion_get_actual (const GncBalanceAssertion *ba);

/** actual - asserted.  Zero when the assertion holds. */
gnc_numeric gnc_balance_assertion_get_delta (const GncBalanceAssertion *ba);

GncBalanceAssertionStatus gnc_balance_assertion_get_status (const GncBalanceAssertion *ba);

/** Convenience wrapper: TRUE only for GNC_BALANCE_ASSERTION_FAIL, so an
 *  assertion whose account has been deleted does not raise an alarm. */
gboolean gnc_balance_assertion_is_failing (const GncBalanceAssertion *ba);

GncBalanceAssertion *gnc_balance_assertion_lookup (const GncGUID *guid,
                                                   const QofBook *book);

/** All assertions in the book, oldest date first.  Free the list with
 *  g_list_free(); the assertions themselves belong to the book. */
GList *gnc_balance_assertion_get_all (const QofBook *book);

/** The assertions for one account, oldest date first.  Free the list
 *  with g_list_free(). */
GList *gnc_balance_assertion_get_for_account (const Account *acc);

/** The failing assertions in the book, oldest date first.  Free the
 *  list with g_list_free(). */
GList *gnc_balance_assertion_get_failing (const QofBook *book);

/** The number of failing assertions in the book.  Cheap enough to call
 *  from a status-bar update. */
guint gnc_balance_assertion_count_failing (const QofBook *book);

/** The number of assertions for @a acc that are failing. */
guint gnc_balance_assertion_count_failing_for_account (const Account *acc);

#ifdef __cplusplus
}
#endif

#endif /* GNC_BALANCE_ASSERTION_H */
/** @} */
/** @} */
