/********************************************************************\
 * gnc-reconciled-balance.h -- Reconciled balance records.           *
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

/** @addtogroup Utils
    @{ */
/** @addtogroup ReconciledBalance Reconciled Balances
    @{ */
/** @file gnc-reconciled-balance.h
 *  @brief A seal on what the book said at a date, checked again later
 *
 *  Finishing a reconciliation tells you the past is right.  Nothing then
 *  watches it.  A transaction dated inside that period can be deleted,
 *  edited, entered late, or arrive twice from a CSV or OFX import, and
 *  the book will not mention it again.
 *
 *  A record seals it: account @a A had balance @a B as of the end of day
 *  @a D, where @a B is what the book itself computed at the moment the
 *  record was made.  One is written automatically when a reconciliation
 *  finishes.  Checking one is a subtraction, and `gnucash-cli --check
 *  reconciled` does it for a whole book.
 *
 *  What it does and does not tell you:
 *
 *  - It detects *any* change to what the account held on or before @a D,
 *    whatever caused it -- the register, an import, a script, the python
 *    bindings, a hand-edited file, a restored backup.  It rests only on
 *    posting dates and amounts, so no code path can slip past it and no
 *    metadata rewrite can falsify it.
 *
 *  - It cannot tell you whether the change was wanted.  A duplicate
 *    import, a back-dated typo and a cheque you wrote in January and
 *    entered in March are the same event to it: a split dated on or
 *    before @a D appeared after the seal was made.  Intent is not in the
 *    data.  So a broken record means "go and look", never "something is
 *    wrong".
 *
 *  - One back-dated transaction breaks every record dated on or after
 *    it, all by the same amount.  That equal delta is itself the
 *    signature of a single late entry, as against the scattered deltas
 *    of real damage.
 *
 *  It enforces nothing.  It marks no split, locks nothing, and never
 *  stops a transaction being entered, edited or deleted.  Nothing in the
 *  GUI reports on it; the check is a separate, deliberate act.
 *
 *  @section storage Where the records are kept
 *
 *  In the book's state file (the .gcm beside the user's preferences),
 *  not in the book:
 *
 *  @code
 *  [3e8c57e826724328a2438b654dadb594]
 *  2026-01-31=798000/100;Reconciled to statement of 31/01/2026, ending 7845.00
 *  @endcode
 *
 *  A group per account guid, an ISO date per record, and the balance in
 *  gnc_numeric's own string form so that it round-trips exactly.  The
 *  amount and the notes are a GKeyFile string list, so GLib handles the
 *  escaping of any ';' inside a note.
 *
 *  This deliberately leaves the data file untouched: no new element, no
 *  new table, and no feature flag, so a book carrying records opens
 *  unchanged in any older GnuCash.  It is also plain text the user can
 *  read, edit or delete without GnuCash's help.
 *
 *  The cost is that the records are local.  They do not travel with the
 *  data file: copy the book to another machine, restore it from a
 *  backup, or rename it, and the records stay behind -- gnc_state_load()
 *  finds the state file by the data file's basename.  An account with no
 *  records is indistinguishable from an account whose records all hold,
 *  so a clean check is never by itself evidence that anything was
 *  sealed.
 *
 *  Other semantics:
 *
 *  - The amount is in the account's own commodity and covers that
 *    account alone; sub-account balances are not included.
 *
 *  - The amount is stored with the engine's internal sign, not the
 *    "reversed balance" convention the GUI displays for income, credit
 *    card, liability and equity accounts.
 *
 *  - The date is stored day-neutral (like a transaction post date) and
 *    the balance is taken at the end of that day.
 */

#ifndef GNC_RECONCILED_BALANCE_H
#define GNC_RECONCILED_BALANCE_H

#include <glib.h>

#include "qof.h"
#include "Account.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** One record.  Owned by the store; borrowed by callers, and invalidated
 *  by any call that writes or by gnc_reconciled_balance_reset_cache(). */
typedef struct GncReconciledBalance GncReconciledBalance;

/** Record that @a account held @a amount at the end of day @a date.  Any
 *  existing record for that account and date is replaced, since the two
 *  together are the record's identity.
 *
 *  The write goes into the in-memory state file; it reaches disk when
 *  gnc_state_save() next runs, which is on saving or closing the book.
 *
 *  @return The stored record. */
GncReconciledBalance *gnc_reconciled_balance_record (Account *account,
                                                     time64 date,
                                                     gnc_numeric amount,
                                                     const char *notes);

/** Forget a record. */
void gnc_reconciled_balance_remove (GncReconciledBalance *rb);

Account *gnc_reconciled_balance_get_account (const GncReconciledBalance *rb);

/** The date the record is for.  Stored day-neutral; the balance is taken
 *  at the end of that day. */
time64 gnc_reconciled_balance_get_date (const GncReconciledBalance *rb);

/** The recorded balance, in the account's commodity and with the
 *  engine's internal sign.  See the file comment. */
gnc_numeric gnc_reconciled_balance_get_amount (const GncReconciledBalance *rb);

/** A free-text note, e.g. the statement the figure came from.  Never
 *  NULL; "" when there is none. */
const char *gnc_reconciled_balance_get_notes (const GncReconciledBalance *rb);

/** The balance the account actually has as of the record's date. */
gnc_numeric gnc_reconciled_balance_get_actual (const GncReconciledBalance *rb);

/** actual - recorded, rounded to the account's smallest unit.  Zero when
 *  the record still holds. */
gnc_numeric gnc_reconciled_balance_get_delta (const GncReconciledBalance *rb);

/** Whether the account no longer holds what the record says. */
gboolean gnc_reconciled_balance_is_broken (const GncReconciledBalance *rb);

/** The balance a record for @a acc on @a date would seal: every split
 *  posted on or before the end of that day, whatever its reconcile
 *  state.  This is what gets recorded, and what a check recomputes. */
gnc_numeric gnc_reconciled_balance_compute (Account *acc, time64 date);

/** An account's records, oldest first.  The caller frees the list, not
 *  the records. */
GList *gnc_reconciled_balance_get_for_account (const Account *acc);

/** Every record in @a book, oldest first, skipping any whose account has
 *  since been deleted.  The caller frees the list, not the records. */
GList *gnc_reconciled_balance_get_all (const QofBook *book);

/** Drop everything read from the state file.  Call after loading a
 *  different book. */
void gnc_reconciled_balance_reset_cache (void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GNC_RECONCILED_BALANCE_H */
/** @} */
/** @} */
