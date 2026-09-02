/********************************************************************\
 * dialog-balance-assertion.cpp -- balance assertion dialog          *
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

#include <memory>
#include <string>
#include <vector>

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
    COL_BASIS,
    COL_STATUS_ICON,
    COL_NOTES,
    COL_ASSERTION,
};

struct BalanceAssertionDialog
{
    GtkWidget *dialog = nullptr;
    GtkWidget *view = nullptr;
    GtkWidget *date_edit = nullptr;
    GtkWidget *amount_edit = nullptr;
    GtkWidget *basis_combo = nullptr;
    GtkWidget *notes_entry = nullptr;
    GtkWidget *remove_button = nullptr;
    GtkListStore *store = nullptr;

    Account *account = nullptr;
    /* Kept alongside the pointer so that the account can still be
     * identified after it has been deleted from the book. */
    GncGUID acct_guid {};
    gint component_id = 0;
    QofSession *session = nullptr;
};

/* This static indicates the debugging module that this .o belongs to. */
static QofLogModule log_module = GNC_MOD_GUI;

/* Handlers named in the .glade file are resolved by name at runtime, so
 * they must not be mangled. */
extern "C"
{
void gnc_balance_assertion_dialog_add_cb (GtkWidget *widget, gpointer data);
void gnc_balance_assertion_dialog_remove_cb (GtkWidget *widget, gpointer data);
void gnc_balance_assertion_dialog_selection_changed_cb (GtkTreeSelection *sel,
                                                        gpointer data);
void gnc_balance_assertion_dialog_response_cb (GtkDialog *dialog, gint response,
                                               gpointer data);
}

/* =================================================================== */

/* xaccPrintAmount hands back one shared static buffer, so a value has to
 * be copied out before the next call. */
static std::string
print_amount (gnc_numeric amount, GNCPrintAmountInfo pinfo)
{
    return xaccPrintAmount (amount, pinfo);
}

static std::string
print_date (time64 date)
{
    char buf[MAX_DATE_LENGTH + 1];
    qof_print_date_buff (buf, MAX_DATE_LENGTH, date);
    return buf;
}

static const char *
basis_label (GncBalanceAssertionBasis basis)
{
    return basis == GNC_BALANCE_ASSERTION_BASIS_RECONCILED
        ? _("Reconciled")
        : _("Posted");
}

static void
add_column (GtkTreeView *view, const char *title, int column_id,
            int sort_column_id, bool right_align)
{
    auto renderer = gtk_cell_renderer_text_new ();

    if (right_align)
        gtk_cell_renderer_set_alignment (renderer, 1.0, 0.5);

    auto column = gtk_tree_view_column_new_with_attributes (title, renderer,
                                                            "text", column_id,
                                                            nullptr);
    gtk_tree_view_column_set_resizable (column, TRUE);
    gtk_tree_view_column_set_alignment (column, right_align ? 1.0 : 0.0);
    gtk_tree_view_column_set_sort_column_id (column, sort_column_id);
    gtk_tree_view_append_column (view, column);
}

static void
setup_columns (BalanceAssertionDialog *bad)
{
    auto view = GTK_TREE_VIEW(bad->view);

    /* The status icon carries no text of its own; the row it sits on
     * spells the discrepancy out. */
    auto renderer = gtk_cell_renderer_pixbuf_new ();
    auto column = gtk_tree_view_column_new_with_attributes ("", renderer,
                                                            "icon-name",
                                                            COL_STATUS_ICON,
                                                            nullptr);
    gtk_tree_view_append_column (view, column);

    add_column (view, _("Date"), COL_DATE, COL_DATE_INT64, false);
    add_column (view, _("Asserted"), COL_ASSERTED, COL_ASSERTED, true);
    add_column (view, _("Actual"), COL_ACTUAL, COL_ACTUAL, true);
    add_column (view, _("Difference"), COL_DIFFERENCE, COL_DIFFERENCE, true);
    add_column (view, _("Basis"), COL_BASIS, COL_BASIS, false);
    add_column (view, _("Notes"), COL_NOTES, COL_NOTES, false);
}

static GncBalanceAssertion *
get_selected (BalanceAssertionDialog *bad)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    GncBalanceAssertion *ba = nullptr;

    auto selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(bad->view));
    if (gtk_tree_selection_get_selected (selection, &model, &iter))
        gtk_tree_model_get (model, &iter, COL_ASSERTION, &ba, -1);

    return ba;
}

static void
refresh_list (BalanceAssertionDialog *bad)
{
    gtk_list_store_clear (bad->store);

    if (!bad->account)
        return;

    auto pinfo = gnc_account_print_info (bad->account, TRUE);
    auto reverse = gnc_reverse_balance (bad->account);
    auto assertions = gnc_balance_assertion_get_for_account (bad->account);

    for (auto node = assertions; node; node = node->next)
    {
        auto ba = GNC_BALANCE_ASSERTION(node->data);
        auto failing = gnc_balance_assertion_is_failing (ba);
        auto date = gnc_balance_assertion_get_date (ba);

        auto asserted = gnc_ui_balance_assertion_get_display_amount (ba);
        auto actual = gnc_balance_assertion_get_actual (ba);
        auto difference = gnc_balance_assertion_get_delta (ba);

        if (reverse)
        {
            actual = gnc_numeric_neg (actual);
            difference = gnc_numeric_neg (difference);
        }

        auto date_str = print_date (date);
        auto asserted_str = print_amount (asserted, pinfo);
        auto actual_str = print_amount (actual, pinfo);
        auto difference_str = failing ? print_amount (difference, pinfo)
                                      : std::string {};

        GtkTreeIter iter;
        gtk_list_store_append (bad->store, &iter);
        gtk_list_store_set (bad->store, &iter,
                            COL_DATE, date_str.c_str(),
                            COL_DATE_INT64, static_cast<gint64>(date),
                            COL_ASSERTED, asserted_str.c_str(),
                            COL_ACTUAL, actual_str.c_str(),
                            COL_DIFFERENCE, difference_str.c_str(),
                            COL_BASIS,
                            basis_label (gnc_balance_assertion_get_basis (ba)),
                            COL_STATUS_ICON,
                            failing ? "dialog-warning" : "emblem-default",
                            COL_NOTES, gnc_balance_assertion_get_notes (ba),
                            COL_ASSERTION, ba,
                            -1);
    }

    g_list_free (assertions);

    gtk_widget_set_sensitive (bad->remove_button, get_selected (bad) != nullptr);
}

/* Fill the entry row with the account's balance on the chosen date, so
 * that "Add" without further typing records what GnuCash already
 * believes. The user overtypes the figure from their statement; if the
 * two agree there is nothing to do, and if they don't the assertion is
 * exactly the record of that disagreement. */
static void
propose_balance (BalanceAssertionDialog *bad)
{
    if (!bad->account)
        return;

    auto date = gnc_date_edit_get_date (GNC_DATE_EDIT(bad->date_edit));
    auto balance = xaccAccountGetBalanceAsOfDate (bad->account,
                                                  gnc_time64_get_day_end (date));

    if (gnc_reverse_balance (bad->account))
        balance = gnc_numeric_neg (balance);

    gnc_amount_edit_set_amount (GNC_AMOUNT_EDIT(bad->amount_edit), balance);
}

/* =================================================================== */
/* Callbacks */

static void
date_changed_cb (GtkWidget *widget, gpointer data)
{
    propose_balance (static_cast<BalanceAssertionDialog*>(data));
}

static GncBalanceAssertionBasis
selected_basis (BalanceAssertionDialog *bad)
{
    auto id = gtk_combo_box_get_active_id (GTK_COMBO_BOX(bad->basis_combo));

    return g_strcmp0 (id, "reconciled") == 0
        ? GNC_BALANCE_ASSERTION_BASIS_RECONCILED
        : GNC_BALANCE_ASSERTION_BASIS_TOTAL;
}

void
gnc_balance_assertion_dialog_add_cb (GtkWidget *widget, gpointer data)
{
    auto bad = static_cast<BalanceAssertionDialog*>(data);

    if (!bad->account)
        return;

    GError *error = nullptr;
    if (!gnc_amount_edit_evaluate (GNC_AMOUNT_EDIT(bad->amount_edit), &error))
    {
        gnc_error_dialog (GTK_WINDOW(bad->dialog), "%s",
                          error ? error->message
                                : _("The balance must be a number."));
        g_clear_error (&error);
        return;
    }

    auto ba = gnc_balance_assertion_new (gnc_get_current_book ());
    gnc_balance_assertion_set_account (ba, bad->account);
    gnc_balance_assertion_set_date
        (ba, gnc_date_edit_get_date (GNC_DATE_EDIT(bad->date_edit)));
    gnc_balance_assertion_set_basis (ba, selected_basis (bad));
    gnc_ui_balance_assertion_set_display_amount
        (ba, gnc_amount_edit_get_amount (GNC_AMOUNT_EDIT(bad->amount_edit)));
    gnc_balance_assertion_set_notes
        (ba, gtk_entry_get_text (GTK_ENTRY(bad->notes_entry)));

    gtk_entry_set_text (GTK_ENTRY(bad->notes_entry), "");

    refresh_list (bad);
}

void
gnc_balance_assertion_dialog_remove_cb (GtkWidget *widget, gpointer data)
{
    auto bad = static_cast<BalanceAssertionDialog*>(data);

    if (auto ba = get_selected (bad))
    {
        gnc_balance_assertion_destroy (ba);
        refresh_list (bad);
    }
}

void
gnc_balance_assertion_dialog_selection_changed_cb (GtkTreeSelection *selection,
                                                   gpointer data)
{
    auto bad = static_cast<BalanceAssertionDialog*>(data);

    gtk_widget_set_sensitive (bad->remove_button, get_selected (bad) != nullptr);
}

void
gnc_balance_assertion_dialog_response_cb (GtkDialog *dialog, gint response,
                                          gpointer data)
{
    auto bad = static_cast<BalanceAssertionDialog*>(data);

    gnc_save_window_size (GNC_PREFS_GROUP, GTK_WINDOW(bad->dialog));
    gnc_close_gui_component (bad->component_id);
}

/* =================================================================== */
/* Component manager glue */

static void
refresh_handler (GHashTable *changes, gpointer user_data)
{
    auto bad = static_cast<BalanceAssertionDialog*>(user_data);

    /* The account may have been deleted from under us, which would
     * leave bad->account dangling -- so resolve the guid, never the
     * stale pointer. */
    bad->account = xaccAccountLookup (&bad->acct_guid, gnc_get_current_book ());
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
    auto bad = static_cast<BalanceAssertionDialog*>(user_data);

    gnc_unregister_gui_component (bad->component_id);
    gtk_widget_destroy (bad->dialog);
    delete bad;
}

static gboolean
find_by_account (gpointer find_data, gpointer user_data)
{
    auto bad = static_cast<BalanceAssertionDialog*>(user_data);

    return bad && bad->account == find_data;
}

/* =================================================================== */

static BalanceAssertionDialog *
create_dialog (GtkWindow *parent, Account *account)
{
    auto bad = std::make_unique<BalanceAssertionDialog>();
    bad->account = account;
    bad->acct_guid = *xaccAccountGetGUID (account);
    bad->session = gnc_get_current_session ();

    auto builder = gtk_builder_new ();
    gnc_builder_add_from_file (builder, "dialog-balance-assertion.glade",
                               "assertion_liststore");
    gnc_builder_add_from_file (builder, "dialog-balance-assertion.glade",
                               "balance_assertion_dialog");

    auto get_widget = [builder](const char *name)
    {
        return GTK_WIDGET(gtk_builder_get_object (builder, name));
    };

    bad->dialog = get_widget ("balance_assertion_dialog");
    bad->view = get_widget ("ba_treeview");
    bad->notes_entry = get_widget ("ba_notes_entry");
    bad->basis_combo = get_widget ("ba_basis_combo");
    bad->remove_button = get_widget ("ba_remove_button");
    bad->store = GTK_LIST_STORE(gtk_builder_get_object (builder,
                                                        "assertion_liststore"));

    auto fullname = gnc_account_get_full_name (account);
    std::string name {fullname};
    g_free (fullname);

    auto title = g_strdup_printf (_("Balance Assertions — %s"), name.c_str());
    gtk_window_set_title (GTK_WINDOW(bad->dialog), title);
    g_free (title);

    gtk_label_set_text (GTK_LABEL(get_widget ("ba_account_label")),
                        name.c_str());

    if (parent)
        gtk_window_set_transient_for (GTK_WINDOW(bad->dialog), parent);

    /* Date */
    bad->date_edit = gnc_date_edit_new (gnc_time (nullptr), FALSE, FALSE);
    gtk_box_pack_start (GTK_BOX(get_widget ("ba_date_box")), bad->date_edit,
                        TRUE, TRUE, 0);
    gtk_widget_show (bad->date_edit);
    gnc_date_make_mnemonic_target (GNC_DATE_EDIT(bad->date_edit),
                                   get_widget ("ba_date_label"));
    g_signal_connect (G_OBJECT(bad->date_edit), "date_changed",
                      G_CALLBACK(date_changed_cb), bad.get());

    /* Amount */
    bad->amount_edit = gnc_amount_edit_new ();
    gnc_amount_edit_set_evaluate_on_enter (GNC_AMOUNT_EDIT(bad->amount_edit),
                                           TRUE);
    gnc_amount_edit_set_print_info (GNC_AMOUNT_EDIT(bad->amount_edit),
                                    gnc_account_print_info (account, FALSE));
    gnc_amount_edit_set_fraction (GNC_AMOUNT_EDIT(bad->amount_edit),
                                  xaccAccountGetCommoditySCU (account));
    gtk_box_pack_start (GTK_BOX(get_widget ("ba_amount_box")), bad->amount_edit,
                        TRUE, TRUE, 0);
    gtk_widget_show (bad->amount_edit);
    gnc_amount_edit_make_mnemonic_target (GNC_AMOUNT_EDIT(bad->amount_edit),
                                          get_widget ("ba_amount_label"));

    setup_columns (bad.get());

    auto selection = gtk_tree_view_get_selection (GTK_TREE_VIEW(bad->view));
    g_signal_connect (G_OBJECT(selection), "changed",
                      G_CALLBACK(gnc_balance_assertion_dialog_selection_changed_cb),
                      bad.get());

    gtk_builder_connect_signals_full (builder, gnc_builder_connect_full_func,
                                      bad.get());
    g_object_unref (G_OBJECT(builder));

    bad->component_id =
        gnc_register_gui_component (DIALOG_BALANCE_ASSERTION_CM_CLASS,
                                    refresh_handler, close_handler, bad.get());
    gnc_gui_component_set_session (bad->component_id, bad->session);

    /* Anything that moves a balance can flip an assertion, so watch
     * transactions and splits as well as the assertions themselves. */
    const QofEventId all_events =
        QOF_EVENT_CREATE | QOF_EVENT_MODIFY | QOF_EVENT_DESTROY;

    for (auto type : { GNC_ID_BALANCE_ASSERTION, GNC_ID_TRANS, GNC_ID_SPLIT })
        gnc_gui_component_watch_entity_type (bad->component_id, type,
                                             all_events);

    gnc_gui_component_watch_entity_type (bad->component_id, GNC_ID_ACCOUNT,
                                         QOF_EVENT_MODIFY | QOF_EVENT_DESTROY);

    gnc_restore_window_size (GNC_PREFS_GROUP, GTK_WINDOW(bad->dialog), parent);

    /* The component manager owns it from here; close_handler deletes it. */
    return bad.release();
}

static void
present_dialog (GtkWindow *parent, Account *account, bool have_date, time64 date)
{
    g_return_if_fail (GNC_IS_ACCOUNT(account));

    auto bad = static_cast<BalanceAssertionDialog*>
        (gnc_find_first_gui_component (DIALOG_BALANCE_ASSERTION_CM_CLASS,
                                       find_by_account, account));
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
    present_dialog (parent, account, false, 0);
    LEAVE (" ");
}

void
gnc_balance_assertion_dialog_for_date (GtkWindow *parent, Account *account,
                                       time64 date)
{
    ENTER (" ");
    present_dialog (parent, account, true, date);
    LEAVE (" ");
}
