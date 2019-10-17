/********************************************************************\
 * ScrubBudget.h -- fix budget amount signs                         *
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
#include <qofbook.h>

/* ================================================================ */

/** analyse book to check whether the budgets need to be scrubbed
 * @param book The book to check
 * @return Whether to offer scrubbing
 */
gboolean gnc_scrub_budget_signs_check (QofBook *book);

/** Fix budget signs
 * A guard is set if we have completed reversal, or there are no
 * budgets in book.
 * @param book The book to scrub
 */
void gnc_scrub_budget_signs (QofBook *book);

/* ==================== END OF FILE ==================== */
