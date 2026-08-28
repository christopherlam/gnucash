/**********************************************************************
 * Transaction.hpp
 *                                                                    *
 * This program is free software; you can redistribute it and/or      *
 * modify it under the terms of the GNU General Public License as     *
 * published by the Free Software Foundation; either version 2 of     *
 * the License, or (at your option) any later version.                *
 *                                                                    *
 * This program is distributed in the hope that it will be useful,    *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of     *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the      *
 * GNU General Public License for more details.                       *
 *                                                                    *
 * You should have received a copy of the GNU General Public License  *
 * along with this program; if not, contact:                          *
 *                                                                    *
 * Free Software Foundation           Voice:  +1-617-542-5942         *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652         *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                     *
 *                                                                    *
 *********************************************************************/

/** @addtogroup Engine
    @{ */
/** @addtogroup Transaction

    @{ */
/** @file Transaction.hpp
 *  @brief Transaction public routines (C++ api)
 */

#ifndef GNC_TRANSACTION_HPP
#define GNC_TRANSACTION_HPP

#include <vector>

#include <Transaction.h>

using SplitsVec = std::vector<Split*>;

/** Returns the transaction's splits as a const vector reference. This
 *  vector is the transaction's internal data structure: do not modify
 *  it, and do not retain the reference past any subsequent edit of the
 *  transaction's splits. */
const SplitsVec& xaccTransGetSplits (const Transaction *trans);

#endif /* GNC_TRANSACTION_HPP */
/** @} */
/** @} */
