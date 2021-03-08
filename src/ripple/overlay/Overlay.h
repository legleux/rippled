//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_OVERLAY_OVERLAY_H_INCLUDED
#define RIPPLE_OVERLAY_OVERLAY_H_INCLUDED

#include <ripple/json/json_value.h>
#include <ripple/overlay/P2POverlay.h>
#include <ripple/overlay/Peer.h>
#include <ripple/overlay/PeerSet.h>
#include <boost/optional.hpp>
#include <functional>
#include <memory>
#include <type_traits>

namespace boost {
namespace asio {
namespace ssl {
class context;
}
}  // namespace asio
}  // namespace boost

namespace ripple {

/** Manages the set of connected peers. */
class Overlay : virtual public P2POverlay<Peer>
{
protected:
    // VFALCO NOTE The requirement of this constructor is an
    //             unfortunate problem with the API for
    //             Stoppable and PropertyStream
    //
    Overlay(Stoppable& parent) : P2POverlay<Peer>(parent)
    {
    }

public:
    virtual ~Overlay() = default;

    /** Calls the checkTracking function on each peer
        @param index the value to pass to the peer's checkTracking function
    */
    virtual void
    checkTracking(std::uint32_t index) = 0;

    /** Broadcast a proposal. */
    virtual void
    broadcast(protocol::TMProposeSet& m) = 0;

    /** Broadcast a validation. */
    virtual void
    broadcast(protocol::TMValidation& m) = 0;

    /** Relay a proposal.
     * @param m the serialized proposal
     * @param uid the id used to identify this proposal
     * @param validator The pubkey of the validator that issued this proposal
     * @return the set of peers which have already sent us this proposal
     */
    virtual std::set<Peer::id_t>
    relay(
        protocol::TMProposeSet& m,
        uint256 const& uid,
        PublicKey const& validator) = 0;

    /** Relay a validation.
     * @param m the serialized validation
     * @param uid the id used to identify this validation
     * @param validator The pubkey of the validator that issued this validation
     * @return the set of peers which have already sent us this validation
     */
    virtual std::set<Peer::id_t>
    relay(
        protocol::TMValidation& m,
        uint256 const& uid,
        PublicKey const& validator) = 0;

    /** Increment and retrieve counter for transaction job queue overflows. */
    virtual void
    incJqTransOverflow() = 0;
    virtual std::uint64_t
    getJqTransOverflow() const = 0;

    /** Returns information reported to the crawl shard RPC command.

        @param hops the maximum jumps the crawler will attempt.
        The number of hops achieved is not guaranteed.
    */
    virtual Json::Value
    crawlShards(bool pubKey, std::uint32_t hops) = 0;
};

}  // namespace ripple

#endif
