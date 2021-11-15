/********************************************************************\
 * dialog-stock-editor.c -- UI for stock editing                    *
 * Copyright (C) 2020 Christopher Lam                               *
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

#include "dialog-utils.h"
#include "engine-helpers.h"
#include "gnc-account-sel.h"
#include "gnc-amount-edit.h"
#include "gnc-date-edit.h"
#include "gnc-component-manager.h"
#include "gnc-event.h"
#include "gnc-gnome-utils.h"
#include "gnc-glib-utils.h"
#include "gnc-helpers.h"
#include "gnc-main-window.h"
#include "gnc-plugin-page-register.h"
#include "gnc-session.h"
#include "gnc-ui.h"
#include "gnc-ui-balances.h"
#include "dialog-stock-editor.h"

static QofLogModule log_module = GNC_MOD_GUI;

static const char *PROP_STOCK_PROCEEDS = "stock-proceeds";
static const char *PROP_STOCK_DIVIDEND = "stock-dividend";
static const char *PROP_STOCK_CAPGAINS = "stock-capgains";
static const char *PROP_STOCK_EXPENSES = "stock-expenses";

enum
{
     ACTION_COL_LABEL = 0,
     ACTION_COL_STOCKAMT_MASK,
     ACTION_COL_STOCKVAL_MASK,
     ACTION_COL_PROCEEDS_MASK,
     ACTION_COL_PROCEEDS,
     ACTION_COL_DIVIDEND_MASK,
     ACTION_COL_DIVIDEND,
     ACTION_COL_CAPGAINS_MASK,
     ACTION_COL_CAPGAINS,
     ACTION_COL_EXPENSES_MASK,
     ACTION_COL_EXPENSES,
     ACTION_COL_NUM_COLUMNS
};

enum
{
     MASK_DISABLED = 0,
     MASK_POSITIVE = 1,
     MASK_ZERO     = 2,
     MASK_NEGATIVE = 4,
};

/** STRUCTS *********************************************************/

typedef struct AccountData
{
    GtkWidget *page;
    GtkWidget *account_sel;
    GtkWidget *amount_edit;
    GtkWidget *desc;
    GtkWidget *memo;
} AccountData;


typedef struct StockAccountData
{
    GtkWidget *page;
    GtkWidget *amount_edit;
    GtkWidget *new_bal;
    GtkWidget *value_edit;
    GtkWidget *price_label;
    GtkWidget *desc;
    GtkWidget *memo;
} StockAccountData;

typedef struct StockEditorWindow
{
    Account *asset_account;        /* The stock account */
    gnc_commodity *trans_currency;
    time64 latest_split_date;
    gint component_id;       /* id of component */

    GtkWidget *window;       /* The stock-editor window                 */
    GtkWidget *date_entry;
    GtkWidget *action_combobox;

    GtkWidget *current_balance_label;
    GtkWidget *new_balance_label;
    GtkWidget *price_label;

    GtkWidget *warning_icon;
    GtkWidget *warning_text;

    GtkWidget *assistant;
    StockAccountData *stock_data;
    AccountData *proceeds_data;
    AccountData *dividend_data;
    AccountData *capgains_data;
    AccountData *fees_exp_data;
    AccountData *fees_cap_data;

    GtkWidget *proceeds_acc;
    GtkWidget *dividend_acc;
    GtkWidget *capgains_acc;
    GtkWidget *expenses_acc;

    GNCAmountEdit *stockamt_val;
    GNCAmountEdit *stockval_val;
    GNCAmountEdit *proceeds_val;
    GNCAmountEdit *dividend_val;
    GNCAmountEdit *capgains_val;
    GNCAmountEdit *capbasis_val;
    GNCAmountEdit *expenses_val;

    GtkWidget *stockacc_memo;
    GtkWidget *proceeds_memo;
    GtkWidget *dividend_memo;
    GtkWidget *capgains_memo;
    GtkWidget *expenses_memo;
    GtkWidget *description_entry;

    GtkWidget *auto_capgain;

    GtkWidget *ok_button;
    GtkWidget *cancel_button;
} StockEditorWindow;


static void
stockeditor_set_title (GtkWidget *window, Account *account)
{
    gchar *fullname = gnc_account_get_full_name (account);
    gchar *title = g_strconcat (fullname, " - ", _("Stock Editor"), NULL);
    gtk_window_set_title (GTK_WINDOW (window), title);
    g_free (fullname);
    g_free (title);
}

static void
stock_editor_destroy (StockEditorWindow *data)
{
    gtk_widget_destroy (data->window);
    g_free (data);
}

static void
cancel_button_cb (GtkWidget *widget, StockEditorWindow *data)
{
    stock_editor_destroy (data);
}

static gboolean
amount_edit_unfocus (GtkWidget *widget, const StockEditorWindow *data)
{
    GtkEntry *entry = GNC_AMOUNT_EDIT (widget)->entry;
    const gchar *text = gtk_entry_get_text (entry);
    GError *error = NULL;

    if (text[0] == '\0') return FALSE;

    if (!gnc_amount_edit_evaluate (GNC_AMOUNT_EDIT (widget), &error))
    {
        /* fixme */
        /* gnc_error_dialog (GTK_WINDOW(priv->parent), "%s", error->message); */
        g_error_free (error);
        return FALSE;
    };

    return TRUE;
}


static Account *
account_sel_get_account (GtkWidget *gae)
{
    if (!gtk_widget_get_sensitive (GTK_WIDGET (gae))) return NULL;
    return gnc_account_sel_get_account (GNC_ACCOUNT_SEL (gae));
}

static gnc_numeric
amount_edit_get_amount (GNCAmountEdit *gae)
{
    gnc_numeric retval = gnc_numeric_zero ();
    if (!gtk_widget_get_sensitive (GTK_WIDGET (gae))) return retval;
    if (!gnc_amount_edit_expr_is_valid (gae, &retval, FALSE, NULL)) return retval;
    return gnc_numeric_zero ();
}


static void
check_acct (GtkWidget *gas, GNCAmountEdit *gae,
            GList **status, gchar *type, gboolean *passes)
{
    Account *acct;
    if (!gtk_widget_get_sensitive (gas) ||
        (gnc_numeric_zero_p (amount_edit_get_amount (gae))))
        return;

    acct = gnc_account_sel_get_account (GNC_ACCOUNT_SEL (gas));
    if (!acct)
    {
        *status = g_list_prepend
            (*status, g_strdup_printf (_("Account %s missing"), type));
        *passes = FALSE;
        return;
    }
    else if (xaccAccountGetPlaceholder (acct))
    {
        *status = g_list_prepend
            (*status, g_strdup_printf (_("Account %s cannot be placeholder"), type));
        *passes = FALSE;
        return;
    }

    return;
}

static void
check_signs (GNCAmountEdit *gae, gint mask, GList **status,
             gchar *type, gboolean *passes)
{
    GList *signs = NULL;
    gchar *sign_str;
    gnc_numeric num = amount_edit_get_amount (gae);
    int cmp = gnc_numeric_compare (num, gnc_numeric_zero ());
    unsigned val_mask = cmp > 0 ? MASK_POSITIVE : cmp < 0 ? MASK_NEGATIVE : MASK_ZERO;

    if (!mask)
    {
        gtk_entry_set_placeholder_text (gae->entry, NULL);
        return;
    }

    if (mask & MASK_POSITIVE)
        signs = g_list_prepend (signs, g_strdup ("positive"));
    if (mask & MASK_NEGATIVE)
        signs = g_list_prepend (signs, g_strdup ("negative"));
    if (mask & MASK_ZERO)
        signs = g_list_prepend (signs, g_strdup ("zero"));

    sign_str = gnc_g_list_stringjoin (signs, " or ");

    if (!(val_mask & mask))
    {
        *passes = FALSE;
        *status = g_list_prepend (*status, g_strdup_printf (_("%s must be %s"),
                                                            type, sign_str));
    }

    gtk_entry_set_placeholder_text (gae->entry, sign_str);

    g_list_free_full (signs, g_free);
    g_free (sign_str);
    return;
}

static void
update_price (gnc_numeric amount, gnc_numeric basis, gnc_commodity *comm,
              const StockEditorWindow *data, GNCPrintAmountInfo printinfo)
{
    const gchar *label = "";
    if (!gnc_numeric_zero_p (amount))
    {
        gnc_numeric price = gnc_numeric_div
            (basis, amount, GNC_DENOM_AUTO, GNC_HOW_DENOM_EXACT | GNC_HOW_RND_ROUND);
        label = xaccPrintAmount (price, (gnc_price_print_info (comm, TRUE)));
    }
    gtk_label_set_text (GTK_LABEL (data->price_label), label);
    return;
}

static void
refresh_all (GtkWidget *widget, const StockEditorWindow *data)
{
    gnc_numeric bal, old_bal, new_bal;
    GNCPrintAmountInfo printinfo;
    gboolean passes;
    GList *status = NULL;
    gchar *status_str;
    gint stockamt_mask, stockval_mask, proceeds_mask, dividend_mask,
        capgains_mask, expenses_mask;
    gnc_numeric stockamt_val, stockval_val, proceeds_val, dividend_val, capgains_val,
        capbasis_val, expenses_val;
    Account *proceeds_acc, *dividend_acc, *capgains_acc, *expenses_acc;
    GtkTreeIter action_iter;

    if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (data->action_combobox),
                                        &action_iter))
    {
        PERR ("shouldn't happen. action should always select item.");
        return;
    }

    gnc_suspend_gui_refresh ();

    gtk_tree_model_get (gtk_combo_box_get_model
                        (GTK_COMBO_BOX (data->action_combobox)),
                        &action_iter,
                        ACTION_COL_STOCKAMT_MASK, &stockamt_mask,
                        ACTION_COL_STOCKVAL_MASK, &stockval_mask,
                        ACTION_COL_PROCEEDS_MASK, &proceeds_mask,
                        ACTION_COL_DIVIDEND_MASK, &dividend_mask,
                        ACTION_COL_CAPGAINS_MASK, &capgains_mask,
                        ACTION_COL_EXPENSES_MASK, &expenses_mask,
                        -1);

    printinfo = gnc_account_print_info (data->asset_account, TRUE);

    proceeds_acc   = account_sel_get_account (data->proceeds_acc);
    dividend_acc   = account_sel_get_account (data->dividend_acc);
    capgains_acc   = account_sel_get_account (data->capgains_acc);
    expenses_acc   = account_sel_get_account (data->expenses_acc);

    stockamt_val   = amount_edit_get_amount (data->stockamt_val);
    stockval_val   = amount_edit_get_amount (data->stockval_val);
    proceeds_val   = amount_edit_get_amount (data->proceeds_val);
    dividend_val   = amount_edit_get_amount (data->dividend_val);
    capgains_val   = amount_edit_get_amount (data->capgains_val);
    capbasis_val   = amount_edit_get_amount (data->capbasis_val);
    expenses_val   = amount_edit_get_amount (data->expenses_val);

    /* update current & new balances */
    old_bal = xaccAccountGetBalance (data->asset_account);
    gtk_label_set_text (GTK_LABEL (data->current_balance_label),
                        xaccPrintAmount (old_bal, printinfo));
    new_bal = gnc_numeric_add_fixed (old_bal, stockamt_val);
    gtk_label_set_text (GTK_LABEL (data->new_balance_label),
                        xaccPrintAmount (new_bal, printinfo));

    if (gtk_widget_get_sensitive (data->capgains_acc))
    {
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->auto_capgain)))
        {
            /* auto-capgain from basis */
            gnc_numeric bal = gnc_numeric_add_fixed (stockval_val, capbasis_val);
            gnc_amount_edit_set_amount (data->capgains_val, gnc_numeric_neg (bal));
        }
        else
        {
            /* auto-basis from capgain*/
            gnc_numeric bal = gnc_numeric_add_fixed (stockval_val, capgains_val);
            gnc_amount_edit_set_amount (data->capbasis_val, gnc_numeric_neg (bal));
        }
    }

    /* if adding stockamt_val to bal causes bal to change signs, it
       means the transaction sold more than available units. bail. */
    if (gnc_numeric_negative_p
        (gnc_numeric_mul
         (old_bal, new_bal, GNC_DENOM_AUTO, GNC_HOW_RND_ROUND)))
    {
        status = g_list_prepend
            (status, g_strdup (_("Cannot sell more units than available.")));
        passes = FALSE;
    }

    /* if a required account is missing, bail out. */
    check_acct (data->proceeds_acc, data->proceeds_val, &status, _("Proceeds"), &passes);
    check_acct (data->expenses_acc, data->expenses_val, &status, _("Fees"), &passes);
    check_acct (data->capgains_acc, data->capgains_val, &status, _("CapGains"), &passes);
    check_acct (data->dividend_acc, data->dividend_val, &status, _("Dividend"), &passes);

    /* warn if date < latest split - current balance will not be correct */
    if (time64CanonicalDayTime (gnc_date_edit_get_date (GNC_DATE_EDIT (data->date_entry))) <
        time64CanonicalDayTime (data->latest_split_date))
    {
        status = g_list_prepend
            (status, g_strdup (_("Date is before latest split. Balances may not \
be valid.")));
    }

    /* test the signs of various field amounts */
    check_signs (data->stockamt_val, stockamt_mask, &status, _("Units"), &passes);
    check_signs (data->stockval_val, stockval_mask, &status, _("Basis"), &passes);
    check_signs (data->proceeds_val, proceeds_mask, &status, _("Proceeds"), &passes);
    check_signs (data->dividend_val, dividend_mask, &status, _("Dividend"), &passes);
    check_signs (data->capgains_val, capgains_mask, &status, _("CapGains"), &passes);
    check_signs (data->expenses_val, expenses_mask, &status, _("Fees"), &passes);
    update_price (stockamt_val, stockval_val, data->trans_currency, data, printinfo);

    /* test for imbalance */
    bal = stockval_val;
    bal = gnc_numeric_add_fixed (bal, proceeds_val);
    bal = gnc_numeric_add_fixed (bal, dividend_val);
    bal = gnc_numeric_add_fixed (bal, expenses_val);
    printinfo = gnc_commodity_print_info (data->trans_currency, TRUE);
    if (!gnc_numeric_zero_p (bal))
    {
        status = g_list_prepend
            (status, g_strdup_printf (_("Imbalance of %s"),
                                      xaccPrintAmount (bal, printinfo)));
        passes = FALSE;
    }

    gtk_widget_set_visible (data->warning_icon, !passes);
    gtk_widget_set_sensitive (data->ok_button, passes);

    status = g_list_reverse (status);
    status_str = gnc_g_list_stringjoin (status, "\n");
    gtk_label_set_text (GTK_LABEL (data->warning_text), status_str);
    g_list_free_full (status, g_free);
    g_free (status_str);

    gnc_resume_gui_refresh ();

    return;
}

static void
capgains_cb (GtkWidget *widget, const StockEditorWindow *data)
{
    gboolean has_capg = gtk_widget_get_sensitive (data->capgains_acc);
    gboolean auto_capgain = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->auto_capgain));

    gtk_widget_set_sensitive (GTK_WIDGET (data->capgains_val), has_capg && !auto_capgain);
    gtk_widget_set_sensitive (GTK_WIDGET (data->capbasis_val), has_capg && auto_capgain);
    gtk_widget_set_sensitive (GTK_WIDGET (data->auto_capgain), has_capg);
    refresh_all (widget, data);
}

static void
action_changed_cb (GtkWidget *widget, const StockEditorWindow *data)
{
    GtkTreeIter action_iter;
    GtkTreeModel *model;
    gchar *stockacc_memo, *proceeds_memo, *dividend_memo,
        *capgains_memo, *expenses_memo;
    gint stockamt_mask, stockval_mask, proceeds_mask, dividend_mask,
        capgains_mask, expenses_mask;

    /* check action combobox and update visibility of fields*/
    if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (data->action_combobox),
                                        &action_iter))
    {
        PERR ("shouldn't happen. action should always select item.");
        return;
    }

    model = gtk_combo_box_get_model (GTK_COMBO_BOX (data->action_combobox));
    gtk_tree_model_get (model, &action_iter,
                        ACTION_COL_STOCKAMT_MASK, &stockamt_mask,
                        ACTION_COL_STOCKVAL_MASK, &stockval_mask,
                        ACTION_COL_PROCEEDS_MASK, &proceeds_mask,
                        ACTION_COL_DIVIDEND_MASK, &dividend_mask,
                        ACTION_COL_CAPGAINS_MASK, &capgains_mask,
                        ACTION_COL_EXPENSES_MASK, &expenses_mask,
                        ACTION_COL_PROCEEDS, &proceeds_memo,
                        ACTION_COL_DIVIDEND, &dividend_memo,
                        ACTION_COL_CAPGAINS, &capgains_memo,
                        ACTION_COL_EXPENSES, &expenses_memo, -1);
    gtk_widget_set_sensitive (GTK_WIDGET (data->stockamt_val), stockamt_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->stockval_val), stockval_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->proceeds_val), proceeds_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->proceeds_acc), proceeds_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->proceeds_memo), proceeds_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->dividend_val), dividend_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->dividend_acc), dividend_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->dividend_memo), dividend_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->capgains_val), capgains_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->capgains_acc), capgains_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->capgains_memo), capgains_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->expenses_val), expenses_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->expenses_acc), expenses_mask);
    gtk_widget_set_sensitive (GTK_WIDGET (data->expenses_memo), expenses_mask);

    gtk_entry_set_text (GTK_ENTRY (data->proceeds_memo), proceeds_memo);
    gtk_entry_set_text (GTK_ENTRY (data->dividend_memo), dividend_memo);
    gtk_entry_set_text (GTK_ENTRY (data->capgains_memo), capgains_memo);
    gtk_entry_set_text (GTK_ENTRY (data->expenses_memo), expenses_memo);

    capgains_cb (widget, data);
}

static void
create_split (Transaction * txn, Account *account, GtkWidget *memo,
              GNCAmountEdit *amount, GNCAmountEdit *value, gboolean reverse)
{
    Split * split;
    gnc_numeric amt, val;
    const gchar *memostr;

    /* if account is NULL it means the account widget is
       disabled. skip creating split. */
    if (!account)
        return;

    memostr = gtk_entry_get_text (GTK_ENTRY (memo));
    amt = amount ? amount_edit_get_amount (amount) : gnc_numeric_zero ();
    val = value  ? amount_edit_get_amount (value)  : gnc_numeric_zero ();

    split = xaccMallocSplit (gnc_get_current_book ());
    xaccAccountBeginEdit (account);
    xaccSplitSetAccount (split, account);
    xaccSplitSetParent (split, txn);
    xaccSplitSetAmount (split, reverse ? gnc_numeric_neg (amt) : amt);
    xaccSplitSetValue (split, reverse ? gnc_numeric_neg (val) : val);
    xaccSplitSetMemo (split, memostr);
    gnc_set_num_action (NULL, split, NULL, memostr);
    xaccAccountCommitEdit (account);
}

static void
ok_button_cb (GtkWidget *widget, StockEditorWindow *data)
{
    Account
        *proceeds_acc = account_sel_get_account (data->proceeds_acc),
        *dividend_acc = account_sel_get_account (data->dividend_acc),
        *capgains_acc = account_sel_get_account (data->capgains_acc),
        *expenses_acc = account_sel_get_account (data->expenses_acc);
    Transaction *txn = xaccMallocTransaction (gnc_get_current_book ());
    const time64 date = gnc_date_edit_get_date (GNC_DATE_EDIT (data->date_entry));
    const gchar *desc = gtk_entry_get_text (GTK_ENTRY (data->description_entry));

    gnc_suspend_gui_refresh ();

    xaccTransBeginEdit (txn);
    xaccTransSetCurrency (txn, data->trans_currency);
    xaccTransSetDatePostedSecsNormalized (txn, date);
    xaccTransSetDescription (txn, desc);

    create_split (txn, data->asset_account, data->stockacc_memo, data->stockamt_val, data->stockval_val, FALSE);
    create_split (txn, proceeds_acc, data->proceeds_memo, data->proceeds_val, data->proceeds_val, FALSE);
    create_split (txn, dividend_acc, data->dividend_memo, data->dividend_val, data->dividend_val, FALSE);
    create_split (txn, expenses_acc, data->expenses_memo, data->expenses_val, data->expenses_val, FALSE);

    if (capgains_acc)
    {
        create_split (txn, capgains_acc, data->capgains_memo, data->capgains_val, data->capgains_val, TRUE);
        create_split (txn, data->asset_account, data->capgains_memo, NULL, data->capgains_val, FALSE);
    }

    xaccTransCommitEdit (txn);

    gnc_resume_gui_refresh ();

    stock_editor_destroy (data);
}

static GtkWidget *
connect_account (GtkBuilder *builder, const gchar *id,
                 const gchar *id_box, StockEditorWindow *data, GList *types)
{
    GtkBox *box = GTK_BOX (gtk_builder_get_object (builder, id_box));
    GtkWidget *retval = gnc_account_sel_new ();
    GList *commodities = g_list_prepend (NULL, data->trans_currency);

    gnc_account_sel_set_acct_filters (GNC_ACCOUNT_SEL (retval), types, commodities);

    gtk_box_pack_start (box, retval, TRUE, TRUE, 0);
    g_signal_connect (retval, "account_sel_changed", G_CALLBACK (refresh_all), data);
    g_list_free (commodities);
    return retval;
}

static GNCAmountEdit *
connect_amount_edit (GtkBuilder *builder, const gchar *id,
                     const Account *account, StockEditorWindow *data)
{
    GtkBox *box = GTK_BOX (gtk_builder_get_object (builder, id));
    GNCAmountEdit *retval = GNC_AMOUNT_EDIT (gnc_amount_edit_new ());

    if (account)
    {
        GNCPrintAmountInfo print_info = gnc_account_print_info (account, FALSE);
        gint scu = xaccAccountGetCommoditySCU (account);
        gnc_amount_edit_set_print_info (GNC_AMOUNT_EDIT (retval), print_info);
        gnc_amount_edit_set_fraction (GNC_AMOUNT_EDIT (retval), scu);
    }
    gtk_box_pack_start (box, GTK_WIDGET (retval), TRUE, TRUE, 0);
    g_signal_connect (retval, "changed", G_CALLBACK (refresh_all), data);
    g_signal_connect (retval, "focus-out-event", G_CALLBACK (amount_edit_unfocus),
                      data);
    return retval;
}


static void
add_action (GtkListStore *store, gchar *text,
            int stockamt_mask, int stockval_mask,
            int proceeds_mask, gchar *proceeds,
            int dividend_mask, gchar *dividend,
            int capgains_mask, gchar *capgains,
            int expenses_mask, gchar *expenses)
{
    gtk_list_store_insert_with_values (store, NULL, -1,
                                       ACTION_COL_LABEL, text,
                                       ACTION_COL_STOCKAMT_MASK, stockamt_mask,
                                       ACTION_COL_STOCKVAL_MASK, stockval_mask,
                                       ACTION_COL_PROCEEDS_MASK, proceeds_mask,
                                       ACTION_COL_DIVIDEND_MASK, dividend_mask,
                                       ACTION_COL_CAPGAINS_MASK, capgains_mask,
                                       ACTION_COL_EXPENSES_MASK, expenses_mask,
                                       ACTION_COL_PROCEEDS, proceeds,
                                       ACTION_COL_DIVIDEND, dividend,
                                       ACTION_COL_CAPGAINS, capgains,
                                       ACTION_COL_EXPENSES, expenses, -1);
}

/* initializes action list. each action has metadata for fields.
 * label: label string
 * 4 field memos
 */
static void
initialize_action (GtkWidget *combobox, gnc_numeric balance)
{
    GtkListStore *store = gtk_list_store_new (ACTION_COL_NUM_COLUMNS,
                                              G_TYPE_STRING, /* label */
                                              G_TYPE_INT,    /* asset amount mask */
                                              G_TYPE_INT,    /* asset value mask */
                                              G_TYPE_INT,    /* proceeds mask */
                                              G_TYPE_STRING, /* proceeds_memo */
                                              G_TYPE_INT,    /* dividend mask */
                                              G_TYPE_STRING, /* dividend_memo */
                                              G_TYPE_INT,    /* capgains mask */
                                              G_TYPE_STRING, /* capgains_memo */
                                              G_TYPE_INT,    /* expenses mask */
                                              G_TYPE_STRING); /* expenses_memo */
    if (gnc_numeric_positive_p (balance))
    {
        add_action (store, _("Buy"),
                    MASK_POSITIVE,
                    MASK_POSITIVE,
                    MASK_NEGATIVE, _("Source"),
                    MASK_DISABLED, "",
                    MASK_DISABLED, "",
                    MASK_ZERO | MASK_POSITIVE, _("Fees"));

        add_action (store, _("Sell"),
                    MASK_NEGATIVE,
                    MASK_NEGATIVE,
                    MASK_POSITIVE, _("Proceeds"),
                    MASK_DISABLED, "",
                    MASK_NEGATIVE | MASK_ZERO | MASK_POSITIVE, _("Capgains"),
                    MASK_ZERO | MASK_POSITIVE, _("Fees"));

        add_action (store, _("Dividend"),
                    MASK_DISABLED,
                    MASK_DISABLED,
                    MASK_POSITIVE, _("Proceeds"),
                    MASK_NEGATIVE, _("Dividend"),
                    MASK_DISABLED, "",
                    MASK_ZERO | MASK_POSITIVE, _("Fees"));

        add_action (store, _("Dividend with reinvestment"),
                    MASK_POSITIVE,
                    MASK_POSITIVE,
                    MASK_POSITIVE, _("Proceeds"),
                    MASK_NEGATIVE, _("Dividend"),
                    MASK_DISABLED, "",
                    MASK_POSITIVE | MASK_ZERO, _("Fees"));

        add_action (store, _("Notional Distribution"),
                    MASK_DISABLED,
                    MASK_POSITIVE,
                    MASK_DISABLED, "",
                    MASK_NEGATIVE, _("Notional Distribution"),
                    MASK_DISABLED, "",
                    MASK_DISABLED, "");

        add_action (store, _("Return of Capital"),
                    MASK_DISABLED,
                    MASK_NEGATIVE,
                    MASK_POSITIVE, _("Proceeds"),
                    MASK_DISABLED, "",
                    MASK_DISABLED, "",
                    MASK_DISABLED, "");

        add_action (store, _("Stock Split"),
                    MASK_NEGATIVE | MASK_POSITIVE,
                    MASK_DISABLED,
                    MASK_DISABLED, "",
                    MASK_DISABLED, "",
                    MASK_DISABLED, "",
                    MASK_DISABLED, "");
    }
    else if (gnc_numeric_negative_p (balance))
    {
        add_action (store, _("Short Sell"),
                    MASK_NEGATIVE,
                    MASK_NEGATIVE,
                    MASK_POSITIVE, _("Source"),
                    MASK_DISABLED, "",
                    MASK_DISABLED, "",
                    MASK_POSITIVE | MASK_ZERO, _("Fees"));

        add_action (store, _("Short Buy"),
                    MASK_POSITIVE,
                    MASK_POSITIVE,
                    MASK_NEGATIVE, _("Proceeds"),
                    MASK_DISABLED, "",
                    MASK_POSITIVE | MASK_ZERO | MASK_NEGATIVE, _("Capgains"),
                    MASK_POSITIVE | MASK_ZERO, _("Fees"));

        add_action (store, _("Compensatory Dividend"),
                    MASK_DISABLED,
                    MASK_DISABLED,
                    MASK_NEGATIVE, _("Proceeds"),
                    MASK_POSITIVE, _("Dividend"),
                    MASK_DISABLED, "",
                    MASK_POSITIVE | MASK_ZERO, _("Fees"));

        add_action (store, _("Compensatory Notional Distribution"),
                    MASK_DISABLED,
                    MASK_NEGATIVE,
                    MASK_DISABLED, "",
                    MASK_POSITIVE, _("Notional Distribution"),
                    MASK_DISABLED, "",
                    MASK_DISABLED, "");

        add_action (store, _("Compensatory Return of Capital"),
                    MASK_DISABLED,
                    MASK_POSITIVE,
                    MASK_NEGATIVE, _("Proceeds"),
                    MASK_DISABLED, "",
                    MASK_DISABLED, "",
                    MASK_DISABLED, "");

        add_action (store, _("Stock Split"),
                    MASK_NEGATIVE | MASK_POSITIVE,
                    MASK_DISABLED,
                    MASK_DISABLED, "",
                    MASK_DISABLED, "",
                    MASK_DISABLED, "",
                    MASK_DISABLED, "");
    }
    else
    {
        add_action (store, _("Open Long"),
                    MASK_POSITIVE,
                    MASK_POSITIVE,
                    MASK_NEGATIVE, _("Source"),
                    MASK_DISABLED, "",
                    MASK_DISABLED, "",
                    MASK_POSITIVE | MASK_ZERO, _("Fees"));

        add_action (store, _("Open Short"),
                    MASK_NEGATIVE,
                    MASK_NEGATIVE,
                    MASK_POSITIVE, _("Source"),
                    MASK_DISABLED, "",
                    MASK_DISABLED, "",
                    MASK_POSITIVE | MASK_ZERO, _("Fees"));
    }
    gtk_combo_box_set_model (GTK_COMBO_BOX (combobox), GTK_TREE_MODEL (store));
    gtk_combo_box_set_active (GTK_COMBO_BOX (combobox), 0);
    g_object_unref (store);
}

static time64
account_get_latest_date (const Account *account)
{
    GList *last = g_list_last (xaccAccountGetSplitList (account));
    return last ? xaccTransGetDate (xaccSplitGetParent (last->data)) : -INT64_MAX;
}

static void combo_changed    (GtkComboBox*, StockEditorWindow*);
static void button_toggled   (GtkCheckButton*, GtkAssistant*);
static void assistant_cancel (GtkAssistant*, gpointer);
static void assistant_close  (GtkAssistant*, gpointer);

typedef struct {
    GtkWidget *widget;
    gint index;
    const gchar *title;
    GtkAssistantPageType type;
    gboolean complete;
} PageInfo;

static void
combo_changed (GtkComboBox *entry, StockEditorWindow *data)
{
    GtkAssistant *assistant = GTK_ASSISTANT (data->assistant);
    gint num = gtk_assistant_get_current_page (assistant);
    GtkWidget *page = gtk_assistant_get_nth_page (assistant, num);
    GtkTreeIter action_iter;
    gint stockamt_mask, stockval_mask, proceeds_mask, dividend_mask,
        capgains_mask, expenses_mask;

    if (!gtk_combo_box_get_active_iter (entry, &action_iter))
    {
        PERR ("shouldn't happen. action should always select item.");
        return;
    }

    gtk_tree_model_get (gtk_combo_box_get_model (entry), &action_iter,
                        ACTION_COL_STOCKAMT_MASK, &stockamt_mask,
                        ACTION_COL_STOCKVAL_MASK, &stockval_mask,
                        ACTION_COL_PROCEEDS_MASK, &proceeds_mask,
                        ACTION_COL_DIVIDEND_MASK, &dividend_mask,
                        ACTION_COL_CAPGAINS_MASK, &capgains_mask,
                        ACTION_COL_EXPENSES_MASK, &expenses_mask,
                        -1);

    gtk_widget_set_visible (data->stock_data->page, (stockamt_mask || stockval_mask));
    gtk_widget_set_visible (data->proceeds_data->page, proceeds_mask);
    gtk_widget_set_visible (data->capgains_data->page, capgains_mask);
    gtk_widget_set_visible (data->fees_cap_data->page, expenses_mask);
    gtk_widget_set_visible (data->fees_exp_data->page, expenses_mask);
    gtk_widget_set_visible (data->dividend_data->page, dividend_mask);
    gtk_widget_set_visible (data->capgains_data->page, capgains_mask);

    gtk_assistant_set_page_complete (assistant, page, TRUE);
}

/* If the check button is toggled, set the page as complete. Otherwise,
 * stop the user from progressing the next page. */
static void
button_toggled (GtkCheckButton *toggle, GtkAssistant *assistant)
{
    gboolean active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (toggle));
    gtk_assistant_set_page_complete (assistant, GTK_WIDGET (toggle), active);
}


/* If the dialog is cancelled, delete it from memory and then clean up after
 * the Assistant structure. */
static void
assistant_cancel (GtkAssistant *assistant,
                  gpointer data)
{
    gtk_widget_destroy (GTK_WIDGET (assistant));
}

/* This function is where you would apply the changes and destroy the assistant. */
static void
assistant_close (GtkAssistant *assistant,
                 gpointer data)
{
    g_print ("You would apply your changes now!\n");
    gtk_widget_destroy (GTK_WIDGET (assistant));
}

static StockAccountData *
add_assistant_stock_page (GtkWidget *grid, const Account *account)
{
    GtkWidget *label, *cell;
    StockAccountData *datum = g_new (StockAccountData, 1);
    gnc_numeric prev_bal = xaccAccountGetBalance (account);
    gnc_commodity *currency = xaccAccountGetCommodity (account);
    GNCPrintAmountInfo printinfo = gnc_commodity_print_info (currency, TRUE);

    datum->page = grid;

    label = gtk_label_new ("Previous Balance");
    gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
    cell = gtk_label_new (xaccPrintAmount (prev_bal, printinfo));
    gtk_grid_attach (GTK_GRID (grid), cell, 1, 0, 1, 1);

    label = gtk_label_new ("Number units purchased");
    gtk_grid_attach (GTK_GRID (grid), label, 0, 1, 1, 1);
    datum->amount_edit = GTK_WIDGET (gnc_amount_edit_new ());
    gtk_grid_attach (GTK_GRID (grid), datum->amount_edit, 1, 1, 1, 1);

    label = gtk_label_new ("New Balance");
    gtk_grid_attach (GTK_GRID (grid), label, 0, 2, 1, 1);
    datum->new_bal = gtk_label_new (xaccPrintAmount (prev_bal, printinfo));
    gtk_grid_attach (GTK_GRID (grid), datum->new_bal, 1, 2, 1, 1);

    gtk_grid_attach (GTK_GRID (grid),
                     gtk_separator_new (GTK_ORIENTATION_HORIZONTAL), 0, 3, 2, 1);

    label = gtk_label_new ("Value of units purchased");
    gtk_grid_attach (GTK_GRID (grid), label, 0, 4, 1, 1);
    datum->value_edit = GTK_WIDGET (gnc_amount_edit_new ());
    gtk_grid_attach (GTK_GRID (grid), datum->value_edit, 1, 4, 1, 1);

    label = gtk_label_new ("Price of units purchased");
    gtk_grid_attach (GTK_GRID (grid), label, 0, 5, 1, 1);
    datum->price_label = gtk_label_new (NULL);
    gtk_grid_attach (GTK_GRID (grid), datum->price_label, 1, 5, 1, 1);

    return datum;
}

static AccountData *
add_assistant_account_page (GtkWidget *grid,
                            gchar *account_label, gchar *amount_label,
                            gchar *description_label, gchar *memo_label,
                            gchar *explanation_label)
{
    GtkWidget *cell;
    AccountData *datum = g_new (AccountData, 1);

    datum->page = grid;

    cell = gtk_label_new (account_label);
    gtk_grid_attach (GTK_GRID (grid), cell, 0, 0, 1, 1);
    datum->account_sel = GTK_WIDGET (gnc_account_sel_new ());
    gtk_grid_attach (GTK_GRID (grid), datum->account_sel, 1, 0, 1, 1);

    cell = gtk_label_new (amount_label);
    gtk_grid_attach (GTK_GRID (grid), cell, 0, 1, 1, 1);
    datum->amount_edit = GTK_WIDGET (gnc_amount_edit_new ());
    gtk_grid_attach (GTK_GRID (grid), datum->amount_edit, 1, 1, 1, 1);

    cell = gtk_label_new (description_label);
    gtk_grid_attach (GTK_GRID (grid), cell, 0, 2, 1, 1);
    datum->desc = GTK_WIDGET (gtk_entry_new ());
    gtk_grid_attach (GTK_GRID (grid), datum->desc, 1, 2, 1, 1);

    cell = gtk_label_new (memo_label);
    gtk_grid_attach (GTK_GRID (grid), cell, 0, 3, 1, 1);
    datum->memo = GTK_WIDGET (gtk_entry_new ());
    gtk_grid_attach (GTK_GRID (grid), datum->memo, 1, 3, 1, 1);

    cell = gtk_label_new (explanation_label);
    gtk_grid_attach (GTK_GRID (grid), cell, 0, 4, 2, 1);

    return datum;
}

/********************************************************************   \
 * stockeditorWindow                                                *
 *   opens up the window to stock-editor                            *
 *                                                                  *
 * Args:   parent  - the parent of this window                      *
 *         account - the account to stock-edit                      *
\********************************************************************/
void gnc_ui_stockeditor_dialog (GtkWidget *parent, Account *account)
{
    GtkBox *box;
    GtkBuilder *builder;
    GList *types;
    StockEditorWindow *data;
    GtkWidget *combo, *label, *button, *progress, *hbox, *gae;
    guint i;
    PageInfo page[9] = {
        { NULL, -1, "Introduction",           GTK_ASSISTANT_PAGE_INTRO,    TRUE},
        { NULL, -1, "Select Action",          GTK_ASSISTANT_PAGE_CONTENT,  TRUE},
        { NULL, -1, "Stock Account",          GTK_ASSISTANT_PAGE_CONTENT,  TRUE},
        { NULL, -1, "Proceeds Account",       GTK_ASSISTANT_PAGE_CONTENT,  TRUE},
        { NULL, -1, "Capitalized Fees",       GTK_ASSISTANT_PAGE_CONTENT,  TRUE},
        { NULL, -1, "Expensed Fees",          GTK_ASSISTANT_PAGE_CONTENT,  TRUE},
        { NULL, -1, "Dividend Account",       GTK_ASSISTANT_PAGE_CONTENT,  TRUE},
        { NULL, -1, "Capital Gains Account",  GTK_ASSISTANT_PAGE_CONTENT,  TRUE},
        { NULL, -1, "Confirmation",           GTK_ASSISTANT_PAGE_CONFIRM,  TRUE},
    };

    g_return_if_fail (parent);
    g_return_if_fail (GNC_IS_ACCOUNT (account));

    data = g_new (StockEditorWindow, 1);

    /* Create a new assistant widget with no pages. */
    data->assistant = gtk_assistant_new ();
    gtk_widget_set_size_request (data->assistant, 600, 400);
    gtk_window_set_title (GTK_WINDOW (data->assistant), "Stock Assistant");
    /* g_signal_connect (G_OBJECT (assistant), "destroy", */
    /*                   G_CALLBACK (gtk_main_quit), NULL); */

    page[0].widget = gtk_label_new ("Stock Assistant");
    page[1].widget = gtk_grid_new (); /* select action */
    page[2].widget = gtk_grid_new (); /* stock acct */
    page[3].widget = gtk_grid_new (); /* proceeds */
    page[4].widget = gtk_grid_new (); /* cap fees */
    page[5].widget = gtk_grid_new (); /* exp fees */
    page[6].widget = gtk_grid_new (); /* dividend */
    page[7].widget = gtk_grid_new (); /* capgains */
    page[8].widget = gtk_label_new ("Text has been entered in the label and the\n"\
                                    "combo box is clicked. If you are done, then\n"\
                                    "it is time to leave!");

    /* Action Page */
    label = gtk_label_new ("Select Action");
    combo = gtk_combo_box_text_new ();
    initialize_action (combo, xaccAccountGetBalance (account));
    gtk_grid_attach (GTK_GRID (page[1].widget), label, 0, 0, 1, 1);
    gtk_grid_attach (GTK_GRID (page[1].widget), combo, 1, 0, 1, 1);

    /* Stock Page */
    data->stock_data = add_assistant_stock_page (page[2].widget, account);

    data->proceeds_data = add_assistant_account_page
        (page[3].widget, "Proceeds Account", "Proceeds Amount",
         "Proceeds Description", "Proceeds Memo",
         "Source or destination of funds");

    data->fees_cap_data = add_assistant_account_page
        (page[4].widget, "Fees (capitalized) Account", "Fees (capitalized) Amount",
         "Fees (capitalized) Description", "Fees (capitalized) Memo",
         "Fees capitalized into stock account; this is "
         "usually only used on stock sell transactions");

    data->fees_exp_data = add_assistant_account_page
        (page[5].widget, "Fees (expensed) Account", "Fees (expensed) Amount",
         "Fees (expensed) Description", "Fees (expensed) Memo",
         "Fees expensed; applies to stock purchases.");

    data->dividend_data = add_assistant_account_page
        (page[6].widget, "Dividend Account", "Dividend Amount",
         "Dividend Description", "Dividend Memo",
         "Dividend amount recorded");

    data->capgains_data = add_assistant_account_page
        (page[7].widget, "Capital Gains Account", "Capital Gains Amount",
         "Capital Gains Description", "Capital Gains Memo",
         "Capital Gains recorded");

    /* Create the necessary widgets for the fourth page. The, Attach the progress bar
     * to the GtkAlignment widget for later access.*/
    /*
    button = gtk_button_new_with_label ("Click me!");
    progress = gtk_progress_bar_new ();
    hbox = gtk_hbox_new (FALSE, 5);
    gtk_box_pack_start (GTK_BOX (hbox), progress, TRUE, FALSE, 5);
    gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 5);
    gtk_container_add (GTK_CONTAINER (page[3].widget), hbox);
    g_object_set_data (G_OBJECT (page[3].widget), "pbar", (gpointer) progress);
    */

    /* Add five pages to the GtkAssistant dialog. */
    for (i = 0; i < 9; i++)
    {
        page[i].index = gtk_assistant_append_page (GTK_ASSISTANT (data->assistant),
                                                   page[i].widget);
        gtk_assistant_set_page_title (GTK_ASSISTANT (data->assistant),
                                      page[i].widget, page[i].title);
        gtk_assistant_set_page_type (GTK_ASSISTANT (data->assistant),
                                     page[i].widget, page[i].type);

        /* Set the introduction and conclusion pages as complete so they can be
         * incremented or closed. */
        gtk_assistant_set_page_complete (GTK_ASSISTANT (data->assistant),
                                         page[i].widget, page[i].complete);
    }

    /* Update whether pages 2 through 4 are complete based upon whether there is
     * text in the GtkEntry, the check button is active, or the progress bar
     * is completely filled. */
    g_signal_connect (G_OBJECT (combo), "changed",
                      G_CALLBACK (combo_changed), (gpointer) data);
    /* g_signal_connect (G_OBJECT (page[2].widget), "toggled", */
    /*                   G_CALLBACK (button_toggled), (gpointer) assistant); */
    g_signal_connect (G_OBJECT (data->assistant), "cancel",
                      G_CALLBACK (assistant_cancel), NULL);
    g_signal_connect (G_OBJECT (data->assistant), "close",
                      G_CALLBACK (assistant_close), NULL);

    gtk_widget_show_all (data->assistant);
    return;

    if (!xaccAccountIsPriced (account))
    {
        PWARN ("Stock Editor for Stock accounts only");
        return;
    }

    data->asset_account = account;
    data->trans_currency = gnc_account_get_currency_or_parent (account);
    data->latest_split_date = account_get_latest_date (account);

    /* Create the dialog box */
    builder = gtk_builder_new();

    gnc_builder_add_from_file (builder, "dialog-stock-editor.glade",
                               "stock_transaction_editor");

    /* window */
    data->window = GTK_WIDGET (gtk_builder_get_object
                               (builder, "stock_transaction_editor"));
    stockeditor_set_title (data->window, account);
    gtk_widget_set_name (GTK_WIDGET(data->window), "gnc-id-stock-editor");

    data->ok_button = GTK_WIDGET (gtk_builder_get_object (builder, "okbutton1"));
    data->cancel_button = GTK_WIDGET (gtk_builder_get_object (builder, "cancelbutton1"));

    data->date_entry = gnc_date_edit_new (gnc_time (NULL), FALSE, FALSE);
    g_signal_connect (data->date_entry, "date_changed", G_CALLBACK (refresh_all), data);
    box = GTK_BOX (gtk_builder_get_object (builder, "post_date_box"));
    gtk_box_pack_end (box, data->date_entry, TRUE, TRUE, 0);

    /* action */
    data->action_combobox = GTK_WIDGET (gtk_builder_get_object (builder, "action_combobox"));
    initialize_action (data->action_combobox, xaccAccountGetBalance (account));
    g_signal_connect (data->action_combobox, "changed", G_CALLBACK (action_changed_cb), data);

    /* description */
    data->description_entry = GTK_WIDGET (gtk_builder_get_object (builder, "description_entry"));

    /* current and new balances */
    data->current_balance_label = GTK_WIDGET (gtk_builder_get_object
                                              (builder, "current_balance_label"));
    data->new_balance_label = GTK_WIDGET (gtk_builder_get_object
                                         (builder, "new_balance_label"));
    gtk_label_set_text (GTK_LABEL (gtk_builder_get_object (builder, "trans_currency_label")),
                        gnc_commodity_get_mnemonic (data->trans_currency));
    data->price_label = GTK_WIDGET (gtk_builder_get_object (builder, "price_label"));

    /* warning text & icon */
    data->warning_text = GTK_WIDGET (gtk_builder_get_object (builder, "warning_text"));
    data->warning_icon = GTK_WIDGET (gtk_builder_get_object (builder, "warning_icon"));

    /* accounts */
    types = g_list_prepend (NULL, GINT_TO_POINTER (ACCT_TYPE_CASH));
    types = g_list_prepend (types, GINT_TO_POINTER (ACCT_TYPE_ASSET));
    types = g_list_prepend (types, GINT_TO_POINTER (ACCT_TYPE_BANK));
    data->proceeds_acc = connect_account (builder, PROP_STOCK_PROCEEDS, "proceeds_account_box", data, types);
    g_list_free (types);

    types = g_list_prepend (NULL, GINT_TO_POINTER (ACCT_TYPE_INCOME));
    data->dividend_acc = connect_account (builder, PROP_STOCK_DIVIDEND, "dividend_account_box", data, types);
    data->capgains_acc = connect_account (builder, PROP_STOCK_CAPGAINS, "capgains_account_box", data, types);
    g_list_free (types);

    types = g_list_prepend (NULL, GINT_TO_POINTER (ACCT_TYPE_EXPENSE));
    data->expenses_acc = connect_account (builder, PROP_STOCK_EXPENSES, "expenses_account_box", data, types);
    g_list_free (types);

    /* Add amount edit box */
    data->stockamt_val = connect_amount_edit (builder, "stockamt_box", account, data);
    data->stockval_val = connect_amount_edit (builder, "stockval_box", NULL, data);
    data->proceeds_val = connect_amount_edit (builder, "proceeds_box", NULL, data);
    data->dividend_val = connect_amount_edit (builder, "dividend_box", NULL, data);
    data->capgains_val = connect_amount_edit (builder, "capgains_box", NULL, data);
    data->capbasis_val = connect_amount_edit (builder, "capbasis_box", NULL, data);
    data->expenses_val = connect_amount_edit (builder, "expenses_box", NULL, data);

    data->auto_capgain = GTK_WIDGET (gtk_builder_get_object (builder, "auto_capgain_check"));

    g_signal_connect (data->auto_capgain, "toggled", G_CALLBACK (capgains_cb), data);

    g_signal_connect (data->ok_button, "clicked", G_CALLBACK (ok_button_cb), data);
    g_signal_connect (data->cancel_button, "clicked", G_CALLBACK (cancel_button_cb), data);

    data->stockacc_memo = GTK_WIDGET (gtk_builder_get_object (builder, "stockacc_memo"));
    data->proceeds_memo = GTK_WIDGET (gtk_builder_get_object (builder, "proceeds_memo"));
    data->dividend_memo = GTK_WIDGET (gtk_builder_get_object (builder, "dividend_memo"));
    data->capgains_memo = GTK_WIDGET (gtk_builder_get_object (builder, "capgains_memo"));
    data->expenses_memo = GTK_WIDGET (gtk_builder_get_object (builder, "expenses_memo"));

    /* Autoconnect signals */
    /* gtk_builder_connect_signals_full (builder, gnc_builder_connect_full_func, */
    /*                                   data->window); */
    if (parent)
        gtk_window_set_transient_for (GTK_WINDOW (data->window), GTK_WINDOW (parent));

    /* gtk_builder_connect_signals(builder, data); */
    g_object_unref (G_OBJECT (builder));

    gtk_widget_show_all (data->window);

    action_changed_cb (NULL, data);
    refresh_all (NULL, data);

    gtk_widget_grab_focus (GTK_WIDGET (data->action_combobox));

    gtk_window_present (GTK_WINDOW (data->window));
}
