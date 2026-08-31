/********************************************************************\
 * Transaction.hpp -- C++ API for the transaction object             *
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

#ifndef GNC_TRANSACTION_HPP
#define GNC_TRANSACTION_HPP

#include <Account.hpp>          /* for SplitsVec */
#include <Transaction.h>

/** Return the transaction's splits, in order.
 *
 *  This is the C++ counterpart of xaccTransGetSplitList(): it hands back a
 *  reference to the transaction's own storage, so it allocates nothing and
 *  the caller frees nothing. The reference is invalidated by anything that
 *  adds or removes a split.
 *
 *  @param trans the transaction; may be nullptr, in which case an empty
 *  vector is returned.
 */
const SplitsVec& xaccTransGetSplits (const Transaction *trans);

#endif /* GNC_TRANSACTION_HPP */
