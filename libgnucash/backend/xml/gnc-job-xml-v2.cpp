/********************************************************************\
 * gnc-job-xml-v2.c -- job xml i/o implementation         *
 *                                                                  *
 * Copyright (C) 2002 Derek Atkins <warlord@MIT.EDU>                *
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
 *                                                                  *
\********************************************************************/
#include <glib.h>

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include "gncJobP.h"
#include <guid.hpp>

#include "gnc-xml-helper.h"
#include "sixtp.h"
#include "sixtp-utils.h"
#include "sixtp-parsers.h"
#include "sixtp-utils.h"
#include "sixtp-dom-parsers.h"
#include "sixtp-dom-generators.h"

#include "gnc-xml.h"
#include "io-gncxml-gen.h"
#include "io-gncxml-v2.h"

#include "gnc-job-xml-v2.h"
#include "gnc-owner-xml-v2.h"
#include "xml-helpers.h"

#define _GNC_MOD_NAME   GNC_ID_JOB

const gchar* job_version_string = "2.0.0";

/* ids */
#define gnc_job_string "gnc:GncJob"
#define job_guid_string "job:guid"
#define job_id_string "job:id"
#define job_name_string "job:name"
#define job_reference_string "job:reference"
#define job_owner_string "job:owner"
#define job_active_string "job:active"
#define job_slots_string "job:slots"

static xmlNodePtr
job_dom_tree_create (GncJob* job)
{
    xmlNodePtr ret;

    ret = xmlNewNode (NULL, BAD_CAST gnc_job_string);
    xmlSetProp (ret, BAD_CAST "version", BAD_CAST job_version_string);

    xmlAddChild (ret, guid_to_dom_tree (job_guid_string,
                                        qof_instance_get_guid (QOF_INSTANCE (job))));

    xmlAddChild (ret, text_to_dom_tree (job_id_string,
                                        gncJobGetID (job)));

    xmlAddChild (ret, text_to_dom_tree (job_name_string,
                                        gncJobGetName (job)));

    maybe_add_string (ret, job_reference_string, gncJobGetReference (job));

    xmlAddChild (ret, gnc_owner_to_dom_tree (job_owner_string,
                                             gncJobGetOwner (job)));

    xmlAddChild (ret, int_to_dom_tree (job_active_string,
                                       gncJobGetActive (job)));

    /* xmlAddChild won't do anything with a NULL, so tests are superfluous. */
    xmlAddChild (ret, qof_instance_slots_to_dom_tree (job_slots_string,
                                                      QOF_INSTANCE (job)));

    return ret;
}

/***********************************************************************/
/* SAX-direct (streaming) job parser: reads a gnc:GncJob straight off
   the SAX character stream, with no intermediate xmlNodePtr built for
   any of its fields. Nothing else in the codebase uses the old
   DOM-based parser this replaces, so it's gone entirely. */

struct job_sax_pdata
{
    GncJob* job;
    GncOwner owner;
    QofBook* book;
};

static gboolean
sax_job_guid_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                  gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<job_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        GncGUID guid;
        if (!string_to_guid (txt, &guid)) return FALSE;

        /* Adopt a job that already exists by this guid (e.g. a
           placeholder created earlier by an owner reference) instead
           of the fresh one sax_job_start() made. */
        GncJob* job = gncJobLookup (pdata->book, &guid);
        if (job)
        {
            gncJobDestroy (pdata->job);
            pdata->job = job;
            gncJobBeginEdit (job);
        }
        else
            gncJobSetGUID (pdata->job, &guid);
        return TRUE;
    });
}

static gboolean
sax_job_id_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<job_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncJobSetID (pdata->job, txt); return TRUE; });
}

static gboolean
sax_job_name_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                  gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<job_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncJobSetName (pdata->job, txt); return TRUE; });
}

static gboolean
sax_job_reference_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                       gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<job_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    { gncJobSetReference (pdata->job, txt); return TRUE; });
}

static gboolean
sax_job_active_end (gpointer, GSList* dfc, GSList*, gpointer parent_data,
                    gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<job_sax_pdata*> (parent_data);
    return sax_apply_chars (dfc, [pdata] (const char* txt) -> gboolean
    {
        gint64 val = 0;
        string_to_gint64 (txt, &val);
        gncJobSetActive (pdata->job, (gboolean) val);
        return TRUE;
    });
}

static gboolean
sax_job_slots_dom_end (gpointer data_for_children, GSList*, GSList*,
                       gpointer parent_data, gpointer, gpointer*, const gchar*)
{
    auto* pdata = static_cast<job_sax_pdata*> (parent_data);
    xmlNodePtr tree = static_cast<xmlNodePtr> (data_for_children);
    gboolean ret = dom_tree_create_instance_slots (tree, QOF_INSTANCE (pdata->job));
    xmlFreeNode (tree);
    return ret;
}

static gboolean
sax_job_owner_start (GSList*, gpointer parent_data, gpointer,
                     gpointer* data_for_children, gpointer*, const gchar*, gchar**)
{
    auto* pdata = static_cast<job_sax_pdata*> (parent_data);
    auto* ctx = g_new (owner_sax_ctx, 1);
    ctx->owner = &pdata->owner;
    ctx->book = pdata->book;
    *data_for_children = ctx;
    return TRUE;
}

static gboolean
sax_job_start (GSList*, gpointer, gpointer global_data, gpointer* data_for_children,
              gpointer*, const gchar* tag, gchar**)
{
    if (!tag)
    {
        *data_for_children = nullptr;
        return TRUE;
    }
    auto* gdata = static_cast<gxpf_data*> (global_data);
    QofBook* book = static_cast<QofBook*> (gdata->bookdata);
    auto* pdata = g_new0 (job_sax_pdata, 1);
    pdata->job = gncJobCreate (book);
    pdata->book = book;
    gncJobBeginEdit (pdata->job);
    *data_for_children = pdata;
    return TRUE;
}

static gboolean
sax_job_end (gpointer data_for_children, GSList*, GSList*, gpointer parent_data,
            gpointer global_data, gpointer*, const gchar* tag)
{
    auto* pdata = static_cast<job_sax_pdata*> (data_for_children);
    if (!pdata) return TRUE;
    if (!tag) { g_free (pdata); return TRUE; }

    gncJobSetOwner (pdata->job, &pdata->owner);
    gncJobCommitEdit (pdata->job);

    auto* gdata = static_cast<gxpf_data*> (global_data);
    gdata->cb (tag, gdata->parsedata, pdata->job);

    g_free (pdata);
    return TRUE;
}

static void
sax_job_fail (gpointer data_for_children, GSList*, GSList*, gpointer, gpointer,
             gpointer*, const gchar*)
{
    auto* pdata = static_cast<job_sax_pdata*> (data_for_children);
    if (!pdata) return;
    gncJobDestroy (pdata->job);
    g_free (pdata);
}

static sixtp*
job_sixtp_parser_create (void)
{
    sixtp* p = sixtp_set_any (
        sixtp_new (), FALSE,
        SIXTP_START_HANDLER_ID, sax_job_start,
        SIXTP_END_HANDLER_ID, sax_job_end,
        SIXTP_FAIL_HANDLER_ID, sax_job_fail,
        SIXTP_NO_MORE_HANDLERS);
    g_return_val_if_fail (p, NULL);

    p = sixtp_add_some_sub_parsers (
        p, TRUE,
        job_guid_string, restore_char_generator (sax_job_guid_end),
        job_id_string, restore_char_generator (sax_job_id_end),
        job_name_string, restore_char_generator (sax_job_name_end),
        job_reference_string, restore_char_generator (sax_job_reference_end),
        job_active_string, restore_char_generator (sax_job_active_end),
        job_slots_string, sixtp_dom_parser_new_rooted (sax_job_slots_dom_end, NULL, NULL),
        NULL, NULL);
    g_return_val_if_fail (p, NULL);

    sixtp_add_sub_parser (p, job_owner_string, sax_owner_parser_new (sax_job_owner_start));

    return p;
}

static gboolean
job_should_be_saved (GncJob* job)
{
    const char* id;

    /* make sure this is a valid job before we save it -- should have an ID */
    id = gncJobGetID (job);
    if (id == NULL || *id == '\0')
        return FALSE;

    return TRUE;
}

static void
do_count (QofInstance* job_p, gpointer count_p)
{
    int* count = static_cast<decltype (count)> (count_p);
    if (job_should_be_saved ((GncJob*)job_p))
        (*count)++;
}

static int
job_get_count (QofBook* book)
{
    int count = 0;
    qof_object_foreach (_GNC_MOD_NAME, book, do_count, (gpointer) &count);
    return count;
}

static void
xml_add_job (QofInstance* job_p, gpointer out_p)
{
    xmlNodePtr node;
    GncJob* job = (GncJob*) job_p;
    FILE* out = static_cast<decltype (out)> (out_p);

    if (ferror (out))
        return;
    if (!job_should_be_saved (job))
        return;

    node = job_dom_tree_create (job);
    xmlElemDump (out, NULL, node);
    xmlFreeNode (node);
    if (ferror (out) || fprintf (out, "\n") < 0)
        return;
}

static gboolean
job_write (FILE* out, QofBook* book)
{
    qof_object_foreach_sorted (_GNC_MOD_NAME, book, xml_add_job, (gpointer) out);
    return ferror (out) == 0;
}

static gboolean
job_ns (FILE* out)
{
    g_return_val_if_fail (out, FALSE);
    return gnc_xml2_write_namespace_decl (out, "job");
}

void
gnc_job_xml_initialize (void)
{
    static GncXmlDataType_t be_data =
    {
        GNC_FILE_BACKEND_VERS,
        gnc_job_string,
        job_sixtp_parser_create,
        NULL,           /* add_item */
        job_get_count,
        job_write,
        NULL,           /* scrub */
        job_ns,
    };

    gnc_xml_register_backend(be_data);
}
