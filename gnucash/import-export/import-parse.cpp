/*
 * import-parse.c -- a generic "parser" API for importers..  Allows importers
 * 	to parse dates and numbers, and provides a UI to ask for users to
 * 	resolve ambiguities.
 *
 * Created by:	Derek Atkins <derek@ihtfp.com>
 * Copyright (c) 2003 Derek Atkins <warlord@MIT.EDU>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <string.h>

#include <cstring>
#include <string_view>
#include <ctre.hpp>

#include "gnc-engine.h"
#include "gnc-ui-util.h"

#include "import-parse.h"

static QofLogModule log_module = GNC_MOD_IMPORT;

/* numeric regular expressions */
static constexpr ctll::fixed_string decimal_radix_regex
    ("^ *\\$?[+\\-]?\\$?[0-9]+ *$|^ *\\$?[+\\-]?\\$?[0-9]?[0-9]?[0-9]?(,[0-9][0-9][0-9])*(\\.[0-9]*)? *$|^ *\\$?[+\\-]?\\$?[0-9]+\\.[0-9]* *$");
static constexpr ctll::fixed_string comma_radix_regex
    ("^ *\\$?[+\\-]?\\$?[0-9]+ *$|^ *\\$?[+\\-]?\\$?[0-9]?[0-9]?[0-9]?(\\.[0-9][0-9][0-9])*(,[0-9]*)? *$|^ *\\$?[+\\-]?\\$?[0-9]+,[0-9]* *$");

/* date regular expressions */
static constexpr ctll::fixed_string date_regex
    ("^ *([0-9]+) *[/.'\\-] *([0-9]+) *[/.'\\-] *([0-9]+).*$|^ *([0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]).*$");
static constexpr ctll::fixed_string date_mdy_regex
    ("([0-9][0-9])([0-9][0-9])([0-9][0-9][0-9][0-9])");
static constexpr ctll::fixed_string date_ymd_regex
    ("([0-9][0-9][0-9][0-9])([0-9][0-9])([0-9][0-9])");

/* Set and clear flags in bit-flags */
#define import_set_flag(i,f) (i = static_cast<GncImportFormat>(static_cast<int>(i) | static_cast<int>(f)))
#define import_clear_flag(i,f) (i = static_cast<GncImportFormat>(static_cast<int>(i) & static_cast<int>(~f)))

static gint
my_strntol(const char *str, int len)
{
    gint res = 0;

    g_return_val_if_fail(str, 0);
    g_return_val_if_fail(len, 0);

    while (len--)
    {

        if (*str < '0' || *str > '9')
        {
            str++;
            continue;
        }

        res *= 10;
        res += *(str++) - '0';
    }
    return res;
}

/*
 * based on a trio of date-component substrings (day/month/year in
 * whatever order the caller's regex captured them), and a list
 * of possible date formats, return the list of formats that this string
 * could actually be.
 */
static GncImportFormat
check_date_format(std::string_view sv0, std::string_view sv1,
                   std::string_view sv2, GncImportFormat fmts)
{
    GncImportFormat res = GNCIF_NONE;
    int len0 = 0, len1 = 0, len2 = 0;
    int val0 = 0, val1 = 0, val2 = 0;

    g_return_val_if_fail(fmts, res);

    /* Compute the lengths */
    len0 = static_cast<int>(sv0.size());
    len1 = static_cast<int>(sv1.size());
    len2 = static_cast<int>(sv2.size());

    /* compute the numeric values */
    val0 = my_strntol(sv0.data(), len0);
    val1 = my_strntol(sv1.data(), len1);
    val2 = my_strntol(sv2.data(), len2);

    /* Filter out the possibilities.  Hopefully only one will remain */

    if (val0 > 12) import_clear_flag(fmts, GNCIF_DATE_MDY);
    if (val0 > 31) import_clear_flag(fmts, GNCIF_DATE_DMY);
    if (val0 < 1)
    {
        import_clear_flag(fmts, GNCIF_DATE_DMY);
        import_clear_flag(fmts, GNCIF_DATE_MDY);
    }

    if (val1 > 12)
    {
        import_clear_flag(fmts, GNCIF_DATE_DMY);
        import_clear_flag(fmts, GNCIF_DATE_YMD);
    }
    if (val1 > 31)
    {
        import_clear_flag(fmts, GNCIF_DATE_MDY);
        import_clear_flag(fmts, GNCIF_DATE_YDM);
    }

    if (val2 > 12) import_clear_flag(fmts, GNCIF_DATE_YDM);
    if (val2 > 31) import_clear_flag(fmts, GNCIF_DATE_YMD);
    if (val2 < 1)
    {
        import_clear_flag(fmts, GNCIF_DATE_YMD);
        import_clear_flag(fmts, GNCIF_DATE_YDM);
    }

    /* if we've got a 4-character year, make sure the value is greater
     * than 1930 and less than 2100.  XXX: be sure to fix this by 2100!
     */
    if (len0 == 4 && (val0 < 1930 || val0 > 2100))
    {
        import_clear_flag(fmts, GNCIF_DATE_YMD);
        import_clear_flag(fmts, GNCIF_DATE_YDM);
    }
    if (len2 == 4 && (val2 < 1930 || val2 > 2100))
    {
        import_clear_flag(fmts, GNCIF_DATE_MDY);
        import_clear_flag(fmts, GNCIF_DATE_DMY);
    }

    /* If the first string has a length of only 1, then it is definitely
     * not a year (although it could be a month or day).
     */
    if (len0 == 1)
    {
        import_clear_flag(fmts, GNCIF_DATE_YMD);
        import_clear_flag(fmts, GNCIF_DATE_YDM);
    }

    return fmts;
}

GncImportFormat
gnc_import_test_numeric(const char* str, GncImportFormat fmts)
{
    GncImportFormat res = GNCIF_NONE;

    g_return_val_if_fail(str, fmts);

    if ((fmts & GNCIF_NUM_PERIOD) && ctre::match<decimal_radix_regex>(std::string_view(str)))
        import_set_flag (res, GNCIF_NUM_PERIOD);

    if ((fmts & GNCIF_NUM_COMMA) && ctre::match<comma_radix_regex>(std::string_view(str)))
        import_set_flag (res, GNCIF_NUM_COMMA);

    return res;
}


GncImportFormat
gnc_import_test_date(const char* str, GncImportFormat fmts)
{
    GncImportFormat res = GNCIF_NONE;

    g_return_val_if_fail(str, fmts);
    g_return_val_if_fail(strlen(str) > 1, fmts);

    if (auto m = ctre::match<date_regex>(std::string_view(str)))
    {
        if (auto g0 = m.get<1>())
            res = check_date_format(g0.to_view(), m.get<2>().to_view(),
                                     m.get<3>().to_view(), fmts);
        else
        {
            /* Hmm, it matches XXXXXXXX, but is this YYYYxxxx or xxxxYYYY?
             * let's try both ways and let the parser check that YYYY is
             * valid.
             */
            #define DATE_LEN 8
            char temp[DATE_LEN + 1];

            auto g4 = m.get<4>();
            g_return_val_if_fail(g4, fmts);
            g_return_val_if_fail(g4.size() == DATE_LEN, fmts);

            /* make a temp copy of the XXXXXXXX string */
            std::memcpy(temp, g4.data(), DATE_LEN);
            temp[DATE_LEN] = '\0';

            /* then check it against the ymd or mdy formats, as necessary */
            if (((fmts & GNCIF_DATE_YDM) || (fmts & GNCIF_DATE_YMD)))
                if (auto m2 = ctre::match<date_ymd_regex>(std::string_view(temp)))
                    import_set_flag (res, check_date_format (m2.get<1>().to_view(),
                                                              m2.get<2>().to_view(),
                                                              m2.get<3>().to_view(), fmts));

            if (((fmts & GNCIF_DATE_DMY) || (fmts & GNCIF_DATE_MDY)))
                if (auto m2 = ctre::match<date_mdy_regex>(std::string_view(temp)))
                    import_set_flag (res, check_date_format (m2.get<1>().to_view(),
                                                              m2.get<2>().to_view(),
                                                              m2.get<3>().to_view(), fmts));
        }
    }

    return res;
}

gboolean
gnc_import_parse_numeric(const char* str, GncImportFormat fmt, gnc_numeric *val)
{
    g_return_val_if_fail(str, FALSE);
    g_return_val_if_fail(val, FALSE);
    g_return_val_if_fail(fmt, FALSE);
    g_return_val_if_fail(!(fmt & (fmt - 1)), FALSE);

    switch (fmt)
    {
    case GNCIF_NUM_PERIOD:
        return xaccParseAmountExtended(str, TRUE, '-', '.', ',', "$+",
                                       val, NULL);
    case GNCIF_NUM_COMMA:
        return xaccParseAmountExtended(str, TRUE, '-', ',', '.', "$+",
                                       val, NULL);
    default:
        PERR("invalid format: %d", fmt);
        return FALSE;
    }
}

/* Handle y2k fixes, etc.
 * obtaining the year "00", "2000", and "19100" all mean the same thing.
 * output is an integer representing the year in the C.E.
 */
static int
fix_year(int y)
{
    /* two-digit numbers less than "70"  are interpreted to be post-2000. */
    if (y < 70)
        return (y + 2000);

    /* fix a common bug in printing post-2000 dates as 19100, etc. */
    if (y > 19000)
        return (1900 + (y - 19000));

    /* At this point we just want to make sure that this is a real date.
     * y _should_ be a 'unix year' (which is the number of years since
     * 1900), but it _COULD_ be a full date (1999, 2001, etc.).  At some
     * point in the future we can't tell the difference, but are we really
     * going to care if this code fails in 3802?
     */
    if (y < 1902)
        return (y + 1900);

    /* y is good as it is */
    return y;
}

gboolean
gnc_import_parse_date(const char *str, GncImportFormat fmt, time64 *val)
{
    char temp[9];
    std::string_view sv0, sv1, sv2;

    int v0 = 0, v1 = 0, v2 = 0;
    int m = 0, d = 0, y = 0;

    g_return_val_if_fail(str, FALSE);
    g_return_val_if_fail(val, FALSE);
    g_return_val_if_fail(fmt, FALSE);
    g_return_val_if_fail(!(fmt & (fmt - 1)), FALSE);

    if (auto dm = ctre::match<date_regex>(std::string_view(str)))
    {
        if (auto g0 = dm.get<1>())
        {
            sv0 = g0.to_view();
            sv1 = dm.get<2>().to_view();
            sv2 = dm.get<3>().to_view();
        }
        else
        {
            /* date is of the form XXXXXXX; save it to a temp string and
             * split it based on the format, either YYYYaabb or aabbYYYY
             */
            auto g4 = dm.get<4>();
            g_return_val_if_fail(g4, FALSE);
            g_return_val_if_fail(g4.size() == 8, FALSE);

            std::memcpy(temp, g4.data(), 8);
            temp[8] = '\0';

            switch (fmt)
            {
            case GNCIF_DATE_DMY:
            case GNCIF_DATE_MDY:
            {
                auto m2 = ctre::match<date_mdy_regex>(std::string_view(temp));
                g_return_val_if_fail(bool(m2), FALSE);
                sv0 = m2.get<1>().to_view();
                sv1 = m2.get<2>().to_view();
                sv2 = m2.get<3>().to_view();
                break;
            }
            case GNCIF_DATE_YMD:
            case GNCIF_DATE_YDM:
            {
                auto m2 = ctre::match<date_ymd_regex>(std::string_view(temp));
                g_return_val_if_fail(bool(m2), FALSE);
                sv0 = m2.get<1>().to_view();
                sv1 = m2.get<2>().to_view();
                sv2 = m2.get<3>().to_view();
                break;
            }
            default:
                PERR("Invalid date format provided: %d", fmt);
                return FALSE;
            }
        }

        /* sv0/sv1/sv2 hold the captured date-component substrings. */

        if (sv0.empty() || sv1.empty() || sv2.empty())
        {
            PERR("can't interpret date %s", str);
            return FALSE;
        }

        /* grab the numerics */
        v0 = my_strntol(sv0.data(), static_cast<int>(sv0.size()));
        v1 = my_strntol(sv1.data(), static_cast<int>(sv1.size()));
        v2 = my_strntol(sv2.data(), static_cast<int>(sv2.size()));

        switch (fmt)
        {
        case GNCIF_DATE_DMY:
            if (v0 > 0 && v0 <= 31 && v1 > 0 && v1 <= 12 && v2 > 0)
            {
                d = v0;
                m = v1;
                y = v2;
            }
            else
                PERR("format is d/m/y but date is %s", str);
            break;

        case GNCIF_DATE_MDY:
            if (v0 > 0 && v0 <= 12 && v1 > 0 && v1 <= 31 && v2 > 0)
            {
                m = v0;
                d = v1;
                y = v2;
            }
            else
                PERR("format is m/d/y but date is %s", str);
            break;

        case GNCIF_DATE_YMD:
            if (v0 > 0 && v1 > 0 && v1 <= 12 && v2 > 0 && v2 <= 31)
            {
                y = v0;
                m = v1;
                d = v2;
            }
            else
                PERR("format is y/m/d but date is %s", str);
            break;

        case GNCIF_DATE_YDM:
            if (v0 > 0 && v1 > 0 && v1 <= 31 && v2 > 0 && v2 <= 12)
            {
                y = v0;
                d = v1;
                m = v2;
            }
            else
                PERR("format is y/d/m but date is %s", str);
            break;

        default:
            PERR("invalid date format: %d", fmt);
        }

        if (!m || !d || !y)
            return FALSE;

        y = fix_year(y);
        *val = gnc_dmy2time64(d, m, y);
        return TRUE;
    }

    return FALSE;
}
