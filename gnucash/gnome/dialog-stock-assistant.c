/********************************************************************\
 * dialog-stock-assistant.c -- UI for stock editing                 *
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
typedef struct StockEditorWindow
{
    GtkWidget *assistant;
    GtkWidget *ok_button;
    GtkWidget *cancel_button;
} StockEditorWindow;


static void
assistant_cleanup (StockEditorWindow *data)
{
    g_print ("assistant_cleanup\n");
    g_free (data);
}

/* If the dialog is cancelled, delete it from memory and then clean up after
 * the Assistant structure. */
static void
assistant_cancel (GtkAssistant *assistant, StockEditorWindow *data)
{
    g_print ("assistant_cancel\n");
    assistant_cleanup (data);
    gtk_widget_destroy (GTK_WIDGET (assistant));
}

/* This function is where you would apply the changes and destroy the assistant. */
static void
assistant_close (GtkAssistant *assistant, StockEditorWindow *data)
{
    g_print ("assistant_close\n");
    assistant_cleanup (data);
}


static void
ok_button_cb (GtkWidget *widget, StockEditorWindow *data)
{
    g_print ("ok_button\n");
    assistant_cleanup (data);
}

static void
cancel_button_cb (GtkWidget *widget, StockEditorWindow *data)
{
    g_print ("cancel_button\n");
    assistant_cleanup (data);
}


static void
gnc_stock_assistant_create (StockEditorWindow *data)
{
    g_return_if_fail (data);
    /* Create a new assistant widget with no pages. */
    data->assistant = gtk_assistant_new ();
    gtk_widget_set_size_request (data->assistant, 600, 400);
    g_signal_connect (G_OBJECT (data->assistant), "cancel",
                      G_CALLBACK (assistant_cancel), (gpointer) data);
    g_signal_connect (G_OBJECT (data->assistant), "close",
                      G_CALLBACK (assistant_close), (gpointer) data);
    /* g_signal_connect (data->ok_button, "clicked", G_CALLBACK (ok_button_cb), data); */
    /* g_signal_connect (data->cancel_button, "clicked", G_CALLBACK (cancel_button_cb), data); */
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
    StockEditorWindow *data;

    g_return_if_fail (parent);
    g_return_if_fail (GNC_IS_ACCOUNT (account) && xaccAccountIsPriced (account));

    data = g_new (StockEditorWindow, 1);

    gnc_stock_assistant_create (data);

    gtk_window_set_transient_for (GTK_WINDOW (data->assistant), GTK_WINDOW (parent));
    gtk_widget_show_all (data->assistant);

    return;
}
