/*
 * gnc-ab-getbalance.c --
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

/**
 * @internal
 * @file gnc-ab-getbalance.c
 * @brief AqBanking getbalance functions
 * @author Copyright (C) 2002 Christian Stimming <stimming@tuhh.de>
 * @author Copyright (C) 2008 Andreas Koehler <andi5.py@gmx.net>
 */

#include <config.h>

#include "gnc-ab-utils.h"

#include <glib/gi18n.h>
#include <aqbanking/banking.h>
# include <aqbanking/types/transaction.h>

#include "gnc-ab-getbalance.h"
#include "gnc-ab-kvp.h"
#include "gnc-gwen-gui.h"
#include "gnc-ui.h"

/* This static indicates the debugging module that this .o belongs to.  */
G_GNUC_UNUSED static QofLogModule log_module = G_LOG_DOMAIN;

void
gnc_ab_getbalance(GtkWidget *parent, Account *gnc_acc)
{
    AB_BANKING *api;
    GNC_AB_ACCOUNT_SPEC *ab_acc;
    GNC_AB_JOB *job = NULL;
    GNC_AB_JOB_LIST2 *job_list = NULL;
    GncGWENGui *gui = NULL;
    AB_IMEXPORTER_CONTEXT *context = NULL;
    GncABImExContextImport *ieci = NULL;
    GNC_AB_JOB_STATUS job_status;

    g_return_if_fail(parent && gnc_acc);

    /* Get the API */
    api = gnc_AB_BANKING_new();
    if (!api)
    {
        g_warning("gnc_ab_gettrans: Couldn't get AqBanking API");
        return;
    }

    /* Get the AqBanking Account */
    ab_acc = gnc_ab_get_ab_account(api, gnc_acc);
    if (!ab_acc)
    {
        g_warning("gnc_ab_getbalance: No AqBanking account found");
        gnc_error_dialog (GTK_WINDOW (parent), _("No valid online banking account assigned."));
        goto cleanup;
    }

    /* Get a GetBalance job and enqueue it */
    if (!AB_AccountSpec_GetTransactionLimitsForCommand(ab_acc, AB_Transaction_CommandGetBalance))
    {
        g_warning("gnc_ab_getbalance: JobGetBalance not available for this "
                  "account");
        gnc_error_dialog (GTK_WINDOW (parent), _("Online action \"Get Balance\" not available for this account."));
        goto cleanup;
    }
    job = AB_Transaction_new();
    AB_Transaction_SetCommand(job, AB_Transaction_CommandGetBalance);
    AB_Transaction_SetUniqueAccountId(job, AB_AccountSpec_GetUniqueId(ab_acc));

    job_list = AB_Transaction_List2_new();
    AB_Transaction_List2_PushBack(job_list, job);
    /* Get a GUI object */
    gui = gnc_GWEN_Gui_get(parent);
    if (!gui)
    {
        g_warning("gnc_ab_getbalance: Couldn't initialize Gwenhywfar GUI");
        goto cleanup;
    }

    /* Create a context to store the results */
    context = AB_ImExporterContext_new();

    /* Execute the job */
    AB_Banking_SendCommands(api, job_list, context);
    /* Ignore the return value of AB_Banking_ExecuteJobs(), as the job's
     * status always describes better whether the job was actually
     * transferred to and accepted by the bank.  See also
     * https://lists.gnucash.org/pipermail/gnucash-de/2008-September/006389.html
     */
    job_status = AB_Transaction_GetStatus(job);
    if (job_status != AB_Transaction_StatusEnqueued
            && job_status != AB_Transaction_StatusPending
            && job_status != AB_Transaction_StatusAccepted)
    {
        g_warning("gnc_ab_getbalance: Error on executing job: %d", job_status);
        gnc_error_dialog (GTK_WINDOW (parent),
                          _("Error on executing job.\n\nStatus: %s"),
                          AB_Transaction_Status_toString(job_status));
        goto cleanup;
    }

    /* Import the results */
    ieci = gnc_ab_import_context(context, AWAIT_BALANCES, FALSE, NULL, parent);

cleanup:
    if (ieci)
        g_free(ieci);
    if (context)
        AB_ImExporterContext_free(context);
    if (gui)
        gnc_GWEN_Gui_release(gui);
     if (job_list)
         AB_Transaction_List2_free(job_list);
     if (job)
         AB_Transaction_free(job);
    gnc_AB_BANKING_fini(api);
}
