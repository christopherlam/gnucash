/********************************************************************\
 * test-xml-reader-edge-cases.cpp -- exhaustive edge-case tests for *
 * the libxml2-DOM-built XML v2 reader (sixtp-to-dom-parser.cpp,    *
 * sixtp-dom-parsers.cpp)                                           *
 *                                                                  *
 * Copyright (C) 2026 The GnuCash Project                          *
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
/** @file test-xml-reader-edge-cases.cpp
 *
 * This exercises the read side of the v2 XML backend (the real libxml2
 * xmlNode tree that sixtp_dom_parser_new builds, plus dom_tree_to_*)
 * against inputs a reader has to deal with: Unicode content split
 * across SAX characters() chunk boundaries, CDATA, comments, mixed
 * content, malformed/non-well-formed XML, and domain-level validation
 * failures (unknown tags, missing required fields).
 *
 * This file is a deliberate baseline: it is meant to land on stable
 * first, against the pre-existing xmlNodePtr/libxml2-DOM reader, so
 * its pass/fail results are on record before the SAX-tree rewrite
 * (which replaces xmlNodePtr with a lightweight GncXmlNode on the read
 * side only) lands on top of it. That follow-up change adapts this
 * same suite call-for-call to the new node type; an unchanged set of
 * passes across both versions is the evidence that the rewrite altered
 * no observable reader behavior.
 *
 * Two harnesses are used deliberately:
 *
 *  - A bare sixtp parser (parse_one_element/parse_wrapped), used for
 *    every case that is *expected* to fail (well-formedness or
 *    domain-validation failures). This mirrors gnc_read_example_account's
 *    wrapping pattern and stops short of a full QofSession/QofBook
 *    load-and-destroy cycle.
 *  - A full qof_session_load, used only for inputs that are expected to
 *    *succeed*, to prove the parsed data round-trips into real Account
 *    objects end to end.
 *
 * This split is intentional, not incidental: a full QofBook that
 * registers business objects (via cashobjects_register) and is then
 * destroyed after a *partially failed* account parse currently crashes
 * in gncTaxTable's per-book teardown (_gncTaxTableDestroy). This is a
 * pre-existing bug in QofSession::load()'s failure path (see the
 * "IMPORTANT" note on qof_session_load's doc comment in qofsession.h):
 * on load failure it destroys and replaces the session's QofBook out
 * from under any pointer callers already hold to it. It reproduces
 * identically regardless of which xmlNode type the reader builds, so
 * it is unrelated to this reader and intentionally not exercised here.
 */
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <optional>
#include <string>

#include <cashobjects.h>
#include <gnc-engine.h>
#include <gnc-uri-utils.h>
#include <Account.h>
#include <kvp-frame.hpp>

#include "qof.h"
#include "sixtp.h"
#include "sixtp-parsers.h"
#include "sixtp-utils.h"
#include "sixtp-dom-parsers.h"
#include "gnc-xml.h"
#include "io-gncxml-gen.h"
#include "io-gncxml-v2.h"

#include <test-stuff.h>
#include <test-engine-stuff.h>

extern KvpFrame* dom_tree_to_kvp_frame (xmlNodePtr node);

#define GNC_LIB_NAME "gncmod-backend-xml"
#define GNC_LIB_REL_PATH "xml"

/***********************************************************************/
/* Small helpers for building xmlNode trees by hand, and for driving
   a real sixtp parser over a raw XML string without going through a
   full QofSession. */

static xmlNodePtr
build_leaf (const char* tag, const char* text)
{
    xmlNodePtr node = xmlNewNode (NULL, BAD_CAST tag);
    if (text)
        xmlNodeAddContentLen (node, BAD_CAST text, strlen (text));
    return node;
}

struct capture_pdata
{
    xmlNodePtr tree = nullptr;
};

static gboolean
capture_end_handler (gpointer data_for_children, GSList*, GSList*,
                     gpointer parent_data, gpointer global_data,
                     gpointer* result, const gchar* tag)
{
    if (parent_data || !tag)
        return TRUE;

    auto pdata = static_cast<capture_pdata*> (global_data);
    pdata->tree = static_cast<xmlNodePtr> (data_for_children);
    /* Hand ownership to pdata->tree; caller frees it. */
    return TRUE;
}

/* Parses a single top-level element ('tag') out of 'xml' and returns
   its captured xmlNode tree (caller must xmlFreeNode it), or nullptr
   if parsing failed (malformed XML, wrong root tag, etc).

   The dom_parser is deliberately nested one level under a synthetic
   wrapping tag rather than being used bare as the top-level sixtp.
   sixtp_context_new's one-time bootstrap call passes the *address* of
   its own top_frame_data field as parent_data (not the NULL that
   top_level_data actually was), so a dom_parser's end_handler would
   see a permanently non-NULL parent_data and skip every callback if it
   were the literal top-level parser - it would never fire, regardless
   of how well-formed the input is. Nesting it under a real tag avoids
   relying on that bootstrap call at all: the outer wrapper's generic
   (non-DOM) start/end handling supplies a genuine NULL parent_data to
   the dom_parser's own first invocation. */
static xmlNodePtr
parse_one_element (const char* tag, const std::string& xml)
{
    capture_pdata pdata;

    sixtp* top_parser = sixtp_new ();
    sixtp_add_some_sub_parsers (top_parser, TRUE, tag,
                               sixtp_dom_parser_new (capture_end_handler, NULL, NULL),
                               NULL, NULL);

    char filename[] = "/tmp/gnc_xml_test_XXXXXX";
    int fd = g_mkstemp (filename);
    write (fd, xml.data (), xml.size ());
    close (fd);

    gpointer parse_result = NULL;
    gboolean ok = sixtp_parse_file (top_parser, filename, NULL, &pdata, &parse_result);

    g_unlink (filename);
    /* Not sixtp_destroy(top_parser): its "tag" child is a
       sixtp_dom_parser_new() result, self-referential via
       SIXTP_MAGIC_CATCHER, which double-frees on destroy (see
       parse_wrapped's comment below for the same landmine). */

    if (!ok)
    {
        if (pdata.tree)
            xmlFreeNode (pdata.tree);
        return nullptr;
    }
    return pdata.tree;
}

/* Wraps 'inner_xml' (one or more sibling elements) under a synthetic
   root and a nested named sub-parser for 'child_tag', exactly the way
   gnc_read_example_account/io-gncxml-v2.cpp nest a *_sixtp_parser_create()
   under a real tag - this is what actually exercises the object-level
   end_handler (dom_tree_to_account etc.), unlike using the object
   parser bare as the top-level sixtp (which never invokes it; see
   sixtp-stack.cpp's parent_data handling of the bootstrap call).
 */
struct wrapped_result
{
    gboolean ok = FALSE;
    int callback_count = 0;
    gboolean object_created = FALSE;
};

static gboolean
wrapped_cb (const char* tag, gpointer globaldata, gpointer data)
{
    auto res = static_cast<wrapped_result*> (globaldata);
    res->callback_count++;
    res->object_created = (data != NULL);
    return TRUE;
}

static wrapped_result
parse_wrapped (sixtp* (*parser_create) (void), const char* child_tag,
              const std::string& xml, QofBook* book)
{
    wrapped_result res;

    sixtp* top_parser = sixtp_new ();
    sixtp* main_parser = sixtp_new ();
    sixtp_add_some_sub_parsers (top_parser, TRUE, "gnc-wrapper", main_parser, NULL, NULL);
    sixtp_add_some_sub_parsers (main_parser, TRUE, child_tag, parser_create (), NULL, NULL);

    char filename[] = "/tmp/gnc_xml_test_XXXXXX";
    int fd = g_mkstemp (filename);
    write (fd, xml.data (), xml.size ());
    close (fd);

    res.ok = gnc_xml_parse_file (top_parser, filename, wrapped_cb, &res, book);

    g_unlink (filename);
    /* Not sixtp_destroy(top_parser): child_tag's parser_create() result
       is itself a sixtp_dom_parser_new() (self-referential via
       SIXTP_MAGIC_CATCHER), so destroying this tree hits the same
       double-free landmine explained in parse_one_element above. */
    return res;
}

/***********************************************************************/
/* 1. xmlNode primitive behavior: UTF-8, chunking, attributes           */
/***********************************************************************/

static void
test_utf8_round_trip (void)
{
    /* 2-byte, 3-byte and 4-byte UTF-8 sequences in one string. */
    const char* utf8 = "caf\xC3\xA9 \xE4\xB8\xAD\xE6\x96\x87 \xF0\x9F\x98\x80";

    xmlNodePtr node = build_leaf ("test", utf8);
    auto text = dom_tree_to_text (node);
    do_test (text.has_value () && *text == utf8,
             "UTF-8 content round-trips through a single add_content call");
    xmlFreeNode (node);
}

static void
test_utf8_chunked_across_codepoint_boundary (void)
{
    const char* utf8 = "caf\xC3\xA9 \xE4\xB8\xAD\xE6\x96\x87 \xF0\x9F\x98\x80";
    size_t len = strlen (utf8);

    /* Split at every possible byte offset, including offsets that land
       mid-way through a multi-byte UTF-8 sequence. sixtp's characters()
       callback delivers a byte length, not a character count, and the
       xmlNode content buffer must reassemble correctly regardless of
       where a SAX chunk boundary happens to fall. This mirrors exactly
       how dom_chars_handler feeds each chunk to xmlNodeAddContentLen in
       production. */
    gboolean all_ok = TRUE;
    for (size_t split = 0; split <= len; ++split)
    {
        xmlNodePtr node = xmlNewNode (NULL, BAD_CAST "test");
        xmlNodeAddContentLen (node, BAD_CAST utf8, (int)split);
        xmlNodeAddContentLen (node, BAD_CAST (utf8 + split), (int)(len - split));
        auto text = dom_tree_to_text (node);
        if (!text || *text != utf8)
        {
            all_ok = FALSE;
            failure_args ("chunked UTF-8 reassembly", __FILE__, __LINE__,
                          "split at byte %zu produced [%s]",
                          split, text ? text->c_str () : "(null)");
        }
        xmlFreeNode (node);
    }
    do_test (all_ok, "UTF-8 reassembles correctly for every possible chunk split point");
}

static void
test_attribute_set_get (void)
{
    xmlNodePtr node = xmlNewNode (NULL, BAD_CAST "test");

    do_test (xmlGetProp (node, BAD_CAST "missing") == NULL,
             "missing attribute lookup returns NULL");

    xmlSetProp (node, BAD_CAST "version", BAD_CAST "2.0.0");
    xmlChar* v = xmlGetProp (node, BAD_CAST "version");
    do_test (v && g_strcmp0 ((char*)v, "2.0.0") == 0, "attribute set/get round-trips");
    xmlFree (v);

    /* overwrite */
    xmlSetProp (node, BAD_CAST "version", BAD_CAST "3.0.0");
    v = xmlGetProp (node, BAD_CAST "version");
    do_test (v && g_strcmp0 ((char*)v, "3.0.0") == 0, "re-setting an attribute overwrites it, doesn't duplicate");
    xmlFree (v);

    /* unicode attribute value */
    xmlSetProp (node, BAD_CAST "note", BAD_CAST "caf\xC3\xA9");
    v = xmlGetProp (node, BAD_CAST "note");
    do_test (v && g_strcmp0 ((char*)v, "caf\xC3\xA9") == 0, "UTF-8 attribute value round-trips");
    xmlFree (v);

    xmlFreeNode (node);
}

static void
test_node_list_get_string_sibling_only (void)
{
    /* <leaf>100<b/>200</leaf> - xmlNodeListGetString walks direct
       siblings only, it does not recurse into <b>'s own children.
       Text that IS a direct sibling of a nested element (the "200"
       here) is still correctly captured even though <b/> sits between
       the two text runs. */
    xmlNodePtr leaf = xmlNewNode (NULL, BAD_CAST "leaf");
    xmlNodeAddContentLen (leaf, BAD_CAST "100", 3);
    xmlNewChild (leaf, NULL, BAD_CAST "b", NULL);
    xmlNodeAddContentLen (leaf, BAD_CAST "200", 3);

    xmlChar* result = xmlNodeListGetString (NULL, leaf->xmlChildrenNode, TRUE);
    do_test_args (g_strcmp0 ((char*)result, "100200") == 0,
                  "xmlNodeListGetString", __FILE__, __LINE__,
                  "expected [100200], got [%s]", (char*)result);
    xmlFree (result);
    xmlFreeNode (leaf);
}

static void
test_node_list_get_string_does_not_recurse (void)
{
    /* <leaf>100<b>NESTED</b>200</leaf> - text genuinely nested INSIDE
       <b> is not visible to a sibling-only walk. */
    xmlNodePtr leaf = xmlNewNode (NULL, BAD_CAST "leaf");
    xmlNodeAddContentLen (leaf, BAD_CAST "100", 3);
    xmlNodePtr b = xmlNewChild (leaf, NULL, BAD_CAST "b", NULL);
    xmlNodeAddContentLen (b, BAD_CAST "NESTED", 6);
    xmlNodeAddContentLen (leaf, BAD_CAST "200", 3);

    xmlChar* result = xmlNodeListGetString (NULL, leaf->xmlChildrenNode, TRUE);
    do_test_args (g_strcmp0 ((char*)result, "100200") == 0,
                  "xmlNodeListGetString ignores nested element text",
                  __FILE__, __LINE__, "expected [100200], got [%s]", (char*)result);
    xmlFree (result);
    xmlFreeNode (leaf);
}

static void
test_node_free_null_is_safe (void)
{
    xmlFreeNode (NULL);
    success ("xmlFreeNode(NULL) does not crash");
}

static void
test_empty_element_has_no_children (void)
{
    xmlNodePtr node = xmlNewNode (NULL, BAD_CAST "empty");
    do_test (node->children == NULL, "a freshly created node has no children");
    do_test (node->properties == NULL, "a freshly created node has no attributes");
    do_test (node->next == NULL && node->prev == NULL, "a freshly created node has no siblings");
    xmlFreeNode (node);
}

/***********************************************************************/
/* 2. xmlReadMemory: fidelity of a standalone parse used by tests       */
/***********************************************************************/

static void
test_from_libxml_preserves_structure (void)
{
    const char* xml_text = "<root attr1=\"v1\" attr2=\"v2\"><a>text-a</a><b>text-b</b></root>";
    xmlDocPtr doc = xmlReadMemory (xml_text, strlen (xml_text), "test.xml", NULL, 0);
    xmlNodePtr root = xmlDocGetRootElement (doc);

    do_test (g_strcmp0 ((char*)root->name, "root") == 0, "xmlReadMemory preserves element name");

    xmlChar* a1 = xmlGetProp (root, BAD_CAST "attr1");
    xmlChar* a2 = xmlGetProp (root, BAD_CAST "attr2");
    do_test (a1 && g_strcmp0 ((char*)a1, "v1") == 0, "xmlReadMemory preserves first attribute");
    do_test (a2 && g_strcmp0 ((char*)a2, "v2") == 0, "xmlReadMemory preserves second attribute");
    xmlFree (a1);
    xmlFree (a2);

    do_test (root->children != NULL && g_strcmp0 ((char*)root->children->name, "a") == 0,
             "xmlReadMemory preserves first child");
    do_test (root->children->next != NULL && g_strcmp0 ((char*)root->children->next->name, "b") == 0,
             "xmlReadMemory preserves second child as a sibling");

    auto text_a = dom_tree_to_text (root->children);
    do_test (text_a.has_value () && *text_a == "text-a", "xmlReadMemory preserves child text content");

    xmlFreeDoc (doc);
}

/***********************************************************************/
/* 3. dom_tree_to_* primitive converters: valid and invalid input       */
/***********************************************************************/

static void
test_dom_tree_to_guid_variants (void)
{
    GncGUID* gp = guid_new ();
    GncGUID g = *gp;
    guid_free (gp);
    gchar buf[GUID_ENCODING_LENGTH + 1];
    guid_to_string_buff (&g, buf);

    {
        xmlNodePtr node = build_leaf ("id", buf);
        xmlSetProp (node, BAD_CAST "type", BAD_CAST "guid");
        auto result = dom_tree_to_guid (node);
        do_test (result.has_value () && guid_equal (&*result, &g), "dom_tree_to_guid: type=\"guid\"");
        xmlFreeNode (node);
    }
    {
        xmlNodePtr node = build_leaf ("id", buf);
        xmlSetProp (node, BAD_CAST "type", BAD_CAST "new");
        auto result = dom_tree_to_guid (node);
        do_test (result.has_value () && guid_equal (&*result, &g), "dom_tree_to_guid: type=\"new\"");
        xmlFreeNode (node);
    }
    {
        /* missing type attribute entirely */
        xmlNodePtr node = build_leaf ("id", buf);
        auto result = dom_tree_to_guid (node);
        do_test (!result.has_value (), "dom_tree_to_guid: missing type attribute is rejected");
        xmlFreeNode (node);
    }
    {
        /* unrecognized type attribute */
        xmlNodePtr node = build_leaf ("id", buf);
        xmlSetProp (node, BAD_CAST "type", BAD_CAST "bogus");
        auto result = dom_tree_to_guid (node);
        do_test (!result.has_value (), "dom_tree_to_guid: unrecognized type attribute is rejected");
        xmlFreeNode (node);
    }
    {
        /* malformed guid text (wrong length / non-hex) */
        xmlNodePtr node = build_leaf ("id", "not-a-valid-guid");
        xmlSetProp (node, BAD_CAST "type", BAD_CAST "guid");
        auto result = dom_tree_to_guid (node);
        do_test (!result.has_value (), "dom_tree_to_guid: malformed guid text is rejected");
        xmlFreeNode (node);
    }
}

static void
test_dom_tree_to_boolean_variants (void)
{
    struct { const char* text; gboolean expect_ok; gboolean expect_val; } cases[] = {
        { "true",  TRUE,  TRUE  },
        { "false", TRUE,  FALSE },
        { "TRUE",  TRUE,  TRUE  },
        { "FaLsE", TRUE,  FALSE },
        { "yes",   FALSE, FALSE },
        { "1",     FALSE, FALSE },
        { "",      FALSE, FALSE },
    };
    for (auto& c : cases)
    {
        xmlNodePtr node = build_leaf ("flag", c.text);
        gboolean val = FALSE;
        gboolean ok = dom_tree_to_boolean (node, &val);
        do_test_args (ok == c.expect_ok && (!ok || val == c.expect_val),
                      "dom_tree_to_boolean", __FILE__, __LINE__,
                      "text=[%s] expected ok=%d val=%d, got ok=%d val=%d",
                      c.text, c.expect_ok, c.expect_val, ok, val);
        xmlFreeNode (node);
    }
}

static void
test_dom_tree_to_number_variants (void)
{
    {
        xmlNodePtr node = build_leaf ("n", "42");
        gint64 v = 0;
        do_test (dom_tree_to_integer (node, &v) && v == 42, "dom_tree_to_integer: plain value");
        xmlFreeNode (node);
    }
    {
        xmlNodePtr node = build_leaf ("n", "-42");
        gint64 v = 0;
        do_test (dom_tree_to_integer (node, &v) && v == -42, "dom_tree_to_integer: negative value");
        xmlFreeNode (node);
    }
    {
        xmlNodePtr node = build_leaf ("n", "  42  ");
        gint64 v = 0;
        do_test (dom_tree_to_integer (node, &v) && v == 42, "dom_tree_to_integer: whitespace-padded value");
        xmlFreeNode (node);
    }
    {
        xmlNodePtr node = build_leaf ("n", "not-a-number");
        gint64 v = 0;
        do_test (!dom_tree_to_integer (node, &v), "dom_tree_to_integer: garbage text is rejected");
        xmlFreeNode (node);
    }
    {
        xmlNodePtr node = build_leaf ("n", "-1");
        guint v = 0;
        do_test (!dom_tree_to_guint (node, &v), "dom_tree_to_guint: negative value is rejected");
        xmlFreeNode (node);
    }
    {
        xmlNodePtr node = build_leaf ("n", "70000");
        guint16 v = 0;
        do_test (!dom_tree_to_guint16 (node, &v), "dom_tree_to_guint16: overflow (>65535) is rejected");
        xmlFreeNode (node);
    }
    {
        xmlNodePtr node = build_leaf ("n", "123/100");
        gnc_numeric v = gnc_numeric_zero ();
        v = dom_tree_to_gnc_numeric (node);
        do_test (!gnc_numeric_check (v) && gnc_numeric_equal (v, gnc_numeric_create (123, 100)),
                 "dom_tree_to_gnc_numeric: valid ratio");
        xmlFreeNode (node);
    }
    {
        xmlNodePtr node = build_leaf ("n", "garbage");
        gnc_numeric v = dom_tree_to_gnc_numeric (node);
        do_test (gnc_numeric_equal (v, gnc_numeric_zero ()),
                 "dom_tree_to_gnc_numeric: garbage text falls back to zero");
        xmlFreeNode (node);
    }
}

static void
test_dom_tree_to_time64_variants (void)
{
    {
        xmlNodePtr node = xmlNewNode (NULL, BAD_CAST "date-posted");
        xmlNodePtr ts = xmlNewChild (node, NULL, BAD_CAST "ts:date", NULL);
        xmlNodeAddContentLen (ts, BAD_CAST "2020-01-01 00:00:00 +0000", 25);
        time64 t = dom_tree_to_time64 (node);
        do_test (t != INT64_MAX, "dom_tree_to_time64: single ts:date parses");
        xmlFreeNode (node);
    }
    {
        /* missing ts:date entirely */
        xmlNodePtr node = xmlNewNode (NULL, BAD_CAST "date-posted");
        time64 t = dom_tree_to_time64 (node);
        do_test (t == INT64_MAX, "dom_tree_to_time64: missing ts:date returns INT64_MAX");
        do_test (!dom_tree_valid_time64 (t, BAD_CAST "date-posted"), "dom_tree_valid_time64 rejects INT64_MAX");
        xmlFreeNode (node);
    }
    {
        /* duplicate ts:date is explicitly rejected by dom_tree_to_time64 */
        xmlNodePtr node = xmlNewNode (NULL, BAD_CAST "date-posted");
        xmlNodePtr ts1 = xmlNewChild (node, NULL, BAD_CAST "ts:date", NULL);
        xmlNodeAddContentLen (ts1, BAD_CAST "2020-01-01 00:00:00 +0000", 25);
        xmlNodePtr ts2 = xmlNewChild (node, NULL, BAD_CAST "ts:date", NULL);
        xmlNodeAddContentLen (ts2, BAD_CAST "2021-01-01 00:00:00 +0000", 25);
        time64 t = dom_tree_to_time64 (node);
        do_test (t == INT64_MAX, "dom_tree_to_time64: duplicate ts:date is rejected");
        xmlFreeNode (node);
    }
}

static void
test_dom_tree_to_gdate_variants (void)
{
    {
        xmlNodePtr node = xmlNewNode (NULL, BAD_CAST "start");
        xmlNodePtr gd = xmlNewChild (node, NULL, BAD_CAST "gdate", NULL);
        xmlNodeAddContentLen (gd, BAD_CAST "2020-04-03", 10);
        GDate* d = dom_tree_to_gdate (node);
        do_test (d && g_date_valid (*&d) && g_date_get_year (d) == 2020, "dom_tree_to_gdate: valid date");
        if (d) g_date_free (d);
        xmlFreeNode (node);
    }
    {
        xmlNodePtr node = xmlNewNode (NULL, BAD_CAST "start");
        xmlNodePtr gd = xmlNewChild (node, NULL, BAD_CAST "gdate", NULL);
        xmlNodeAddContentLen (gd, BAD_CAST "not-a-date", 10);
        GDate* d = dom_tree_to_gdate (node);
        do_test (d == NULL, "dom_tree_to_gdate: malformed date text is rejected");
        xmlFreeNode (node);
    }
    {
        xmlNodePtr node = xmlNewNode (NULL, BAD_CAST "start");
        GDate* d = dom_tree_to_gdate (node);
        do_test (d == NULL, "dom_tree_to_gdate: missing gdate child is rejected");
        xmlFreeNode (node);
    }
}

/***********************************************************************/
/* 4. KVP frame round-trips: nested frames, lists, every value type     */
/***********************************************************************/

static xmlNodePtr
kvp_slot (const char* key, const char* type, const char* text_value)
{
    xmlNodePtr slot = xmlNewNode (NULL, BAD_CAST "slot");
    xmlNodePtr k = xmlNewChild (slot, NULL, BAD_CAST "slot:key", NULL);
    xmlNodeAddContentLen (k, BAD_CAST key, strlen (key));
    xmlNodePtr v = xmlNewChild (slot, NULL, BAD_CAST "slot:value", NULL);
    xmlSetProp (v, BAD_CAST "type", BAD_CAST type);
    xmlNodeAddContentLen (v, BAD_CAST text_value, strlen (text_value));
    return slot;
}

static void
test_kvp_frame_scalar_types (void)
{
    struct { const char* type; const char* text; } cases[] = {
        { "integer", "42" },
        { "double",  "3.5" },
        { "string",  "hello world" },
    };
    for (auto& c : cases)
    {
        xmlNodePtr frame_node = xmlNewNode (NULL, BAD_CAST "slots");
        xmlNodePtr slot = kvp_slot ("k", c.type, c.text);
        xmlAddChild (frame_node, slot);

        KvpFrame* frame = dom_tree_to_kvp_frame (frame_node);
        do_test_args (frame != nullptr, "dom_tree_to_kvp_frame scalar", __FILE__, __LINE__,
                      "type=%s", c.type);
        if (frame)
        {
            auto val = frame->get_slot ({"k"});
            do_test_args (val != nullptr, "dom_tree_to_kvp_frame scalar has value",
                          __FILE__, __LINE__, "type=%s", c.type);
            delete frame;
        }
        xmlFreeNode (frame_node);
    }
}

static void
test_kvp_frame_nested (void)
{
    /* <slots><slot><slot:key>outer</slot:key><slot:value type="frame">
         <slot><slot:key>inner</slot:key><slot:value type="integer">7</slot:value></slot>
       </slot:value></slot></slots> */
    xmlNodePtr frame_node = xmlNewNode (NULL, BAD_CAST "slots");
    xmlNodePtr outer_slot = xmlNewNode (NULL, BAD_CAST "slot");
    xmlAddChild (frame_node, outer_slot);

    xmlNodePtr outer_key = xmlNewChild (outer_slot, NULL, BAD_CAST "slot:key", NULL);
    xmlNodeAddContentLen (outer_key, BAD_CAST "outer", 5);

    xmlNodePtr outer_value = xmlNewChild (outer_slot, NULL, BAD_CAST "slot:value", NULL);
    xmlSetProp (outer_value, BAD_CAST "type", BAD_CAST "frame");

    xmlNodePtr inner_slot = kvp_slot ("inner", "integer", "7");
    xmlAddChild (outer_value, inner_slot);

    KvpFrame* frame = dom_tree_to_kvp_frame (frame_node);
    do_test (frame != nullptr, "dom_tree_to_kvp_frame: nested frame parses");
    if (frame)
    {
        auto nested = frame->get_slot ({"outer", "inner"});
        do_test (nested != nullptr, "dom_tree_to_kvp_frame: nested value is reachable by path");
        delete frame;
    }
    xmlFreeNode (frame_node);
}

/***********************************************************************/
/* 5. Comments, CDATA, and mixed content through the real sixtp parser  */
/***********************************************************************/

static void
test_comment_is_invisible (void)
{
    xmlNodePtr tree = parse_one_element ("test",
        "<test>before<!-- a comment in the middle -->after</test>");
    do_test (tree != nullptr, "comment: well-formed document with a comment parses");
    if (tree)
    {
        auto text = dom_tree_to_text (tree);
        do_test_args (text.has_value () && *text == "beforeafter",
                      "comment content is invisible to the reader", __FILE__, __LINE__,
                      "got [%s]", text ? text->c_str () : "(null)");
        xmlFreeNode (tree);
    }
}

static void
test_cdata_becomes_plain_text (void)
{
    xmlNodePtr tree = parse_one_element ("test",
        "<test><![CDATA[<not-a-tag> & raw text]]></test>");
    do_test (tree != nullptr, "CDATA: well-formed document parses");
    if (tree)
    {
        auto text = dom_tree_to_text (tree);
        do_test_args (text.has_value () && *text == "<not-a-tag> & raw text",
                      "CDATA content comes through verbatim as plain text", __FILE__, __LINE__,
                      "got [%s]", text ? text->c_str () : "(null)");
        xmlFreeNode (tree);
    }
}

static void
test_mixed_content_direct_siblings (void)
{
    xmlNodePtr tree = parse_one_element ("test", "<test>100<b/>200</test>");
    do_test (tree != nullptr, "mixed content: well-formed document parses");
    if (tree)
    {
        xmlChar* text = xmlNodeListGetString (NULL, tree->xmlChildrenNode, TRUE);
        do_test_args (g_strcmp0 ((char*)text, "100200") == 0,
                      "mixed content: direct-sibling text around a nested element is captured",
                      __FILE__, __LINE__, "got [%s]", (char*)text);
        xmlFree (text);
        xmlFreeNode (tree);
    }
}

/***********************************************************************/
/* 6. Well-formedness failures: caught by libxml2 before any xmlNode   */
/*    is even built, unaffected by which tree type the reader uses.    */
/***********************************************************************/

static void
expect_parse_failure (const char* label, const char* xml)
{
    xmlNodePtr tree = parse_one_element ("test", xml);
    do_test_args (tree == nullptr, label, __FILE__, __LINE__, "expected rejection, got a tree");
    if (tree)
        xmlFreeNode (tree);
}

static void
test_well_formedness_failures (void)
{
    expect_parse_failure ("mismatched closing tag is rejected",
        "<test><a>x</mismatched></test>");
    expect_parse_failure ("truncated/unclosed document is rejected",
        "<test><a>x");
    expect_parse_failure ("unescaped bare ampersand is rejected",
        "<test>foo & bar</test>");
    expect_parse_failure ("empty document is rejected", "");
    expect_parse_failure ("non-XML content is rejected", "this is not xml at all\n");

    {
        /* Illegal raw control byte (0x01) inside element content: not a
           legal XML 1.0 character even when the bytes are otherwise
           valid UTF-8. */
        char raw[] = "<test>foo\x01""bar</test>";
        xmlNodePtr tree = parse_one_element ("test", std::string (raw, sizeof (raw) - 1));
        do_test (tree == nullptr, "illegal control byte in content is rejected");
        if (tree)
            xmlFreeNode (tree);
    }
}

/***********************************************************************/
/* 7. Domain-level validation: unknown tags, missing required fields,   */
/*    exercised through the real gnc:account parser nested under a     */
/*    wrapping tag (not used bare as the top-level sixtp).              */
/***********************************************************************/

static void
test_account_domain_validation (void)
{
    QofBook* book = qof_book_new ();

    {
        wrapped_result res = parse_wrapped (
            gnc_account_sixtp_parser_create, "gnc:account",
            "<gnc-wrapper><gnc:account version=\"2.0.0\">"
            "<act:name>Checking</act:name>"
            "<act:id type=\"guid\">aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa</act:id>"
            "<act:type>ASSET</act:type>"
            "</gnc:account></gnc-wrapper>",
            book);
        do_test (res.ok && res.callback_count == 1 && res.object_created,
                 "a complete, valid gnc:account is accepted and creates an object");
    }
    {
        wrapped_result res = parse_wrapped (
            gnc_account_sixtp_parser_create, "gnc:account",
            "<gnc-wrapper><gnc:account version=\"2.0.0\">"
            "<act:name>Checking</act:name>"
            "<act:id type=\"guid\">bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb</act:id>"
            "<act:type>ASSET</act:type>"
            "<act:bogus>unrecognized child element</act:bogus>"
            "</gnc:account></gnc-wrapper>",
            book);
        do_test (!res.ok && !res.object_created,
                 "an account with an unrecognized child tag is rejected, not silently accepted");
    }
    {
        wrapped_result res = parse_wrapped (
            gnc_account_sixtp_parser_create, "gnc:account",
            "<gnc-wrapper><gnc:account version=\"2.0.0\">"
            "<act:name>Checking</act:name>"
            "<act:id type=\"guid\">cccccccccccccccccccccccccccccccc</act:id>"
            /* act:type deliberately omitted - it is a required field */
            "</gnc:account></gnc-wrapper>",
            book);
        do_test (!res.ok && !res.object_created,
                 "an account missing a required field (act:type) is rejected");
    }

    qof_book_destroy (book);
}

/***********************************************************************/
/* 8. Full end-to-end session load: only for inputs that should SUCCEED */
/*    (see the file-level comment for why failures use the harness      */
/*    above instead of a full QofSession/QofBook cycle).                */
/***********************************************************************/

static std::string
wrap_minimal_book (const std::string& account_xml, int account_count, int call_num)
{
    char root_guid[33], child_guid[33], book_guid[33];
    snprintf (root_guid, sizeof (root_guid), "%032x", call_num * 3 + 1);
    snprintf (child_guid, sizeof (child_guid), "%032x", call_num * 3 + 2);
    snprintf (book_guid, sizeof (book_guid), "%032x", call_num * 3 + 3);

    std::string xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
        "<gnc-v2\n"
        "  xmlns:gnc=\"http://www.gnucash.org/XML/gnc\"\n"
        "  xmlns:book=\"http://www.gnucash.org/XML/book\"\n"
        "  xmlns:act=\"http://www.gnucash.org/XML/act\"\n"
        "  xmlns:cd=\"http://www.gnucash.org/XML/cd\">\n"
        "<gnc:count-data cd:type=\"book\">1</gnc:count-data>\n"
        "<gnc:book version=\"2.0.0\">\n"
        "<book:id type=\"guid\">" + std::string (book_guid) + "</book:id>\n"
        "<gnc:count-data cd:type=\"commodity\">0</gnc:count-data>\n"
        "<gnc:count-data cd:type=\"account\">" + std::to_string (account_count) + "</gnc:count-data>\n"
        "<gnc:count-data cd:type=\"transaction\">0</gnc:count-data>\n"
        "<gnc:account version=\"2.0.0\">\n"
        "<act:name>ROOT</act:name>\n"
        "<act:id type=\"guid\">" + std::string (root_guid) + "</act:id>\n"
        "<act:type>ROOT</act:type>\n"
        "</gnc:account>\n";

    std::string subst_account = account_xml;
    size_t pos;
    while ((pos = subst_account.find ("CHILDGUID")) != std::string::npos)
        subst_account.replace (pos, 9, child_guid);
    while ((pos = subst_account.find ("ROOTGUID")) != std::string::npos)
        subst_account.replace (pos, 8, root_guid);

    xml += subst_account;
    xml += "</gnc:book>\n</gnc-v2>\n";
    return xml;
}

static std::string
make_valid_account (const std::string& name_elem)
{
    return
        "<gnc:account version=\"2.0.0\">\n"
        + name_elem +
        "<act:id type=\"guid\">CHILDGUID</act:id>\n"
        "<act:type>ASSET</act:type>\n"
        "<act:commodity><cmdty:space>ISO4217</cmdty:space><cmdty:id>USD</cmdty:id></act:commodity>\n"
        "<act:commodity-scu>100</act:commodity-scu>\n"
        "<act:parent type=\"guid\">ROOTGUID</act:parent>\n"
        "</gnc:account>\n";
}

static int g_session_call_num = 0;

static void
load_and_check_account_name (const char* label, const std::string& name_elem,
                             const char* expect_name)
{
    g_session_call_num++;
    std::string xml = wrap_minimal_book (make_valid_account (name_elem), 2, g_session_call_num);

    char filename[] = "/tmp/gnc_xml_test_XXXXXX.gnucash";
    int fd = g_mkstemp (filename);
    write (fd, xml.data (), xml.size ());
    close (fd);

    QofBook* book = qof_book_new ();
    QofSession* session = qof_session_new (book);
    char* url = gnc_uri_normalize_uri (filename, FALSE);
    qof_session_begin (session, url, SESSION_READ_ONLY);
    g_free (url);
    qof_session_load (session, NULL);

    do_test_args (qof_session_get_error (session) == ERR_BACKEND_NO_ERR,
                  label, __FILE__, __LINE__, "session load failed for a well-formed file");

    Account* root = gnc_book_get_root_account (book);
    GList* children = gnc_account_get_children (root);
    gboolean found = FALSE;
    for (GList* n = children; n; n = n->next)
    {
        Account* a = (Account*) n->data;
        if (g_strcmp0 (xaccAccountGetName (a), expect_name) == 0)
            found = TRUE;
    }
    g_list_free (children);
    do_test_args (found, label, __FILE__, __LINE__,
                  "expected account named [%s] not found after full session load", expect_name);

    qof_session_end (session);
    /* Deliberately not calling qof_book_destroy here for a book that
       went through a full session load with business objects
       registered - see the file-level comment. */
    g_unlink (filename);
}

static void
test_full_session_load_edge_cases (void)
{
    load_and_check_account_name ("session load: plain ASCII account name",
                                 "<act:name>Checking</act:name>\n", "Checking");

    load_and_check_account_name ("session load: Unicode account name round-trips",
                                 "<act:name>Caf\xC3\xA9 \xE4\xB8\xAD\xE6\x96\x87 \xF0\x9F\x98\x80</act:name>\n",
                                 "Caf\xC3\xA9 \xE4\xB8\xAD\xE6\x96\x87 \xF0\x9F\x98\x80");

    load_and_check_account_name ("session load: CDATA account name",
                                 "<act:name><![CDATA[Checking]]></act:name>\n", "Checking");

    load_and_check_account_name ("session load: comment before account name",
                                 "<!-- a comment --><act:name>Checking</act:name>\n", "Checking");
}

/***********************************************************************/

int
main (int argc, char** argv)
{
    g_setenv ("GNC_UNINSTALLED", "1", TRUE);
    qof_init ();
    cashobjects_register ();
    do_test (qof_load_backend_library (GNC_LIB_REL_PATH, GNC_LIB_NAME),
             "loading gnc-backend-xml GModule failed");

    test_utf8_round_trip ();
    test_utf8_chunked_across_codepoint_boundary ();
    test_attribute_set_get ();
    test_node_list_get_string_sibling_only ();
    test_node_list_get_string_does_not_recurse ();
    test_node_free_null_is_safe ();
    test_empty_element_has_no_children ();

    test_from_libxml_preserves_structure ();

    test_dom_tree_to_guid_variants ();
    test_dom_tree_to_boolean_variants ();
    test_dom_tree_to_number_variants ();
    test_dom_tree_to_time64_variants ();
    test_dom_tree_to_gdate_variants ();

    test_kvp_frame_scalar_types ();
    test_kvp_frame_nested ();

    test_comment_is_invisible ();
    test_cdata_becomes_plain_text ();
    test_mixed_content_direct_siblings ();

    test_well_formedness_failures ();
    test_account_domain_validation ();

    test_full_session_load_edge_cases ();

    print_test_results ();
    qof_close ();
    exit (get_rv ());
}
