/**
 * Copyright (c) 2011-2015 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin-node.
 *
 * libbitcoin-node is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License with
 * additional permissions to the one published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version. For more information see LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "utility.hpp"

#include <cstddef>
#include <cstdint>
#include <bitcoin/blockchain.hpp>

namespace libbitcoin {
namespace node {
namespace test {

using namespace bc::blockchain;
using namespace bc::chain;
using namespace bc::config;

const config::checkpoint check42
{
    "4242424242424242424242424242424242424242424242424242424242424242", 42
};
const config::checkpoint::list no_checks;
const config::checkpoint::list one_check{ check42 };

// Create a headers message of specified size, starting with a genesis header.
message::headers::ptr message_factory(size_t count)
{
    return message_factory(count, null_hash);
}

// Create a headers message of specified size, using specified previous hash.
message::headers::ptr message_factory(size_t count, const hash_digest& hash)
{
    auto previous_hash = hash;
    const auto headers = std::make_shared<message::headers>();
    auto& elements = headers->elements;

    for (size_t height = 0; height < count; ++height)
    {
        const header current_header{ 0, previous_hash, {}, 0, 0, 0, 0 };
        elements.push_back(current_header);
        previous_hash = current_header.hash();
    }

    return headers;
}

blockchain_fixture::blockchain_fixture(bool import_result)
  : import_result_(import_result)
{
}

void blockchain_fixture::start(result_handler handler)
{
}

void blockchain_fixture::stop(result_handler handler)
{
}

void blockchain_fixture::close()
{
}

bool blockchain_fixture::import(block::ptr block, uint64_t height)
{
    return import_result_;
}

void blockchain_fixture::store(block::ptr block, block_store_handler handler)
{
}

void blockchain_fixture::fetch_block_header(uint64_t height,
    block_header_fetch_handler handler)
{
}

void blockchain_fixture::fetch_block_header(const hash_digest& hash,
    block_header_fetch_handler handler)
{
}

void blockchain_fixture::fetch_block_locator(
    block_locator_fetch_handler handle_fetch)
{
}

void blockchain_fixture::fetch_locator_block_hashes(
    const message::get_blocks& locator, const hash_digest& threshold,
    size_t limit, locator_block_hashes_fetch_handler handler)
{
}

void blockchain_fixture::fetch_missing_block_hashes(const hash_list& hashes,
    missing_block_hashes_fetch_handler handler)
{
}

void blockchain_fixture::fetch_block_transaction_hashes(uint64_t height,
    transaction_hashes_fetch_handler handler)
{
}

void blockchain_fixture::fetch_block_transaction_hashes(
    const hash_digest& hash, transaction_hashes_fetch_handler handler)
{
}

void blockchain_fixture::fetch_block_height(const hash_digest& hash,
    block_height_fetch_handler handler)
{
}

void blockchain_fixture::fetch_last_height(last_height_fetch_handler handler)
{
}

void blockchain_fixture::fetch_transaction(const hash_digest& hash,
    transaction_fetch_handler handler)
{
}

void blockchain_fixture::fetch_transaction_index(const hash_digest& hash,
    transaction_index_fetch_handler handler)
{
}

void blockchain_fixture::fetch_spend(const output_point& outpoint,
    spend_fetch_handler handler)
{
}

void blockchain_fixture::fetch_history(const wallet::payment_address& address,
    uint64_t limit, uint64_t from_height, history_fetch_handler handler)
{
}

void blockchain_fixture::fetch_stealth(const binary& prefix,
    uint64_t from_height, stealth_fetch_handler handler)
{
}

void blockchain_fixture::subscribe_reorganize(
    organizer::reorganize_handler handler)
{
}

} // namespace test
} // namespace node
} // namespace libbitcoin