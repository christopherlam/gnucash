/*
 * gnucash-cli.cpp -- The command line entry point for GnuCash
 *
 * Copyright (C) 2020 Geert Janssens <geert@kobaltwit.be>
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
#include <config.h>

#include <libguile.h>
#include <guile-mappings.h>
#ifdef __MINGW32__
#include <Windows.h>
#include <fcntl.h>
#endif

#include "gnucash-commands.hpp"
#include "gnucash-core-app.hpp"

#include <gnc-filepath-utils.h>
#include <gnc-engine-guile.h>
#include <gnc-prefs.h>
#include <gnc-prefs-utils.h>
#include <gnc-session.h>
#include <gnc-state.h>
#include <gnc-reconciled-balance.h>
#include <gnc-ui-util.h>
#include <Account.h>
#include <qoflog.h>

#include <boost/locale.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <gnc-report.h>
#include <gnc-quotes.hpp>

namespace bl = boost::locale;

static std::string empty_string{};

/* This static indicates the debugging module that this .o belongs to.  */
static QofLogModule log_module = GNC_MOD_GUI;

static int
cleanup_and_exit_with_failure (QofSession *session)
{
    if (session)
    {
        auto error{qof_session_get_error (session)};
        if (error != ERR_BACKEND_NO_ERR)
        {
            if (error == ERR_BACKEND_LOCKED)
                PERR ("File is locked, won't open.");
            else
                PERR ("Session Error: %s\n",
                      qof_session_get_error_message (session));
        }
        qof_session_destroy (session);
    }
    qof_event_resume();
    return 1;
}

static void gnc_shutdown_cli (int exit_status)
{
    gnc_hook_run (HOOK_SHUTDOWN, NULL);
    gnc_engine_shutdown ();
    exit (exit_status);
}

/* scm_boot_guile doesn't expect to return, so call shutdown ourselves here */
static void
scm_cleanup_and_exit_with_failure (QofSession *session)
{
    cleanup_and_exit_with_failure (session);
    gnc_shutdown_cli (1);
}

static void
report_session_percentage (const char *message, double percent)
{
    static double previous = 0.0;
    if ((percent - previous) < 5.0)
        return;
    PINFO ("\r%3.0f%% complete...", percent);
    previous = percent;
    return;
}

/* Don't try to use std::string& for the members of the following struct, it
 * results in the values getting corrupted as it passes through initializing
 * Scheme when compiled with Clang.
 */
struct run_report_args {
    const std::string& file_to_load;
    const std::string& run_report;
    const std::string& export_type;
    const std::string& output_file;
};

static inline void
write_report_file (const char *html, const char* file)
{
    if (!file || !html || !*html) return;
    auto ofs{gnc_open_filestream(file)};
    if (!ofs)
    {
        std::cerr << "Failed to open file " << file << " for writing\n";
        return;
    }
    ofs << html << std::endl;
    // ofs destructor will close the file
}

static void
scm_run_report (void *data,
                [[maybe_unused]] int argc, [[maybe_unused]] char **argv)
{
    auto args = static_cast<run_report_args*>(data);

    scm_c_eval_string("(debug-set! stack 200000)");
    scm_c_use_module ("gnucash utilities");
    scm_c_use_module ("gnucash app-utils");
    scm_c_use_module ("gnucash reports");

    gnc_report_init ();
    Gnucash::gnc_load_scm_config ([](const gchar *msg){ PINFO ("%s", msg); });
    gnc_prefs_init ();
    qof_event_suspend ();

    auto datafile = args->file_to_load.c_str();
    auto check_report_cmd = scm_c_eval_string ("gnc:cmdline-check-report");
    auto get_report_cmd = scm_c_eval_string ("gnc:cmdline-get-report-id");
    auto run_export_cmd = scm_c_eval_string ("gnc:cmdline-template-export");
    /* We generally insist on using scm_from_utf8_string() throughout GnuCash
     * because all GUI-sourced strings and all file-sourced strings are encoded
     * that way. In this case, though, the input is coming from a shell window
     * and Microsoft Windows shells are generally not capable of entering UTF8
     * so it's necessary here to allow guile to read the locale and interpret
     * the input in that encoding.
     */
    auto report = scm_from_locale_string (args->run_report.c_str());
    auto type = !args->export_type.empty() ?
                scm_from_locale_string (args->export_type.c_str()) : SCM_BOOL_F;

    if (scm_is_false (scm_call_2 (check_report_cmd, report, type)))
        scm_cleanup_and_exit_with_failure (nullptr);

    PINFO ("Loading datafile %s...\n", datafile);

    auto session = gnc_get_current_session ();
    if (!session)
        scm_cleanup_and_exit_with_failure (session);

    qof_session_begin (session, datafile, SESSION_READ_ONLY);
    if (qof_session_get_error (session) != ERR_BACKEND_NO_ERR)
        scm_cleanup_and_exit_with_failure (session);

    qof_session_load (session, report_session_percentage);
    if (qof_session_get_error (session) != ERR_BACKEND_NO_ERR)
        scm_cleanup_and_exit_with_failure (session);

    if (!args->export_type.empty())
    {
        SCM retval = scm_call_2 (run_export_cmd, report, type);
        SCM query_result = scm_c_eval_string ("gnc:html-document?");
        SCM get_export_string = scm_c_eval_string ("gnc:html-document-export-string");
        SCM get_export_error = scm_c_eval_string ("gnc:html-document-export-error");

        if (scm_is_false (scm_call_1 (query_result, retval)))
        {
            std::cerr << _("This report must be upgraded to \
return a document object with export-string or export-error.") << std::endl;
            scm_cleanup_and_exit_with_failure (nullptr);
        }

        SCM export_string = scm_call_1 (get_export_string, retval);
        SCM export_error = scm_call_1 (get_export_error, retval);

        if (scm_is_string (export_string))
        {
            auto output = scm_to_utf8_string (export_string);
            if (!args->output_file.empty())
            {
                write_report_file(output, args->output_file.c_str());
            }
            else
            {
                std::cout << output << std::endl;
            }
            g_free (output);
        }
        else if (scm_is_string (export_error))
        {
            auto err = scm_to_utf8_string (export_error);
            std::cerr << err << std::endl;
            g_free (err);
            scm_cleanup_and_exit_with_failure (nullptr);
        }
        else
        {
            std::cerr << _("This report must be upgraded to \
return a document object with export-string or export-error.") << std::endl;
            scm_cleanup_and_exit_with_failure (nullptr);
        }
    }
    else
    {
        SCM id = scm_call_1(get_report_cmd, report);

        if (scm_is_false (id))
            scm_cleanup_and_exit_with_failure (nullptr);
        char *html, *errmsg;

        if (gnc_run_report_with_error_handling (scm_to_int(id), &html, &errmsg))
        {
            if (!args->output_file.empty())
            {
                write_report_file(html, args->output_file.c_str());
            }
            else
            {
                std::cout << html << std::endl;
            }
            g_free (html);
        }
        else
        {
            std::cerr << errmsg << std::endl;
            g_free (errmsg);
        }
    }

    qof_session_destroy (session);

    qof_event_resume ();
    gnc_shutdown_cli (0);
    return;
}


struct show_report_args {
    const std::string& file_to_load;
    const std::string& show_report;
};

static void
scm_report_show (void *data,
                [[maybe_unused]] int argc, [[maybe_unused]] char **argv)
{
    auto args = static_cast<show_report_args*>(data);

    scm_c_eval_string("(debug-set! stack 200000)");
    scm_c_use_module ("gnucash utilities");
    scm_c_use_module ("gnucash app-utils");
    scm_c_use_module ("gnucash reports");
    gnc_report_init ();
    Gnucash::gnc_load_scm_config ([](const gchar *msg){ PINFO ("%s", msg); });

    if (!args->file_to_load.empty())
    {
        auto datafile = args->file_to_load.c_str();
        PINFO ("Loading datafile %s...\n", datafile);

        auto session = gnc_get_current_session ();
        if (session)
        {
            qof_session_begin (session, datafile, SESSION_READ_ONLY);
            if (qof_session_get_error (session) == ERR_BACKEND_NO_ERR)
                qof_session_load (session, report_session_percentage);
        }
    }

    scm_call_2 (scm_c_eval_string ("gnc:cmdline-report-show"),
                scm_from_locale_string (args->show_report.c_str ()),
                scm_current_output_port ());
    gnc_shutdown_cli (0);
    return;
}


static void
scm_report_list ([[maybe_unused]] void *data,
                 [[maybe_unused]] int argc, [[maybe_unused]] char **argv)
{
    scm_c_eval_string("(debug-set! stack 200000)");
    scm_c_use_module ("gnucash app-utils");
    scm_c_use_module ("gnucash reports");
    gnc_report_init ();
    Gnucash::gnc_load_scm_config ([](const gchar *msg){ PINFO ("%s", msg); });

    scm_call_1 (scm_c_eval_string ("gnc:cmdline-report-list"),
                scm_current_output_port ());
    gnc_shutdown_cli (0);
    return;
}

int
Gnucash::check_finance_quote (void)
{
    gnc_prefs_init ();
    try
    {
        GncQuotes quotes;
        std::cout << bl::format (bl::translate ("Found Finance::Quote version {1}.")) % quotes.version() << "\n";
        std::cout << bl::translate ("Finance::Quote sources:\n");
        int count{0};
        const auto width{12};
        for (auto source : quotes.sources())
        {
            auto mul{source.length() / width + 1};
            count += mul;
            if (count > 6)
            {
                count = mul;
                std::cout << "\n";
            }
            std::cout << std::setw(mul * (width + 1)) << std::left << source;
        }
        std::cout << std::endl;
        return 0;
    }
    catch (const GncQuoteException& err)
    {
        std::cout << err.what() << std::endl;
        return 1;
    }
}

int
Gnucash::add_quotes (const bo_str& uri)
{
    int rv{};
    gnc_prefs_init ();
    qof_event_suspend();

    auto session = gnc_get_current_session();
    if (!session)
        return 1;

    qof_session_begin(session, uri->c_str(), SESSION_NORMAL_OPEN);
    if (qof_session_get_error(session) != ERR_BACKEND_NO_ERR)
        return cleanup_and_exit_with_failure (session);

    qof_session_load(session, NULL);
    if (qof_session_get_error(session) != ERR_BACKEND_NO_ERR)
        return cleanup_and_exit_with_failure (session);

    try
    {
        GncQuotes quotes;
        std::cout << bl::format (bl::translate ("Found Finance::Quote version {1}.")) % quotes.version() << std::endl;
        auto quote_sources = quotes.sources();
        gnc_quote_source_set_fq_installed (quotes.version().c_str(), quote_sources);
        quotes.fetch(qof_session_get_book(session));
        if (quotes.had_failures())
        {
            std::cerr << quotes.report_failures() << std::endl;
            rv = 2;
        }
    }
    catch (const GncQuoteException& err)
    {
        std::cerr << bl::translate("Price retrieval failed: ") << err.what() << std::endl;
    }
    qof_session_save(session, NULL);
    if (qof_session_get_error(session) != ERR_BACKEND_NO_ERR)
        return cleanup_and_exit_with_failure (session);

    qof_session_destroy(session);
    qof_event_resume();
    return rv;
}

int
Gnucash::report_quotes (const char* source, const StrVec& commodities, bool verbose)
{
    gnc_prefs_init();
    try
    {
        GncQuotes quotes;
        quotes.report(source, commodities, verbose);
        if (quotes.had_failures())
            std::cerr << quotes.report_failures() << std::endl;
    }
    catch (const GncQuoteException& err)
    {
        std::cerr << bl::translate("Price retrieval failed: ") << err.what() << std::endl;
        return -1;
   }
    return 0;
}

int
Gnucash::run_report (const bo_str& file_to_load,
                     const bo_str& run_report,
                     const bo_str& export_type,
                     const bo_str& output_file)
{
    auto args = run_report_args { file_to_load ? *file_to_load : empty_string,
                                  run_report ? *run_report : empty_string,
                                  export_type ? *export_type : empty_string,
                                  output_file ? *output_file : empty_string };
    if (run_report && !run_report->empty())
        scm_boot_guile (0, nullptr, scm_run_report, &args);

    return 0;
}

int
Gnucash::report_show (const bo_str& file_to_load,
                      const bo_str& show_report)
{
    auto args = show_report_args { file_to_load ? *file_to_load : empty_string,
                                   show_report ? *show_report : empty_string };
    if (show_report && !show_report->empty())
        scm_boot_guile (0, nullptr, scm_report_show, &args);

    return 0;
}

int
Gnucash::report_list (void)
{
    scm_boot_guile (0, nullptr, scm_report_list, NULL);
    return 0;
}

/* ================================================================ *
 * Checking reconciled balances                                     *
 * ================================================================ */

/* One row of output. Held rather than printed as we go so that the
 * records for an account can be examined together before any of them is
 * described -- the pattern across them says more than any one does. */
struct RecnCheckRow
{
    std::string date;
    std::string recorded;
    std::string actual;
    std::string delta;
    std::string notes;
    bool broken;
};

/* Amounts are printed the way the register shows them, so that a credit
 * card's -1,204.55 does not read as 1,204.55 here and the other way
 * round in the GUI. */
static std::string
recn_print (const Account *acc, gnc_numeric amount)
{
    auto pinfo = gnc_account_print_info (acc, TRUE);

    if (gnc_reverse_balance (acc))
        amount = gnc_numeric_neg (amount);

    return xaccPrintAmount (amount, pinfo);
}

static std::string
recn_print_date (time64 date)
{
    char buf[MAX_DATE_LENGTH + 1];

    qof_print_date_buff (buf, MAX_DATE_LENGTH, date);
    return buf;
}

/* Every broken record out by the same amount is the signature of one
 * transaction entered after the fact with an earlier date -- a cheque
 * cashed late, most often -- rather than of damage in several places.
 * Saying which of the two it looks like is the whole value of reading
 * an account's records together. */
static bool
recn_shares_one_delta (const std::vector<gnc_numeric>& deltas)
{
    if (deltas.size() < 2)
        return false;

    for (const auto& d : deltas)
        if (!gnc_numeric_equal (d, deltas.front()))
            return false;

    return true;
}

static void
recn_report_account (const Account *acc, const std::vector<RecnCheckRow>& rows,
                     const std::vector<gnc_numeric>& broken_deltas,
                     bool verbose)
{
    auto name = gnc_account_get_full_name (const_cast<Account*>(acc));

    std::cout << name << "\n";
    g_free (name);

    for (const auto& row : rows)
    {
        if (!row.broken && !verbose)
            continue;

        std::cout << "  " << (row.broken ? "BROKEN" : "ok    ")
                  << "  " << std::setw (12) << row.date
                  << "  recorded " << std::setw (14) << std::right << row.recorded
                  << "  now " << std::setw (14) << std::right << row.actual;

        if (row.broken)
            std::cout << "  out by " << std::setw (14) << std::right << row.delta;

        if (!row.notes.empty())
            std::cout << "  (" << row.notes << ")";

        std::cout << std::left << "\n";
    }

    if (recn_shares_one_delta (broken_deltas))
        std::cout << bl::format
            (bl::translate ("  All {1} out by the same {2}: this is what one "
                            "transaction entered late with an earlier date "
                            "looks like."))
            % broken_deltas.size ()
            % recn_print (acc, broken_deltas.front ())
                  << "\n";
    else if (broken_deltas.size () > 1)
        std::cout << bl::translate
            ("  Out by differing amounts: more than one thing has changed.")
                  << "\n";

    std::cout << std::endl;
}

int
Gnucash::check_reconciled_balances (const bo_str& file_to_load, bool verbose)
{
    gnc_prefs_init ();
    qof_event_suspend ();

    auto session = gnc_get_current_session ();
    if (!session)
        return 1;

    /* Read-only: checking must never take the lock, so that it can be
     * run against a book that is open in GnuCash at the time. */
    qof_session_begin (session, file_to_load->c_str (), SESSION_READ_ONLY);
    if (qof_session_get_error (session) != ERR_BACKEND_NO_ERR)
        return cleanup_and_exit_with_failure (session);

    qof_session_load (session, NULL);
    if (qof_session_get_error (session) != ERR_BACKEND_NO_ERR)
        return cleanup_and_exit_with_failure (session);

    /* The records live in the state file, which the GUI loads on opening
     * a book. Nothing has done that for us here. */
    gnc_state_load (session);

    auto book = qof_session_get_book (session);
    auto all = gnc_reconciled_balance_get_all (book);
    guint total = 0, broken = 0;

    /* Records arrive sorted by date across the whole book; group them by
     * account so that each account's pattern can be read at once. */
    std::map<const Account*, std::vector<RecnCheckRow>> by_account;
    std::map<const Account*, std::vector<gnc_numeric>> deltas;
    std::vector<const Account*> order;

    for (auto n = all; n; n = n->next)
    {
        auto rb = static_cast<GncReconciledBalance*> (n->data);
        auto acc = gnc_reconciled_balance_get_account (rb);
        auto is_broken = gnc_reconciled_balance_is_broken (rb);

        if (by_account.find (acc) == by_account.end ())
            order.push_back (acc);

        by_account[acc].push_back
            ({ recn_print_date (gnc_reconciled_balance_get_date (rb)),
               recn_print (acc, gnc_reconciled_balance_get_amount (rb)),
               recn_print (acc, gnc_reconciled_balance_get_actual (rb)),
               recn_print (acc, gnc_reconciled_balance_get_delta (rb)),
               gnc_reconciled_balance_get_notes (rb),
               static_cast<bool> (is_broken) });

        if (is_broken)
        {
            deltas[acc].push_back (gnc_reconciled_balance_get_delta (rb));
            ++broken;
        }

        ++total;
    }

    g_list_free (all);

    std::cout << std::left;

    for (auto acc : order)
    {
        /* With nothing broken there is nothing to say about an account
         * unless the user asked to see the lot. */
        if (deltas[acc].empty () && !verbose)
            continue;

        recn_report_account (acc, by_account[acc], deltas[acc], verbose);
    }

    if (total == 0)
        /* Not the same as a clean book, and worth distinguishing: no
         * record was ever written for this file on this machine. */
        std::cout << bl::translate
            ("No reconciled balances are recorded for this file.") << std::endl;
    else if (broken == 0)
        std::cout << bl::format
            (bl::translate ("{1} reconciled balance still holds.",
                            "All {1} reconciled balances still hold.", total))
            % total << std::endl;
    else
        std::cout << bl::format
            (bl::translate ("{1} of {2} reconciled balances no longer holds.",
                            "{1} of {2} reconciled balances no longer hold.",
                            broken)) % broken % total << std::endl;

    qof_session_destroy (session);
    qof_event_resume ();

    return broken ? 2 : 0;
}
