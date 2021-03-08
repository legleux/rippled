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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/HashRouter.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/app/misc/ValidatorSite.h>
#include <ripple/basics/base64.h>
#include <ripple/basics/make_SSLContext.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/core/DatabaseCon.h>
#include <ripple/nodestore/DatabaseShard.h>
#include <ripple/overlay/Cluster.h>
#include <ripple/overlay/impl/OverlayImpl.h>
#include <ripple/overlay/impl/PeerImp.h>
#include <ripple/overlay/predicates.h>
#include <ripple/peerfinder/make_Manager.h>
#include <ripple/rpc/handlers/GetCounts.h>
#include <ripple/rpc/json_body.h>
#include <ripple/server/SimpleWriter.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/utility/in_place_factory.hpp>

namespace ripple {

namespace CrawlOptions {
enum {
    Disabled = 0,
    Overlay = (1 << 0),
    ServerInfo = (1 << 1),
    ServerCounts = (1 << 2),
    Unl = (1 << 3)
};
}
//------------------------------------------------------------------------------

OverlayImpl::OverlayImpl(
    Application& app,
    Setup const& setup,
    Stoppable& parent,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
    : P2POverlay<PeerImp::Peer_t>(parent)
    , Overlay(parent)
    , P2POverlayImpl<OverlayImpl, PeerImp>(
          app,
          setup,
          parent,
          serverHandler,
          resourceManager,
          resolver,
          io_service,
          config,
          collector)
    , app_(app)
    , timer_count_(0)
    , slots_(app, *this)
{
    beast::PropertyStream::Source::add(m_peerFinder.get());
}

OverlayImpl::~OverlayImpl()
{
    stop();

    // Block until dependent objects have been destroyed.
    // This is just to catch improper use of the Stoppable API.
    //
    std::unique_lock<decltype(mutex_)> lock(mutex_);
    cond_.wait(lock, [this] { return list_.empty(); });
}

OverlayImpl::Timer::Timer(OverlayImpl& overlay)
    : Child<OverlayImpl>(overlay), timer_(overlay_.io_service_)
{
}

void
OverlayImpl::Timer::stop()
{
    error_code ec;
    timer_.cancel(ec);
}

void
OverlayImpl::Timer::run()
{
    timer_.expires_from_now(std::chrono::seconds(1));
    timer_.async_wait(overlay_.strand_.wrap(std::bind(
        &Timer::on_timer, shared_from_this(), std::placeholders::_1)));
}

void
OverlayImpl::Timer::on_timer(error_code ec)
{
    if (ec || overlay_.isStopping())
    {
        if (ec && ec != boost::asio::error::operation_aborted)
        {
            JLOG(overlay_.journal_.error()) << "on_timer: " << ec.message();
        }
        return;
    }

    overlay_.m_peerFinder->once_per_second();
    overlay_.sendEndpoints();
    overlay_.autoConnect();

    if ((++overlay_.timer_count_ % Tuning::checkIdlePeers) == 0)
        overlay_.deleteIdlePeers();

    timer_.expires_from_now(std::chrono::seconds(1));
    timer_.async_wait(overlay_.strand_.wrap(std::bind(
        &Timer::on_timer, shared_from_this(), std::placeholders::_1)));
}

//------------------------------------------------------------------------------

void
OverlayImpl::onManifests(
    std::shared_ptr<protocol::TMManifests> const& m,
    std::shared_ptr<PeerImp> const& from)
{
    auto const n = m->list_size();
    auto const& journal = from->pjournal();

    protocol::TMManifests relay;

    for (std::size_t i = 0; i < n; ++i)
    {
        auto& s = m->list().Get(i).stobject();

        if (auto mo = deserializeManifest(s))
        {
            auto const serialized = mo->serialized;

            auto const result =
                app_.validatorManifests().applyManifest(std::move(*mo));

            if (result == ManifestDisposition::accepted)
            {
                relay.add_list()->set_stobject(s);

                // N.B.: this is important; the applyManifest call above moves
                //       the loaded Manifest out of the optional so we need to
                //       reload it here.
                mo = deserializeManifest(serialized);
                assert(mo);

                app_.getOPs().pubManifest(*mo);

                if (app_.validators().listed(mo->masterKey))
                {
                    auto db = app_.getWalletDB().checkoutDb();

                    soci::transaction tr(*db);
                    static const char* const sql =
                        "INSERT INTO ValidatorManifests (RawData) VALUES "
                        "(:rawData);";
                    soci::blob rawData(*db);
                    convert(serialized, rawData);
                    *db << sql, soci::use(rawData);
                    tr.commit();
                }
            }
        }
        else
        {
            JLOG(journal.debug())
                << "Malformed manifest #" << i + 1 << ": " << strHex(s);
            continue;
        }
    }

    if (!relay.list().empty())
        for_each([m2 = std::make_shared<Message>(relay, protocol::mtMANIFESTS)](
                     std::shared_ptr<PeerImp>&& p) { p->send(m2); });
}

Json::Value
OverlayImpl::crawlShards(bool pubKey, std::uint32_t hops)
{
    using namespace std::chrono;
    using namespace std::chrono_literals;

    Json::Value jv(Json::objectValue);
    auto const numPeers{size()};
    if (numPeers == 0)
        return jv;

    // If greater than a hop away, we may need to gather or freshen data
    if (hops > 0)
    {
        // Prevent crawl spamming
        clock_type::time_point const last(csLast_.load());
        if ((clock_type::now() - last) > 60s)
        {
            auto const timeout(seconds((hops * hops) * 10));
            std::unique_lock<std::mutex> l{csMutex_};

            // Check if already requested
            if (csIDs_.empty())
            {
                {
                    std::lock_guard lock{mutex_};
                    for (auto& id : ids_)
                        csIDs_.emplace(id.first);
                }

                // Relay request to active peers
                protocol::TMGetPeerShardInfo tmGPS;
                tmGPS.set_hops(hops);
                foreach(send_always(std::make_shared<Message>(
                    tmGPS, protocol::mtGET_PEER_SHARD_INFO)));

                if (csCV_.wait_for(l, timeout) == std::cv_status::timeout)
                {
                    csIDs_.clear();
                    csCV_.notify_all();
                }
                csLast_ = duration_cast<seconds>(
                    clock_type::now().time_since_epoch());
            }
            else
                csCV_.wait_for(l, timeout);
        }
    }

    // Combine the shard info from peers and their sub peers
    hash_map<PublicKey, PeerImp::ShardInfo> peerShardInfo;
    for_each([&](std::shared_ptr<PeerImp> const& peer) {
        if (auto psi = peer->getPeerShardInfo())
        {
            // e is non-const so it may be moved from
            for (auto& e : *psi)
            {
                auto it{peerShardInfo.find(e.first)};
                if (it != peerShardInfo.end())
                    // The key exists so join the shard indexes.
                    it->second.shardIndexes += e.second.shardIndexes;
                else
                    peerShardInfo.emplace(std::move(e));
            }
        }
    });

    // Prepare json reply
    auto& av = jv[jss::peers] = Json::Value(Json::arrayValue);
    for (auto const& e : peerShardInfo)
    {
        auto& pv{av.append(Json::Value(Json::objectValue))};
        if (pubKey)
            pv[jss::public_key] = toBase58(TokenType::NodePublic, e.first);

        auto const& address{e.second.endpoint.address()};
        if (!address.is_unspecified())
            pv[jss::ip] = address.to_string();

        pv[jss::complete_shards] = to_string(e.second.shardIndexes);
    }

    return jv;
}

void
OverlayImpl::lastLink(std::uint32_t id)
{
    // Notify threads when every peer has received a last link.
    // This doesn't account for every node that might reply but
    // it is adequate.
    std::lock_guard l{csMutex_};
    if (csIDs_.erase(id) && csIDs_.empty())
        csCV_.notify_all();
}

Json::Value
OverlayImpl::getOverlayInfo()
{
    using namespace std::chrono;
    Json::Value jv;
    auto& av = jv["active"] = Json::Value(Json::arrayValue);

    for_each([&](std::shared_ptr<PeerImp>&& sp) {
        auto& pv = av.append(Json::Value(Json::objectValue));
        pv[jss::public_key] = base64_encode(
            sp->getNodePublic().data(), sp->getNodePublic().size());
        pv[jss::type] = sp->slot()->inbound() ? "in" : "out";
        pv[jss::uptime] = static_cast<std::uint32_t>(
            duration_cast<seconds>(sp->uptime()).count());
        if (sp->crawl())
        {
            pv[jss::ip] = sp->getRemoteAddress().address().to_string();
            if (sp->slot()->inbound())
            {
                if (auto port = sp->slot()->listening_port())
                    pv[jss::port] = *port;
            }
            else
            {
                pv[jss::port] = std::to_string(sp->getRemoteAddress().port());
            }
        }

        {
            auto version{sp->getVersion()};
            if (!version.empty())
                // Could move here if Json::value supported moving from strings
                pv[jss::version] = version;
        }

        std::uint32_t minSeq, maxSeq;
        sp->ledgerRange(minSeq, maxSeq);
        if (minSeq != 0 || maxSeq != 0)
            pv[jss::complete_ledgers] =
                std::to_string(minSeq) + "-" + std::to_string(maxSeq);

        if (auto shardIndexes = sp->getShardIndexes())
            pv[jss::complete_shards] = to_string(*shardIndexes);
    });

    return jv;
}

Json::Value
OverlayImpl::getServerInfo()
{
    bool const humanReadable = false;
    bool const admin = false;
    bool const counters = false;

    Json::Value server_info =
        app_.getOPs().getServerInfo(humanReadable, admin, counters);

    // Filter out some information
    server_info.removeMember(jss::hostid);
    server_info.removeMember(jss::load_factor_fee_escalation);
    server_info.removeMember(jss::load_factor_fee_queue);
    server_info.removeMember(jss::validation_quorum);

    if (server_info.isMember(jss::validated_ledger))
    {
        Json::Value& validated_ledger = server_info[jss::validated_ledger];

        validated_ledger.removeMember(jss::base_fee);
        validated_ledger.removeMember(jss::reserve_base_xrp);
        validated_ledger.removeMember(jss::reserve_inc_xrp);
    }

    return server_info;
}

Json::Value
OverlayImpl::getServerCounts()
{
    return getCountsJson(app_, 10);
}

Json::Value
OverlayImpl::getUnlInfo()
{
    Json::Value validators = app_.validators().getJson();

    if (validators.isMember(jss::publisher_lists))
    {
        Json::Value& publisher_lists = validators[jss::publisher_lists];

        for (auto& publisher : publisher_lists)
        {
            publisher.removeMember(jss::list);
        }
    }

    validators.removeMember(jss::signing_keys);
    validators.removeMember(jss::trusted_validator_keys);
    validators.removeMember(jss::validation_quorum);

    Json::Value validatorSites = app_.validatorSites().getJson();

    if (validatorSites.isMember(jss::validator_sites))
    {
        validators[jss::validator_sites] =
            std::move(validatorSites[jss::validator_sites]);
    }

    return validators;
}

bool
OverlayImpl::processCrawl(http_request_type const& req, Handoff& handoff)
{
    if (req.target() != "/crawl" ||
        setup_.crawlOptions == CrawlOptions::Disabled)
        return false;

    boost::beast::http::response<json_body> msg;
    msg.version(req.version());
    msg.result(boost::beast::http::status::ok);
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");
    msg.body()["version"] = Json::Value(2u);

    if (setup_.crawlOptions & CrawlOptions::Overlay)
    {
        msg.body()["overlay"] = getOverlayInfo();
    }
    if (setup_.crawlOptions & CrawlOptions::ServerInfo)
    {
        msg.body()["server"] = getServerInfo();
    }
    if (setup_.crawlOptions & CrawlOptions::ServerCounts)
    {
        msg.body()["counts"] = getServerCounts();
    }
    if (setup_.crawlOptions & CrawlOptions::Unl)
    {
        msg.body()["unl"] = getUnlInfo();
    }

    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return true;
}

bool
OverlayImpl::processValidatorList(
    http_request_type const& req,
    Handoff& handoff)
{
    // If the target is in the form "/vl/<validator_list_public_key>",
    // return the most recent validator list for that key.
    constexpr std::string_view prefix("/vl/");

    if (!req.target().starts_with(prefix.data()) || !setup_.vlEnabled)
        return false;

    std::uint32_t version = 1;

    boost::beast::http::response<json_body> msg;
    msg.version(req.version());
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");

    auto fail = [&msg, &handoff](auto status) {
        msg.result(status);
        msg.insert("Content-Length", "0");

        msg.body() = Json::nullValue;

        msg.prepare_payload();
        handoff.response = std::make_shared<SimpleWriter>(msg);
        return true;
    };

    auto key = req.target().substr(prefix.size());

    if (auto slash = key.find('/'); slash != boost::string_view::npos)
    {
        auto verString = key.substr(0, slash);
        if (!boost::conversion::try_lexical_convert(verString, version))
            return fail(boost::beast::http::status::bad_request);
        key = key.substr(slash + 1);
    }

    if (key.empty())
        return fail(boost::beast::http::status::bad_request);

    // find the list
    auto vl = app_.validators().getAvailable(key, version);

    if (!vl)
    {
        // 404 not found
        return fail(boost::beast::http::status::not_found);
    }
    else if (!*vl)
    {
        return fail(boost::beast::http::status::bad_request);
    }
    else
    {
        msg.result(boost::beast::http::status::ok);

        msg.body() = *vl;

        msg.prepare_payload();
        handoff.response = std::make_shared<SimpleWriter>(msg);
        return true;
    }
}

bool
OverlayImpl::processHealth(http_request_type const& req, Handoff& handoff)
{
    if (req.target() != "/health")
        return false;
    boost::beast::http::response<json_body> msg;
    msg.version(req.version());
    msg.insert("Server", BuildInfo::getFullVersionString());
    msg.insert("Content-Type", "application/json");
    msg.insert("Connection", "close");

    auto info = getServerInfo();

    int last_validated_ledger_age = -1;
    if (info.isMember("validated_ledger"))
        last_validated_ledger_age = info["validated_ledger"]["age"].asInt();
    bool amendment_blocked = false;
    if (info.isMember("amendment_blocked"))
        amendment_blocked = true;
    int number_peers = info["peers"].asInt();
    std::string server_state = info["server_state"].asString();
    auto load_factor =
        info["load_factor"].asDouble() / info["load_base"].asDouble();

    enum { healthy, warning, critical };
    int health = healthy;
    auto set_health = [&health](int state) {
        if (health < state)
            health = state;
    };

    msg.body()[jss::info] = Json::objectValue;
    if (last_validated_ledger_age >= 7 || last_validated_ledger_age < 0)
    {
        msg.body()[jss::info]["validated_ledger"] = last_validated_ledger_age;
        if (last_validated_ledger_age < 20)
            set_health(warning);
        else
            set_health(critical);
    }

    if (amendment_blocked)
    {
        msg.body()[jss::info]["amendment_blocked"] = true;
        set_health(critical);
    }

    if (number_peers <= 7)
    {
        msg.body()[jss::info]["peers"] = number_peers;
        if (number_peers != 0)
            set_health(warning);
        else
            set_health(critical);
    }

    if (!(server_state == "full" || server_state == "validating" ||
          server_state == "proposing"))
    {
        msg.body()[jss::info]["server_state"] = server_state;
        if (server_state == "syncing" || server_state == "tracking" ||
            server_state == "connected")
        {
            set_health(warning);
        }
        else
            set_health(critical);
    }

    if (load_factor > 100)
    {
        msg.body()[jss::info]["load_factor"] = load_factor;
        if (load_factor < 1000)
            set_health(warning);
        else
            set_health(critical);
    }

    switch (health)
    {
        case healthy:
            msg.result(boost::beast::http::status::ok);
            break;
        case warning:
            msg.result(boost::beast::http::status::service_unavailable);
            break;
        case critical:
            msg.result(boost::beast::http::status::internal_server_error);
            break;
    }

    msg.prepare_payload();
    handoff.response = std::make_shared<SimpleWriter>(msg);
    return true;
}

bool
OverlayImpl::processRequest(http_request_type const& req, Handoff& handoff)
{
    // Take advantage of || short-circuiting
    return processCrawl(req, handoff) || processValidatorList(req, handoff) ||
        processHealth(req, handoff);
}

void
OverlayImpl::checkTracking(std::uint32_t index)
{
    for_each(
        [index](std::shared_ptr<PeerImp>&& sp) { sp->checkTracking(index); });
}

void
OverlayImpl::broadcast(protocol::TMProposeSet& m)
{
    auto const sm = std::make_shared<Message>(m, protocol::mtPROPOSE_LEDGER);
    for_each([&](std::shared_ptr<P2Peer>&& p) { p->send(sm); });
}

std::set<Peer::id_t>
OverlayImpl::relay(
    protocol::TMProposeSet& m,
    uint256 const& uid,
    PublicKey const& validator)
{
    if (auto const toSkip = app_.getHashRouter().shouldRelay(uid))
    {
        auto const sm =
            std::make_shared<Message>(m, protocol::mtPROPOSE_LEDGER, validator);
        for_each([&](std::shared_ptr<P2Peer>&& p) {
            if (toSkip->find(p->id()) == toSkip->end())
                p->send(sm);
        });
        return *toSkip;
    }
    return {};
}

void
OverlayImpl::broadcast(protocol::TMValidation& m)
{
    auto const sm = std::make_shared<Message>(m, protocol::mtVALIDATION);
    for_each([sm](std::shared_ptr<P2Peer>&& p) { p->send(sm); });
}

std::set<Peer::id_t>
OverlayImpl::relay(
    protocol::TMValidation& m,
    uint256 const& uid,
    PublicKey const& validator)
{
    if (auto const toSkip = app_.getHashRouter().shouldRelay(uid))
    {
        auto const sm =
            std::make_shared<Message>(m, protocol::mtVALIDATION, validator);
        for_each([&](std::shared_ptr<P2Peer>&& p) {
            if (toSkip->find(p->id()) == toSkip->end())
                p->send(sm);
        });
        return *toSkip;
    }
    return {};
}

std::shared_ptr<Message>
OverlayImpl::getManifestsMessage()
{
    std::lock_guard g(manifestLock_);

    if (auto seq = app_.validatorManifests().sequence();
        seq != manifestListSeq_)
    {
        protocol::TMManifests tm;

        app_.validatorManifests().for_each_manifest(
            [&tm](std::size_t s) { tm.mutable_list()->Reserve(s); },
            [&tm, &hr = app_.getHashRouter()](Manifest const& manifest) {
                tm.add_list()->set_stobject(
                    manifest.serialized.data(), manifest.serialized.size());
                hr.addSuppression(manifest.hash());
            });

        manifestMessage_.reset();

        if (tm.list_size() != 0)
            manifestMessage_ =
                std::make_shared<Message>(tm, protocol::mtMANIFESTS);

        manifestListSeq_ = seq;
    }

    return manifestMessage_;
}

//------------------------------------------------------------------------------

void
OverlayImpl::sendEndpoints()
{
    auto const result = m_peerFinder->buildEndpointsForPeers();
    for (auto const& e : result)
    {
        std::shared_ptr<PeerImp> peer;
        {
            std::lock_guard lock(mutex_);
            auto const iter = m_peers.find(e.first);
            if (iter != m_peers.end())
                peer = iter->second.lock();
        }
        if (peer)
            peer->sendEndpoints(e.second.begin(), e.second.end());
    }
}

std::shared_ptr<Message>
makeSquelchMessage(
    PublicKey const& validator,
    bool squelch,
    uint32_t squelchDuration)
{
    protocol::TMSquelch m;
    m.set_squelch(squelch);
    m.set_validatorpubkey(validator.data(), validator.size());
    if (squelch)
        m.set_squelchduration(squelchDuration);
    return std::make_shared<Message>(m, protocol::mtSQUELCH);
}

void
OverlayImpl::unsquelch(PublicKey const& validator, Peer::id_t id) const
{
    if (auto peer = findPeerByShortID(id);
        peer && app_.config().VP_REDUCE_RELAY_SQUELCH)
    {
        // optimize - multiple message with different
        // validator might be sent to the same peer
        peer->send(makeSquelchMessage(validator, false, 0));
    }
}

void
OverlayImpl::squelch(
    PublicKey const& validator,
    Peer::id_t id,
    uint32_t squelchDuration) const
{
    if (auto peer = findPeerByShortID(id);
        peer && app_.config().VP_REDUCE_RELAY_SQUELCH)
    {
        peer->send(makeSquelchMessage(validator, true, squelchDuration));
    }
}

void
OverlayImpl::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    std::set<Peer::id_t>&& peers,
    protocol::MessageType type)
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_,
            [this, key, validator, peers = std::move(peers), type]() mutable {
                updateSlotAndSquelch(key, validator, std::move(peers), type);
            });

    for (auto id : peers)
        slots_.updateSlotAndSquelch(key, validator, id, type);
}

void
OverlayImpl::updateSlotAndSquelch(
    uint256 const& key,
    PublicKey const& validator,
    Peer::id_t peer,
    protocol::MessageType type)
{
    if (!strand_.running_in_this_thread())
        return post(strand_, [this, key, validator, peer, type]() {
            updateSlotAndSquelch(key, validator, peer, type);
        });

    slots_.updateSlotAndSquelch(key, validator, peer, type);
}

void
OverlayImpl::deletePeer(Peer::id_t id)
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&OverlayImpl::deletePeer, this, id));

    slots_.deletePeer(id, true);
}

void
OverlayImpl::deleteIdlePeers()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&OverlayImpl::deleteIdlePeers, this));

    slots_.deleteIdlePeers();
}

//------------------------------------------------------------------------------
void
OverlayImpl::onEvtStart()
{
    auto const timer = std::make_shared<Timer>(*this);
    std::lock_guard lock(mutex_);
    list_.emplace(timer.get(), timer);
    timer_ = timer;
    timer->run();
}
//------------------------------------------------------------------------------

Overlay::Setup
setup_Overlay(BasicConfig const& config)
{
    Overlay::Setup setup;

    {
        auto const& section = config.section("overlay");
        setup.context = make_SSLContext("");

        set(setup.ipLimit, "ip_limit", section);
        if (setup.ipLimit < 0)
            Throw<std::runtime_error>("Configured IP limit is invalid");

        std::string ip;
        set(ip, "public_ip", section);
        if (!ip.empty())
        {
            boost::system::error_code ec;
            setup.public_ip = beast::IP::Address::from_string(ip, ec);
            if (ec || beast::IP::is_private(setup.public_ip))
                Throw<std::runtime_error>("Configured public IP is invalid");
        }
    }

    {
        auto const& section = config.section("crawl");
        auto const& values = section.values();

        if (values.size() > 1)
        {
            Throw<std::runtime_error>(
                "Configured [crawl] section is invalid, too many values");
        }

        bool crawlEnabled = true;

        // Only allow "0|1" as a value
        if (values.size() == 1)
        {
            try
            {
                crawlEnabled = boost::lexical_cast<bool>(values.front());
            }
            catch (boost::bad_lexical_cast const&)
            {
                Throw<std::runtime_error>(
                    "Configured [crawl] section has invalid value: " +
                    values.front());
            }
        }

        if (crawlEnabled)
        {
            if (get<bool>(section, "overlay", true))
            {
                setup.crawlOptions |= CrawlOptions::Overlay;
            }
            if (get<bool>(section, "server", true))
            {
                setup.crawlOptions |= CrawlOptions::ServerInfo;
            }
            if (get<bool>(section, "counts", false))
            {
                setup.crawlOptions |= CrawlOptions::ServerCounts;
            }
            if (get<bool>(section, "unl", true))
            {
                setup.crawlOptions |= CrawlOptions::Unl;
            }
        }
    }
    {
        auto const& section = config.section("vl");

        set(setup.vlEnabled, "enabled", section);
    }

    try
    {
        auto id = config.legacy("network_id");

        if (!id.empty())
        {
            if (id == "main")
                id = "0";

            if (id == "testnet")
                id = "1";

            if (id == "devnet")
                id = "2";

            setup.networkID = beast::lexicalCastThrow<std::uint32_t>(id);
        }
    }
    catch (...)
    {
        Throw<std::runtime_error>(
            "Configured [network_id] section is invalid: must be a number "
            "or one of the strings 'main', 'testnet' or 'devnet'.");
    }

    return setup;
}

std::unique_ptr<Overlay>
make_Overlay(
    Application& app,
    Overlay::Setup const& setup,
    Stoppable& parent,
    ServerHandler& serverHandler,
    Resource::Manager& resourceManager,
    Resolver& resolver,
    boost::asio::io_service& io_service,
    BasicConfig const& config,
    beast::insight::Collector::ptr const& collector)
{
    return std::make_unique<OverlayImpl>(
        app,
        setup,
        parent,
        serverHandler,
        resourceManager,
        resolver,
        io_service,
        config,
        collector);
}

}  // namespace ripple
