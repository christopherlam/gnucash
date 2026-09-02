/********************************************************************
 * gtest-gnc-balance-assertion.cpp: Test balance assertions.        *
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
\********************************************************************/

#include <config.h>

#include "../Account.h"
#include "../Split.h"
#include "../Transaction.h"
#include "../gnc-balance-assertion.h"
#include "../gnc-commodity.h"
#include "../gnc-features.h"
#include "../cashobjects.h"

#include <qof.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <gtest/gtest.h>
#pragma GCC diagnostic pop

static const time64 JAN_15 = 1673740800; /* 2023-01-15 00:00:00 UTC */
static const time64 FEB_15 = 1676419200; /* 2023-02-15 00:00:00 UTC */

class BalanceAssertionTest : public testing::Test
{
protected:
    void SetUp () override
    {
        static bool registered = false;
        if (!registered)
        {
            qof_init ();
            ASSERT_TRUE (cashobjects_register ());
            registered = true;
        }

        m_book = qof_book_new ();
        m_root = gnc_account_create_root (m_book);
        m_curr = gnc_commodity_new (m_book, "Dollar", "CURRENCY", "USD",
                                    nullptr, 100);

        m_bank = make_account ("Bank", ACCT_TYPE_BANK);
        m_income = make_account ("Income", ACCT_TYPE_INCOME);
    }

    void TearDown () override
    {
        qof_book_destroy (m_book);
        m_book = nullptr;
    }

    Account *make_account (const char *name, GNCAccountType type)
    {
        Account *acc = xaccMallocAccount (m_book);
        xaccAccountBeginEdit (acc);
        xaccAccountSetName (acc, name);
        xaccAccountSetType (acc, type);
        xaccAccountSetCommodity (acc, m_curr);
        xaccAccountCommitEdit (acc);
        gnc_account_append_child (m_root, acc);
        return acc;
    }

    /* Post `amount' into m_bank from m_income on `date'. Returns the
     * bank-side split, so a test can reconcile it. */
    Split *add_transaction (time64 date, gnc_numeric amount)
    {
        Transaction *trans = xaccMallocTransaction (m_book);
        xaccTransBeginEdit (trans);
        xaccTransSetCurrency (trans, m_curr);
        xaccTransSetDatePostedSecs (trans, date);

        Split *to = xaccMallocSplit (m_book);
        xaccSplitSetParent (to, trans);
        xaccSplitSetAccount (to, m_bank);
        xaccSplitSetValue (to, amount);
        xaccSplitSetAmount (to, amount);

        Split *from = xaccMallocSplit (m_book);
        xaccSplitSetParent (from, trans);
        xaccSplitSetAccount (from, m_income);
        xaccSplitSetValue (from, gnc_numeric_neg (amount));
        xaccSplitSetAmount (from, gnc_numeric_neg (amount));

        xaccTransCommitEdit (trans);

        return to;
    }

    /* Mark a split reconciled as the reconcile window does: state 'y',
     * with the *statement* date as the split's reconcile date. */
    void reconcile_split (Split *split, time64 statement_date)
    {
        auto trans = xaccSplitGetParent (split);
        xaccTransBeginEdit (trans);
        xaccSplitSetReconcile (split, YREC);
        xaccSplitSetDateReconciledSecs (split, statement_date);
        xaccTransCommitEdit (trans);
    }

    GncBalanceAssertion *make_assertion (Account *acc, time64 date,
                                         gnc_numeric amount)
    {
        GncBalanceAssertion *ba = gnc_balance_assertion_new (m_book);
        gnc_balance_assertion_set_account (ba, acc);
        gnc_balance_assertion_set_date (ba, date);
        gnc_balance_assertion_set_amount (ba, amount);
        return ba;
    }

    static gnc_numeric dollars (int n) { return gnc_numeric_create (n * 100, 100); }

    QofBook *m_book = nullptr;
    Account *m_root = nullptr;
    Account *m_bank = nullptr;
    Account *m_income = nullptr;
    gnc_commodity *m_curr = nullptr;
};

TEST_F (BalanceAssertionTest, NewAssertionRoundTripsItsFields)
{
    GncBalanceAssertion *ba = make_assertion (m_bank, JAN_15, dollars (50));

    EXPECT_EQ (m_bank, gnc_balance_assertion_get_account (ba));
    EXPECT_TRUE (gnc_numeric_equal (dollars (50),
                                    gnc_balance_assertion_get_amount (ba)));

    gnc_balance_assertion_set_notes (ba, "statement 3");
    EXPECT_STREQ ("statement 3", gnc_balance_assertion_get_notes (ba));

    /* The date is stored day-neutral, so it need not come back
     * bit-identical -- but it must stay on the same day. */
    EXPECT_EQ (gnc_time64_get_day_start (JAN_15),
               gnc_time64_get_day_start (gnc_balance_assertion_get_date (ba)));
}

TEST_F (BalanceAssertionTest, LookupFindsAssertionInBook)
{
    GncBalanceAssertion *ba = make_assertion (m_bank, JAN_15, dollars (50));
    const GncGUID *guid = gnc_balance_assertion_get_guid (ba);

    EXPECT_EQ (ba, gnc_balance_assertion_lookup (guid, m_book));
}

TEST_F (BalanceAssertionTest, CreatingAnAssertionFlagsTheBookFeature)
{
    EXPECT_FALSE (gnc_features_check_used (m_book,
                                           GNC_FEATURE_BALANCE_ASSERTIONS));
    make_assertion (m_bank, JAN_15, dollars (50));
    EXPECT_TRUE (gnc_features_check_used (m_book,
                                          GNC_FEATURE_BALANCE_ASSERTIONS));
}

TEST_F (BalanceAssertionTest, MatchingBalancePasses)
{
    add_transaction (JAN_15, dollars (50));
    GncBalanceAssertion *ba = make_assertion (m_bank, JAN_15, dollars (50));

    EXPECT_EQ (GNC_BALANCE_ASSERTION_PASS, gnc_balance_assertion_get_status (ba));
    EXPECT_FALSE (gnc_balance_assertion_is_failing (ba));
    EXPECT_TRUE (gnc_numeric_zero_p (gnc_balance_assertion_get_delta (ba)));
}

TEST_F (BalanceAssertionTest, MismatchedBalanceFails)
{
    add_transaction (JAN_15, dollars (50));
    GncBalanceAssertion *ba = make_assertion (m_bank, JAN_15, dollars (40));

    EXPECT_EQ (GNC_BALANCE_ASSERTION_FAIL, gnc_balance_assertion_get_status (ba));
    EXPECT_TRUE (gnc_numeric_equal (dollars (10),
                                    gnc_balance_assertion_get_delta (ba)));
}

TEST_F (BalanceAssertionTest, BalanceIsTakenAtEndOfTheAssertedDay)
{
    /* Posted on the asserted date itself: it must count. */
    add_transaction (JAN_15, dollars (50));
    /* Posted later: it must not. */
    add_transaction (FEB_15, dollars (25));

    GncBalanceAssertion *ba = make_assertion (m_bank, JAN_15, dollars (50));
    EXPECT_EQ (GNC_BALANCE_ASSERTION_PASS, gnc_balance_assertion_get_status (ba));

    GncBalanceAssertion *later = make_assertion (m_bank, FEB_15, dollars (75));
    EXPECT_EQ (GNC_BALANCE_ASSERTION_PASS,
               gnc_balance_assertion_get_status (later));
}

/* The whole point of the memoised balance: a later edit has to be
 * noticed without anyone telling the assertion about it. */
TEST_F (BalanceAssertionTest, StatusFollowsLaterEdits)
{
    add_transaction (JAN_15, dollars (50));
    GncBalanceAssertion *ba = make_assertion (m_bank, JAN_15, dollars (50));
    ASSERT_EQ (GNC_BALANCE_ASSERTION_PASS, gnc_balance_assertion_get_status (ba));

    add_transaction (JAN_15, dollars (5));
    EXPECT_EQ (GNC_BALANCE_ASSERTION_FAIL, gnc_balance_assertion_get_status (ba));

    gnc_balance_assertion_set_amount (ba, dollars (55));
    EXPECT_EQ (GNC_BALANCE_ASSERTION_PASS, gnc_balance_assertion_get_status (ba));
}

TEST_F (BalanceAssertionTest, AssertionWithNoAccountIsUnknownNotFailing)
{
    GncBalanceAssertion *ba = gnc_balance_assertion_new (m_book);
    gnc_balance_assertion_set_date (ba, JAN_15);
    gnc_balance_assertion_set_amount (ba, dollars (50));

    EXPECT_EQ (nullptr, gnc_balance_assertion_get_account (ba));
    EXPECT_EQ (GNC_BALANCE_ASSERTION_UNKNOWN,
               gnc_balance_assertion_get_status (ba));
    EXPECT_FALSE (gnc_balance_assertion_is_failing (ba));
}

TEST_F (BalanceAssertionTest, ListsAreSortedByDateAndFilteredByAccount)
{
    make_assertion (m_bank, FEB_15, dollars (75));
    make_assertion (m_bank, JAN_15, dollars (50));
    make_assertion (m_income, JAN_15, dollars (0));

    GList *all = gnc_balance_assertion_get_all (m_book);
    ASSERT_EQ (3u, g_list_length (all));
    g_list_free (all);

    GList *bank = gnc_balance_assertion_get_for_account (m_bank);
    ASSERT_EQ (2u, g_list_length (bank));
    EXPECT_EQ (gnc_time64_get_day_start (JAN_15),
               gnc_time64_get_day_start (gnc_balance_assertion_get_date
                                         (GNC_BALANCE_ASSERTION (bank->data))));
    g_list_free (bank);
}

TEST_F (BalanceAssertionTest, CountsOnlyFailingAssertions)
{
    add_transaction (JAN_15, dollars (50));
    make_assertion (m_bank, JAN_15, dollars (50)); /* passes */
    make_assertion (m_bank, FEB_15, dollars (99)); /* fails */

    EXPECT_EQ (1u, gnc_balance_assertion_count_failing (m_book));
    EXPECT_EQ (1u, gnc_balance_assertion_count_failing_for_account (m_bank));
    EXPECT_EQ (0u, gnc_balance_assertion_count_failing_for_account (m_income));

    GList *failing = gnc_balance_assertion_get_failing (m_book);
    EXPECT_EQ (1u, g_list_length (failing));
    g_list_free (failing);
}

TEST_F (BalanceAssertionTest, DestroyRemovesAssertionFromTheBook)
{
    GncBalanceAssertion *ba = make_assertion (m_bank, JAN_15, dollars (50));
    GncGUID guid = *gnc_balance_assertion_get_guid (ba);

    gnc_balance_assertion_destroy (ba);

    EXPECT_EQ (nullptr, gnc_balance_assertion_lookup (&guid, m_book));
    EXPECT_EQ (nullptr, gnc_balance_assertion_get_all (m_book));
}

/* An assertion about an account that no longer exists could never be
 * evaluated again, so it goes when the account does. */
TEST_F (BalanceAssertionTest, DeletingTheAccountRemovesItsAssertions)
{
    make_assertion (m_bank, JAN_15, dollars (50));
    make_assertion (m_income, JAN_15, dollars (0));
    ASSERT_EQ (2u, g_list_length (gnc_balance_assertion_get_all (m_book)));

    xaccAccountBeginEdit (m_bank);
    xaccAccountDestroy (m_bank);
    m_bank = nullptr;

    GList *remaining = gnc_balance_assertion_get_all (m_book);
    EXPECT_EQ (1u, g_list_length (remaining));
    if (remaining)
    {
        EXPECT_EQ (m_income, gnc_balance_assertion_get_account
                   (GNC_BALANCE_ASSERTION (remaining->data)));
    }
    g_list_free (remaining);
}

/* ================================================================ */
/* Reconciled basis */

TEST_F (BalanceAssertionTest, BasisDefaultsToTotalAndRoundTrips)
{
    auto ba = make_assertion (m_bank, JAN_15, dollars (50));
    EXPECT_EQ (GNC_BALANCE_ASSERTION_BASIS_TOTAL,
               gnc_balance_assertion_get_basis (ba));

    gnc_balance_assertion_set_basis (ba, GNC_BALANCE_ASSERTION_BASIS_RECONCILED);
    EXPECT_EQ (GNC_BALANCE_ASSERTION_BASIS_RECONCILED,
               gnc_balance_assertion_get_basis (ba));
}

TEST_F (BalanceAssertionTest, ReconciledBasisCountsOnlyReconciledSplits)
{
    auto reconciled = add_transaction (JAN_15, dollars (50));
    add_transaction (JAN_15, dollars (30));      /* left unreconciled */
    reconcile_split (reconciled, JAN_15);

    auto ba = make_assertion (m_bank, JAN_15, dollars (50));
    gnc_balance_assertion_set_basis (ba, GNC_BALANCE_ASSERTION_BASIS_RECONCILED);

    EXPECT_EQ (GNC_BALANCE_ASSERTION_PASS, gnc_balance_assertion_get_status (ba));

    /* The same figure on the total basis would be wrong, because the
     * unreconciled 30 counts there. */
    gnc_balance_assertion_set_basis (ba, GNC_BALANCE_ASSERTION_BASIS_TOTAL);
    EXPECT_EQ (GNC_BALANCE_ASSERTION_FAIL, gnc_balance_assertion_get_status (ba));
}

/* The property that makes the reconcile hook worth having: an item
 * posted before the statement date but cleared on a later statement
 * carries the later reconcile date, so it stays out of the earlier
 * assertion and that assertion keeps holding. */
TEST_F (BalanceAssertionTest, ReconciledBasisIgnoresLaterClearedItems)
{
    auto january = add_transaction (JAN_15, dollars (50));
    reconcile_split (january, JAN_15);

    auto ba = make_assertion (m_bank, JAN_15, dollars (50));
    gnc_balance_assertion_set_basis (ba, GNC_BALANCE_ASSERTION_BASIS_RECONCILED);
    ASSERT_EQ (GNC_BALANCE_ASSERTION_PASS, gnc_balance_assertion_get_status (ba));

    /* A cheque written in January that only clears on the February
     * statement. Its posting date is before the January statement date. */
    auto late = add_transaction (JAN_15, dollars (-20));
    reconcile_split (late, FEB_15);

    EXPECT_EQ (GNC_BALANCE_ASSERTION_PASS, gnc_balance_assertion_get_status (ba));

    /* On the total basis the same assertion would now break, which is
     * why the reconcile hook does not use it. */
    gnc_balance_assertion_set_basis (ba, GNC_BALANCE_ASSERTION_BASIS_TOTAL);
    EXPECT_EQ (GNC_BALANCE_ASSERTION_FAIL, gnc_balance_assertion_get_status (ba));
}

TEST_F (BalanceAssertionTest, ReconciledBasisNoticesAnUnreconciledSplit)
{
    auto split = add_transaction (JAN_15, dollars (50));
    reconcile_split (split, JAN_15);

    auto ba = make_assertion (m_bank, JAN_15, dollars (50));
    gnc_balance_assertion_set_basis (ba, GNC_BALANCE_ASSERTION_BASIS_RECONCILED);
    ASSERT_EQ (GNC_BALANCE_ASSERTION_PASS, gnc_balance_assertion_get_status (ba));

    auto trans = xaccSplitGetParent (split);
    xaccTransBeginEdit (trans);
    xaccSplitSetReconcile (split, NREC);
    xaccTransCommitEdit (trans);

    EXPECT_EQ (GNC_BALANCE_ASSERTION_FAIL, gnc_balance_assertion_get_status (ba));
}

TEST_F (BalanceAssertionTest, ReconciledBasisIgnoresVoidedTransactions)
{
    auto split = add_transaction (JAN_15, dollars (50));
    reconcile_split (split, JAN_15);

    auto voided = add_transaction (JAN_15, dollars (99));
    reconcile_split (voided, JAN_15);

    auto ba = make_assertion (m_bank, JAN_15, dollars (50));
    gnc_balance_assertion_set_basis (ba, GNC_BALANCE_ASSERTION_BASIS_RECONCILED);
    ASSERT_EQ (GNC_BALANCE_ASSERTION_FAIL, gnc_balance_assertion_get_status (ba));

    auto trans = xaccSplitGetParent (voided);
    xaccTransBeginEdit (trans);
    xaccTransVoid (trans, "test");
    xaccTransCommitEdit (trans);

    EXPECT_EQ (GNC_BALANCE_ASSERTION_PASS, gnc_balance_assertion_get_status (ba));
}
