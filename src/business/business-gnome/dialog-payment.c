/*
 * dialog-payment.c -- Dialog for payment entry
 * Copyright (C) 2002,2006 Derek Atkins
 * Author: Derek Atkins <warlord@MIT.EDU>
 * Copyright (c) 2006 David Hampton <hampton@employees.org>
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

#include "config.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "dialog-utils.h"
#include "gnc-component-manager.h"
#include "gnc-ui.h"
#include "gnc-gui-query.h"
#include "gnc-ui-util.h"
#include "qof.h"
#include "gnc-date-edit.h"
#include "gnc-amount-edit.h"
#include "gnc-gtk-utils.h"
#include "gnc-tree-view-account.h"
#include "Transaction.h"
#include "Account.h"
#include "gncOwner.h"

#include "gncInvoice.h"

#include "dialog-payment.h"
#include "business-gnome-utils.h"

#include "dialog-transfer.h"

#define DIALOG_PAYMENT_CUSTOMER_CM_CLASS "customer-payment-dialog"
#define DIALOG_PAYMENT_VENDOR_CM_CLASS "vendor-payment-dialog"

struct _payment_window
{
    GtkWidget   * dialog;

    GtkWidget   * num_entry;
    GtkWidget   * memo_entry;
    GtkWidget   * post_combo;
    GtkWidget   * owner_choice;
    GtkWidget   * amount_edit;
    GtkWidget   * date_edit;
    GtkWidget   * acct_tree;
    GtkWidget   * docs_list_tree_view;

    gint          component_id;
    QofBook     * book;
    GncOwner      owner;
    GncInvoice  * invoice;
    Account     * post_acct;
    GList       * acct_types;
    GList       * acct_commodities;

    Transaction * pre_existing_txn;
};

void gnc_ui_payment_window_set_num (PaymentWindow *pw, const char* num)
{
    g_assert(pw);
    gtk_entry_set_text(GTK_ENTRY (pw->num_entry), num);
}
void gnc_ui_payment_window_set_memo (PaymentWindow *pw, const char* memo)
{
    g_assert(pw);
    gtk_entry_set_text(GTK_ENTRY (pw->memo_entry), memo);
}
void gnc_ui_payment_window_set_date (PaymentWindow *pw, const GDate *date)
{
    g_assert(pw);
    g_assert(date);
    gnc_date_edit_set_gdate (GNC_DATE_EDIT (pw->date_edit), date);
}
void gnc_ui_payment_window_set_amount (PaymentWindow *pw, gnc_numeric amount)
{
    g_assert(pw);
    gnc_amount_edit_set_amount (GNC_AMOUNT_EDIT(pw->amount_edit), amount);
}
void gnc_ui_payment_window_set_postaccount (PaymentWindow *pw, const Account* account)
{
    g_assert(pw);
    g_assert(account);
    {
        gchar *acct_string = gnc_account_get_full_name (account);
        gnc_cbe_set_by_string(GTK_COMBO_BOX_ENTRY(pw->post_combo), acct_string);
        g_free(acct_string);
    }
}
void gnc_ui_payment_window_set_xferaccount (PaymentWindow *pw, const Account* account)
{
    g_assert(pw);
    g_assert(account);
    gnc_tree_view_account_set_selected_account(GNC_TREE_VIEW_ACCOUNT(pw->acct_tree),
            (Account*)account);
}

static gboolean gnc_payment_dialog_has_pre_existing_txn(const PaymentWindow* pw)
{
    return pw->pre_existing_txn != NULL;
}
int  gnc_payment_dialog_post_to_changed_cb (GtkWidget *widget, gpointer data);
void gnc_payment_dialog_document_selection_changed_cb (GtkWidget *widget, gpointer data);
void gnc_payment_ok_cb (GtkWidget *widget, gpointer data);
void gnc_payment_cancel_cb (GtkWidget *widget, gpointer data);
void gnc_payment_window_destroy_cb (GtkWidget *widget, gpointer data);
void gnc_payment_acct_tree_row_activated_cb (GtkWidget *widget, GtkTreePath *path,
        GtkTreeViewColumn *column, PaymentWindow *pw);


static void
gnc_payment_window_refresh_handler (GHashTable *changes, gpointer data)
{
    PaymentWindow *pw = data;

    pw->post_acct = gnc_account_select_combo_fill (pw->post_combo, pw->book, pw->acct_types, pw->acct_commodities);
}

static void
gnc_payment_window_close_handler (gpointer data)
{
    PaymentWindow *pw = data;

    if (pw)
        gtk_widget_destroy (pw->dialog);
}

static void
calculate_selected_total_helper (GtkTreeModel *model,
                                 GtkTreePath *path,
                                 GtkTreeIter *iter,
                                 gpointer data)
{
    gnc_numeric *subtotal = (gnc_numeric*) data;
    gnc_numeric cur_val;
    GValue value = { 0 };
    GNCLot *lot;
    Account *acct;
    gnc_commodity *currency;

    gtk_tree_model_get_value (model, iter, 5, &value);
    lot = (GNCLot *) g_value_get_pointer (&value);
    g_value_unset (&value);

    /* Find the amount's currency to determine the required precision */
    acct = gnc_lot_get_account (lot);
    currency = xaccAccountGetCommodity (acct);

    cur_val = gnc_lot_get_balance (lot);
    *subtotal = gnc_numeric_add (*subtotal, cur_val,
                                 gnc_commodity_get_fraction (currency), GNC_HOW_RND_ROUND_HALF_UP);
}

static gnc_numeric
gnc_payment_dialog_calculate_selected_total (PaymentWindow *pw)
{
    GtkTreeSelection *selection;
    GList *list=NULL, *node;
    gnc_numeric val = gnc_numeric_zero();

    if (!pw->docs_list_tree_view || !GTK_IS_TREE_VIEW(pw->docs_list_tree_view))
        return gnc_numeric_zero();

    /* Figure out if anything is set in the current list */
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(pw->docs_list_tree_view));

    gtk_tree_selection_selected_foreach (selection,
                                         calculate_selected_total_helper,
                                         (gpointer) &val);

    return val;
}

static void
gnc_payment_dialog_document_selection_changed (PaymentWindow *pw)
{
    GNCLot *lot;
    gnc_numeric val;

    /* Don't change the amount based on the selected documents
     * in case this payment is from a pre-existing txn
     */
    if (gnc_payment_dialog_has_pre_existing_txn (pw))
        return;

    /* Set the payment amount in the dialog */
    val = gnc_payment_dialog_calculate_selected_total (pw);
    gnc_ui_payment_window_set_amount(pw, val);
}

static gboolean
gnc_lot_match_owner (GNCLot *lot, gpointer user_data)
{
    const GncOwner *req_owner = user_data;
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

static void
gnc_payment_window_fill_docs_list (PaymentWindow *pw)
{
    GtkListStore *store;
    GList *list=NULL, *node;

    g_return_if_fail (pw->docs_list_tree_view && GTK_IS_TREE_VIEW(pw->docs_list_tree_view));

    /* Get a list of open lots for this owner and post account */
    if (pw->owner.owner.undefined)
        list = xaccAccountFindOpenLots (pw->post_acct, gnc_lot_match_owner,
                                        &pw->owner, NULL);

    /* Clear the existing list */
    store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(pw->docs_list_tree_view)));
    gtk_list_store_clear(store);

    /* Add the documents and overpayments to the tree view */
    for (node = list; node; node = node->next)
    {
        GNCLot *lot = node->data;
        const gchar *doc_date_str = NULL;
        const gchar *doc_num_str  = NULL;
        const gchar *doc_type_str = NULL;
        const gchar *doc_id_str   = NULL;
        const gchar *doc_deb_str  = NULL;
        const gchar *doc_cred_str = NULL;
        GtkTreeIter iter;
        Timespec doc_date;
        GncInvoice *document;
        gnc_numeric debit = gnc_numeric_zero();
        gnc_numeric credit = gnc_numeric_zero();
        GncInvoiceType doc_type = GNC_INVOICE_UNDEFINED;

        /* Find the lot's document if it exists,
         * it could also be a prepayment lot. */
        document = gncInvoiceGetInvoiceFromLot (lot);

        /* Find the document's date or pre-payment date */
        if (document)
            doc_date = gncInvoiceGetDatePosted (document);
        else
        {
            /* Calculate the payment date based on the lot splits */
            Transaction *trans = xaccSplitGetParent (gnc_lot_get_latest_split (lot));
            if (trans)
                doc_date = xaccTransRetDatePostedTS (trans);
            else
                continue; /* Not valid split in this lot, skip it */
        }
        doc_date_str = gnc_print_date (doc_date);

        /* Find the document type. No type means pre-payment in this case */
        if (document)
        {
            doc_type     = gncInvoiceGetType (document);
            doc_type_str = gncInvoiceGetTypeString (document);
        }
        else
            doc_type_str = _("Pre-Payment");

        /* Find the document id. Empty for pre-payments. */
        if (document)
        {
            doc_id_str = gncInvoiceGetID (document);
        }

        /* Find the debit/credit amount.
         * Invoices/bills are debit
         * Credit notes are credit
         * Pre-payments are credit
         */
        if (document)
        {
            if (!gncInvoiceGetIsCreditNote (document))
                debit = gnc_lot_get_balance (lot);
            else
                credit = gnc_lot_get_balance (lot);
        }
        else
        {
            /* This is a pre-payment */
                credit = gnc_lot_get_balance (lot);
        }

        /* Only display non-zero debits/credits */
        if (!gnc_numeric_zero_p (debit))
            doc_deb_str = xaccPrintAmount (debit, gnc_default_print_info (FALSE));
        if (!gnc_numeric_zero_p (credit))
            doc_cred_str = xaccPrintAmount (credit, gnc_default_print_info (FALSE));

        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter,
                            0, doc_date_str,
                            1, doc_id_str,
                            2, doc_type_str,
                            3, doc_deb_str,
                            4, doc_cred_str,
                            5, (gpointer)lot,
                            -1);

    }

    g_list_free (list);

    /* Highlight the preset invoice if it's in the new list */
    if (pw->invoice)
    {
        GtkTreeIter iter;
        GtkTreeModel *model = GTK_TREE_MODEL (store);
        GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(pw->docs_list_tree_view));

        if (gtk_tree_model_get_iter_first (model, &iter))
        {
            do
            {
                GValue value = { 0 };
                GNCLot *lot;
                GncInvoice *invoice;

                gtk_tree_model_get_value (model, &iter, 5, &value);
                lot = (GNCLot *) g_value_get_pointer (&value);
                g_value_unset (&value);


                invoice = gncInvoiceGetInvoiceFromLot (lot);
                if (!invoice)
                    continue;

                if (pw->invoice == invoice)
                {
                    gtk_tree_selection_select_iter (selection, &iter);
                    gnc_payment_dialog_document_selection_changed (pw);
                    break;
                }
            }
            while (gtk_tree_model_iter_next (model, &iter));
        }
    }
}

static void
gnc_payment_dialog_owner_changed (PaymentWindow *pw)
{
    Account *last_acct = NULL;
    Account *post_acct = NULL;
    GncGUID *guid = NULL;
    KvpValue* value;
    KvpFrame* slots;
    gchar *text;
    GncOwner *owner = &pw->owner;

    /* If the owner changed, the initial invoice is no longer valid */
    pw->invoice = NULL;

    /* Now handle the account tree */
    slots = gncOwnerGetSlots(owner);
    if (slots)
    {
        value = kvp_frame_get_slot_path(slots, "payment", "last_acct", NULL);
        if (value)
        {
            guid = kvp_value_get_guid(value);
        }
    }

    /* refresh the post and acc available accounts, but cleanup first */
    if (pw->acct_types)
    {
        g_list_free(pw->acct_types);
        pw->acct_types = NULL;
    }

    if (pw->acct_commodities)
    {
        g_list_free(pw->acct_commodities);
        pw->acct_commodities = NULL;
    }

    pw->acct_types = gncOwnerGetAccountTypesList(owner);
    if (gncOwnerIsValid(owner))
        pw->acct_commodities = gncOwnerGetCommoditiesList (owner);
    pw->post_acct = gnc_account_select_combo_fill (pw->post_combo, pw->book, pw->acct_types, pw->acct_commodities);

    /* Update list of documents and pre-payments */
    gnc_payment_window_fill_docs_list (pw);

    if (guid)
    {
        last_acct = xaccAccountLookup(guid, pw->book);
    }

    /* Set the last-used transfer account, but only if we didn't
     * create this dialog from a pre-existing transaction. */
    if (last_acct && !gnc_payment_dialog_has_pre_existing_txn(pw))
    {
        gnc_tree_view_account_set_selected_account(GNC_TREE_VIEW_ACCOUNT(pw->acct_tree),
                last_acct);
    }
}

static void
gnc_payment_dialog_post_to_changed (PaymentWindow *pw)
{
    gnc_payment_window_fill_docs_list (pw);
}

static void
gnc_payment_dialog_remember_account (PaymentWindow *pw, Account *acc)
{
    KvpValue* value;
    KvpFrame* slots = gncOwnerGetSlots(&pw->owner);

    if (!acc) return;
    if (!slots) return;

    value = kvp_value_new_guid(xaccAccountGetGUID(acc));
    if (!value) return;

    xaccAccountBeginEdit (acc);
    kvp_frame_set_slot_path(slots, value, "payment", "last_acct", NULL);
    qof_instance_set_dirty (QOF_INSTANCE (acc));
    xaccAccountCommitEdit (acc);
    kvp_value_delete(value);
}


static void
gnc_payment_set_owner (PaymentWindow *pw, GncOwner *owner)
{
    gnc_owner_set_owner (pw->owner_choice, owner);
    gnc_payment_dialog_owner_changed(pw);
}

static int
gnc_payment_dialog_owner_changed_cb (GtkWidget *widget, gpointer data)
{
    PaymentWindow *pw = data;
    GncOwner owner;

    if (!pw) return FALSE;

    gncOwnerCopy (&(pw->owner), &owner);
    gnc_owner_get_owner (pw->owner_choice, &owner);

    /* If this owner really changed, then reset ourselves */
    if (!gncOwnerEqual (&owner, &(pw->owner)))
    {
        gncOwnerCopy (&owner, &(pw->owner));
        gnc_payment_dialog_owner_changed(pw);
    }

    return FALSE;
}

void
gnc_payment_dialog_document_selection_changed_cb (GtkWidget *widget, gpointer data)
{
    PaymentWindow *pw = data;

    if (!pw) return;

    gnc_payment_dialog_document_selection_changed (pw);
}

int
gnc_payment_dialog_post_to_changed_cb (GtkWidget *widget, gpointer data)
{
    PaymentWindow *pw = data;
    Account *post_acct;

    if (!pw) return FALSE;

    post_acct = gnc_account_select_combo_get_active (pw->post_combo);

    /* If this invoice really changed, then reset ourselves */
    if (post_acct != pw->post_acct)
    {
        pw->post_acct = post_acct;
        gnc_payment_dialog_post_to_changed(pw);
    }

    return FALSE;
}

void
gnc_payment_ok_cb (GtkWidget *widget, gpointer data)
{
    PaymentWindow *pw = data;
    const char *text = NULL;
    Account *post, *acc;
    gnc_numeric amount;

    if (!pw)
        return;

    /* Verify the amount is non-zero */
    amount = gnc_amount_edit_get_amount (GNC_AMOUNT_EDIT (pw->amount_edit));
    if (gnc_numeric_check (amount) || !gnc_numeric_positive_p (amount))
    {
        text = _("You must enter the amount of the payment.  "
                 "The payment amount must be greater than zero.");
        gnc_error_dialog (pw->dialog, "%s", text);
        return;
    }

    /* Verify the user has selected an owner */
    gnc_owner_get_owner (pw->owner_choice, &(pw->owner));
    if (!gncOwnerIsValid(&pw->owner))
    {
        text = _("You must select a company for payment processing.");
        gnc_error_dialog (pw->dialog, "%s", text);
        return;
    }

    /* Verify the user has selected a transfer account */
    acc = gnc_tree_view_account_get_selected_account (GNC_TREE_VIEW_ACCOUNT(pw->acct_tree));
    if (!acc)
    {
        text = _("You must select a transfer account from the account tree.");
        gnc_error_dialog (pw->dialog, "%s", text);
        return;
    }

    /* Verify the "post" account */

    post = gnc_account_select_combo_get_active (pw->post_combo);
    if (!post)
    {
        text = _("Your must enter a valid account name for posting.");
        gnc_error_dialog (pw->dialog, "%s", text);
        return;
    }

    /* Ok, now post the damn thing */
    gnc_suspend_gui_refresh ();
    {
        const char *memo, *num;
        Timespec date;
        gnc_numeric exch = gnc_numeric_create(1, 1); //default to "one to one" rate

        /* Obtain all our ancillary information */
        memo = gtk_entry_get_text (GTK_ENTRY (pw->memo_entry));
        num = gtk_entry_get_text (GTK_ENTRY (pw->num_entry));
        date = gnc_date_edit_get_date_ts (GNC_DATE_EDIT (pw->date_edit));

        /* If the 'acc' account and the post account don't have the same
           currency, we need to get the user to specify the exchange rate */
        if (!gnc_commodity_equal(xaccAccountGetCommodity(acc), xaccAccountGetCommodity(post)))
        {
            XferDialog* xfer;

            text = _("The transfer and post accounts are associated with different currencies.  Please specify the conversion rate.");

            xfer = gnc_xfer_dialog(pw->dialog, acc);
            gnc_info_dialog(pw->dialog, "%s", text);

            gnc_xfer_dialog_select_to_account(xfer, post);
            gnc_xfer_dialog_set_amount(xfer, amount);

            /* All we want is the exchange rate so prevent the user from thinking
               it makes sense to mess with other stuff */
            gnc_xfer_dialog_set_from_show_button_active(xfer, FALSE);
            gnc_xfer_dialog_set_to_show_button_active(xfer, FALSE);
            gnc_xfer_dialog_hide_from_account_tree(xfer);
            gnc_xfer_dialog_hide_to_account_tree(xfer);
            gnc_xfer_dialog_is_exchange_dialog(xfer, &exch);
            gnc_xfer_dialog_run_until_done(xfer);
        }

        if (!gnc_payment_dialog_has_pre_existing_txn(pw))
        {
            /* Now apply the payment */
            gncOwnerApplyPayment (&pw->owner, pw->invoice,
                                  post, acc, amount, exch, date, memo, num);
        }
        else
        {
            // New implementation: Allow the user to have a
            // pre-selected transaction
            Transaction *txn = pw->pre_existing_txn;
            GncOwner *owner = &pw->owner;

            Split *xfer_split = xaccTransFindSplitByAccount(txn, acc);

            if (xaccTransGetCurrency(txn) != gncOwnerGetCurrency (owner))
            {
                g_message("Uh oh, mismatching currency/commodity between selected transaction and owner. We fall back to manual creation of a new transaction.");
                xfer_split = NULL;
            }

            if (!xfer_split)
            {
                g_message("Huh? Asset account not found anymore. Fully deleting old txn and now creating a new one.");

                xaccTransBeginEdit (txn);
                xaccTransDestroy (txn);
                xaccTransCommitEdit (txn);

                pw->pre_existing_txn = NULL;
                gncOwnerApplyPayment (owner, pw->invoice,
                                      post, acc, amount, exch, date, memo, num);
            }
            else
            {
                int i = 0;
                xaccTransBeginEdit (txn);
                while (i < xaccTransCountSplits(txn))
                {
                    Split *split = xaccTransGetSplit (txn, i);
                    if (split == xfer_split)
                    {
                        ++i;
                    }
                    else
                    {
                        xaccSplitDestroy(split);
                    }
                }

                // We have opened the txn for editing, and we have deleted all the other splits.
                gncOwnerAssignPaymentTxn (owner, txn,
                                          post, pw->invoice);

                xaccTransCommitEdit (txn);
            }
        }

    }
    gnc_resume_gui_refresh ();

    /* Save the transfer account, acc */
    gnc_payment_dialog_remember_account(pw, acc);

    gnc_ui_payment_window_destroy (pw);
}

void
gnc_payment_cancel_cb (GtkWidget *widget, gpointer data)
{
    PaymentWindow *pw = data;
    gnc_ui_payment_window_destroy (pw);
}

void
gnc_payment_window_destroy_cb (GtkWidget *widget, gpointer data)
{
    PaymentWindow *pw = data;

    if (!pw) return;

    gnc_unregister_gui_component (pw->component_id);

    g_list_free (pw->acct_types);
    g_list_free (pw->acct_commodities);
    g_free (pw);
}

void
gnc_payment_acct_tree_row_activated_cb (GtkWidget *widget, GtkTreePath *path,
                                        GtkTreeViewColumn *column, PaymentWindow *pw)
{
    GtkTreeView *view;
    GtkTreeModel *model;
    GtkTreeIter iter;

    g_return_if_fail(widget);
    view = GTK_TREE_VIEW(widget);

    model = gtk_tree_view_get_model(view);
    if (gtk_tree_model_get_iter(model, &iter, path))
    {
        if (gtk_tree_model_iter_has_child(model, &iter))
        {
            /* There are children,
             * just expand or collapse the row. */
            if (gtk_tree_view_row_expanded(view, path))
                gtk_tree_view_collapse_row(view, path);
            else
                gtk_tree_view_expand_row(view, path, FALSE);
        }
        else
            /* It's an account without any children, so click the Ok button. */
            gnc_payment_ok_cb(widget, pw);
    }
}

/* Select the list of accounts to show in the tree */
static void
gnc_payment_set_account_types (GncTreeViewAccount *tree)
{
    AccountViewInfo avi;
    int i;

    gnc_tree_view_account_get_view_info (tree, &avi);

    for (i = 0; i < NUM_ACCOUNT_TYPES; i++)
        switch (i)
        {
        case ACCT_TYPE_BANK:
        case ACCT_TYPE_CASH:
        case ACCT_TYPE_CREDIT:
        case ACCT_TYPE_ASSET:
        case ACCT_TYPE_LIABILITY:
            avi.include_type[i] = TRUE;
            break;
        default:
            avi.include_type[i] = FALSE;
            break;
        }

    gnc_tree_view_account_set_view_info (tree, &avi);
}

static gboolean
find_handler (gpointer find_data, gpointer user_data)
{
    PaymentWindow *pw = user_data;

    return (pw != NULL);
}

static PaymentWindow *
new_payment_window (GncOwner *owner, QofBook *book, GncInvoice *invoice)
{
    PaymentWindow *pw;
    GtkBuilder *builder;
    GtkWidget *box, *label;
    GtkTreeSelection *selection;
    char * cm_class = (gncOwnerGetType (owner) == GNC_OWNER_CUSTOMER ?
                       DIALOG_PAYMENT_CUSTOMER_CM_CLASS :
                       DIALOG_PAYMENT_VENDOR_CM_CLASS);

    /*
     * Find an existing payment window.  If found, bring it to
     * the front.  If we have an actual owner, then set it in
     * the window.
     */

    pw = gnc_find_first_gui_component (cm_class, find_handler, NULL);
    if (pw)
    {
        if (gncOwnerIsValid(owner))
            gnc_payment_set_owner (pw, owner);

        // Reset the setting about the pre-existing TXN
        pw->pre_existing_txn = NULL;

        gtk_window_present (GTK_WINDOW(pw->dialog));
        return(pw);
    }

    /* Ok, we need a new window */

    pw = g_new0 (PaymentWindow, 1);
    pw->book = book;
    gncOwnerCopy (owner, &(pw->owner));

    /* Compute the post-to account types */
    pw->acct_types = gncOwnerGetAccountTypesList (owner);

    if (gncOwnerIsValid(owner))
        pw->acct_commodities = gncOwnerGetCommoditiesList (owner);

    /* Open and read the Glade File */
    builder = gtk_builder_new();
    gnc_builder_add_from_file (builder, "dialog-payment.glade", "docs_list_hor_adj");
    gnc_builder_add_from_file (builder, "dialog-payment.glade", "docs_list_vert_adj");
    gnc_builder_add_from_file (builder, "dialog-payment.glade", "docs_list_model");
    gnc_builder_add_from_file (builder, "dialog-payment.glade", "post_combo_model");
    gnc_builder_add_from_file (builder, "dialog-payment.glade", "Payment Dialog");
    pw->dialog = GTK_WIDGET (gtk_builder_get_object (builder, "Payment Dialog"));

    /* Grab the widgets and build the dialog */
    pw->num_entry = GTK_WIDGET (gtk_builder_get_object (builder, "num_entry"));
    pw->memo_entry = GTK_WIDGET (gtk_builder_get_object (builder, "memo_entry"));
    pw->post_combo = GTK_WIDGET (gtk_builder_get_object (builder, "post_combo"));
    gtk_combo_box_entry_set_text_column( GTK_COMBO_BOX_ENTRY( pw->post_combo ), 0 );
    gnc_cbe_require_list_item(GTK_COMBO_BOX_ENTRY(pw->post_combo));

    label = GTK_WIDGET (gtk_builder_get_object (builder, "owner_label"));
    box = GTK_WIDGET (gtk_builder_get_object (builder, "owner_box"));
    pw->owner_choice = gnc_owner_select_create (label, box, book, owner);

    box = GTK_WIDGET (gtk_builder_get_object (builder, "amount_box"));
    pw->amount_edit = gnc_amount_edit_new ();
    gtk_box_pack_start (GTK_BOX (box), pw->amount_edit, TRUE, TRUE, 0);
    gnc_amount_edit_set_evaluate_on_enter (GNC_AMOUNT_EDIT (pw->amount_edit),
                                           TRUE);
    gnc_amount_edit_set_amount (GNC_AMOUNT_EDIT (pw->amount_edit), gnc_numeric_zero());

    box = GTK_WIDGET (gtk_builder_get_object (builder, "date_box"));
    pw->date_edit = gnc_date_edit_new (time(NULL), FALSE, FALSE);
    gtk_box_pack_start (GTK_BOX (box), pw->date_edit, TRUE, TRUE, 0);

    pw->docs_list_tree_view = GTK_WIDGET (gtk_builder_get_object (builder, "docs_list_tree_view"));
    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(pw->docs_list_tree_view));
    gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

    box = GTK_WIDGET (gtk_builder_get_object (builder, "acct_window"));
    pw->acct_tree = GTK_WIDGET(gnc_tree_view_account_new (FALSE));
    gtk_container_add (GTK_CONTAINER (box), pw->acct_tree);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW(pw->acct_tree), FALSE);
    gnc_payment_set_account_types (GNC_TREE_VIEW_ACCOUNT (pw->acct_tree));

    /* Set the dialog for the 'new' owner.
     * Note that this also sets the post account tree. */
    gnc_payment_dialog_owner_changed(pw);

    /* Set the dialog for the 'new' invoice */
    pw->invoice = invoice;
    if (invoice)
    {
        Account *postacct = gncInvoiceGetPostedAcc (invoice);
        if (postacct)
        {
            gchar *acct_string = gnc_account_get_full_name (postacct);
            gnc_cbe_set_by_string(GTK_COMBO_BOX_ENTRY(pw->post_combo), acct_string);
            gnc_payment_dialog_post_to_changed(pw);
            g_free(acct_string);
        }
    }

    /* Setup signals */
    gtk_builder_connect_signals_full( builder,
                                      gnc_builder_connect_full_func,
                                      pw);

    g_signal_connect (G_OBJECT (pw->owner_choice), "changed",
                      G_CALLBACK (gnc_payment_dialog_owner_changed_cb), pw);

    g_signal_connect (G_OBJECT (pw->acct_tree), "row-activated",
                      G_CALLBACK (gnc_payment_acct_tree_row_activated_cb), pw);

    /* Register with the component manager */
    pw->component_id =
        gnc_register_gui_component (cm_class,
                                    gnc_payment_window_refresh_handler,
                                    gnc_payment_window_close_handler,
                                    pw);

    /* Watch for any new or changed accounts */
    gnc_gui_component_watch_entity_type (pw->component_id,
                                         GNC_ID_ACCOUNT,
                                         QOF_EVENT_CREATE | QOF_EVENT_MODIFY |
                                         QOF_EVENT_DESTROY);

    /* Show it all */
    gtk_widget_show_all (pw->dialog);
    g_object_unref(G_OBJECT(builder));

    /* Warn the user if they have no valid post-to accounts */
    {
        const gchar *text;
        const char *acct_type;

        text = gtk_combo_box_get_active_text(GTK_COMBO_BOX(pw->post_combo));
        if (!text || safe_strcmp (text, "") == 0)
        {

            /* The code below assumes there will only be one account type.
             * Let's assert this to protect from potential future changes. */
            g_assert (g_list_length (pw->acct_types) == 1);
            acct_type = xaccAccountGetTypeStr(GPOINTER_TO_INT(pw->acct_types->data));
            gnc_warning_dialog(pw->dialog,
                               _("You have no valid \"Post To\" accounts.  "
                                 "Please create an account of type \"%s\" "
                                 "before you continue to process this payment.  "
                                 "Perhaps you want to create an Invoice or "
                                 "Bill first?"),
                               acct_type);
        }
    }

    return pw;
}


void
gnc_ui_payment_window_destroy (PaymentWindow *pw)
{
    if (!pw) return;
    gnc_close_gui_component (pw->component_id);
}

PaymentWindow *
gnc_ui_payment_new_with_invoice (const GncOwner *owner, QofBook *book,
                                 GncInvoice *invoice)
{
    GncOwner owner_def;

    if (!book) return NULL;
    if (owner)
    {
        /* Figure out the company */
        gncOwnerCopy (gncOwnerGetEndOwner (owner), &owner_def);
    }
    else
    {
        gncOwnerInitCustomer (&owner_def, NULL);
    }

    return new_payment_window (&owner_def, book, invoice);
}

PaymentWindow *
gnc_ui_payment_new (GncOwner *owner, QofBook *book)
{
    return gnc_ui_payment_new_with_invoice (owner, book, NULL);
}

// ////////////////////////////////////////////////////////////

static gboolean isAssetLiabType(GNCAccountType t)
{
    switch (t)
    {
    case ACCT_TYPE_RECEIVABLE:
    case ACCT_TYPE_PAYABLE:
        return FALSE;
    default:
        return (xaccAccountTypesCompatible(ACCT_TYPE_ASSET, t)
                || xaccAccountTypesCompatible(ACCT_TYPE_LIABILITY, t));
    }
}
static gboolean isAPARType(GNCAccountType t)
{
    switch (t)
    {
    case ACCT_TYPE_RECEIVABLE:
    case ACCT_TYPE_PAYABLE:
        return TRUE;
    default:
        return FALSE;
    }
}
static void increment_if_asset_account (gpointer data,
                                        gpointer user_data)
{
    int *r = user_data;
    const Split *split = data;
    const Account *account = xaccSplitGetAccount(split);
    if (isAssetLiabType(xaccAccountGetType(account)))
        ++(*r);
}
static int countAssetAccounts(SplitList* slist)
{
    int result = 0;
    g_list_foreach(slist, &increment_if_asset_account, &result);
    return result;
}

static gint predicate_is_asset_account(gconstpointer a,
                                       gconstpointer user_data)
{
    const Split *split = a;
    const Account *account = xaccSplitGetAccount(split);
    if (isAssetLiabType(xaccAccountGetType(account)))
        return 0;
    else
        return -1;
}
static gint predicate_is_apar_account(gconstpointer a,
                                      gconstpointer user_data)
{
    const Split *split = a;
    const Account *account = xaccSplitGetAccount(split);
    if (isAPARType(xaccAccountGetType(account)))
        return 0;
    else
        return -1;
}
static Split *getFirstAssetAccountSplit(SplitList* slist)
{
    GList *r = g_list_find_custom(slist, NULL, &predicate_is_asset_account);
    if (r)
        return r->data;
    else
        return NULL;
}
static Split *getFirstAPARAccountSplit(SplitList* slist)
{
    GList *r = g_list_find_custom(slist, NULL, &predicate_is_apar_account);
    if (r)
        return r->data;
    else
        return NULL;
}

// ///////////////

gboolean gnc_ui_payment_is_customer_payment(const Transaction *txn)
{
    SplitList *slist;
    gboolean result = TRUE;

    Split *assetaccount_split;
    gnc_numeric amount;

    if (!txn)
        return result;

    // We require the txn to have one split in an A/R or A/P account.

    slist = xaccTransGetSplitList(txn);
    if (!slist)
        return result;
    if (countAssetAccounts(slist) == 0)
    {
        g_message("No asset splits in txn \"%s\"; cannot use this for assigning a payment.",
                  xaccTransGetDescription(txn));
        return result;
    }

    assetaccount_split = getFirstAssetAccountSplit(slist);
    amount = xaccSplitGetValue(assetaccount_split);
    result = gnc_numeric_positive_p(amount); // positive amounts == customer
    //g_message("Amount=%s", gnc_numeric_to_string(amount));
    return result;
}

// ///////////////

PaymentWindow * gnc_ui_payment_new_with_txn (GncOwner *owner, Transaction *txn)
{
    SplitList *slist;

    Split *assetaccount_split;
    Split *postaccount_split;
    gnc_numeric amount;
    PaymentWindow *pw;

    if (!txn)
        return NULL;

    // We require the txn to have one split in an A/R or A/P account.

    slist = xaccTransGetSplitList(txn);
    if (!slist)
        return NULL;
    if (countAssetAccounts(slist) == 0)
    {
        g_message("No asset splits in txn \"%s\"; cannot use this for assigning a payment.",
                  xaccTransGetDescription(txn));
        return NULL;
    }

    assetaccount_split = getFirstAssetAccountSplit(slist);
    postaccount_split = getFirstAPARAccountSplit(slist); // watch out: Might be NULL
    amount = xaccSplitGetValue(assetaccount_split);

    pw = gnc_ui_payment_new(owner,
                            qof_instance_get_book(QOF_INSTANCE(txn)));
    g_assert(assetaccount_split); // we can rely on this because of the countAssetAccounts() check above
    //g_message("Amount=%s", gnc_numeric_to_string(amount));

    // Fill in the values from the given txn
    pw->pre_existing_txn = txn;
    gnc_ui_payment_window_set_num(pw, xaccTransGetNum(txn));
    gnc_ui_payment_window_set_memo(pw, xaccTransGetDescription(txn));
    {
        GDate txn_date = xaccTransGetDatePostedGDate (txn);
        gnc_ui_payment_window_set_date(pw, &txn_date);
    }
    gnc_ui_payment_window_set_amount(pw, gnc_numeric_abs(amount));
    gnc_ui_payment_window_set_xferaccount(pw, xaccSplitGetAccount(assetaccount_split));
    if (postaccount_split)
        gnc_ui_payment_window_set_postaccount(pw, xaccSplitGetAccount(postaccount_split));

    return pw;
}
