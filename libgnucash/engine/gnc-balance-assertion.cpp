/********************************************************************\
 * gnc-balance-assertion.cpp -- Balance assertion implementation.    *
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

#include <config.h>
#include <qof.h>
#include <qofinstance-p.h>

#include "Account.h"
#include "Transaction.h"
#include "gnc-balance-assertion.h"
#include "gnc-engine.h"
#include "gnc-features.h"

static QofLogModule log_module = GNC_MOD_ENGINE;

enum
{
    PROP_0,
    PROP_ACCOUNT,               /* Table */
    PROP_DATE,                  /* Table */
    PROP_AMOUNT,                /* Table */
    PROP_NOTES,                 /* Table */
};

struct balance_assertion_s
{
    QofInstance inst;
};

typedef struct GncBalanceAssertionPrivate
{
    /* The account is held by guid rather than by pointer: an assertion
     * outlives the deletion of its account, and a stale pointer would
     * be a use-after-free waiting to happen. */
    GncGUID acct_guid;

    time64 date;
    gnc_numeric amount;
    const char *notes;

    /* Memoised evaluation. Walking an account's splits on every
     * account-tree redraw would be wasteful, so the actual balance is
     * cached and thrown away whenever anything in the book that could
     * affect a balance changes. See bump_generation(). */
    guint64 cache_generation;
    gnc_numeric cached_actual;
} GncBalanceAssertionPrivate;

#define GET_PRIVATE(o) \
    ((GncBalanceAssertionPrivate*) \
     gnc_balance_assertion_get_instance_private((GncBalanceAssertion*)o))

struct _GncBalanceAssertionClass
{
    QofInstanceClass parent_class;
};

G_DEFINE_TYPE_WITH_PRIVATE(GncBalanceAssertion, gnc_balance_assertion,
                           QOF_TYPE_INSTANCE)

/* ================================================================ */
/* Cache invalidation.
 *
 * Generation 0 is reserved to mean "never evaluated", so the counter
 * starts at 1 and only ever grows. */

static guint64 balance_generation = 1;

static void
bump_generation (void)
{
    ++balance_generation;
}

static void
gnc_balance_assertion_destroy_for_account (const Account *acc);

static void
balance_assertion_event_handler (QofInstance *entity, QofEventId event_type,
                                 gpointer user_data, gpointer event_data)
{
    if (!entity)
        return;
    if (GNC_IS_TRANSACTION (entity) || GNC_IS_SPLIT (entity))
    {
        if (event_type & (QOF_EVENT_CREATE | QOF_EVENT_MODIFY | QOF_EVENT_DESTROY))
            bump_generation ();
    }
    else if (GNC_IS_ACCOUNT (entity))
    {
        if (event_type & QOF_EVENT_DESTROY)
        {
            bump_generation ();
            gnc_balance_assertion_destroy_for_account (GNC_ACCOUNT (entity));
        }
        else if (event_type & QOF_EVENT_MODIFY)
            bump_generation ();
    }
}

/* ================================================================ */

static void
gnc_balance_assertion_init (GncBalanceAssertion *ba)
{
    GncBalanceAssertionPrivate *priv = GET_PRIVATE (ba);

    priv->acct_guid = *guid_null ();
    priv->date = gnc_time64_get_day_neutral (gnc_time (nullptr));
    priv->amount = gnc_numeric_zero ();
    priv->notes = CACHE_INSERT ("");
    priv->cache_generation = 0;
    priv->cached_actual = gnc_numeric_zero ();
}

static void
gnc_balance_assertion_dispose (GObject *bap)
{
    G_OBJECT_CLASS(gnc_balance_assertion_parent_class)->dispose (bap);
}

static void
gnc_balance_assertion_finalize (GObject *bap)
{
    G_OBJECT_CLASS(gnc_balance_assertion_parent_class)->finalize (bap);
}

static void
gnc_balance_assertion_get_property (GObject *object, guint prop_id,
                                    GValue *value, GParamSpec *pspec)
{
    GncBalanceAssertion *ba;
    GncBalanceAssertionPrivate *priv;
    Time64 time;

    g_return_if_fail (GNC_IS_BALANCE_ASSERTION (object));

    ba = GNC_BALANCE_ASSERTION (object);
    priv = GET_PRIVATE (ba);
    switch (prop_id)
    {
    case PROP_ACCOUNT:
        g_value_take_object (value, gnc_balance_assertion_get_account (ba));
        break;
    case PROP_DATE:
        time.t = priv->date;
        g_value_set_boxed (value, &time);
        break;
    case PROP_AMOUNT:
        g_value_set_boxed (value, &priv->amount);
        break;
    case PROP_NOTES:
        g_value_set_string (value, priv->notes);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gnc_balance_assertion_set_property (GObject *object, guint prop_id,
                                    const GValue *value, GParamSpec *pspec)
{
    GncBalanceAssertion *ba;
    Time64 *t;

    g_return_if_fail (GNC_IS_BALANCE_ASSERTION (object));

    ba = GNC_BALANCE_ASSERTION (object);
    g_assert (qof_instance_get_editlevel (ba));

    switch (prop_id)
    {
    case PROP_ACCOUNT:
        gnc_balance_assertion_set_account
            (ba, GNC_ACCOUNT (g_value_get_object (value)));
        break;
    case PROP_DATE:
        t = (Time64*) g_value_get_boxed (value);
        gnc_balance_assertion_set_date (ba, t->t);
        break;
    case PROP_AMOUNT:
        gnc_balance_assertion_set_amount
            (ba, *(gnc_numeric*) g_value_get_boxed (value));
        break;
    case PROP_NOTES:
        gnc_balance_assertion_set_notes (ba, g_value_get_string (value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gnc_balance_assertion_class_init (GncBalanceAssertionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->dispose = gnc_balance_assertion_dispose;
    gobject_class->finalize = gnc_balance_assertion_finalize;
    gobject_class->get_property = gnc_balance_assertion_get_property;
    gobject_class->set_property = gnc_balance_assertion_set_property;

    g_object_class_install_property
        (gobject_class,
         PROP_ACCOUNT,
         g_param_spec_object ("account",
                              "Account",
                              "The account whose balance is asserted.",
                              GNC_TYPE_ACCOUNT,
                              G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_DATE,
         g_param_spec_boxed ("date",
                             "Assertion Date",
                             "The date on whose close the balance is asserted.",
                             GNC_TYPE_TIME64,
                             G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_AMOUNT,
         g_param_spec_boxed ("amount",
                             "Asserted Amount",
                             "The balance the account is asserted to have, in "
                             "the account's commodity.",
                             GNC_TYPE_NUMERIC,
                             G_PARAM_READWRITE));

    g_object_class_install_property
        (gobject_class,
         PROP_NOTES,
         g_param_spec_string ("notes",
                              "Notes",
                              "Free-form note, e.g. the statement the figure "
                              "was taken from.",
                              nullptr,
                              G_PARAM_READWRITE));
}

/* ================================================================ */

static void
commit_err (QofInstance *inst, QofBackendError errcode)
{
    PERR ("Failed to commit: %d", errcode);
    gnc_engine_signal_commit_error (errcode);
}

static void
gnc_balance_assertion_free (QofInstance *inst)
{
    GncBalanceAssertion *ba;
    GncBalanceAssertionPrivate *priv;

    if (inst == nullptr)
        return;
    g_return_if_fail (GNC_IS_BALANCE_ASSERTION (inst));

    ba = GNC_BALANCE_ASSERTION (inst);
    priv = GET_PRIVATE (ba);

    qof_event_gen (&ba->inst, QOF_EVENT_DESTROY, nullptr);

    CACHE_REMOVE (priv->notes);

    g_object_unref (ba);
}

static void noop (QofInstance *inst) {}

void
gnc_balance_assertion_begin_edit (GncBalanceAssertion *ba)
{
    qof_begin_edit (QOF_INSTANCE (ba));
}

void
gnc_balance_assertion_commit_edit (GncBalanceAssertion *ba)
{
    if (!qof_commit_edit (QOF_INSTANCE (ba))) return;
    qof_commit_edit_part2 (QOF_INSTANCE (ba), commit_err, noop,
                           gnc_balance_assertion_free);
}

GncBalanceAssertion *
gnc_balance_assertion_new (QofBook *book)
{
    g_return_val_if_fail (book, nullptr);

    ENTER (" ");

    auto ba { static_cast<GncBalanceAssertion*>
              (g_object_new (GNC_TYPE_BALANCE_ASSERTION, nullptr)) };
    qof_instance_init_data (&ba->inst, GNC_ID_BALANCE_ASSERTION, book);

    /* Older GnuCash reads a book containing assertions happily, but
     * silently drops them on the next save. Flag the book so that it
     * says so instead. */
    gnc_features_set_used (book, GNC_FEATURE_BALANCE_ASSERTIONS);

    qof_event_gen (&ba->inst, QOF_EVENT_CREATE, nullptr);

    LEAVE (" ");
    return ba;
}

void
gnc_balance_assertion_destroy (GncBalanceAssertion *ba)
{
    g_return_if_fail (GNC_IS_BALANCE_ASSERTION (ba));
    gnc_balance_assertion_begin_edit (ba);
    qof_instance_set_dirty (&ba->inst);
    qof_instance_set_destroying (ba, TRUE);
    gnc_balance_assertion_commit_edit (ba);
}

const GncGUID *
gnc_balance_assertion_get_guid (const GncBalanceAssertion *ba)
{
    g_return_val_if_fail (GNC_IS_BALANCE_ASSERTION (ba), guid_null ());
    return qof_instance_get_guid (QOF_INSTANCE (ba));
}

/* ================================================================ */
/* Accessors */

Account *
gnc_balance_assertion_get_account (const GncBalanceAssertion *ba)
{
    g_return_val_if_fail (GNC_IS_BALANCE_ASSERTION (ba), nullptr);

    GncBalanceAssertionPrivate *priv = GET_PRIVATE (ba);
    if (guid_equal (&priv->acct_guid, guid_null ()))
        return nullptr;

    return xaccAccountLookup (&priv->acct_guid,
                              qof_instance_get_book (QOF_INSTANCE (ba)));
}

void
gnc_balance_assertion_set_account (GncBalanceAssertion *ba, Account *acc)
{
    g_return_if_fail (GNC_IS_BALANCE_ASSERTION (ba));

    GncBalanceAssertionPrivate *priv = GET_PRIVATE (ba);
    const GncGUID *guid = acc ? xaccAccountGetGUID (acc) : guid_null ();

    if (guid_equal (&priv->acct_guid, guid))
        return;

    gnc_balance_assertion_begin_edit (ba);
    priv->acct_guid = *guid;
    priv->cache_generation = 0;
    qof_instance_set_dirty (&ba->inst);
    gnc_balance_assertion_commit_edit (ba);

    qof_event_gen (&ba->inst, QOF_EVENT_MODIFY, nullptr);
}

time64
gnc_balance_assertion_get_date (const GncBalanceAssertion *ba)
{
    g_return_val_if_fail (GNC_IS_BALANCE_ASSERTION (ba), 0);
    return GET_PRIVATE (ba)->date;
}

void
gnc_balance_assertion_set_date (GncBalanceAssertion *ba, time64 date)
{
    g_return_if_fail (GNC_IS_BALANCE_ASSERTION (ba));

    GncBalanceAssertionPrivate *priv = GET_PRIVATE (ba);
    time64 neutral = gnc_time64_get_day_neutral (date);

    if (priv->date == neutral)
        return;

    gnc_balance_assertion_begin_edit (ba);
    priv->date = neutral;
    priv->cache_generation = 0;
    qof_instance_set_dirty (&ba->inst);
    gnc_balance_assertion_commit_edit (ba);

    qof_event_gen (&ba->inst, QOF_EVENT_MODIFY, nullptr);
}

gnc_numeric
gnc_balance_assertion_get_amount (const GncBalanceAssertion *ba)
{
    g_return_val_if_fail (GNC_IS_BALANCE_ASSERTION (ba), gnc_numeric_zero ());
    return GET_PRIVATE (ba)->amount;
}

void
gnc_balance_assertion_set_amount (GncBalanceAssertion *ba, gnc_numeric amount)
{
    g_return_if_fail (GNC_IS_BALANCE_ASSERTION (ba));
    g_return_if_fail (!gnc_numeric_check (amount));

    GncBalanceAssertionPrivate *priv = GET_PRIVATE (ba);
    if (gnc_numeric_equal (priv->amount, amount))
        return;

    gnc_balance_assertion_begin_edit (ba);
    priv->amount = amount;
    qof_instance_set_dirty (&ba->inst);
    gnc_balance_assertion_commit_edit (ba);

    qof_event_gen (&ba->inst, QOF_EVENT_MODIFY, nullptr);
}

const char *
gnc_balance_assertion_get_notes (const GncBalanceAssertion *ba)
{
    g_return_val_if_fail (GNC_IS_BALANCE_ASSERTION (ba), nullptr);
    return GET_PRIVATE (ba)->notes;
}

void
gnc_balance_assertion_set_notes (GncBalanceAssertion *ba, const char *notes)
{
    g_return_if_fail (GNC_IS_BALANCE_ASSERTION (ba));

    GncBalanceAssertionPrivate *priv = GET_PRIVATE (ba);
    if (!notes)
        notes = "";
    if (!g_strcmp0 (priv->notes, notes))
        return;

    gnc_balance_assertion_begin_edit (ba);
    CACHE_REPLACE (priv->notes, notes);
    qof_instance_set_dirty (&ba->inst);
    gnc_balance_assertion_commit_edit (ba);

    qof_event_gen (&ba->inst, QOF_EVENT_MODIFY, nullptr);
}

/* ================================================================ */
/* Evaluation */

gnc_numeric
gnc_balance_assertion_get_actual (const GncBalanceAssertion *ba)
{
    g_return_val_if_fail (GNC_IS_BALANCE_ASSERTION (ba), gnc_numeric_zero ());

    GncBalanceAssertionPrivate *priv = GET_PRIVATE (ba);
    Account *acc = gnc_balance_assertion_get_account (ba);

    if (!acc)
        return gnc_numeric_zero ();

    if (priv->cache_generation != balance_generation)
    {
        priv->cached_actual =
            xaccAccountGetBalanceAsOfDate (acc, gnc_time64_get_day_end (priv->date));
        priv->cache_generation = balance_generation;
    }

    return priv->cached_actual;
}

gnc_numeric
gnc_balance_assertion_get_delta (const GncBalanceAssertion *ba)
{
    g_return_val_if_fail (GNC_IS_BALANCE_ASSERTION (ba), gnc_numeric_zero ());

    Account *acc = gnc_balance_assertion_get_account (ba);
    if (!acc)
        return gnc_numeric_zero ();

    /* Round to the account's commodity so that a difference smaller
     * than the smallest representable unit -- the residue of an
     * imported rate conversion, say -- doesn't read as a failure. An
     * account with no commodity yet has nothing to round to, so compare
     * the values as they stand. */
    int denom = xaccAccountGetCommoditySCU (acc);
    if (denom <= 0)
        return gnc_numeric_sub (gnc_balance_assertion_get_actual (ba),
                                gnc_balance_assertion_get_amount (ba),
                                GNC_DENOM_AUTO, GNC_HOW_DENOM_LCD);

    return gnc_numeric_sub (gnc_balance_assertion_get_actual (ba),
                            gnc_balance_assertion_get_amount (ba),
                            denom, GNC_HOW_RND_ROUND_HALF_UP);
}

GncBalanceAssertionStatus
gnc_balance_assertion_get_status (const GncBalanceAssertion *ba)
{
    g_return_val_if_fail (GNC_IS_BALANCE_ASSERTION (ba),
                          GNC_BALANCE_ASSERTION_UNKNOWN);

    if (!gnc_balance_assertion_get_account (ba))
        return GNC_BALANCE_ASSERTION_UNKNOWN;

    return gnc_numeric_zero_p (gnc_balance_assertion_get_delta (ba))
        ? GNC_BALANCE_ASSERTION_PASS
        : GNC_BALANCE_ASSERTION_FAIL;
}

gboolean
gnc_balance_assertion_is_failing (const GncBalanceAssertion *ba)
{
    return gnc_balance_assertion_get_status (ba) == GNC_BALANCE_ASSERTION_FAIL;
}

/* ================================================================ */
/* Collections */

GncBalanceAssertion *
gnc_balance_assertion_lookup (const GncGUID *guid, const QofBook *book)
{
    g_return_val_if_fail (guid, nullptr);
    g_return_val_if_fail (book, nullptr);

    QofCollection *col = qof_book_get_collection (book, GNC_ID_BALANCE_ASSERTION);
    return GNC_BALANCE_ASSERTION (qof_collection_lookup_entity (col, guid));
}

static gint
compare_by_date (gconstpointer a, gconstpointer b)
{
    time64 da = gnc_balance_assertion_get_date (GNC_BALANCE_ASSERTION (a));
    time64 db = gnc_balance_assertion_get_date (GNC_BALANCE_ASSERTION (b));

    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

typedef struct
{
    GList *list;
    const GncGUID *acct_guid;   /* NULL: any account */
    gboolean failing_only;
} CollectData;

static void
collect_assertion_cb (QofInstance *inst, gpointer user_data)
{
    CollectData *data = (CollectData*) user_data;
    GncBalanceAssertion *ba = GNC_BALANCE_ASSERTION (inst);

    /* Match on the stored guid rather than on the resolved account:
     * this also has to work while the account is being torn down, when
     * it has already left the book's entity collection. */
    if (data->acct_guid &&
        !guid_equal (&GET_PRIVATE (ba)->acct_guid, data->acct_guid))
        return;

    if (data->failing_only && !gnc_balance_assertion_is_failing (ba))
        return;

    data->list = g_list_prepend (data->list, ba);
}

static GList *
collect_assertions (const QofBook *book, const Account *acc,
                    gboolean failing_only)
{
    if (!book)
        return nullptr;

    CollectData data { nullptr, acc ? xaccAccountGetGUID (acc) : nullptr,
                       failing_only };
    qof_collection_foreach (qof_book_get_collection (book, GNC_ID_BALANCE_ASSERTION),
                            collect_assertion_cb, &data);

    return g_list_sort (data.list, compare_by_date);
}

GList *
gnc_balance_assertion_get_all (const QofBook *book)
{
    return collect_assertions (book, nullptr, FALSE);
}

GList *
gnc_balance_assertion_get_for_account (const Account *acc)
{
    g_return_val_if_fail (GNC_IS_ACCOUNT (acc), nullptr);
    return collect_assertions (qof_instance_get_book (QOF_INSTANCE (acc)),
                               acc, FALSE);
}

GList *
gnc_balance_assertion_get_failing (const QofBook *book)
{
    return collect_assertions (book, nullptr, TRUE);
}

guint
gnc_balance_assertion_count_failing (const QofBook *book)
{
    GList *failing = gnc_balance_assertion_get_failing (book);
    guint count = g_list_length (failing);
    g_list_free (failing);
    return count;
}

guint
gnc_balance_assertion_count_failing_for_account (const Account *acc)
{
    g_return_val_if_fail (GNC_IS_ACCOUNT (acc), 0);

    GList *list = collect_assertions (qof_instance_get_book (QOF_INSTANCE (acc)),
                                      acc, TRUE);
    guint count = g_list_length (list);
    g_list_free (list);
    return count;
}

/* An account that is deleted takes its assertions with it: an
 * assertion about an account that no longer exists can never be
 * evaluated, and leaving it in the book would only produce an
 * unresolvable warning. Collect first, destroy afterwards -- the
 * destroy modifies the collection we would otherwise be iterating. */
static void
gnc_balance_assertion_destroy_for_account (const Account *acc)
{
    QofBook *book = qof_instance_get_book (QOF_INSTANCE (acc));
    if (!book || qof_book_shutting_down (book))
        return;

    GList *list = collect_assertions (book, acc, FALSE);
    for (GList *n = list; n; n = n->next)
        gnc_balance_assertion_destroy (GNC_BALANCE_ASSERTION (n->data));
    g_list_free (list);
}

/* ================================================================ */
/* QOF registration */

static void
destroy_assertion_on_book_close (QofInstance *ent, gpointer data)
{
    gnc_balance_assertion_destroy (GNC_BALANCE_ASSERTION (ent));
}

static void
gnc_balance_assertion_book_end (QofBook *book)
{
    QofCollection *col = qof_book_get_collection (book, GNC_ID_BALANCE_ASSERTION);
    qof_collection_foreach (col, destroy_assertion_on_book_close, nullptr);
}

static const char *
gnc_balance_assertion_printable (gpointer obj)
{
    static char buf[128];
    GncBalanceAssertion *ba = GNC_BALANCE_ASSERTION (obj);
    Account *acc = gnc_balance_assertion_get_account (ba);
    char datebuf[MAX_DATE_LENGTH + 1];

    qof_print_date_buff (datebuf, MAX_DATE_LENGTH,
                         gnc_balance_assertion_get_date (ba));
    g_snprintf (buf, sizeof (buf), "%s @ %s",
                acc ? xaccAccountGetName (acc) : "(no account)", datebuf);
    return buf;
}

#ifdef _MSC_VER
# define DI(x) /* */
#else
# define DI(x) x
#endif

static QofObject balance_assertion_object_def =
{
    DI(.interface_version = ) QOF_OBJECT_VERSION,
    DI(.e_type            = ) GNC_ID_BALANCE_ASSERTION,
    DI(.type_label        = ) "Balance Assertion",
    DI(.create            = ) (void*(*)(QofBook*)) gnc_balance_assertion_new,
    DI(.book_begin        = ) nullptr,
    DI(.book_end          = ) gnc_balance_assertion_book_end,
    DI(.is_dirty          = ) qof_collection_is_dirty,
    DI(.mark_clean        = ) qof_collection_mark_clean,
    DI(.foreach           = ) qof_collection_foreach,
    DI(.printable         = ) gnc_balance_assertion_printable,
    DI(.version_cmp       = ) (int (*)(gpointer, gpointer)) qof_instance_version_cmp,
};

gboolean
gnc_balance_assertion_register (void)
{
    static QofParam params[] =
    {
        {
            "account", GNC_ID_ACCOUNT,
            (QofAccessFunc) gnc_balance_assertion_get_account,
            (QofSetterFunc) gnc_balance_assertion_set_account
        },
        {
            "date", QOF_TYPE_DATE,
            (QofAccessFunc) gnc_balance_assertion_get_date,
            (QofSetterFunc) gnc_balance_assertion_set_date
        },
        {
            "amount", QOF_TYPE_NUMERIC,
            (QofAccessFunc) gnc_balance_assertion_get_amount,
            (QofSetterFunc) gnc_balance_assertion_set_amount
        },
        {
            "notes", QOF_TYPE_STRING,
            (QofAccessFunc) gnc_balance_assertion_get_notes,
            (QofSetterFunc) gnc_balance_assertion_set_notes
        },
        {
            QOF_PARAM_BOOK, QOF_ID_BOOK,
            (QofAccessFunc) qof_instance_get_book, nullptr
        },
        {
            QOF_PARAM_GUID, QOF_TYPE_GUID,
            (QofAccessFunc) qof_instance_get_guid, nullptr
        },
        { nullptr },
    };

    qof_class_register (GNC_ID_BALANCE_ASSERTION, (QofSortFunc) nullptr, params);

    static gboolean handler_registered = FALSE;
    if (!handler_registered)
    {
        qof_event_register_handler (balance_assertion_event_handler, nullptr);
        handler_registered = TRUE;
    }

    return qof_object_register (&balance_assertion_object_def);
}
