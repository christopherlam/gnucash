/********************************************************************\
 * gnc-reconciled-balance.cpp -- Reconciled balance records.         *
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

#include <config.h>

#include <glib.h>

#include <map>
#include <memory>
#include <string>

#include "Account.h"
#include "Account.hpp"
#include "Split.h"
#include "Transaction.h"
#include "gnc-engine.h"
#include "gnc-hooks.h"
#include "gnc-state.h"
#include "gnc-reconciled-balance.h"

[[maybe_unused]] static QofLogModule log_module = GNC_MOD_GUI;

/* The date is the key within an account's group, so it is written in a
 * form that sorts lexicographically and carries no locale. */
#define DATE_FORMAT "%Y-%m-%d"

/* Owning handles for the strings GLib hands back, so that the parsing
 * below can bail out of a bad entry with a bare continue. */
struct GStrvDelete { void operator() (gchar **p) const { g_strfreev (p); } };
struct GStrDelete  { void operator() (gchar *p)  const { g_free (p); } };

using GStrvPtr = std::unique_ptr<gchar*, GStrvDelete>;
using GStrPtr  = std::unique_ptr<gchar, GStrDelete>;

struct GncReconciledBalance
{
    GncReconciledBalance (Account *acc, time64 d, gnc_numeric amt,
                          std::string n)
        : account {acc}, date {d}, amount {amt}, notes {std::move (n)} {}

    Account *account;
    /* Also the key of the map holding this record; kept here so that a
     * caller with only the record can ask for it. */
    time64 date;
    gnc_numeric amount;
    std::string notes;
};

/* One account's records, by day-neutral date. The container carries the
 * two rules the records obey: they are ordered by date, and one date
 * holds one record -- so re-recording a date replaces rather than
 * duplicates, without anyone having to remember to check.
 *
 * A map also keeps records where they are put. Callers borrow raw
 * pointers into it and hold them across further calls, which a vector's
 * reallocation would invalidate. */
using RecordMap = std::map<time64, GncReconciledBalance>;

/* Every account's records, by account guid. */
static std::map<std::string, RecordMap> s_records;
static bool s_hook_registered = false;

/* ================================================================ */
/* State file access */

static std::string
account_group (const Account *acc)
{
    char guid_str[GUID_ENCODING_LENGTH + 1];

    guid_to_string_buff (xaccAccountGetGUID (acc), guid_str);
    return guid_str;
}

static std::string
date_key (time64 date)
{
    GDate d;

    gnc_gdate_set_time64 (&d, date);

    char buf[32];
    g_date_strftime (buf, sizeof (buf), DATE_FORMAT, &d);
    return buf;
}

static bool
date_from_key (const char *key, time64 *date)
{
    int y, m, d;

    if (!key || sscanf (key, "%4d-%2d-%2d", &y, &m, &d) != 3)
        return false;

    /* Range-check before the cast: GDateMonth only names 1..12. */
    if (m < 1 || m > 12 || !g_date_valid_dmy (d, static_cast<GDateMonth> (m), y))
        return false;

    *date = gnc_dmy2time64_neutral (d, m, y);
    return true;
}

/* A record names its account by pointer, so nothing cached may outlive
 * the account. The state file entry goes with it: a deleted account
 * never comes back under the same guid, so the group would only
 * accumulate. */
static void
account_event_handler (QofInstance *entity, QofEventId event_type, gpointer,
                       gpointer)
{
    if (!entity || !GNC_IS_ACCOUNT (entity) || !(event_type & QOF_EVENT_DESTROY))
        return;

    auto group = account_group (GNC_ACCOUNT (entity));

    s_records.erase (group);

    if (auto keyfile = gnc_state_get_current ())
        g_key_file_remove_group (keyfile, group.c_str(), nullptr);
}

static void
ensure_hooks (void)
{
    if (s_hook_registered)
        return;

    qof_event_register_handler (account_event_handler, nullptr);
    s_hook_registered = true;
}

/* Read one account's group out of the state file. The value is a
 * GKeyFile string list -- amount first, then the notes -- so that GLib
 * escapes any ';' the user types rather than us splitting by hand. */
static RecordMap
load_account (Account *acc)
{
    RecordMap records;
    auto keyfile = gnc_state_get_current ();
    auto group = account_group (acc);

    if (!keyfile || !g_key_file_has_group (keyfile, group.c_str()))
        return records;

    gsize n_keys = 0;
    GStrvPtr keys { g_key_file_get_keys (keyfile, group.c_str(), &n_keys,
                                      nullptr) };

    for (gsize i = 0; keys && i < n_keys; ++i)
    {
        const char *key = keys.get()[i];
        time64 date;

        /* Anything unreadable is skipped and reported rather than
         * dropped silently: this is a file users are invited to edit, so
         * a typo should say so and leave the rest alone. It survives the
         * next write, since a write replaces the whole group from what
         * was read. */
        if (!date_from_key (key, &date))
        {
            PWARN ("ignoring malformed reconciled balance key '%s'", key);
            continue;
        }

        gsize n_fields = 0;
        GStrvPtr fields { g_key_file_get_string_list (keyfile, group.c_str(), key,
                                                   &n_fields, nullptr) };
        if (!fields || n_fields < 1)
        {
            PWARN ("ignoring empty reconciled balance for '%s'", key);
            continue;
        }

        auto amount = gnc_numeric_from_string (fields.get()[0]);
        if (gnc_numeric_check (amount))
        {
            PWARN ("ignoring unreadable reconciled balance '%s'",
                   fields.get()[0]);
            continue;
        }

        const char *notes = (n_fields > 1) ? fields.get()[1] : nullptr;

        records.try_emplace (date, acc, date, amount, notes ? notes : "");
    }

    return records;
}

static RecordMap&
records_for (Account *acc)
{
    ensure_hooks ();

    auto group = account_group (acc);
    auto it = s_records.find (group);

    if (it == s_records.end())
        it = s_records.emplace (group, load_account (acc)).first;

    return it->second;
}

/* Write an account's records back, replacing whatever was there. */
static void
store_account (Account *acc, const RecordMap& records)
{
    auto keyfile = gnc_state_get_current ();
    auto group = account_group (acc);

    if (!keyfile)
        return;

    g_key_file_remove_group (keyfile, group.c_str(), nullptr);

    for (const auto& [date, rb] : records)
    {
        GStrPtr amount { gnc_numeric_to_string (rb.amount) };
        const char *fields[2] = { amount.get(), rb.notes.c_str() };

        g_key_file_set_string_list (keyfile, group.c_str(),
                                    date_key (date).c_str(), fields, 2);
    }
}

void
gnc_reconciled_balance_reset_cache (void)
{
    s_records.clear ();
}

/* ================================================================ */
/* Records */

GncReconciledBalance *
gnc_reconciled_balance_record (Account *account, time64 date,
                               gnc_numeric amount, const char *notes)
{
    g_return_val_if_fail (GNC_IS_ACCOUNT (account), nullptr);
    g_return_val_if_fail (!gnc_numeric_check (amount), nullptr);

    auto& records = records_for (account);
    auto neutral = gnc_time64_get_day_neutral (date);
    std::string note_text { notes ? notes : "" };

    /* The account and the date together are the record's identity, so a
     * second record for the same date overwrites the first. */
    auto [it, inserted] = records.try_emplace (neutral, account, neutral,
                                               amount, note_text);
    if (!inserted)
    {
        it->second.amount = amount;
        it->second.notes = std::move (note_text);
    }

    store_account (account, records);

    return &it->second;
}

void
gnc_reconciled_balance_remove (GncReconciledBalance *rb)
{
    g_return_if_fail (rb && GNC_IS_ACCOUNT (rb->account));

    auto account = rb->account;
    auto& records = records_for (account);

    /* The record's date is its key, so this needs no search -- and rb is
     * dangling from here on. */
    records.erase (rb->date);

    store_account (account, records);
}

Account *
gnc_reconciled_balance_get_account (const GncReconciledBalance *rb)
{
    g_return_val_if_fail (rb, nullptr);
    return rb->account;
}

time64
gnc_reconciled_balance_get_date (const GncReconciledBalance *rb)
{
    g_return_val_if_fail (rb, 0);
    return rb->date;
}

gnc_numeric
gnc_reconciled_balance_get_amount (const GncReconciledBalance *rb)
{
    g_return_val_if_fail (rb, gnc_numeric_zero ());
    return rb->amount;
}

const char *
gnc_reconciled_balance_get_notes (const GncReconciledBalance *rb)
{
    g_return_val_if_fail (rb, nullptr);
    return rb->notes.c_str();
}

/* ================================================================ */
/* Evaluation */

gnc_numeric
gnc_reconciled_balance_compute (Account *acc, time64 date)
{
    g_return_val_if_fail (GNC_IS_ACCOUNT (acc), gnc_numeric_zero ());

    /* Every split posted on or before the date, whatever its reconcile
     * state. A record seals what the book said, so the check has to rest
     * on facts an outside edit would have to change to do damage --
     * posting dates and amounts -- and not on bookkeeping metadata.
     * Split reconcile dates in particular are rewritten wholesale by the
     * importer (import-backend.cpp), which would break every record in
     * the book after a routine CSV or OFX import. */
    return xaccAccountGetBalanceAsOfDate (acc, gnc_time64_get_day_end (date));
}

gnc_numeric
gnc_reconciled_balance_get_actual (const GncReconciledBalance *rb)
{
    g_return_val_if_fail (rb, gnc_numeric_zero ());

    if (!rb->account)
        return gnc_numeric_zero ();

    return gnc_reconciled_balance_compute (rb->account, rb->date);
}

gnc_numeric
gnc_reconciled_balance_get_delta (const GncReconciledBalance *rb)
{
    g_return_val_if_fail (rb, gnc_numeric_zero ());

    if (!rb->account)
        return gnc_numeric_zero ();

    /* Round to the account's commodity so that a difference smaller than
     * the smallest representable unit -- the residue of an imported rate
     * conversion, say -- doesn't read as a failure. An account with no
     * commodity yet has nothing to round to. */
    int denom = xaccAccountGetCommoditySCU (rb->account);
    if (denom <= 0)
        return gnc_numeric_sub (gnc_reconciled_balance_get_actual (rb),
                                rb->amount, GNC_DENOM_AUTO,
                                GNC_HOW_DENOM_LCD);

    return gnc_numeric_sub (gnc_reconciled_balance_get_actual (rb), rb->amount,
                            denom, GNC_HOW_RND_ROUND_HALF_UP);
}

gboolean
gnc_reconciled_balance_is_broken (const GncReconciledBalance *rb)
{
    g_return_val_if_fail (rb, FALSE);

    if (!rb->account)
        return FALSE;

    return !gnc_numeric_zero_p (gnc_reconciled_balance_get_delta (rb));
}

/* ================================================================ */
/* Collections */

GList *
gnc_reconciled_balance_get_for_account (const Account *acc)
{
    g_return_val_if_fail (GNC_IS_ACCOUNT (acc), nullptr);

    /* The map is ordered by date, so prepending and reversing gives the
     * oldest first with no sort. */
    GList *list = nullptr;
    for (auto& [date, rb] : records_for (GNC_ACCOUNT (acc)))
        list = g_list_prepend (list, &rb);

    return g_list_reverse (list);
}

static void
collect_from_account (Account *acc, gpointer user_data)
{
    auto list = static_cast<GList**>(user_data);

    for (auto& [date, rb] : records_for (acc))
        *list = g_list_prepend (*list, &rb);
}

static gint
compare_by_date (gconstpointer a, gconstpointer b)
{
    auto da = static_cast<const GncReconciledBalance*>(a)->date;
    auto db = static_cast<const GncReconciledBalance*>(b)->date;

    return (da < db) ? -1 : (da > db) ? 1 : 0;
}

/* Walking the live account tree rather than the state file's groups is
 * what skips records belonging to accounts that no longer exist. */
GList *
gnc_reconciled_balance_get_all (const QofBook *book)
{
    if (!book)
        return nullptr;

    auto root = gnc_book_get_root_account (const_cast<QofBook*>(book));
    if (!root)
        return nullptr;

    GList *list = nullptr;
    gnc_account_foreach_descendant (root, collect_from_account, &list);

    return g_list_sort (list, compare_by_date);
}
