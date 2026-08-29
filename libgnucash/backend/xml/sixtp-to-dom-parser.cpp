/********************************************************************
 * sixtp-to-dom-parser.c                                            *
 * Copyright 2001 Gnumatic, Inc.                                    *
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
 ********************************************************************/
#include <config.h>
#include <ctype.h>

#include <glib.h>

#include "sixtp-parsers.h"
#include "sixtp-utils.h"
#include "sixtp.h"

static xmlNsPtr global_namespace = NULL;

/* Don't pass anything in the data_for_children value to this
   function.  It'll cause a segfault */
static gboolean dom_start_handler (
    GSList* sibling_data, gpointer parent_data, gpointer global_data,
    gpointer* data_for_children, gpointer* result, const gchar* tag,
    gchar** attrs)
{
    xmlNodePtr thing;
    gchar** atptr = attrs;

    if (parent_data == NULL)
    {
        thing = xmlNewNode (global_namespace, BAD_CAST tag);
        /* only publish the result if we're the parent */
        *result = thing;
    }
    else
    {
        thing = xmlNewChild ((xmlNodePtr) parent_data,
                             global_namespace,
                             BAD_CAST tag,
                             NULL);
        *result = NULL;
    }
    *data_for_children = thing;

    if (attrs != NULL)
    {
        while (*atptr != 0)
        {
            gchar* attr0 = g_strdup (atptr[0]);
            gchar* attr1 = g_strdup (atptr[1]);
            xmlSetProp (thing, checked_char_cast (attr0),
                        checked_char_cast (attr1));
            g_free (attr0);
            g_free (attr1);
            atptr += 2;
        }
    }
    return TRUE;
}

static void
dom_fail_handler (gpointer data_for_children,
                  GSList* data_from_children,
                  GSList* sibling_data,
                  gpointer parent_data,
                  gpointer global_data,
                  gpointer* result,
                  const gchar* tag)
{
    if (*result) xmlFreeNode (static_cast<xmlNodePtr> (*result));
}

static gboolean dom_chars_handler (
    GSList* sibling_data, gpointer parent_data, gpointer global_data,
    gpointer* result, const char* text, int length)
{
    if (length > 0)
    {
        gchar* newtext = g_strndup (text,length);
        xmlNodeAddContentLen ((xmlNodePtr)parent_data,
                              checked_char_cast (newtext), length);
        g_free (newtext);
    }
    return TRUE;
}

sixtp*
sixtp_dom_parser_new (sixtp_end_handler ender,
                      sixtp_result_handler cleanup_result_by_default_func,
                      sixtp_result_handler cleanup_result_on_fail_func)
{
    sixtp* top_level;

    g_return_val_if_fail (ender, NULL);

    if (! (top_level =
               sixtp_set_any (sixtp_new (), FALSE,
                              SIXTP_START_HANDLER_ID, dom_start_handler,
                              SIXTP_CHARACTERS_HANDLER_ID, dom_chars_handler,
                              SIXTP_END_HANDLER_ID, ender,
                              SIXTP_FAIL_HANDLER_ID, dom_fail_handler,
                              SIXTP_NO_MORE_HANDLERS)))
    {
        return NULL;
    }

    if (cleanup_result_by_default_func)
    {
        sixtp_set_cleanup_result (top_level, cleanup_result_by_default_func);
    }

    if (cleanup_result_by_default_func)
    {
        sixtp_set_result_fail (top_level, cleanup_result_on_fail_func);
    }

    if (!sixtp_add_sub_parser (top_level, SIXTP_MAGIC_CATCHER, top_level))
    {
        sixtp_destroy (top_level);
        return NULL;
    }

    return top_level;
}

/* dom_start_handler() decides whether it's building the root of a DOM
   subtree or another node inside one already in progress by checking
   whether parent_data is NULL. That works for sixtp_dom_parser_new()
   because it is always plugged in directly under a plain sixtp_new()
   parser (no start handler of its own, so parent_data is NULL at that
   boundary). sixtp_dom_parser_new_rooted() is for the opposite case:
   plugging a small DOM-based sub-parser (used for things like kvp
   frames, which nest arbitrarily and aren't worth reimplementing as a
   SAX-direct parser) into a tag of an otherwise SAX-direct, non-DOM
   parser tree, where parent_data is some unrelated caller-owned struct,
   not an xmlNodePtr. The entry point below forces the "start a new
   root" branch unconditionally; every tag nested inside it goes back to
   the ordinary, unrooted DOM builder, which does see real xmlNodePtr
   parent_data from there on down. */
static gboolean
dom_start_handler_rooted (GSList* sibling_data, gpointer parent_data,
                          gpointer global_data, gpointer* data_for_children,
                          gpointer* result, const gchar* tag, gchar** attrs)
{
    return dom_start_handler (sibling_data, NULL, global_data,
                              data_for_children, result, tag, attrs);
}

sixtp*
sixtp_dom_parser_new_rooted (sixtp_end_handler ender,
                             sixtp_result_handler cleanup_result_by_default_func,
                             sixtp_result_handler cleanup_result_on_fail_func)
{
    sixtp* inner;
    sixtp* entry;

    g_return_val_if_fail (ender, NULL);

    /* inner builds ordinary (non-root) DOM nodes for everything nested
       inside the rooted entry point below. It deliberately has no end
       handler of its own: a non-root dom_start_handler() call never
       publishes *result (the built node is simply linked into its
       parent via xmlNewChild(), and freed along with the rest of the
       subtree when the caller's ender() frees the root), so there is
       nothing for an end handler to do at these levels -- only the
       root closing tag, handled by entry below, should ever call
       ender(). */
    if (! (inner =
               sixtp_set_any (sixtp_new (), FALSE,
                              SIXTP_START_HANDLER_ID, dom_start_handler,
                              SIXTP_CHARACTERS_HANDLER_ID, dom_chars_handler,
                              SIXTP_FAIL_HANDLER_ID, dom_fail_handler,
                              SIXTP_NO_MORE_HANDLERS)))
    {
        return NULL;
    }

    if (!sixtp_add_sub_parser (inner, SIXTP_MAGIC_CATCHER, inner))
    {
        sixtp_destroy (inner);
        return NULL;
    }

    if (! (entry =
               sixtp_set_any (sixtp_new (), FALSE,
                              SIXTP_START_HANDLER_ID, dom_start_handler_rooted,
                              SIXTP_CHARACTERS_HANDLER_ID, dom_chars_handler,
                              SIXTP_END_HANDLER_ID, ender,
                              SIXTP_FAIL_HANDLER_ID, dom_fail_handler,
                              SIXTP_NO_MORE_HANDLERS)))
    {
        sixtp_destroy (inner);
        return NULL;
    }

    if (cleanup_result_by_default_func)
    {
        sixtp_set_cleanup_result (entry, cleanup_result_by_default_func);
        sixtp_set_cleanup_result (inner, cleanup_result_by_default_func);
    }

    if (cleanup_result_on_fail_func)
    {
        sixtp_set_result_fail (entry, cleanup_result_on_fail_func);
        sixtp_set_result_fail (inner, cleanup_result_on_fail_func);
    }

    if (!sixtp_add_sub_parser (entry, SIXTP_MAGIC_CATCHER, inner))
    {
        sixtp_destroy (inner);
        sixtp_destroy (entry);
        return NULL;
    }

    return entry;
}
