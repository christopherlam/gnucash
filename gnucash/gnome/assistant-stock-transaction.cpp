/********************************************************************\
 * assistant-stock-transaction.c -- stock assistant for GnuCash     *
 * Copyright (C) 2022 Christopher Lam                               *
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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <vector>
#include <string>

extern "C" {
#include "Transaction.h"
#include "engine-helpers.h"
#include "dialog-utils.h"
#include "assistant-stock-transaction.h"
#include "gnc-account-sel.h"
#include "gnc-amount-edit.h"
#include "gnc-component-manager.h"
#include "gnc-currency-edit.h"
#include "gnc-date-edit.h"
#include "qof.h"
#include "gnc-gui-query.h"
#include "gnc-tree-view-account.h"
#include "gnc-ui.h"
#include "gnc-ui-util.h"

void gnc_stock_transaction_dialog (GtkWidget *parent, Account * initial);
void stock_assistant_prepare (GtkAssistant  *assistant, GtkWidget *page,
                              gpointer user_data);
void stock_assistant_finish  (GtkAssistant *assistant, gpointer user_data);
void stock_assistant_cancel  (GtkAssistant *gtkassistant, gpointer user_data);
}

#define ASSISTANT_STOCK_TRANSACTION_CM_CLASS "assistant-stock-transaction"

enum transaction_cols
{
    TRANSACTION_COL_ACCOUNT = 0,
    TRANSACTION_COL_FULLNAME,
    TRANSACTION_COL_MNEMONIC,
    TRANSACTION_COL_SHARES,
    NUM_TRANSACTION_COLS
};


enum split_cols
{
    SPLIT_COL_ACCOUNT = 0,
    SPLIT_COL_MEMO,
    SPLIT_COL_DEBIT,
    SPLIT_COL_CREDIT,
    NUM_SPLIT_COLS
};

/** structures *********************************************************/

typedef enum
{
    DISABLED = 0,
    ENABLED_DEBIT,
    ENABLED_CREDIT
} FieldMask;

typedef struct
{
    uint stock_amount;
    uint stock_value;
    uint cash_amount;
    uint fees_amount;
    bool fees_capitalize;
    uint divi_amount;
    bool capg_amount;
    std::string friendly_name;
    std::string explanation;
} TxnTypeInfo;

static const std::vector<TxnTypeInfo> starting_types
{
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_CREDIT,         // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        DISABLED,               // divi_amt
        false,                  // capg_amt
        "Open buy",
        "Initial stock purchase"
    },
    {
        ENABLED_CREDIT,         // stock_amt
        ENABLED_CREDIT,         // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        DISABLED,               // divi_amt
        false,                  // capg_amt
        "Open short",
        "Initial stock short-sale"
    }
};

static const std::vector<TxnTypeInfo> open_types
{
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_CREDIT,         // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        DISABLED,               // divi_amt
        false,                  // capg_amt
        "Buy",
        "Buying stock."
    },
    {
        ENABLED_CREDIT,         // stock_amt
        ENABLED_CREDIT,         // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        false,                  // fees_capitalize
        DISABLED,               // divi_amt
        true,                   // capg_amt
        "Sell",
        "Selling stock, and record capital gains/loss"
    },
    {
        DISABLED,               // stock_amt
        DISABLED,               // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        false,                  // fees_capitalize
        ENABLED_CREDIT,         // divi_amt
        false,                  // capg_amt
        "Dividend",
        "Company issues dividends to holder"
    },
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        ENABLED_CREDIT,         // divi_amt
        false,                  // capg_amt
        "Dividend reinvestment (w/ remainder)",
        "Company issues dividend which is reinvested. Some dividends are paid to holder"
    },
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        DISABLED,               // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        ENABLED_CREDIT,         // divi_amt
        false,                  // capg_amt
        "Dividend reinvestment (w/o remainder)",
        "Company issues dividend which is wholly reinvested."
    },
    {
        DISABLED,               // stock_amt
        ENABLED_CREDIT,         // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        DISABLED,               // divi_amt
        false,                  // capg_amt
        "Return of Capital",
        "Stock returns capital to holder"
    },
    {
        DISABLED,               // stock_amt
        ENABLED_DEBIT,          // stock_val
        DISABLED,               // cash_amt
        ENABLED_DEBIT,          // fees_amt
        false,                  // fees_capitalize
        ENABLED_CREDIT,         // divi_amt
        false,                  // capg_amt
        "Notional distribution",
        "Stock returns a notional distribution"
    },
    {
        ENABLED_DEBIT,          // stock_amt
        DISABLED,               // stock_val
        DISABLED,               // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        DISABLED,               // divi_amt
        false,                  // capg_amt
        "Stock split",
        "Stock price is fractionally reduced"
    },
    {
        ENABLED_CREDIT,         // stock_amt
        DISABLED,               // stock_val
        DISABLED,               // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        DISABLED,               // divi_amt
        false,                  // capg_amt
        "Reverse split",
        "Stocks price is fractionally increased."
    },
    {
        ENABLED_CREDIT,         // stock_amt
        ENABLED_CREDIT,         // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        false,                  // fees_capitalize
        DISABLED,               // divi_amt
        false,                  // capg_amt
        "Reverse split w/ cash in lieu for fractionals",
        "Stocks price is fractionally increased. Fractional remaining stock is returned as cash."
    },
};

static const std::vector<TxnTypeInfo> short_types
{
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        ENABLED_DEBIT,          // divi_amt
        ENABLED_DEBIT,          // capg_amt
        "Short sell",
        ""
    },
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        false,                  // fees_capitalize
        ENABLED_DEBIT,          // divi_amt
        ENABLED_DEBIT,          // capg_amt
        "Cover buy",
        ""
    },
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        false,                  // fees_capitalize
        ENABLED_DEBIT,          // divi_amt
        ENABLED_DEBIT,          // capg_amt
        "Compensatory Dividend",
        ""
    },
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        ENABLED_DEBIT,          // divi_amt
        ENABLED_DEBIT,          // capg_amt
        "Dividend reinvestment (w/ remainder)",
        ""
    },
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        ENABLED_DEBIT,          // divi_amt
        ENABLED_DEBIT,          // capg_amt
        "Dividend reinvestment (w/o remainder)",
        ""
    },
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        ENABLED_DEBIT,          // divi_amt
        ENABLED_DEBIT,          // capg_amt
        "Compensatory Return of Capital",
        ""
    },
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        false,                  // fees_capitalize
        ENABLED_DEBIT,          // divi_amt
        ENABLED_DEBIT,          // capg_amt
        "Compensatory Notional distribution",
        ""
    },
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        ENABLED_DEBIT,          // divi_amt
        ENABLED_DEBIT,          // capg_amt
        "Stock split",
        ""
    },
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        true,                   // fees_capitalize
        ENABLED_DEBIT,          // divi_amt
        ENABLED_DEBIT,          // capg_amt
        "Reverse split",
        ""
    },
    {
        ENABLED_DEBIT,          // stock_amt
        ENABLED_DEBIT,          // stock_val
        ENABLED_DEBIT,          // cash_amt
        ENABLED_DEBIT,          // fees_amt
        false,                  // fees_capitalize
        ENABLED_DEBIT,          // divi_amt
        ENABLED_DEBIT,          // capg_amt
        "Reverse split w/ cash in lieu for fractionals",
        ""
    },
};

typedef struct
{
    GtkWidget * window;
    GtkWidget * assistant;

    std::vector<TxnTypeInfo> txn_types;
    Account   * acct;
    gnc_commodity * currency;

    // transaction type page
    GtkWidget * transaction_type_page;
    GtkWidget * transaction_type_combo;
    GtkWidget * transaction_type_explanation;
    TxnTypeInfo txn_type;

    // transaction details page
    GtkWidget * transaction_details_page;
    GtkWidget * date_edit;
    GtkWidget * transaction_description_entry;

    // stock amount page
    gnc_numeric balance_at_date;
    GtkWidget * stock_amount_page;
    GtkWidget * prev_amount;
    GtkWidget * next_amount;
    GtkWidget * stock_amount_edit;

    // stock value page
    GtkWidget * stock_value_page;
    GtkWidget * stock_value_edit;
    GtkWidget * price_amount;
    GtkWidget * stock_memo_edit;

    // cash page
    GtkWidget * cash_page;
    GtkWidget * cash_account;
    GtkWidget * cash_memo_edit;
    GtkWidget * cash_amount;

    // fees page
    GtkWidget * fees_page;
    GtkWidget * capitalize_fees_checkbox;
    GtkWidget * fees_account;
    GtkWidget * fees_memo_edit;
    GtkWidget * fees_amount;

    // capgains page
    GtkWidget * capgains_page;
    GtkWidget * capgains_account;
    GtkWidget * capgains_memo_edit;
    GtkWidget * capgains_amount;

    // finish page
    GtkWidget * finish_page;
    GtkWidget * split_view;
} StockTransactionInfo;


/** declarations *******************************************************/
void     stock_assistant_window_destroy_cb (GtkWidget *object, gpointer user_data);
void     stock_assistant_details_prepare   (GtkAssistant *assistant, gpointer user_data);

/******* implementations ***********************************************/
void
stock_assistant_window_destroy_cb (GtkWidget *object, gpointer user_data)
{
    StockTransactionInfo *info = (StockTransactionInfo *)user_data;
    gnc_unregister_gui_component_by_data (ASSISTANT_STOCK_TRANSACTION_CM_CLASS, info);
    g_free (info);
}

static void refresh_page_transaction_type (GtkWidget *widget, gpointer user_data)
{
    StockTransactionInfo *info = (StockTransactionInfo *)user_data;

    auto type_idx = gtk_combo_box_get_active (GTK_COMBO_BOX (widget));
    info->txn_type = info->txn_types.at (type_idx); // fixme: need to check bounds

    auto explanation = info->transaction_type_explanation;
    gtk_label_set_text (GTK_LABEL (explanation), info->txn_type.explanation.c_str ());

    // set default capitalize fees setting
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (info->capitalize_fees_checkbox),
                                  info->txn_type.fees_capitalize);
}


static void refresh_page_stock_amount (GtkWidget *widget, gpointer user_data)
{
    StockTransactionInfo *info = (StockTransactionInfo *)user_data;

    auto pinfo = gnc_commodity_print_info (xaccAccountGetCommodity (info->acct), true);
    auto bal = info->balance_at_date;
    auto bal_str = xaccPrintAmount (bal, pinfo);
    gtk_label_set_text (GTK_LABEL(info->prev_amount), bal_str);

    gnc_numeric stock_delta;

    if (!gnc_amount_edit_expr_is_valid (GNC_AMOUNT_EDIT (info->stock_amount_edit),
                                        &stock_delta, true, nullptr))
    {
        if (info->txn_type.stock_amount == ENABLED_CREDIT)
            stock_delta = gnc_numeric_neg (stock_delta);
        bal = gnc_numeric_add_fixed (bal, stock_delta);

        bal_str = xaccPrintAmount (bal, pinfo);
        gtk_label_set_text (GTK_LABEL(info->next_amount), bal_str);
    }
    else
        gtk_label_set_text (GTK_LABEL(info->next_amount), nullptr);
}


static void refresh_page_stock_value (GtkWidget *widget, gpointer user_data)
{
    StockTransactionInfo *info = (StockTransactionInfo *)user_data;

    if (info->txn_type.stock_amount == DISABLED ||
        info->txn_type.stock_value == DISABLED)
        return;

    gnc_numeric amount, value;
    if (gnc_amount_edit_expr_is_valid (GNC_AMOUNT_EDIT (info->stock_amount_edit),
                                       &amount, true, nullptr) ||
        gnc_amount_edit_expr_is_valid (GNC_AMOUNT_EDIT (info->stock_value_edit),
                                       &value, true, nullptr))
        return;

    if (gnc_numeric_zero_p (value))
        return;

    auto price = gnc_numeric_div (value, amount, GNC_DENOM_AUTO, GNC_HOW_RND_ROUND);
    auto pinfo = gnc_commodity_print_info (info->currency, true);
    auto price_str = xaccPrintAmount (price, pinfo);
    gtk_label_set_text (GTK_LABEL (info->price_amount), price_str);
}

static void refresh_page_cash (GtkWidget *widget, gpointer user_data)
{
    StockTransactionInfo *info = (StockTransactionInfo *)user_data;
    g_assert (info->txn_type.cash_amount != DISABLED);
    // check amount and account exist
}

static void refresh_page_fees (GtkWidget *widget, gpointer user_data)
{
    StockTransactionInfo *info = (StockTransactionInfo *)user_data;
    auto capitalize_fees = gtk_toggle_button_get_active
        (GTK_TOGGLE_BUTTON (info->capitalize_fees_checkbox));
    gtk_widget_set_sensitive (info->fees_account, !capitalize_fees);
}

static void refresh_page_capgains (GtkWidget *widget, gpointer user_data)
{
    StockTransactionInfo *info = (StockTransactionInfo *)user_data;
    return;
}

static void
add_line (GtkListStore **list, gnc_numeric& debit, gnc_numeric& credit,
          uint splitfield, Account *acct, GtkWidget *memo, GtkWidget *gae,
          gnc_commodity *comm, bool& fail)
{
    if (splitfield == DISABLED)
        return;

    const gchar* amtstr;
    gnc_numeric amount;

    if (gnc_amount_edit_expr_is_valid (GNC_AMOUNT_EDIT (gae), &amount, true, nullptr))
    {
        amtstr = "missing";
        fail = true;
    }
    else
    {
        if (splitfield == ENABLED_DEBIT)
            debit = gnc_numeric_add_fixed (debit, amount);
        else
        {
            amount = gnc_numeric_neg (amount);
            credit = gnc_numeric_add_fixed (credit, amount);
        }
        amtstr = xaccPrintAmount (amount, gnc_commodity_print_info (comm, true));
    }

    auto memostr = gtk_entry_get_text (GTK_ENTRY (memo));
    auto acctstr = acct ? xaccAccountGetName (acct) : "missing";

    if (!acct) fail = true;

    GtkTreeIter iter;
    gtk_list_store_append (*list, &iter);
    gtk_list_store_set (*list, &iter,
                        SPLIT_COL_ACCOUNT, acctstr,
                        SPLIT_COL_MEMO, memostr,
                        SPLIT_COL_DEBIT, (splitfield == ENABLED_DEBIT) ? amtstr : "",
                        SPLIT_COL_CREDIT, (splitfield == ENABLED_CREDIT) ? amtstr : "",
                        -1);
}

static void refresh_page_finish (StockTransactionInfo *info)
{
    auto view = GTK_TREE_VIEW (info->split_view);
    auto list = GTK_LIST_STORE (gtk_tree_view_get_model(view));
    gtk_list_store_clear (list);

    gnc_numeric debit = gnc_numeric_zero ();
    gnc_numeric credit = gnc_numeric_zero ();
    bool fail = false;

    add_line (&list, debit, credit, info->txn_type.stock_amount, info->acct, info->stock_memo_edit, info->stock_value_edit, info->currency, fail);

    add_line (&list, debit, credit, info->txn_type.cash_amount, gnc_account_sel_get_account (GNC_ACCOUNT_SEL (info->cash_account)), info->cash_memo_edit, info->cash_amount, info->currency, fail);

    for (gint i = 0; i < 5; i++)
    {
        GtkTreeIter iter;
        gtk_list_store_append (list, &iter);
        gtk_list_store_set (list, &iter,
                            SPLIT_COL_ACCOUNT, "acc",
                            SPLIT_COL_MEMO, "memo",
                            SPLIT_COL_DEBIT, "$10.00",
                            SPLIT_COL_CREDIT, "$20.00",
                            -1);
    }
}

void stock_assistant_prepare (GtkAssistant  *assistant, GtkWidget *page,
                              gpointer user_data)
{
    StockTransactionInfo *info = (StockTransactionInfo *)user_data;
    gint currentpage = gtk_assistant_get_current_page(assistant);

    g_print ("stock_assistant_prepare. current page = %d\n", currentpage);

    switch (currentpage)
    {
    case 1:
        refresh_page_transaction_type (info->transaction_type_combo, info);
        gtk_widget_grab_focus (info->transaction_type_combo);
        break;
    case 3:
        info->balance_at_date = xaccAccountGetBalanceAsOfDate
            (info->acct, gnc_date_edit_get_date_end (GNC_DATE_EDIT (info->date_edit)));
        refresh_page_stock_amount (info->stock_amount_edit, info);
        // fixme: the following doesn't work???
        gtk_widget_grab_focus (gnc_amount_edit_gtk_entry
                               (GNC_AMOUNT_EDIT (info->stock_amount_edit)));
        break;
    case 4:
        refresh_page_stock_value (info->stock_value_edit, info);
        // fixme: ditto
        gtk_widget_grab_focus (gnc_amount_edit_gtk_entry
                               (GNC_AMOUNT_EDIT (info->stock_value_edit)));
        break;
    case 5:
        refresh_page_cash (info->cash_amount, info);
        break;
    case 6:
        refresh_page_fees (info->fees_amount, info);
        break;
    case 7:
        refresh_page_capgains (info->capgains_amount, info);
        break;
    case 8:
        refresh_page_finish (info);
        break;
    }
}

static gint
forward_page_func (gint current_page, gpointer user_data)
{
    auto info = (StockTransactionInfo *)user_data;
    auto txn_type = info->txn_type;

    if (current_page == 2 && txn_type.stock_amount == DISABLED) current_page++;
    if (current_page == 3 && txn_type.stock_value == DISABLED)  current_page++;
    if (current_page == 4 && txn_type.cash_amount == DISABLED)  current_page++;
    if (current_page == 5 && txn_type.fees_amount == DISABLED)  current_page++;
    if (current_page == 6 && txn_type.capg_amount == false)     current_page++;
    current_page++;
    return current_page;
}


void
stock_assistant_finish (GtkAssistant *assistant, gpointer user_data)
{
    StockTransactionInfo *info = (StockTransactionInfo *)user_data;
    std::vector<Account*> account_commits;

    auto account = info->acct;
    g_return_if_fail (account != NULL);

    auto amount = gnc_amount_edit_get_amount (GNC_AMOUNT_EDIT (info->stock_amount_edit));
    g_return_if_fail (!gnc_numeric_zero_p (amount));

    gnc_suspend_gui_refresh ();

    xaccAccountBeginEdit (account);
    account_commits.emplace_back (account);

    auto trans = xaccMallocTransaction (gnc_get_current_book ());
    xaccTransBeginEdit (trans);
    xaccTransSetCurrency (trans, gnc_account_get_currency_or_parent (account));
    // xaccTransSetDescription (trans, gtk_entry_get_text (GTK_ENTRY (info->description_entry)));

    auto date = gnc_date_edit_get_date (GNC_DATE_EDIT (info->date_edit));
    xaccTransSetDatePostedSecsNormalized (trans, date);

    auto split = xaccMallocSplit (gnc_get_current_book ());
    xaccSplitSetParent (split, trans);
    xaccSplitSetAccount (split, account);
    xaccSplitSetAmount (split, amount);
    xaccSplitMakeStockSplit (split);
    gnc_set_num_action (NULL, split, NULL, C_("Action Column", "Split"));

    // amount = gnc_amount_edit_get_amount (GNC_AMOUNT_EDIT (info->price_edit));
    amount = gnc_numeric_zero (); // fixme -- price = value/amount
    if (gnc_numeric_positive_p (amount))
    {
        // auto ce = GNC_CURRENCY_EDIT (info->price_currency_edit);
        auto price = gnc_price_create (gnc_get_current_book ());

        gnc_price_begin_edit (price);
        gnc_price_set_commodity (price, xaccAccountGetCommodity (account));
        // gnc_price_set_currency (price, gnc_currency_edit_get_currency (ce));
        gnc_price_set_time64 (price, date);
        gnc_price_set_source (price, PRICE_SOURCE_STOCK_SPLIT);
        gnc_price_set_typestr (price, PRICE_TYPE_UNK);
        gnc_price_set_value (price, amount);
        gnc_price_commit_edit (price);

        auto book = gnc_get_current_book ();
        auto pdb = gnc_pricedb_get_db (book);

        if (!gnc_pricedb_add_price (pdb, price))
            gnc_error_dialog (GTK_WINDOW (info->window), "%s", _("Error adding price."));
    }

    amount = gnc_amount_edit_get_amount (GNC_AMOUNT_EDIT (info->cash_amount));
    if (gnc_numeric_positive_p (amount))
    {
        const char *memo = gtk_entry_get_text (GTK_ENTRY (info->cash_memo_edit));

        /* asset split */
        // account = gnc_tree_view_account_get_selected_account
        //     (GNC_TREE_VIEW_ACCOUNT(info->asset_tree));
        account_commits.emplace_back (account);

        split = xaccMallocSplit (gnc_get_current_book ());
        xaccAccountBeginEdit (account);
        xaccSplitSetAccount (split, account);
        xaccSplitSetParent (split, trans);
        xaccSplitSetAmount (split, amount);
        xaccSplitSetValue (split, amount);
        xaccSplitSetMemo (split, memo);

        /* income split */
        // account = gnc_tree_view_account_get_selected_account
        //     (GNC_TREE_VIEW_ACCOUNT(info->income_tree));
        account_commits.emplace_back (account);

        split = xaccMallocSplit (gnc_get_current_book ());
        xaccAccountBeginEdit (account);
        xaccSplitSetAccount (split, account);
        xaccSplitSetParent (split, trans);
        xaccSplitSetAmount (split, gnc_numeric_neg (amount));
        xaccSplitSetValue (split, gnc_numeric_neg (amount));
        xaccSplitSetMemo (split, memo);
    }

    xaccTransCommitEdit (trans);

    for (auto& it : account_commits)
        xaccAccountCommitEdit (it);

    gnc_resume_gui_refresh ();

    gnc_close_gui_component_by_data (ASSISTANT_STOCK_TRANSACTION_CM_CLASS, info);
}


void
stock_assistant_cancel (GtkAssistant *assistant, gpointer user_data)
{
    StockTransactionInfo *info = (StockTransactionInfo *)user_data;
    gnc_close_gui_component_by_data (ASSISTANT_STOCK_TRANSACTION_CM_CLASS, info);
}

static void fill_transaction_types (GtkWidget *widget, StockTransactionInfo *info)
{
    GtkComboBoxText *combo = GTK_COMBO_BOX_TEXT (widget);
    gtk_combo_box_text_remove_all (combo);
    for (auto& it : info->txn_types)
        gtk_combo_box_text_append_text (combo, it.friendly_name.c_str() );
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
}

static GtkWidget * get_widget (GtkBuilder *builder, const gchar * ID)
{
    g_return_val_if_fail (builder && ID, nullptr);
    return GTK_WIDGET (gtk_builder_get_object (builder, ID));
}

static GtkWidget * create_gas (GtkBuilder *builder, gint row,
                               std::vector<GNCAccountType> type, gnc_commodity *currency,
                               const gchar *table_ID, const gchar *label_ID)
{
    auto table = get_widget (builder, table_ID);
    auto label = get_widget (builder, label_ID);
    auto gas = gnc_account_sel_new ();
    GList *acct_list = nullptr;
    for (auto& it : type)
        acct_list = g_list_prepend (acct_list, (gpointer)it);
    auto curr_list = g_list_prepend (nullptr, currency);
    gnc_account_sel_set_new_account_ability (GNC_ACCOUNT_SEL (gas), true);
    gnc_account_sel_set_acct_filters (GNC_ACCOUNT_SEL (gas), acct_list, curr_list);
    gtk_widget_show (gas);
    gtk_grid_attach (GTK_GRID(table), gas, 1, row, 1, 1);
    gtk_label_set_mnemonic_widget (GTK_LABEL(label), gas);
    g_list_free (acct_list);
    g_list_free (curr_list);
    return gas;
}

static GtkWidget * create_gae (GtkBuilder *builder, gint row, gnc_commodity *comm,
                               const gchar *table_ID, const gchar *label_ID)
{
    // shares amount
    auto table = get_widget (builder, table_ID);
    auto label = get_widget (builder, label_ID);
    auto info = gnc_commodity_print_info (comm, true);
    auto gae = gnc_amount_edit_new ();
    gnc_amount_edit_set_evaluate_on_enter (GNC_AMOUNT_EDIT (gae), TRUE);
    gnc_amount_edit_set_print_info (GNC_AMOUNT_EDIT (gae), info);
    gtk_grid_attach (GTK_GRID(table), gae, 1, row, 1, 1);
    gtk_widget_show (gae);
    gnc_amount_edit_make_mnemonic_target (GNC_AMOUNT_EDIT (gae), label);
    return gae;
}

static GtkWidget *
stock_assistant_create (StockTransactionInfo *info)
{
    auto builder = gtk_builder_new();
    gnc_builder_add_from_file  (builder , "assistant-stock-transaction.glade", "stock_transaction_assistant");
    auto window = get_widget (builder, "stock_transaction_assistant");
    info->window = window;

    // Set the name for this assistant so it can be easily manipulated with css
    gtk_widget_set_name (GTK_WIDGET(window), "gnc-id-assistant-stock-transaction");

    auto balance = xaccAccountGetBalance (info->acct);
    info->txn_types = gnc_numeric_zero_p (balance) ? starting_types :
        gnc_numeric_positive_p (balance) ? open_types : short_types;

    auto commodity = xaccAccountGetCommodity (info->acct);
    info->currency = gnc_account_get_currency_or_parent (info->acct);

    /* Transaction Page Widgets */
    auto table = get_widget (builder, "transaction_type_table");
    info->transaction_type_page = get_widget (builder, "transaction_type_page");
    info->transaction_type_combo = get_widget (builder, "transaction_type_page_combobox");
    info->transaction_type_explanation = get_widget (builder, "transaction_type_page_explanation");
    fill_transaction_types (info->transaction_type_combo, info);
    g_signal_connect (info->transaction_type_combo, "changed",
                      G_CALLBACK (refresh_page_transaction_type), info);

    /* Transaction Details Widgets */
    table = get_widget (builder, "transaction_details_table");
    info->transaction_details_page = get_widget (builder, "transaction_details_page");
    auto date = gnc_date_edit_new (gnc_time (NULL), FALSE, FALSE);
    auto label = get_widget (builder, "transaction_date_label");
    gtk_grid_attach (GTK_GRID(table), date, 1, 0, 1, 1);
    gtk_widget_show (date);
    info->date_edit = date;
    info->transaction_description_entry = get_widget (builder, "transaction_description_entry");
    gnc_date_make_mnemonic_target (GNC_DATE_EDIT(date), label);

    /* Stock Amount Page Widgets */
    info->stock_amount_page = get_widget (builder, "stock_amount_page");
    info->prev_amount = get_widget (builder, "prev_balance_amount");
    info->stock_amount_edit = create_gae (builder, 1, commodity, "stock_amount_table", "stock_amount_label");
    info->next_amount = get_widget (builder, "next_balance_amount");
    g_signal_connect (info->stock_amount_edit, "changed",
                      G_CALLBACK (refresh_page_stock_amount), info);

    /* Stock Value Page Widgets */
    info->stock_value_page = get_widget (builder, "stock_value_page");
    info->stock_value_edit = create_gae (builder, 0, info->currency, "stock_value_table", "stock_value_label");
    info->price_amount = get_widget (builder, "stock_price_amount");
    info->stock_memo_edit = get_widget (builder, "stock_memo_edit");
    g_signal_connect (info->stock_value_edit, "changed",
                      G_CALLBACK (refresh_page_stock_value), info);

    /* Cash Page Widgets */
    table = get_widget (builder, "cash_table");
    info->cash_page = get_widget (builder, "cash_details_page");
    info->cash_account = create_gas (builder, 0, { ACCT_TYPE_ASSET, ACCT_TYPE_BANK }, info->currency,  "cash_table", "cash_account_label");
    info->cash_amount = create_gae (builder, 1, info->currency, "cash_table", "cash_label");
    info->cash_memo_edit = get_widget (builder, "cash_memo_entry");

    /* Fees Page Widgets */
    table = get_widget (builder, "fees_table");
    info->fees_page = get_widget (builder, "fees_details_page");
    auto capitalize_check = get_widget (builder, "capitalize_fees_checkbutton");
    info->capitalize_fees_checkbox = capitalize_check;
    info->fees_account = create_gas (builder, 1, { ACCT_TYPE_EXPENSE }, info->currency, "fees_table", "fees_account_label");
    info->fees_amount = create_gae (builder, 2, info->currency, "fees_table", "fees_label");
    info->fees_memo_edit = get_widget (builder, "cash_memo_entry");
    g_signal_connect (capitalize_check, "toggled",
                      G_CALLBACK (refresh_page_fees), info);

    /* Capgains Page Widgets */
    table = get_widget (builder, "capgains_table");
    info->capgains_page = get_widget (builder, "capgains_details_page");
    info->capgains_account = create_gas (builder, 0, { ACCT_TYPE_INCOME }, info->currency, "capgains_table", "capgains_account_label");
    info->capgains_amount = create_gae (builder, 1, info->currency, "capgains_table", "capgains_label");
    info->capgains_memo_edit = get_widget (builder, "capgains_memo_entry");

    /* Finish Page Widgets */
    info->finish_page = get_widget (builder, "finish_page");
    info->split_view = get_widget (builder, "transaction_view");
    auto view = GTK_TREE_VIEW (info->split_view);
    gtk_tree_view_set_grid_lines (GTK_TREE_VIEW(view), gnc_tree_view_get_grid_lines_pref ());
    auto store = gtk_list_store_new (NUM_SPLIT_COLS, G_TYPE_STRING, G_TYPE_STRING,
                                     G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_view_set_model(view, GTK_TREE_MODEL(store));
    g_object_unref(store);

    auto renderer = gtk_cell_renderer_text_new();
    auto column = gtk_tree_view_column_new_with_attributes
        (_("Account"), renderer, "text", SPLIT_COL_ACCOUNT, nullptr);
    gtk_tree_view_append_column(view, column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes
        (_("Memo"), renderer, "text", SPLIT_COL_MEMO, nullptr);
    gtk_tree_view_append_column(view, column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes
        (_("Debit"), renderer, "text", SPLIT_COL_DEBIT, nullptr);
    gtk_tree_view_append_column(view, column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes
        (_("Credit"), renderer, "text", SPLIT_COL_CREDIT, nullptr);
    gtk_tree_view_append_column(view, column);

    g_signal_connect (G_OBJECT(window), "destroy",
                      G_CALLBACK (stock_assistant_window_destroy_cb), info);

    gtk_assistant_set_forward_page_func (GTK_ASSISTANT(window),
                                         (GtkAssistantPageFunc)forward_page_func,
                                         info, nullptr);
    gtk_builder_connect_signals(builder, info);
    g_object_unref(G_OBJECT(builder));

    // initialize sensitivites and checkbutton by calling cb functions.
    refresh_page_transaction_type (info->transaction_type_combo, info);
    refresh_page_fees (info->capitalize_fees_checkbox, info);

    return window;
}

static void
refresh_handler (GHashTable *changes, gpointer user_data)
{
    StockTransactionInfo *info = (StockTransactionInfo *)user_data;
    Account *account = info->acct;

    // fixme: to test that account is still available (hasn't been deleted
    // after launching assistant!!!)
    if (false)
    {
        gnc_close_gui_component_by_data (ASSISTANT_STOCK_TRANSACTION_CM_CLASS, info);
        return;
    }
}

static void
close_handler (gpointer user_data)
{
    StockTransactionInfo *info = (StockTransactionInfo *)user_data;

    gtk_widget_destroy (info->window);
}

/********************************************************************\
 * gnc_stock_transaction_dialog                                           *
 *   opens up a window to record a stock transaction                      *
 *                                                                  *
 * Args:   parent  - the parent ofthis window                       *
 *         initial - the initial account to use                     *
 * Return: nothing                                                  *
\********************************************************************/
void
gnc_stock_transaction_dialog (GtkWidget *parent, Account * initial)
{
    StockTransactionInfo *info = g_new0 (StockTransactionInfo, 1);

    info->acct = initial;

    stock_assistant_create (info);

    auto component_id = gnc_register_gui_component (ASSISTANT_STOCK_TRANSACTION_CM_CLASS,
                                                    refresh_handler, close_handler, info);

    gnc_gui_component_watch_entity_type (component_id, GNC_ID_ACCOUNT,
                                         QOF_EVENT_MODIFY | QOF_EVENT_DESTROY);

    gtk_window_set_transient_for (GTK_WINDOW (info->window), GTK_WINDOW(parent));
    gtk_widget_show_all (info->window);

    gnc_window_adjust_for_screen (GTK_WINDOW(info->window));
}
