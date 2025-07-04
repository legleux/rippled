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

#ifndef RIPPLE_OVERLAY_OVERLAYIMPL_H_INCLUDED
#define RIPPLE_OVERLAY_OVERLAYIMPL_H_INCLUDED

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Job.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/overlay/Slot.h>
#include <xrpld/overlay/detail/Handshake.h>
#include <xrpld/overlay/detail/TrafficCount.h>
#include <xrpld/overlay/detail/TxMetrics.h>
#include <xrpld/peerfinder/PeerfinderManager.h>
#include <xrpld/rpc/ServerHandler.h>

#include <xrpl/basics/Resolver.h>
#include <xrpl/basics/UnorderedContainers.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/server/Handoff.h>

#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/container/flat_map.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace ripple {

class PeerImp;
class BasicConfig;

class OverlayImpl : public Overlay, public reduce_relay::SquelchHandler
{
public:
    class Child
    {
    protected:
        OverlayImpl& overlay_;

        explicit Child(OverlayImpl& overlay);

        virtual ~Child();

    public:
        virtual void
        stop() = 0;
    };

private:
    using clock_type = std::chrono::steady_clock;
    using socket_type = boost::asio::ip::tcp::socket;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using error_code = boost::system::error_code;

    struct Timer : Child, std::enable_shared_from_this<Timer>
    {
        boost::asio::basic_waitable_timer<clock_type> timer_;
        bool stopping_{false};

        explicit Timer(OverlayImpl& overlay);

        void
        stop() override;

        void
        async_wait();

        void
        on_timer(error_code ec);
    };

    Application& app_;
    boost::asio::io_service& io_service_;
    std::optional<boost::asio::io_service::work> work_;
    boost::asio::io_service::strand strand_;
    mutable std::recursive_mutex mutex_;  // VFALCO use std::mutex
    std::condition_variable_any cond_;
    std::weak_ptr<Timer> timer_;
    boost::container::flat_map<Child*, std::weak_ptr<Child>> list_;
    Setup setup_;
    beast::Journal const journal_;
    ServerHandler& serverHandler_;
    Resource::Manager& m_resourceManager;
    std::unique_ptr<PeerFinder::Manager> m_peerFinder;
    TrafficCount m_traffic;
    hash_map<std::shared_ptr<PeerFinder::Slot>, std::weak_ptr<PeerImp>> m_peers;
    hash_map<Peer::id_t, std::weak_ptr<PeerImp>> ids_;
    Resolver& m_resolver;
    std::atomic<Peer::id_t> next_id_;
    int timer_count_;
    std::atomic<uint64_t> jqTransOverflow_{0};
    std::atomic<uint64_t> peerDisconnects_{0};
    std::atomic<uint64_t> peerDisconnectsCharges_{0};

    reduce_relay::Slots<UptimeClock> slots_;

    // Transaction reduce-relay metrics
    metrics::TxMetrics txMetrics_;

    // A message with the list of manifests we send to peers
    std::shared_ptr<Message> manifestMessage_;
    // Used to track whether we need to update the cached list of manifests
    std::optional<std::uint32_t> manifestListSeq_;
    // Protects the message and the sequence list of manifests
    std::mutex manifestLock_;

    //--------------------------------------------------------------------------

public:
    OverlayImpl(
        Application& app,
        Setup const& setup,
        ServerHandler& serverHandler,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    OverlayImpl(OverlayImpl const&) = delete;
    OverlayImpl&
    operator=(OverlayImpl const&) = delete;

    void
    start() override;

    void
    stop() override;

    PeerFinder::Manager&
    peerFinder()
    {
        return *m_peerFinder;
    }

    Resource::Manager&
    resourceManager()
    {
        return m_resourceManager;
    }

    Setup const&
    setup() const
    {
        return setup_;
    }

    Handoff
    onHandoff(
        std::unique_ptr<stream_type>&& bundle,
        http_request_type&& request,
        endpoint_type remote_endpoint) override;

    void
    connect(beast::IP::Endpoint const& remote_endpoint) override;

    int
    limit() override;

    std::size_t
    size() const override;

    Json::Value
    json() override;

    PeerSequence
    getActivePeers() const override;

    /** Get active peers excluding peers in toSkip.
       @param toSkip peers to skip
       @param active a number of active peers
       @param disabled a number of peers with tx reduce-relay
           feature disabled
       @param enabledInSkip a number of peers with tx reduce-relay
           feature enabled and in toSkip
       @return active peers less peers in toSkip
     */
    PeerSequence
    getActivePeers(
        std::set<Peer::id_t> const& toSkip,
        std::size_t& active,
        std::size_t& disabled,
        std::size_t& enabledInSkip) const;

    void checkTracking(std::uint32_t) override;

    std::shared_ptr<Peer>
    findPeerByShortID(Peer::id_t const& id) const override;

    std::shared_ptr<Peer>
    findPeerByPublicKey(PublicKey const& pubKey) override;

    void
    broadcast(protocol::TMProposeSet& m) override;

    void
    broadcast(protocol::TMValidation& m) override;

    std::set<Peer::id_t>
    relay(
        protocol::TMProposeSet& m,
        uint256 const& uid,
        PublicKey const& validator) override;

    std::set<Peer::id_t>
    relay(
        protocol::TMValidation& m,
        uint256 const& uid,
        PublicKey const& validator) override;

    void
    relay(
        uint256 const&,
        std::optional<std::reference_wrapper<protocol::TMTransaction>> m,
        std::set<Peer::id_t> const& skip) override;

    std::shared_ptr<Message>
    getManifestsMessage();

    //--------------------------------------------------------------------------
    //
    // OverlayImpl
    //

    void
    add_active(std::shared_ptr<PeerImp> const& peer);

    void
    remove(std::shared_ptr<PeerFinder::Slot> const& slot);

    /** Called when a peer has connected successfully
        This is called after the peer handshake has been completed and during
        peer activation. At this point, the peer address and the public key
        are known.
    */
    void
    activate(std::shared_ptr<PeerImp> const& peer);

    // Called when an active peer is destroyed.
    void
    onPeerDeactivate(Peer::id_t id);

    // UnaryFunc will be called as
    //  void(std::shared_ptr<PeerImp>&&)
    //
    template <class UnaryFunc>
    void
    for_each(UnaryFunc&& f) const
    {
        std::vector<std::weak_ptr<PeerImp>> wp;
        {
            std::lock_guard lock(mutex_);

            // Iterate over a copy of the peer list because peer
            // destruction can invalidate iterators.
            wp.reserve(ids_.size());

            for (auto& x : ids_)
                wp.push_back(x.second);
        }

        for (auto& w : wp)
        {
            if (auto p = w.lock())
                f(std::move(p));
        }
    }

    // Called when TMManifests is received from a peer
    void
    onManifests(
        std::shared_ptr<protocol::TMManifests> const& m,
        std::shared_ptr<PeerImp> const& from);

    static bool
    isPeerUpgrade(http_request_type const& request);

    template <class Body>
    static bool
    isPeerUpgrade(boost::beast::http::response<Body> const& response)
    {
        if (!is_upgrade(response))
            return false;
        return response.result() ==
            boost::beast::http::status::switching_protocols;
    }

    template <class Fields>
    static bool
    is_upgrade(boost::beast::http::header<true, Fields> const& req)
    {
        if (req.version() < 11)
            return false;
        if (req.method() != boost::beast::http::verb::get)
            return false;
        if (!boost::beast::http::token_list{req["Connection"]}.exists(
                "upgrade"))
            return false;
        return true;
    }

    template <class Fields>
    static bool
    is_upgrade(boost::beast::http::header<false, Fields> const& req)
    {
        if (req.version() < 11)
            return false;
        if (!boost::beast::http::token_list{req["Connection"]}.exists(
                "upgrade"))
            return false;
        return true;
    }

    static std::string
    makePrefix(std::uint32_t id);

    void
    reportInboundTraffic(TrafficCount::category cat, int bytes);

    void
    reportOutboundTraffic(TrafficCount::category cat, int bytes);

    void
    incJqTransOverflow() override
    {
        ++jqTransOverflow_;
    }

    std::uint64_t
    getJqTransOverflow() const override
    {
        return jqTransOverflow_;
    }

    void
    incPeerDisconnect() override
    {
        ++peerDisconnects_;
    }

    std::uint64_t
    getPeerDisconnect() const override
    {
        return peerDisconnects_;
    }

    void
    incPeerDisconnectCharges() override
    {
        ++peerDisconnectsCharges_;
    }

    std::uint64_t
    getPeerDisconnectCharges() const override
    {
        return peerDisconnectsCharges_;
    }

    std::optional<std::uint32_t>
    networkID() const override
    {
        return setup_.networkID;
    }

    /** Updates message count for validator/peer. Sends TMSquelch if the number
     * of messages for N peers reaches threshold T. A message is counted
     * if a peer receives the message for the first time and if
     * the message has been  relayed.
     * @param key Unique message's key
     * @param validator Validator's public key
     * @param peers Peers' id to update the slots for
     * @param type Received protocol message type
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        std::set<Peer::id_t>&& peers,
        protocol::MessageType type);

    /** Overload to reduce allocation in case of single peer
     */
    void
    updateSlotAndSquelch(
        uint256 const& key,
        PublicKey const& validator,
        Peer::id_t peer,
        protocol::MessageType type);

    /** Called when the peer is deleted. If the peer was selected to be the
     * source of messages from the validator then squelched peers have to be
     * unsquelched.
     * @param id Peer's id
     */
    void
    deletePeer(Peer::id_t id);

    Json::Value
    txMetrics() const override
    {
        return txMetrics_.json();
    }

    /** Add tx reduce-relay metrics. */
    template <typename... Args>
    void
    addTxMetrics(Args... args)
    {
        if (!strand_.running_in_this_thread())
            return post(
                strand_,
                std::bind(&OverlayImpl::addTxMetrics<Args...>, this, args...));

        txMetrics_.addMetrics(args...);
    }

private:
    void
    squelch(
        PublicKey const& validator,
        Peer::id_t const id,
        std::uint32_t squelchDuration) const override;

    void
    unsquelch(PublicKey const& validator, Peer::id_t id) const override;

    std::shared_ptr<Writer>
    makeRedirectResponse(
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type const& request,
        address_type remote_address);

    std::shared_ptr<Writer>
    makeErrorResponse(
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type const& request,
        address_type remote_address,
        std::string msg);

    /** Handles crawl requests. Crawl returns information about the
        node and its peers so crawlers can map the network.

        @return true if the request was handled.
    */
    bool
    processCrawl(http_request_type const& req, Handoff& handoff);

    /** Handles validator list requests.
        Using a /vl/<hex-encoded public key> URL, will retrieve the
        latest valdiator list (or UNL) that this node has for that
        public key, if the node trusts that public key.

        @return true if the request was handled.
    */
    bool
    processValidatorList(http_request_type const& req, Handoff& handoff);

    /** Handles health requests. Health returns information about the
        health of the node.

        @return true if the request was handled.
    */
    bool
    processHealth(http_request_type const& req, Handoff& handoff);

    /** Handles non-peer protocol requests.

        @return true if the request was handled.
    */
    bool
    processRequest(http_request_type const& req, Handoff& handoff);

    /** Returns information about peers on the overlay network.
        Reported through the /crawl API
        Controlled through the config section [crawl] overlay=[0|1]
    */
    Json::Value
    getOverlayInfo();

    /** Returns information about the local server.
        Reported through the /crawl API
        Controlled through the config section [crawl] server=[0|1]
    */
    Json::Value
    getServerInfo();

    /** Returns information about the local server's performance counters.
        Reported through the /crawl API
        Controlled through the config section [crawl] counts=[0|1]
    */
    Json::Value
    getServerCounts();

    /** Returns information about the local server's UNL.
        Reported through the /crawl API
        Controlled through the config section [crawl] unl=[0|1]
    */
    Json::Value
    getUnlInfo();

    //--------------------------------------------------------------------------

    //
    // PropertyStream
    //

    void
    onWrite(beast::PropertyStream::Map& stream) override;

    //--------------------------------------------------------------------------

    void
    remove(Child& child);

    void
    stopChildren();

    void
    autoConnect();

    void
    sendEndpoints();

    /** Send once a second transactions' hashes aggregated by peers. */
    void
    sendTxQueue();

    /** Check if peers stopped relaying messages
     * and if slots stopped receiving messages from the validator */
    void
    deleteIdlePeers();

private:
    struct TrafficGauges
    {
        TrafficGauges(
            std::string const& name,
            beast::insight::Collector::ptr const& collector)
            : name(name)
            , bytesIn(collector->make_gauge(name, "Bytes_In"))
            , bytesOut(collector->make_gauge(name, "Bytes_Out"))
            , messagesIn(collector->make_gauge(name, "Messages_In"))
            , messagesOut(collector->make_gauge(name, "Messages_Out"))
        {
        }
        std::string const name;
        beast::insight::Gauge bytesIn;
        beast::insight::Gauge bytesOut;
        beast::insight::Gauge messagesIn;
        beast::insight::Gauge messagesOut;
    };

    struct Stats
    {
        template <class Handler>
        Stats(
            Handler const& handler,
            beast::insight::Collector::ptr const& collector,
            std::unordered_map<TrafficCount::category, TrafficGauges>&&
                trafficGauges_)
            : peerDisconnects(
                  collector->make_gauge("Overlay", "Peer_Disconnects"))
            , trafficGauges(std::move(trafficGauges_))
            , hook(collector->make_hook(handler))
        {
        }

        beast::insight::Gauge peerDisconnects;
        std::unordered_map<TrafficCount::category, TrafficGauges> trafficGauges;
        beast::insight::Hook hook;
    };

    Stats m_stats;
    std::mutex m_statsMutex;

private:
    void
    collect_metrics()
    {
        auto counts = m_traffic.getCounts();
        std::lock_guard lock(m_statsMutex);
        XRPL_ASSERT(
            counts.size() == m_stats.trafficGauges.size(),
            "ripple::OverlayImpl::collect_metrics : counts size do match");

        for (auto const& [key, value] : counts)
        {
            auto it = m_stats.trafficGauges.find(key);
            if (it == m_stats.trafficGauges.end())
                continue;

            auto& gauge = it->second;

            XRPL_ASSERT(
                gauge.name == value.name,
                "ripple::OverlayImpl::collect_metrics : gauge and counter "
                "match");

            gauge.bytesIn = value.bytesIn;
            gauge.bytesOut = value.bytesOut;
            gauge.messagesIn = value.messagesIn;
            gauge.messagesOut = value.messagesOut;
        }

        m_stats.peerDisconnects = getPeerDisconnect();
    }
};

}  // namespace ripple

#endif
