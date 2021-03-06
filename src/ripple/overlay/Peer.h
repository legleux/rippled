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

#ifndef RIPPLE_OVERLAY_PEER_H_INCLUDED
#define RIPPLE_OVERLAY_PEER_H_INCLUDED

#include <ripple/basics/base_uint.h>
#include <ripple/json/json_value.h>
#include <ripple/overlay/P2Peer.h>
#include <ripple/protocol/PublicKey.h>

namespace ripple {

// Maximum hops to attempt when crawling shards. cs = crawl shards
static constexpr std::uint32_t csHopLimit = 3;

enum class ProtocolFeature {
    ValidatorListPropagation,
    ValidatorList2Propagation,
    LedgerReplay,
};

/** Represents a peer connection in the overlay. */
class Peer : public virtual P2Peer, public P2PeerEvents
{
public:
    using ptr = std::shared_ptr<Peer>;

    virtual ~Peer() = default;

    /** Returns `true` if this connection is a member of the cluster. */
    virtual bool
    cluster() const = 0;

    virtual bool
    supportsFeature(ProtocolFeature f) const = 0;

    virtual boost::optional<std::size_t>
    publisherListSequence(PublicKey const&) const = 0;

    virtual void
    setPublisherListSequence(PublicKey const&, std::size_t const) = 0;

    //
    // Ledger
    //

    virtual uint256 const&
    getClosedLedgerHash() const = 0;
    virtual bool
    hasLedger(uint256 const& hash, std::uint32_t seq) const = 0;
    virtual void
    ledgerRange(std::uint32_t& minSeq, std::uint32_t& maxSeq) const = 0;
    virtual bool
    hasShard(std::uint32_t shardIndex) const = 0;
    virtual bool
    hasTxSet(uint256 const& hash) const = 0;
    virtual void
    cycleStatus() = 0;
    virtual bool
    hasRange(std::uint32_t uMin, std::uint32_t uMax) = 0;
};

}  // namespace ripple

#endif
