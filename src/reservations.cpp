/**
 * Copyright (c) 2011-2016 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * libbitcoin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License with
 * additional permissions to the one published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version. For more information see LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <bitcoin/node/reservations.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>
#include <bitcoin/bitcoin.hpp>
#include <bitcoin/node/reservation.hpp>

namespace libbitcoin {
namespace node {

using namespace bc::blockchain;
using namespace bc::chain;

// The protocol maximum size of get data block requests.
static constexpr size_t max_block_request = 50000;

reservations::reservations(hash_queue& hashes, block_chain& chain,
    const settings& settings)
  : hashes_(hashes),
    blockchain_(chain),
    max_request_(max_block_request),
    timeout_(settings.block_timeout_seconds)
{
    initialize(settings.download_connections);
}

bool reservations::import(block::ptr block, size_t height)
{
    // Thread safe.
    return blockchain_.import(block, height);
}

// Rate methods.
//-----------------------------------------------------------------------------

// A statistical summary of block import rates.
// This computation is not synchronized across rows because rates are cached.
reservations::rate_statistics reservations::rates() const
{
    // Copy row pointer table to prevent need for lock during iteration.
    auto rows = table();
    const auto idle = [](reservation::ptr row)
    {
        return row->idle();
    };

    // Remove idle rows.
    rows.erase(std::remove_if(rows.begin(), rows.end(), idle), rows.end());
    const auto active_rows = rows.size();

    std::vector<double> rates(active_rows);
    const auto normal_rate = [](reservation::ptr row)
    {
        return row->rate().normal();
    };

    // Convert to a rates table and sum.
    std::transform(rows.begin(), rows.end(), rates.begin(), normal_rate);
    const auto total = std::accumulate(rates.begin(), rates.end(), 0.0);

    // Calculate mean and sum of deviations.
    const auto mean = reservation::divide<double>(total, active_rows);
    const auto summary = [mean](double initial, double rate)
    {
        const auto difference = mean - rate;
        return initial + (difference * difference);
    };

    // Calculate the standard deviation in the rate deviations.
    auto squares = std::accumulate(rates.begin(), rates.end(), 0.0, summary);
    auto quotient = reservation::divide<double>(squares, active_rows);
    auto standard_deviation = sqrt(quotient);
    return{ active_rows, mean, standard_deviation };
}

// Table methods.
//-----------------------------------------------------------------------------

reservation::list reservations::table() const
{
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    shared_lock lock(mutex_);

    return table_;
    ///////////////////////////////////////////////////////////////////////////
}

void reservations::remove(reservation::ptr row)
{
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    const auto it = std::find(table_.begin(), table_.end(), row);

    if (it == table_.end())
    {
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return;
    }

    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    mutex_.unlock_upgrade_and_lock();

    table_.erase(it);

    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////
}

// Hash methods.
//-----------------------------------------------------------------------------

void reservations::initialize(size_t size)
{
    // Guard against overflow by capping size.
    const size_t max_rows = max_size_t / max_request_;
    auto rows = std::min(max_rows, size);

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    // Ensure that there is at least one block per row.
    const auto blocks = hashes_.size();
    rows = std::min(rows, blocks);

    if (rows == 0)
    {
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return;
    }

    // Allocate no more than 50k headers per row.
    const auto max_allocation = rows * max_request_;
    const auto allocation = std::min(blocks, max_allocation);

    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    mutex_.unlock_upgrade_and_lock();

    table_.reserve(rows);

    for (auto row = 0; row < rows; ++row)
        table_.push_back(std::make_shared<reservation>(*this, row, timeout_));

    size_t height;
    hash_digest hash;

    // The (allocation / rows) * rows cannot exceed allocation.
    // The remainder is retained by the hash list for later reservation.
    for (size_t base = 0; base < (allocation / rows); ++base)
    {
        for (size_t row = 0; row < rows; ++row)
        {
            hashes_.dequeue(hash, height);
            table_[row]->insert(hash, height);
        }
    }

    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    log::debug(LOG_PROTOCOL)
        << "Reserved " << allocation << " blocks to " << rows << " slots.";
}

bool reservations::populate(reservation::ptr minimal)
{
    bool populated;

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock();

    // Take from unallocated or allocated hashes.
    populated = reserve(minimal) || partition(minimal);

    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    if (populated)
        log::debug(LOG_PROTOCOL)
            << "Populated " << minimal->size() << " blocks to slot ("
            << minimal->slot() << ").";

    return populated;
}

// This can cause reduction of an active reservation.
bool reservations::partition(reservation::ptr minimal)
{
    const auto maximal = find_maximal();
    return maximal && maximal != minimal && maximal->partition(minimal);
}

reservation::ptr reservations::find_maximal()
{
    if (table_.empty())
        return nullptr;

    // The maximal row is that with the most block hashes reserved.
    const auto comparer = [](reservation::ptr left, reservation::ptr right)
    {
        return left->size() < right->size();
    };

    return *std::max_element(table_.begin(), table_.end(), comparer);
}

bool reservations::reserve(reservation::ptr minimal)
{
    const auto size = hashes_.size();
    const auto existing = minimal->size();
    const auto allocation = std::min(size, max_request_ - existing);

    size_t height;
    hash_digest hash;

    for (size_t block = 0; block < allocation; ++block)
    {
        hashes_.dequeue(hash, height);
        minimal->insert(hash, height);
    }

    // This may become empty between insert and this test, which is okay.
    return !minimal->empty();
}

} // namespace node
} // namespace libbitcoin
