//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2021 Ripple Labs Inc.

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

#ifndef RIPPLE_OVERLAY_P2OVERLAY_H_INCLUDED
#define RIPPLE_OVERLAY_P2OVERLAY_H_INCLUDED

#include <ripple/beast/utility/PropertyStream.h>
#include <ripple/core/Stoppable.h>
#include <ripple/json/json_value.h>
#include <ripple/overlay/P2Peer.h>
#include <ripple/overlay/PeerSet.h>
#include <ripple/server/Handoff.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
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

class P2POverlayEvents
{
public:
    virtual ~P2POverlayEvents() = default;

protected:
    virtual void
    onEvtStart() = 0;
};

/** Manages the set of connected peers. */
template <typename Peer_t>
class P2POverlay : public Stoppable, public beast::PropertyStream::Source
{
protected:
    using socket_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<socket_type>;

    // VFALCO NOTE The requirement of this constructor is an
    //             unfortunate problem with the API for
    //             Stoppable and PropertyStream
    //
    P2POverlay(Stoppable& parent)
        : Stoppable("Overlay", parent), beast::PropertyStream::Source("peers")
    {
    }

public:
    struct Setup
    {
        explicit Setup() = default;

        std::shared_ptr<boost::asio::ssl::context> context;
        beast::IP::Address public_ip;
        int ipLimit = 0;
        std::uint32_t crawlOptions = 0;
        std::optional<std::uint32_t> networkID;
        bool vlEnabled = true;
    };

    using PeerSequence = std::vector<std::shared_ptr<Peer_t>>;

    virtual ~P2POverlay() = default;

    /** Conditionally accept an incoming HTTP request. */
    virtual Handoff
    onHandoff(
        std::unique_ptr<stream_type>&& bundle,
        http_request_type&& request,
        boost::asio::ip::tcp::endpoint remote_address) = 0;

    /** Establish a peer connection to the specified endpoint.
        The call returns immediately, the connection attempt is
        performed asynchronously.
    */
    virtual void
    connect(beast::IP::Endpoint const& address) = 0;

    /** Returns the maximum number of peers we are configured to allow. */
    virtual int
    limit() = 0;

    /** Returns the number of active peers.
        Active peers are only those peers that have completed the
        handshake and are using the peer protocol.
    */
    virtual std::size_t
    size() const = 0;

    /** Return diagnostics on the status of all peers.
        @deprecated This is superceded by PropertyStream
    */
    virtual Json::Value
    json() = 0;

    /** Returns a sequence representing the current list of peers.
        The snapshot is made at the time of the call.
    */
    virtual PeerSequence
    getActivePeers() const = 0;

    /** Returns the peer with the matching short id, or null. */
    virtual std::shared_ptr<Peer_t>
    findPeerByShortID(P2Peer::id_t const& id) const = 0;

    /** Returns the peer with the matching public key, or null. */
    virtual std::shared_ptr<Peer_t>
    findPeerByPublicKey(PublicKey const& pubKey) = 0;

    /** Visit every active peer.
     *
     * The visitor must be invocable as:
     *     Function(std::shared_ptr<Peer> const& peer);
     *
     * @param f the invocable to call with every peer
     */
    template <class Function>
    void
    foreach(Function f) const
    {
        for (auto const& p : getActivePeers())
            f(p);
    }

    /** Increment and retrieve counters for total peer disconnects, and
     * disconnects we initiate for excessive resource consumption.
     */
    virtual void
    incPeerDisconnect() = 0;
    virtual std::uint64_t
    getPeerDisconnect() const = 0;
    virtual void
    incPeerDisconnectCharges() = 0;
    virtual std::uint64_t
    getPeerDisconnectCharges() const = 0;

    /** Returns the ID of the network this server is configured for, if any.

    The ID is just a numerical identifier, with the IDs 0, 1 and 2 used to
    identify the mainnet, the testnet and the devnet respectively.

    @return The numerical identifier configured by the administrator of the
            server. An unseated optional, otherwise.
*/
    virtual boost::optional<std::uint32_t>
    networkID() const = 0;
};

}  // namespace ripple

#endif
