/********************************************************************\
 * dialog-balance-assertion.c -- balance assertion dialog            *
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

#include "dialog-balance-assertion.h"

#include "dialog-utils.h"
#include "gnc-amount-edit.h"
#include "gnc-balance-assertion.h"
#include "gnc-component-manager.h"
#include "gnc-date-edit.h"
#include "gnc-engine.h"
#include "gnc-gnome-utils.h"
#include "gnc-gui-query.h"
#include "gnc-prefs.h"
#include "gnc-session.h"
#include "gnc-ui-balances.h"
#include "gnc-ui-util.h"

#define DIALOG_BALANCE_ASSERTION_CM_CLASS "dialog-balance-assertion"
#define GNC_PREFS_GROUP "dialogs.balance-assertion"

/* Columns of the list store defined in dialog-balance-assertion.glade */
enum BalanceAssertionColumn
{
    COL_DATE,
    COL_DATE_INT64,             /* sort key for COL_DATE */
    COL_ASSERTED,
    COL_ACTUAL,
    COL_DIFFERENCE,
    COL_STATUS_ICON,
    COL_NOTES,
    COL_ASSERTION,
};

typedef struct
{
    GtkWidget *dialog;
    GtkWidget *view;
    GtkWidget *date_edit;
    GtkWidget *amount_edit;
    GtkWidget *notes_entry;
    GtkWidget *remove_button;
    GtkListStore *store;

    Account *account;
    /* Kept alongside the pointer so that the account can still be
     * identified after it has been deleted from the book. */
    GncGUID acct_guid;
    gint component_id;
    QofSession *session;
} BalanceAssertionDialog;

/* This static indicates the debugging module that this .o belongs to. */
static QofLogModule log_module = GNC_MOD_GUI;

static void refresh_list (BalanceAssertionDialog *bad);

/* =================================================================== */

static void
add_column (GtkTreeView *view, const char *title, int column_id,
            int sort_column_id, gboolean right_align)
{
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
    GtkTreeViewColumn *column;

    if (right_align)
        gtk_cell_renderer_set_alignment (renderer, 1.0, 0.5);

    column = gtk_tree_view_column_new_with_attributes (title, renderer,
                                                       "text", column_id,
                                                       NULL);
    gtk_tree_view_column_set_resizable (column, TRUE);
    gtk_tree_view_column_set_alignment (column, right_align ? 1.0 : 0.0);
    gtk_tree_view_column_set_sort_column_id (column, sort_column_id);
    gtk_tree_view_append_column (view, column);
}

static void
setup_columns (BalanceAssertionDialog *bad)
{
    GtkTreeView *view = GTK_TREE_VIEW(bad->view);
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    /* The status icon carries no text of its own; the row it sits on
     * spells the discrepancy out. */
    renderer = gtk_cell_renderer_pixbuf_new ();
    column = gtk_tree_view_column_new_with_attributes ("", renderer,
                                                       "icon-name",
                                                       COL_STATUS_ICON,
                                                       NULL);
    gtk_tree_view_append_column (view, column);

    add_column (view, _("Date"), COL_DATE, COL_DATE_INT64, FALSE);
    add_column (view, _("Asserted"), COL_ASSERTED, COL_ASSERTED, TRUE);
    add_column (view, _("Actual"), COL_ACTUAL, COL_ACTUAL, TRUE);
    add_column (view, _("Difference"), COL_DIFFERENCE, COL_DIFFERENCE, TRUE);
    add_column (view, _("Notes"), COL_NOTES, COL_NOTES, FALSE);
}

static GncBalanceAssertion *
get_selected (BalanceAssertionDialog *bad)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GncBalanceAssertion *ba = NULL;

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(bad->view));
    if (gtk_tree_selection_get_selected (selection, &model, &iter))
        gtk_tree_model_get (model, &iter, COL_ASSERTION, &ba, -1);

    return ba;
}

static void
refresh_list (BalanceAssertionDialog *bad)
{
    GList *assertions, *node;
    GNCPrintAmountInfo pinfo;
    char datebuf[MAX_DATE_LENGTH + 1];

    gtk_list_store_clear (bad->store);

    if (!bad->account)
        return;

    pinfo = gnc_account_print_info (bad->account, TRUE);
    assertions = gnc_balance_assertion_get_for_account (bad->account);

    for (node = assertions; node; node = node->next)
    {
        GncBalanceAssertion *ba = GNC_BALANCE_ASSERTION(node->data);
        GtkTreeIter iter;
        gboolean failing = gnc_balance_assertion_is_failing (ba);
        time64 date = gnc_balance_assertion_get_date (ba);
        gnc_numeric asserted = gnc_ui_balance_assertion_get_display_amount (ba);
        gnc_numeric actual = gnc_balance_assertion_get_actual (ba);
        gnc_numeric difference = gnc_balance_assertion_get_delta (ba);

        if (gnc_reverse_balance (bad->account))
        {
            actual = gnc_numeric_neg (actual);
            difference = gnc_numeric_neg (difference);
        }

        qof_print_date_buff (datebuf, MAX_DATE_LENGTH, date);

        /* xaccPrintAmount hands back one shared static buffer, so each
         * value has to be copied out before the next call. */
        char *asserted_str = g_strdup (xaccPrintAmount (asserted, pinfo));
        char *actual_str = g_strdup (xaccPrintAmount (actual, pinfo));
        char *difference_str = failing
            ? g_strdup (xaccPrintAmount (difference, pinfo))
            : g_strdup ("");

        gtk_list_store_append (bad->store, &iter);
        gtk_list_store_set (bad->store, &iter,
                            COL_DATE, datebuf,
                            COL_DATE_INT64, (gint64)date,
                            COL_ASSERTED, asserted_str,
                            COL_ACTUAL, actual_str,
                            COL_DIFFERENCE, difference_str,
                            COL_STATUS_ICON,
                            failing ? "dialog-warning" : "emblem-default",
                            COL_NOTES, gnc_balance_assertion_get_notes (ba),
                            COL_ASSERTION, ba,
                            -1);

        g_free (asserted_str);
        g_free (actual_str);
        g_free (difference_str);
    }

    g_list_free (assertions);

    gtk_widget_set_sensitive (bad->remove_button, get_selected (bad) != NULL);
}

/* Fill the entry row with the account's balance on the chosen date, so
 * that "Add" without further typing records what GnuCash already
 * believes. The user overtypes the figure from their statement; if the
 * two agree there is nothing to do, and if they don't the assertion is
 * exactly the record of that disagreement. */
static void
propose_balance (BalanceAssertionDialog *bad)
{
    time64 date;
    gnc_numeric balance;

    if (!bad->account)
        return;

    date = gnc_date_edit_get_date (GNC_DATE_EDIT(bad->date_edit));
    balance = xaccAccountGetBalanceAsOfDate (bad->account,
                                             gnc_time64_get_day_end (date));

    if (gnc_reverse_balance (bad->account))
        balance = gnc_numeric_neg (balance);

    gnc_amount_edit_set_amount (GNC_AMOUNT_EDIT(bad->amount_edit), balance);
}

/* =================================================================== */
/* Callbacks */

void gnc_balance_assertion_dialog_add_cb (GtkWidget *widget, gpointer data);
void gnc_balance_assertion_dialog_remove_cb (GtkWidget *widget, gpointer data);
void gnc_balance_assertion_dialog_selection_changed_cb (GtkTreeSelection *sel,
                                                        gpointer data);
void gnc_balance_assertion_dialog_response_cb (GtkDialog *dialog, gint response,
                                               gpointer data);

static void
date_changed_cb (GtkWidget *widget, gpointer data)
{
    propose_balance ((BalanceAssertionDialog*)data);
}

void
gnc_balance_assertion_dialog_add_cb (GtkWidget *widget, gpointer data)
{
    BalanceAssertionDialog *bad = data;
    GncBalanceAssertion *ba;
    GError *error = NULL;
    const char *notes;
    time64 date;

    if (!bad->account)
        return;

    if (!gnc_amount_edit_evaluate (GNC_AMOUNT_EDIT(bad->amount_edit), &error))
    {
        gnc_error_dialog (GTK_WINDOW(bad->dialog), "%s",
                          error ? error->message
                                : _("The balance must be a number."));
        g_clear_error (&error);
        return;
    }

    date = gnc_date_edit_get_date (GNC_DATE_EDIT(bad->date_edit));
    notes = gtk_entry_get_text (GTK_ENTRY(bad->notes_entry));

    ba = gnc_balance_assertion_new (gnc_get_current_book ());
    gnc_balance_assertion_set_account (ba, bad->account);
    gnc_balance_assertion_set_date (ba, date);
    gnc_ui_balance_assertion_set_display_amount
        (ba, gnc_amount_edit_get_amount (GNC_AMOUNT_EDIT(bad->amount_edit)));
    gnc_balance_assertion_set_notes (ba, notes);

    gtk_entry_set_text (GTK_ENTRY(bad->notes_entry), "");

    refresh_list (bad);
}

void
gnc_balance_assertion_dialog_remove_cb (GtkWidget *widget, gpointer data)
{
    BalanceAssertionDialog *bad = data;
    GncBalanceAssertion *ba = get_selected (bad);

    if (!ba)
        return;

    gnc_balance_assertion_destroy (ba);
    refresh_list (bad);
}

void
gnc_balance_assertion_dialog_selection_changed_cb (GtkTreeSelection *selection,
                                                   gpointer data)
{
    BalanceAssertionDialog *bad = data;

    gtk_widget_set_sensitive (bad->remove_button, get_selected (bad) != NULL);
}

void
gnc_balance_assertion_dialog_response_cb (GtkDialog *dialog, gint response,
                                          gpointer data)
{
    BalanceAssertionDialog *bad = data;

    gnc_save_window_size (GNC_PREFS_GROUP, GTK_WINDOW(bad->dialog));
    gnc_close_gui_component (bad->component_id);
}

/* =================================================================== */
/* Component manager glue */

static void
refresh_handler (GHashTable *changes, gpointer user_data)
{
    BalanceAssertionDialog *bad = user_data;

    /* The account may have been deleted from under us, which would
     * leave bad->account dangling -- so resolve the guid, never the
     * stale pointer. */
    bad->account = xaccAccountLookup (&bad->acct_guid,
                                      gnc_get_current_book ());
    if (!bad->account)
    {
        gnc_close_gui_component (bad->component_id);
        return;
    }

    refresh_list (bad);
}

static void
close_handler (gpointer user_data)
{
    BalanceAssertionDialog *bad = user_data;

    gnc_unregister_gui_component (bad->component_id);
    gtk_widget_destroy (bad->dialog);
    g_free (bad);
}

static gboolean
find_by_account (gpointer find_data, gpointer user_data)
{
    BalanceAssertionDialog *bad = user_data;

    return bad && bad->account == find_data;
}

/* =================================================================== */

static BalanceAssertionDialog *
create_dialog (GtkWindow *parent, Account *account)
{
    BalanceAssertionDialog *bad;
    GtkBuilder *builder;
    GtkWidget *box, *label;
    GtkTreeSelection *selection;
    char *fullname, *title;

    bad = g_new0 (BalanceAssertionDialog, 1);
    bad->account = account;
    bad->acct_guid = *xaccAccountGetGUID (account);
    bad->session = gnc_get_current_session ();

    builder = gtk_builder_new ();
    gnc_builder_add_from_file (builder, "dialog-balance-assertion.glade",
                               "assertion_liststore");
    gnc_builder_add_from_file (builder, "dialog-balance-assertion.glade",
                               "balance_assertion_dialog");

    bad->dialog = GTK_WIDGET(gtk_builder_get_object (builder,
                                                     "balance_assertion_dialog"));
    bad->view = GTK_WIDGET(gtk_builder_get_object (builder, "ba_treeview"));
    bad->notes_entry = GTK_WIDGET(gtk_builder_get_object (builder,
                                                          "ba_notes_entry"));
    bad->remove_button = GTK_WIDGET(gtk_builder_get_object (builder,
                                                            "ba_remove_button"));
    bad->store = GTK_LIST_STORE(gtk_builder_get_object (builder,
                                                        "assertion_liststore"));

    fullname = gnc_account_get_full_name (account);
    title = g_strdup_printf (_("Balance Assertions — %s"), fullname);
    gtk_window_set_title (GTK_WINDOW(bad->dialog), title);
    g_free (title);

    label = GTK_WIDGET(gtk_builder_get_object (builder, "ba_account_label"));
    gtk_label_set_text (GTK_LABEL(label), fullname);
    g_free (fullname);

    if (parent)
        gtk_window_set_transient_for (GTK_WINDOW(bad->dialog), parent);

    /* Date */
    box = GTK_WIDGET(gtk_builder_get_object (builder, "ba_date_box"));
    bad->date_edit = gnc_date_edit_new (gnc_time (NULL), FALSE, FALSE);
    gtk_box_pack_start (GTK_BOX(box), bad->date_edit, TRUE, TRUE, 0);
    gtk_widget_show (bad->date_edit);
    label = GTK_WIDGET(gtk_builder_get_object (builder, "ba_date_label"));
    gnc_date_make_mnemonic_target (GNC_DATE_EDIT(bad->date_edit), label);
    g_signal_connect (G_OBJECT(bad->date_edit), "date_changed",
                      G_CALLBACK(date_changed_cb), bad);

    /* Amount */
    box = GTK_WIDGET(gtk_builder_get_object (builder, "ba_amount_box"));
    bad->amount_edit = gnc_amount_edit_new ();
    gnc_amount_edit_set_evaluate_on_enter (GNC_AMOUNT_EDIT(bad->amount_edit),
                                           TRUE);
    gnc_amount_edit_set_print_info (GNC_AMOUNT_EDIT(bad->amount_edit),
                                    gnc_account_print_info (account, FALSE));
    gnc_amount_edit_set_fraction (GNC_AMOUNT_EDIT(bad->amount_edit),
                                  xaccAccountGetCommoditySCU (account));
    gtk_box_pack_start (GTK_BOX(box), bad->amount_edit, TRUE, TRUE, 0);
    gtk_widget_show (bad->amount_edit);
    label = GTK_WIDGET(gtk_builder_get_object (builder, "ba_amount_label"));
    gnc_amount_edit_make_mnemonic_target (GNC_AMOUNT_EDIT(bad->amount_edit),
                                          label);

    setup_columns (bad);

    selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(bad->view));
    g_signal_connect (G_OBJECT(selection), "changed",
                      G_CALLBACK(gnc_balance_assertion_dialog_selection_changed_cb),
                      bad);

    gtk_builder_connect_signals_full (builder, gnc_builder_connect_full_func,
                                      bad);
    g_object_unref (G_OBJECT(builder));

    bad->component_id =
        gnc_register_gui_component (DIALOG_BALANCE_ASSERTION_CM_CLASS,
                                    refresh_handler, close_handler, bad);
    gnc_gui_component_set_session (bad->component_id, bad->session);

    /* Anything that moves a balance can flip an assertion, so watch
     * transactions and splits as well as the assertions themselves. */
    gnc_gui_component_watch_entity_type (bad->component_id,
                                         GNC_ID_BALANCE_ASSERTION,
                                         QOF_EVENT_CREATE | QOF_EVENT_MODIFY |
                                         QOF_EVENT_DESTROY);
    gnc_gui_component_watch_entity_type (bad->component_id, GNC_ID_TRANS,
                                         QOF_EVENT_CREATE | QOF_EVENT_MODIFY |
                                         QOF_EVENT_DESTROY);
    gnc_gui_component_watch_entity_type (bad->component_id, GNC_ID_SPLIT,
                                         QOF_EVENT_CREATE | QOF_EVENT_MODIFY |
                                         QOF_EVENT_DESTROY);
    gnc_gui_component_watch_entity_type (bad->component_id, GNC_ID_ACCOUNT,
                                         QOF_EVENT_MODIFY | QOF_EVENT_DESTROY);

    gnc_restore_window_size (GNC_PREFS_GROUP, GTK_WINDOW(bad->dialog), parent);

    return bad;
}

static void
present_dialog (GtkWindow *parent, Account *account, gboolean have_date,
                time64 date)
{
    BalanceAssertionDialog *bad;

    g_return_if_fail (GNC_IS_ACCOUNT(account));

    bad = gnc_find_first_gui_component (DIALOG_BALANCE_ASSERTION_CM_CLASS,
                                        find_by_account, account);
    if (!bad)
        bad = create_dialog (parent, account);

    if (have_date)
        gnc_date_edit_set_time (GNC_DATE_EDIT(bad->date_edit), date);

    propose_balance (bad);
    refresh_list (bad);

    gtk_widget_show_all (bad->dialog);
    gtk_window_present (GTK_WINDOW(bad->dialog));
}

void
gnc_balance_assertion_dialog (GtkWindow *parent, Account *account)
{
    ENTER (" ");
    present_dialog (parent, account, FALSE, 0);
    LEAVE (" ");
}

void
gnc_balance_assertion_dialog_for_date (GtkWindow *parent, Account *account,
                                       time64 date)
{
    ENTER (" ");
    present_dialog (parent, account, TRUE, date);
    LEAVE (" ");
}
