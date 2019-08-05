/**
 * Copyright (c) 2011-2019 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <bitcoin/node/protocols/protocol_block_in.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <boost/format.hpp>
#include <bitcoin/blockchain.hpp>
#include <bitcoin/network.hpp>
#include <bitcoin/node/define.hpp>
#include <bitcoin/node/full_node.hpp>
#include <bitcoin/node/utility/performance.hpp>
#include <bitcoin/node/utility/reservation.hpp>

namespace libbitcoin {
namespace node {

#define NAME "block_sync"
#define CLASS protocol_block_in

using namespace bc::blockchain;
using namespace bc::network;
using namespace bc::system;
using namespace bc::system::message;
using namespace std::placeholders;

// The moving window in which block average download rate is measured.
static const asio::seconds monitor_interval(5);

inline bool is_witness(uint64_t services)
{
    return (services & version::service::node_witness) != 0;
}

// Depends on protocol_header_sync, which requires protocol version 31800.
protocol_block_in::protocol_block_in(full_node& node, channel::ptr channel,
    safe_chain& chain)
  : protocol_timer(node, channel, true, NAME),
    node_(node),
    chain_(chain),
    reservation_(node.get_reservation()),
    require_witness_(is_witness(node.network_settings().services)),
    peer_witness_(is_witness(channel->peer_version()->services())),
    CONSTRUCT_TRACK(protocol_block_in)
{
}

// Start sequence.
// ----------------------------------------------------------------------------

void protocol_block_in::start()
{
    protocol_timer::start(monitor_interval, BIND1(handle_event, _1));

    chain_.subscribe_headers(BIND4(handle_reindexed, _1, _2, _3, _4));
    SUBSCRIBE2(block, handle_receive_block, _1, _2);

    // This is the end of the start sequence.
    send_get_blocks();
}

// Download sequence.
// ----------------------------------------------------------------------------

void protocol_block_in::send_get_blocks()
{
    if (stopped())
        return;

    // Don't start downloading blocks until the header chain is current.
    // This protects against disk fill and allows hashes to be distributed.
    if (chain_.is_candidates_stale())
        return;

    // Repopulate if empty and new work has arrived.
    auto request = reservation_->request();

    // Or we may be the same channel and with hashes already requested.
    if (request.inventories().empty())
        return;

    // Convert requested message types to corresponding witness types.
    if (require_witness_)
        request.to_witness();

    LOG_DEBUG(LOG_NODE)
        << "Sending request of " << request.inventories().size()
        << " hashes for slot (" << reservation_->slot() << ").";

    SEND2(request, handle_send, _1, request.command);
}

bool protocol_block_in::handle_receive_block(const code& ec,
    block_const_ptr message)
{
    if (stopped(ec))
        return false;

    if (ec)
    {
        LOG_ERROR(LOG_NODE)
            << "Failure in block receive for slot (" << reservation_->slot()
            << ") " << ec.message();
        stop(ec);
        return false;
    }

    // TODO: this should fail from start() but need to verify that is allowed.
    // TODO: v3 suspends incoming blocks from start, but does not stop channel.
    // Stop if required witness is unavailable.
    if (require_witness_ && !peer_witness_)
    {
        stop(error::channel_stopped);
        return false;
    }

    // This channel was slowest, so half of its reservation has been taken.
    if (reservation_->stopped())
    {
        LOG_DEBUG(LOG_NODE)
            << "Restarting partitioned slot (" << reservation_->slot()
            << ") : [" << reservation_->size() << "]";
        stop(error::channel_stopped);
        return false;
    }

    size_t height;

    // The reservation may have become stopped between the stop test and this
    // call, so the block may either be unrequested or moved to another slot.
    // There is currently no way to know the difference, so log both options.
    if (!reservation_->find_height_and_erase(message->hash(), height))
    {
        LOG_DEBUG(LOG_NODE)
            << "Unrequested or partitioned block on slot ("
            << reservation_->slot() << ").";
        stop(error::channel_stopped);
        return false;
    }

    // TODO: change organizer to async for consistency.
    // Add the block's transactions to the store.
    // If this is the validation target then validator advances here.
    // Block validation failure will not cause an error here.
    // If any block fails validation then reindexation will be triggered.
    // Successful block validation with sufficient height triggers block reorg.
    // However the reorgnization notification cannot be sent from here.
    //#########################################################################
    const auto error_code = chain_.organize(message, height);
    //#########################################################################

    if (error_code)
    {
        LOG_FATAL(LOG_NODE)
            << "Failure organize block for slot (" << reservation_->slot()
            << "), store is now corrupted: " << error_code.message();
        stop(error_code);
        return false;
    }

    // Recompute rate performance, excluding store cost.
    reservation_->update_history(message);

    // Only log every 100th block, until current number of blocks.
    const auto period = chain_.is_blocks_stale() ? 100u : 1u;

    if ((height % period) == 0)
        report(*message, height);

    send_get_blocks();
    return true;
}

template<typename Type>
float to_float(const asio::duration& time)
{
    const auto count = std::chrono::duration_cast<Type>(time).count();
    return static_cast<float>(count);
}

template<typename Type>
size_t to_ratio(const asio::duration& time, size_t value)
{
    return static_cast<size_t>(std::round(to_float<Type>(time) / value));
}

void protocol_block_in::report(const chain::block& block, size_t height) const
{
    // TODO: convert deserialize, check, associate to per input in microsecs.
    // Block #height [hash] (slot) deserial accept associate rate local% remain.
    static const auto form = "Block  #%06i [%s] "
        "%|4i| txs %|4i| ins %|3i| des %|3i| chk %|3i| dep (%02i) %i";

    const auto transactions = block.transactions().size();
    const auto inputs = std::max(block.total_inputs(), size_t(1));
    ////const auto record = reservation_->rate();
    ////const auto database_percentage = record.ratio() * 100;

    LOG_INFO(LOG_NODE)
        << boost::format(form) %
            height %
            encode_hash(block.hash()) %
            transactions %
            inputs %

            // network total (net)
            to_ratio<asio::microseconds>(block.metadata.deserialize, inputs) %

            // check total (chk)
            to_ratio<asio::microseconds>(block.metadata.check, inputs) %

            // deposit total (dep)
            to_ratio<asio::microseconds>(block.metadata.associate, inputs) %

            ////performance::to_megabits_per_second(record.rate()) %
            ////database_percentage %
            reservation_->slot() %
            node_.download_queue_size();
}

// Events.
// ----------------------------------------------------------------------------

// Use header indexation as a block request trigger.
bool protocol_block_in::handle_reindexed(code ec, size_t,
    header_const_ptr_list_const_ptr, header_const_ptr_list_const_ptr)
{
    if (stopped(ec))
        return false;

    if (ec)
    {
        LOG_ERROR(LOG_NODE)
            << "Failure in header index for slot (" << reservation_->slot()
            << ") " << ec.message();
        stop(ec);
        return false;
    }

    // When the queue is empty and a new header is announced the download is
    // not directed to the peer that made the announcement. This can lead to
    // delay in obtaining the block, which can be costly for mining. On the
    // other hand optimal mining relies on the compact block protocol, not full
    // block requests, so this is considered acceptable behavior here.

    send_get_blocks();
    return true;
}

// Fired by base timer and stop handler.
void protocol_block_in::handle_event(const code& ec)
{
    if (stopped(ec))
    {
        // No longer receiving blocks, so free up the reservation.
        reservation_->stop();

        // Trigger unsubscribe or protocol will hang until next header indexed.
        chain_.unsubscribe();
        return;
    }

    if (ec && ec != error::channel_timeout)
    {
        LOG_ERROR(LOG_NODE)
            << "Failure in block sync timer for slot (" << reservation_->slot()
            << ") " << ec.message();
        stop(ec);
        return;
    }

    // This ensures that a stall does not persist.
    if (reservation_->expired())
    {
        LOG_DEBUG(LOG_NODE)
            << "Restarting slow slot (" << reservation_->slot() << ") : ["
            << reservation_->size() << "]";
        stop(ec);
        return;
    }
}

} // namespace node
} // namespace libbitcoin
