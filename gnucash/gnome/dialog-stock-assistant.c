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


/********************************************************************   \
 * stockeditorWindow                                                *
 *   opens up the window to stock-editor                            *
 *                                                                  *
 * Args:   parent  - the parent of this window                      *
 *         account - the account to stock-edit                      *
\********************************************************************/
void gnc_ui_stockeditor_dialog (GtkWidget *parent, Account *account)
{
    GtkWidget *assistant;

    g_return_if_fail (parent);
    g_return_if_fail (GNC_IS_ACCOUNT (account) && xaccAccountIsPriced (account));

    /* Create a new assistant widget with no pages. */
    assistant = gtk_assistant_new ();
    gtk_widget_set_size_request (assistant, 600, 400);

    gtk_window_set_transient_for (GTK_WINDOW (assistant), GTK_WINDOW (parent));

    gtk_widget_show_all (assistant);

    return;
}
