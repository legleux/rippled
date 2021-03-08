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

#ifndef RIPPLE_OVERLAY_P2POVERLAYIMPL_H_INCLUDED
#define RIPPLE_OVERLAY_P2POVERLAYIMPL_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/basics/Resolver.h>
#include <ripple/basics/UnorderedContainers.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/core/Job.h>
#include <ripple/overlay/Message.h>
#include <ripple/overlay/P2POverlay.h>
#include <ripple/overlay/Slot.h>
#include <ripple/overlay/impl/Child.h>
#include <ripple/overlay/impl/ConnectAttempt.h>
#include <ripple/overlay/impl/Handshake.h>
#include <ripple/overlay/impl/TrafficCount.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/peerfinder/make_Manager.h>
#include <ripple/resource/ResourceManager.h>
#include <ripple/rpc/ServerHandler.h>
#include <ripple/rpc/json_body.h>
#include <ripple/server/Handoff.h>
#include <ripple/server/SimpleWriter.h>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/optional.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <boost/utility/in_place_factory.hpp>

namespace ripple {

class BasicConfig;

template <typename OverlayImplmnt, typename PeerImplmnt>
class P2POverlayImpl : virtual public P2POverlay<typename PeerImplmnt::Peer_t>,
                       public P2POverlayEvents
{
    friend OverlayImplmnt;
    using Setup_t = typename P2POverlay<typename PeerImplmnt::Peer_t>::Setup;
    using PeerSequence_t =
        typename P2POverlay<typename PeerImplmnt::Peer_t>::PeerSequence;

protected:
    using socket_type = boost::asio::ip::tcp::socket;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using error_code = boost::system::error_code;

    Application& app_;
    boost::asio::io_service& io_service_;
    boost::optional<boost::asio::io_service::work> work_;
    boost::asio::io_service::strand strand_;
    mutable std::recursive_mutex mutex_;  // VFALCO use std::mutex
    std::condition_variable_any cond_;
    boost::container::
        flat_map<Child<OverlayImplmnt>*, std::weak_ptr<Child<OverlayImplmnt>>>
            list_;
    Setup_t setup_;
    beast::Journal const journal_;
    ServerHandler& serverHandler_;
    Resource::Manager& m_resourceManager;
    std::unique_ptr<PeerFinder::Manager> m_peerFinder;
    TrafficCount m_traffic;
    hash_map<std::shared_ptr<PeerFinder::Slot>, std::weak_ptr<PeerImplmnt>>
        m_peers;
    hash_map<P2Peer::id_t, std::weak_ptr<PeerImplmnt>> ids_;
    Resolver& m_resolver;
    std::atomic<P2Peer::id_t> next_id_;
    std::atomic<uint64_t> peerDisconnects_{0};
    std::atomic<uint64_t> peerDisconnectsCharges_{0};

    boost::optional<std::uint32_t> networkID_;

public:
    P2POverlayImpl(
        Application& app,
        Setup_t const& setup,
        Stoppable& parent,
        ServerHandler& serverHandler,
        Resource::Manager& resourceManager,
        Resolver& resolver,
        boost::asio::io_service& io_service,
        BasicConfig const& config,
        beast::insight::Collector::ptr const& collector);

    ~P2POverlayImpl();

    P2POverlayImpl(P2POverlayImpl<OverlayImplmnt, PeerImplmnt> const&) = delete;
    P2POverlayImpl&
    operator=(P2POverlayImpl<OverlayImplmnt, PeerImplmnt> const&) = delete;

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

    ServerHandler&
    serverHandler()
    {
        return serverHandler_;
    }

    Setup_t const&
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

    PeerSequence_t
    getActivePeers() const override;

    std::shared_ptr<typename PeerImplmnt::Peer_t>
    findPeerByShortID(P2Peer::id_t const& id) const override;

    std::shared_ptr<typename PeerImplmnt::Peer_t>
    findPeerByPublicKey(PublicKey const& pubKey) override;

    //--------------------------------------------------------------------------
    //
    // OverlayImpl
    //

    void
    add_active(std::shared_ptr<PeerImplmnt> const& peer);

    void
    remove(std::shared_ptr<PeerFinder::Slot> const& slot);

    /** Called when a peer has connected successfully
        This is called after the peer handshake has been completed and during
        peer activation. At this point, the peer address and the public key
        are known.
    */
    void
    activate(std::shared_ptr<PeerImplmnt> const& peer);

    // Called when an active peer is destroyed.
    void
    onPeerDeactivate(P2Peer::id_t id);

    // UnaryFunc will be called as
    //  void(std::shared_ptr<PeerImplmnt>&&)
    //
    template <class UnaryFunc>
    void
    for_each(UnaryFunc&& f) const
    {
        std::vector<std::weak_ptr<PeerImplmnt>> wp;
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
    reportTraffic(TrafficCount::category cat, bool isInbound, int bytes);

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

    boost::optional<std::uint32_t>
    networkID() const override
    {
        return networkID_;
    }

protected:
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

    virtual bool
    processRequest(http_request_type const& req, Handoff& handoff) = 0;

    /** Returns information about the local server's UNL.
        Reported through the /crawl API
        Controlled through the config section [crawl] unl=[0|1]
    */
    Json::Value
    getUnlInfo();

    //--------------------------------------------------------------------------

    //
    // Stoppable
    //

    void
    checkStopped();

    void
    onPrepare() override;

    void
    onStart() override;

    void
    onStop() override;

    void
    onChildrenStopped() override;

    //
    // PropertyStream
    //

    void
    onWrite(beast::PropertyStream::Map& stream) override;

    //--------------------------------------------------------------------------

public:
    void
    remove(Child<OverlayImplmnt>& child);

protected:
    void
    stop();

    void
    autoConnect();

protected:
    struct TrafficGauges
    {
        TrafficGauges(
            char const* name,
            beast::insight::Collector::ptr const& collector)
            : bytesIn(collector->make_gauge(name, "Bytes_In"))
            , bytesOut(collector->make_gauge(name, "Bytes_Out"))
            , messagesIn(collector->make_gauge(name, "Messages_In"))
            , messagesOut(collector->make_gauge(name, "Messages_Out"))
        {
        }
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
            std::vector<TrafficGauges>&& trafficGauges_)
            : peerDisconnects(
                  collector->make_gauge("Overlay", "Peer_Disconnects"))
            , trafficGauges(std::move(trafficGauges_))
            , hook(collector->make_hook(handler))
        {
        }

        beast::insight::Gauge peerDisconnects;
        std::vector<TrafficGauges> trafficGauges;
        beast::insight::Hook hook;
    };

    Stats m_stats;
    std::mutex m_statsMutex;

protected:
    void
    collect_metrics()
    {
        auto counts = m_traffic.getCounts();
        std::lock_guard lock(m_statsMutex);
        assert(counts.size() == m_stats.trafficGauges.size());

        for (std::size_t i = 0; i < counts.size(); ++i)
        {
            m_stats.trafficGauges[i].bytesIn = counts[i].bytesIn;
            m_stats.trafficGauges[i].bytesOut = counts[i].bytesOut;
            m_stats.trafficGauges[i].messagesIn = counts[i].messagesIn;
            m_stats.trafficGauges[i].messagesOut = counts[i].messagesOut;
        }
        m_stats.peerDisconnects = getPeerDisconnect();
    }
};

//------------------------------------------------------------------------------

template <typename OverlayImplmnt, typename PeerImplmnt>
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::P2POverlayImpl(
    Application& app,
    Setup_t const& setup,
    Stoppable& parent,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
    : P2POverlay<typename PeerImplmnt::Peer_t>(parent)
    , app_(app)
    , io_service_(io_service)
    , work_(boost::in_place(std::ref(io_service_)))
    , strand_(io_service_)
    , setup_(setup)
    , journal_(app_.journal("Overlay"))
    , serverHandler_(serverHandler)
    , m_resourceManager(resourceManager)
    , m_peerFinder(PeerFinder::make_Manager(
          *this,
          io_service,
          stopwatch(),
          app_.journal("PeerFinder"),
          config,
          collector))
    , m_resolver(resolver)
    , next_id_(1)
    , m_stats(
          std::bind(
              &P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::collect_metrics,
              this),
          collector,
          [counts = m_traffic.getCounts(), collector]() {
              std::vector<TrafficGauges> ret;
              ret.reserve(counts.size());

              for (size_t i = 0; i < counts.size(); ++i)
              {
                  ret.push_back(TrafficGauges(counts[i].name, collector));
              }

              return ret;
          }())
{
    beast::PropertyStream::Source::add(m_peerFinder.get());
}

template <typename OverlayImplmnt, typename PeerImplmnt>
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::~P2POverlayImpl()
{
    stop();

    // Block until dependent objects have been destroyed.
    // This is just to catch improper use of the Stoppable API.
    //
    std::unique_lock<decltype(mutex_)> lock(mutex_);
    cond_.wait(lock, [this] { return list_.empty(); });
}

//------------------------------------------------------------------------------

template <typename OverlayImplmnt, typename PeerImplmnt>
Handoff
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::onHandoff(
    std::unique_ptr<stream_type>&& stream_ptr,
    http_request_type&& request,
    endpoint_type remote_endpoint)
{
    auto const id = next_id_++;
    beast::WrappedSink sink(app_.logs()["Peer"], makePrefix(id));
    beast::Journal journal(sink);

    Handoff handoff;
    if (processRequest(request, handoff))
        return handoff;
    if (!isPeerUpgrade(request))
        return handoff;

    handoff.moved = true;

    JLOG(journal.debug()) << "Peer connection upgrade from " << remote_endpoint;

    error_code ec;
    auto const local_endpoint(
        stream_ptr->next_layer().socket().local_endpoint(ec));
    if (ec)
    {
        JLOG(journal.debug()) << remote_endpoint << " failed: " << ec.message();
        return handoff;
    }

    auto consumer = m_resourceManager.newInboundEndpoint(
        beast::IPAddressConversion::from_asio(remote_endpoint));
    if (consumer.disconnect())
        return handoff;

    auto const slot = m_peerFinder->new_inbound_slot(
        beast::IPAddressConversion::from_asio(local_endpoint),
        beast::IPAddressConversion::from_asio(remote_endpoint));

    if (slot == nullptr)
    {
        // self-connect, close
        handoff.moved = false;
        return handoff;
    }

    // Validate HTTP request

    {
        auto const types = beast::rfc2616::split_commas(request["Connect-As"]);
        if (std::find_if(types.begin(), types.end(), [](std::string const& s) {
                return boost::iequals(s, "peer");
            }) == types.end())
        {
            handoff.moved = false;
            handoff.response =
                makeRedirectResponse(slot, request, remote_endpoint.address());
            handoff.keep_alive = beast::rfc2616::is_keep_alive(request);
            return handoff;
        }
    }

    auto const negotiatedVersion = negotiateProtocolVersion(request["Upgrade"]);
    if (!negotiatedVersion)
    {
        m_peerFinder->on_closed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot,
            request,
            remote_endpoint.address(),
            "Unable to agree on a protocol version");
        handoff.keep_alive = false;
        return handoff;
    }

    auto const sharedValue = makeSharedValue(*stream_ptr, journal);
    if (!sharedValue)
    {
        m_peerFinder->on_closed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot,
            request,
            remote_endpoint.address(),
            "Incorrect security cookie");
        handoff.keep_alive = false;
        return handoff;
    }

    try
    {
        auto publicKey = verifyHandshake(
            request,
            *sharedValue,
            setup_.networkID,
            setup_.public_ip,
            remote_endpoint.address(),
            app_);

        {
            // The node gets a reserved slot if it is in our cluster
            // or if it has a reservation.
            bool const reserved =
                static_cast<bool>(app_.cluster().member(publicKey)) ||
                app_.peerReservations().contains(publicKey);
            auto const result =
                m_peerFinder->activate(slot, publicKey, reserved);
            if (result != PeerFinder::Result::success)
            {
                m_peerFinder->on_closed(slot);
                JLOG(journal.debug())
                    << "Peer " << remote_endpoint << " redirected, slots full";
                handoff.moved = false;
                handoff.response = makeRedirectResponse(
                    slot, request, remote_endpoint.address());
                handoff.keep_alive = false;
                return handoff;
            }
        }

#if 0  // TBD add mkInboundPeer()
        auto const peer = std::make_shared<PeerImplmnt>(
                app_,
                id,
                slot,
                std::move(request),
                publicKey,
                *negotiatedVersion,
                consumer,
                std::move(stream_ptr),
                *this);
        {
            // As we are not on the strand, run() must be called
            // while holding the lock, otherwise new I/O can be
            // queued after a call to stop().
            std::lock_guard<decltype(mutex_)> lock(mutex_);
            {
                auto const result = m_peers.emplace(peer->slot(), peer);
                assert(result.second);
                (void)result.second;
            }
            list_.emplace(peer.get(), peer);

            peer->run();
        }
#endif
        handoff.moved = true;
        return handoff;
    }
    catch (std::exception const& e)
    {
        JLOG(journal.debug()) << "Peer " << remote_endpoint
                              << " fails handshake (" << e.what() << ")";

        m_peerFinder->on_closed(slot);
        handoff.moved = false;
        handoff.response = makeErrorResponse(
            slot, request, remote_endpoint.address(), e.what());
        handoff.keep_alive = false;
        return handoff;
    }
}

//------------------------------------------------------------------------------

template <typename OverlayImplmnt, typename PeerImplmnt>
bool
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::isPeerUpgrade(
    http_request_type const& request)
{
    if (!is_upgrade(request))
        return false;
    auto const versions = parseProtocolVersions(request["Upgrade"]);
    return !versions.empty();
}

template <typename OverlayImplmnt, typename PeerImplmnt>
std::string
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::makePrefix(std::uint32_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

template <typename OverlayImplmnt, typename PeerImplmnt>
std::shared_ptr<Writer>
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::makeRedirectResponse(
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type const& request,
    address_type remote_address)
{
    boost::beast::http::response<json_body> msg;
    msg.version(request.version());
    msg.result(boost::beast::http::status::service_unavailable);
    msg.insert("Server", BuildInfo::getFullVersionString());
    {
        std::ostringstream ostr;
        ostr << remote_address;
        msg.insert("Remote-Address", ostr.str());
    }
    msg.insert("Content-Type", "application/json");
    msg.insert(boost::beast::http::field::connection, "close");
    msg.body() = Json::objectValue;
    {
        Json::Value& ips = (msg.body()["peer-ips"] = Json::arrayValue);
        for (auto const& _ : m_peerFinder->redirect(slot))
            ips.append(_.address.to_string());
    }
    msg.prepare_payload();
    return std::make_shared<SimpleWriter>(msg);
}

template <typename OverlayImplmnt, typename PeerImplmnt>
std::shared_ptr<Writer>
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::makeErrorResponse(
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type const& request,
    address_type remote_address,
    std::string text)
{
    boost::beast::http::response<boost::beast::http::empty_body> msg;
    msg.version(request.version());
    msg.result(boost::beast::http::status::bad_request);
    msg.reason("Bad Request (" + text + ")");
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Remote-Address", remote_address.to_string());
    msg.insert(boost::beast::http::field::connection, "close");
    msg.prepare_payload();
    return std::make_shared<SimpleWriter>(msg);
}

//------------------------------------------------------------------------------

template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::connect(
    beast::IP::Endpoint const& remote_endpoint)
{
    assert(work_);

    auto usage = resourceManager().newOutboundEndpoint(remote_endpoint);
    if (usage.disconnect())
    {
        JLOG(journal_.info()) << "Over resource limit: " << remote_endpoint;
        return;
    }

    auto const slot = peerFinder().new_outbound_slot(remote_endpoint);
    if (slot == nullptr)
    {
        JLOG(journal_.debug()) << "Connect: No slot for " << remote_endpoint;
        return;
    }

    auto const p = std::make_shared<ConnectAttempt<OverlayImplmnt>>(
        app_,
        io_service_,
        beast::IPAddressConversion::to_asio_endpoint(remote_endpoint),
        usage,
        setup_.context,
        next_id_++,
        slot,
        app_.journal("Peer"),
        static_cast<OverlayImplmnt&>(*this));

    std::lock_guard lock(mutex_);
    list_.emplace(p.get(), p);
    p->run();
}

//------------------------------------------------------------------------------

// Adds a peer that is already handshaked and active
template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::add_active(
    std::shared_ptr<PeerImplmnt> const& peer)
{
    std::lock_guard lock(mutex_);

    {
        auto const result = m_peers.emplace(peer->slot(), peer);
        assert(result.second);
        (void)result.second;
    }

    {
        auto const result = ids_.emplace(
            std::piecewise_construct,
            std::make_tuple(peer->id()),
            std::make_tuple(peer));
        assert(result.second);
        (void)result.second;
    }

    list_.emplace(peer.get(), peer);

    JLOG(journal_.debug()) << "activated " << peer->getRemoteAddress() << " ("
                           << peer->id() << ":"
                           << toBase58(
                                  TokenType::NodePublic, peer->getNodePublic())
                           << ")";

    // As we are not on the strand, run() must be called
    // while holding the lock, otherwise new I/O can be
    // queued after a call to stop().
    peer->run();
}

template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::remove(
    std::shared_ptr<PeerFinder::Slot> const& slot)
{
    std::lock_guard lock(mutex_);
    auto const iter = m_peers.find(slot);
    assert(iter != m_peers.end());
    m_peers.erase(iter);
}

//------------------------------------------------------------------------------
//
// Stoppable
//
//------------------------------------------------------------------------------

// Caller must hold the mutex
template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::checkStopped()
{
    if (this->isStopping() && this->areChildrenStopped() && list_.empty())
        this->stopped();
}

template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::onPrepare()
{
    PeerFinder::Config config = PeerFinder::Config::makeConfig(
        app_.config(),
        serverHandler_.setup().overlay.port,
        !app_.getValidationPublicKey().empty(),
        setup_.ipLimit);

    m_peerFinder->setConfig(config);

    // Populate our boot cache: if there are no entries in [ips] then we use
    // the entries in [ips_fixed].
    auto bootstrapIps =
        app_.config().IPS.empty() ? app_.config().IPS_FIXED : app_.config().IPS;

    // If nothing is specified, default to several well-known high-capacity
    // servers to serve as bootstrap:
    if (bootstrapIps.empty())
    {
        // Pool of servers operated by Ripple Labs Inc. - https://ripple.com
        bootstrapIps.push_back("r.ripple.com 51235");

        // Pool of servers operated by Alloy Networks - https://www.alloy.ee
        bootstrapIps.push_back("zaphod.alloy.ee 51235");

        // Pool of servers operated by ISRDC - https://isrdc.in
        bootstrapIps.push_back("sahyadri.isrdc.in 51235");
    }

    m_resolver.resolve(
        bootstrapIps,
        [this](
            std::string const& name,
            std::vector<beast::IP::Endpoint> const& addresses) {
            std::vector<std::string> ips;
            ips.reserve(addresses.size());
            for (auto const& addr : addresses)
            {
                if (addr.port() == 0)
                    ips.push_back(to_string(addr.at_port(DEFAULT_PEER_PORT)));
                else
                    ips.push_back(to_string(addr));
            }

            std::string const base("config: ");
            if (!ips.empty())
                m_peerFinder->addFallbackStrings(base + name, ips);
        });

    // Add the ips_fixed from the rippled.cfg file
    if (!app_.config().standalone() && !app_.config().IPS_FIXED.empty())
    {
        m_resolver.resolve(
            app_.config().IPS_FIXED,
            [this](
                std::string const& name,
                std::vector<beast::IP::Endpoint> const& addresses) {
                std::vector<beast::IP::Endpoint> ips;
                ips.reserve(addresses.size());

                for (auto& addr : addresses)
                {
                    if (addr.port() == 0)
                        ips.emplace_back(addr.address(), DEFAULT_PEER_PORT);
                    else
                        ips.emplace_back(addr);
                }

                if (!ips.empty())
                    m_peerFinder->addFixedPeer(name, ips);
            });
    }
}

template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::onStart()
{
    onEvtStart();
}

template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::onStop()
{
    strand_.dispatch(
        std::bind(&P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::stop, this));
}

template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::onChildrenStopped()
{
    std::lock_guard lock(mutex_);
    checkStopped();
}

//------------------------------------------------------------------------------
//
// PropertyStream
//
//------------------------------------------------------------------------------

template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::onWrite(
    beast::PropertyStream::Map& stream)
{
    beast::PropertyStream::Set set("traffic", stream);
    auto const stats = m_traffic.getCounts();
    for (auto const& i : stats)
    {
        if (i)
        {
            beast::PropertyStream::Map item(set);
            item["category"] = i.name;
            item["bytes_in"] = std::to_string(i.bytesIn.load());
            item["messages_in"] = std::to_string(i.messagesIn.load());
            item["bytes_out"] = std::to_string(i.bytesOut.load());
            item["messages_out"] = std::to_string(i.messagesOut.load());
        }
    }
}

//------------------------------------------------------------------------------
/** A peer has connected successfully
This is called after the peer handshake has been completed and during
peer activation. At this point, the peer address and the public key
are known.
*/
template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::activate(
    std::shared_ptr<PeerImplmnt> const& peer)
{
    // Now track this peer
    {
        std::lock_guard lock(mutex_);
        auto const result(ids_.emplace(
            std::piecewise_construct,
            std::make_tuple(peer->id()),
            std::make_tuple(peer)));
        assert(result.second);
        (void)result.second;
    }

    JLOG(journal_.debug()) << "activated " << peer->getRemoteAddress() << " ("
                           << peer->id() << ":"
                           << toBase58(
                                  TokenType::NodePublic, peer->getNodePublic())
                           << ")";

    // We just accepted this peer so we have non-zero active peers
    assert(size() != 0);
}

template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::onPeerDeactivate(P2Peer::id_t id)
{
    std::lock_guard lock(mutex_);
    ids_.erase(id);
}

template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::reportTraffic(
    TrafficCount::category cat,
    bool isInbound,
    int number)
{
    m_traffic.addCount(cat, isInbound, number);
}

/** The number of active peers on the network
Active peers are only those peers that have completed the handshake
and are running the Ripple protocol.
*/
template <typename OverlayImplmnt, typename PeerImplmnt>
std::size_t
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::size() const
{
    std::lock_guard lock(mutex_);
    return ids_.size();
}

template <typename OverlayImplmnt, typename PeerImplmnt>
int
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::limit()
{
    return m_peerFinder->config().maxPeers;
}

// Returns information on verified peers.
template <typename OverlayImplmnt, typename PeerImplmnt>
Json::Value
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::json()
{
    Json::Value json;
    for (auto const& peer : getActivePeers())
    {
        json.append(peer->json());
    }
    return json;
}

template <typename OverlayImplmnt, typename PeerImplmnt>
typename P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::PeerSequence_t
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::getActivePeers() const
{
    PeerSequence_t ret;
    ret.reserve(size());

    for_each([&ret](std::shared_ptr<PeerImplmnt>&& sp) {
        ret.emplace_back(std::move(sp));
    });

    return ret;
}

template <typename OverlayImplmnt, typename PeerImplmnt>
std::shared_ptr<typename PeerImplmnt::Peer_t>
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::findPeerByShortID(
    P2Peer::id_t const& id) const
{
    std::lock_guard lock(mutex_);
    auto const iter = ids_.find(id);
    if (iter != ids_.end())
        return iter->second.lock();
    return {};
}

// A public key hash map was not used due to the peer connect/disconnect
// update overhead outweighing the performance of a small set linear search.
template <typename OverlayImplmnt, typename PeerImplmnt>
std::shared_ptr<typename PeerImplmnt::Peer_t>
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::findPeerByPublicKey(
    PublicKey const& pubKey)
{
    std::lock_guard lock(mutex_);
    for (auto const& e : ids_)
    {
        if (auto peer = e.second.lock())
        {
            if (peer->getNodePublic() == pubKey)
                return peer;
        }
    }
    return {};
}

//------------------------------------------------------------------------------

template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::remove(
    Child<OverlayImplmnt>& child)
{
    std::lock_guard lock(mutex_);
    list_.erase(&child);
    if (list_.empty())
        checkStopped();
}

template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::stop()
{
    // Calling list_[].second->stop() may cause list_ to be modified
    // (P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::remove() may be called on
    // this same thread).  So iterating directly over list_ to call
    // child->stop() could lead to undefined behavior.
    //
    // Therefore we copy all of the weak/shared ptrs out of list_ before we
    // start calling stop() on them.  That guarantees
    // P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::remove() won't be called
    // until vector<> children leaves scope.
    std::vector<std::shared_ptr<Child<OverlayImplmnt>>> children;
    {
        std::lock_guard lock(mutex_);
        if (!work_)
            return;
        work_ = boost::none;

        children.reserve(list_.size());
        for (auto const& element : list_)
        {
            children.emplace_back(element.second.lock());
        }
    }  // lock released

    for (auto const& child : children)
    {
        if (child != nullptr)
            child->stop();
    }
}

template <typename OverlayImplmnt, typename PeerImplmnt>
void
P2POverlayImpl<OverlayImplmnt, PeerImplmnt>::autoConnect()
{
    auto const result = m_peerFinder->autoconnect();
    for (auto addr : result)
        connect(addr);
}

}  // namespace ripple

#endif
