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

#ifndef RIPPLE_OVERLAY_P2PEERIMP_H_INCLUDED
#define RIPPLE_OVERLAY_P2PEERIMP_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/basics/RangeSet.h>
#include <ripple/beast/utility/WrappedSink.h>
#include <ripple/overlay/P2Peer.h>
#include <ripple/overlay/impl/Child.h>
#include <ripple/overlay/impl/Handshake.h>
#include <ripple/overlay/impl/OverlayImplTraits.h>
#include <ripple/overlay/impl/ProtocolMessage.h>
#include <ripple/overlay/impl/ProtocolVersion.h>
#include <ripple/overlay/impl/TrafficCount.h>
#include <ripple/overlay/impl/Tuning.h>
#include <ripple/peerfinder/PeerfinderManager.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/resource/Consumer.h>
#include <ripple/resource/Fees.h>

#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/optional.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <cstdint>
#include <memory>
#include <queue>
#include <shared_mutex>

namespace ripple {

namespace {
/** The threshold above which we treat a peer connection as high latency */
std::chrono::milliseconds constexpr peerHighLatency{300};
}  // namespace

template <typename PeerImplmnt>
class P2PeerImp : public virtual P2Peer,
                  public P2PeerEvents,
                  public Child<typename OverlayImplTraits<PeerImplmnt>::OverlayImpl_t>
{
    using OverlayImpl_t = typename OverlayImplTraits<PeerImplmnt>::OverlayImpl_t;
protected:
    using clock_type = std::chrono::steady_clock;
    using error_code = boost::system::error_code;
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using address_type = boost::asio::ip::address;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using Compressed = compression::Compressed;

    id_t const id_;
    beast::WrappedSink sink_;
    beast::WrappedSink p_sink_;
    beast::Journal const journal_;
    beast::Journal const p_journal_;
    std::unique_ptr<stream_type> stream_ptr_;
    socket_type& socket_;
    stream_type& stream_;
    boost::asio::strand<boost::asio::executor> strand_;

    // Updated at each stage of the connection process to reflect
    // the current conditions as closely as possible.
    beast::IP::Endpoint const remote_address_;

    // These are up here to prevent warnings about order of initializations
    //
    // OverlayImpl& overlay_;
    bool const inbound_;

    // Protocol version to use for this link
    ProtocolVersion protocol_;

    bool detaching_ = false;
    // Node public key of peer.
    PublicKey const publicKey_;
    std::string name_;
    boost::shared_mutex mutable nameMutex_;

    boost::optional<std::chrono::milliseconds> latency_;

    std::mutex mutable recentLock_;
    Resource::Consumer usage_;
    Resource::Charge fee_;
    std::shared_ptr<PeerFinder::Slot> const slot_;
    boost::beast::multi_buffer read_buffer_;
    http_request_type request_;
    http_response_type response_;
    boost::beast::http::fields const& headers_;
    std::queue<std::shared_ptr<Message>> send_queue_;
    bool gracefulClose_ = false;
    int large_sendq_ = 0;

    Compressed compressionEnabled_ = Compressed::Off;

    class Metrics
    {
    public:
        Metrics() = default;
        Metrics(Metrics const&) = delete;
        Metrics&
        operator=(Metrics const&) = delete;
        Metrics(Metrics&&) = delete;
        Metrics&
        operator=(Metrics&&) = delete;

        void
        add_message(std::uint64_t bytes);
        std::uint64_t
        average_bytes() const;
        std::uint64_t
        total_bytes() const;

    private:
        boost::shared_mutex mutable mutex_;
        boost::circular_buffer<std::uint64_t> rollingAvg_{30, 0ull};
        clock_type::time_point intervalStart_{clock_type::now()};
        std::uint64_t totalBytes_{0};
        std::uint64_t accumBytes_{0};
        std::uint64_t rollingAvgBytes_{0};
    };

    struct
    {
        Metrics sent;
        Metrics recv;
    } metrics_;

public:
    P2PeerImp(P2PeerImp const&) = delete;
    P2PeerImp&
    operator=(P2PeerImp const&) = delete;

    /** Create an active incoming peer from an established ssl connection. */
    P2PeerImp(
        Config const& config,
        Logs& logs,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr,
        OverlayImpl_t& overlay);

    /** Create outgoing, handshaked peer. */
    // VFALCO legacyPublicKey should be implied by the Slot
    template <class Buffers>
    P2PeerImp(
        Config const& config,
        Logs& logs,
        std::unique_ptr<stream_type>&& stream_ptr,
        Buffers const& buffers,
        std::shared_ptr<PeerFinder::Slot>&& slot,
        http_response_type&& response,
        Resource::Consumer usage,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        id_t id,
        OverlayImpl_t& overlay);

    virtual ~P2PeerImp();

    beast::Journal const&
    pjournal() const
    {
        return p_journal_;
    }

    std::shared_ptr<PeerFinder::Slot> const&
    slot()
    {
        return slot_;
    }

    // Work-around for calling shared_from_this in constructors
    void
    run();

    //
    // Network
    //

    void
    send(std::shared_ptr<Message> const& m) override;

    beast::IP::Endpoint
    getRemoteAddress() const override
    {
        return remote_address_;
    }

    void
    charge(Resource::Charge const& fee) override;

    //
    // Identity
    //

    Peer::id_t
    id() const override
    {
        return id_;
    }

    PublicKey const&
    getNodePublic() const override
    {
        return publicKey_;
    }

    /** Return the version of rippled that the peer is running, if reported. */
    std::string
    getVersion() const;

    Json::Value
    json() override;

    // Called to determine our priority for querying
    int
    getScore(bool haveItem) const override;

    bool
    isHighLatency() const override;

    void
    fail(std::string const& reason);

    bool
    compressionEnabled() const override
    {
        return compressionEnabled_ == Compressed::On;
    }

protected:
    void
    close();

    void
    fail(std::string const& name, error_code ec);

    void
    gracefulClose();

    static std::string
    makePrefix(id_t id);

    // Called when SSL shutdown completes
    void
    onShutdown(error_code ec);

    void
    doAccept();

    std::string
    name() const;

    std::string
    domain() const;

    std::optional<std::uint32_t>
    networkID() const;

    //
    // protocol message loop
    //

    // Starts the protocol message loop
    void
    doProtocolStart();

    // Called when protocol message bytes are received
    void
    onReadMessage(error_code ec, std::size_t bytes_transferred);

    // Called when protocol messages bytes are sent
    void
    onWriteMessage(error_code ec, std::size_t bytes_transferred);

protected:
    bool
    isSocketOpen() const override
    {
        return socket_.is_open();
    }

    std::size_t
    queueSize() const override
    {
        return send_queue_.size();
    }

    virtual std::shared_ptr<PeerImplmnt>
    shared() = 0;
};

//------------------------------------------------------------------------------

template <typename PeerImplmnt>
template <class Buffers>
P2PeerImp<PeerImplmnt>::P2PeerImp(
    Config const& config,
    Logs& logs,
    std::unique_ptr<stream_type>&& stream_ptr,
    Buffers const& buffers,
    std::shared_ptr<PeerFinder::Slot>&& slot,
    http_response_type&& response,
    Resource::Consumer usage,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    id_t id,
    OverlayImpl_t& overlay)
    : Child<OverlayImpl_t>(overlay)
    , id_(id)
    , sink_(logs.journal("Peer"), makePrefix(id))
    , p_sink_(logs.journal("Protocol"), makePrefix(id))
    , journal_(sink_)
    , p_journal_(p_sink_)
    , stream_ptr_(std::move(stream_ptr))
    , socket_(stream_ptr_->next_layer().socket())
    , stream_(*stream_ptr_)
    , strand_(socket_.get_executor())
    , remote_address_(slot->remote_endpoint())
    , inbound_(false)
    , protocol_(protocol)
    , publicKey_(publicKey)
    , usage_(usage)
    , fee_(Resource::feeLightPeer)
    , slot_(std::move(slot))
    , response_(std::move(response))
    , headers_(response_)
    , compressionEnabled_(
          peerFeatureEnabled(headers_, FEATURE_COMPR, "lz4", config.COMPRESSION)
              ? Compressed::On
              : Compressed::Off)
{
    read_buffer_.commit(boost::asio::buffer_copy(
        read_buffer_.prepare(boost::asio::buffer_size(buffers)), buffers));
    JLOG(journal_.debug()) << "compression enabled "
                           << (compressionEnabled_ == Compressed::On) << " "
                           << id_;
}

template <typename PeerImplmnt>
P2PeerImp<PeerImplmnt>::P2PeerImp(
    Config const& config,
    Logs& logs,
    id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    Resource::Consumer consumer,
    std::unique_ptr<stream_type>&& stream_ptr,
    OverlayImpl_t& overlay)
    : Child<OverlayImpl_t>(overlay)
    , id_(id)
    , sink_(logs.journal("Peer"), makePrefix(id))
    , p_sink_(logs.journal("Protocol"), makePrefix(id))
    , journal_(sink_)
    , p_journal_(p_sink_)
    , stream_ptr_(std::move(stream_ptr))
    , socket_(stream_ptr_->next_layer().socket())
    , stream_(*stream_ptr_)
    , strand_(socket_.get_executor())
    , remote_address_(slot->remote_endpoint())
    , inbound_(true)
    , protocol_(protocol)
    , publicKey_(publicKey)
    , usage_(consumer)
    , fee_(Resource::feeLightPeer)
    , slot_(slot)
    , request_(std::move(request))
    , headers_(request_)
    , compressionEnabled_(
          peerFeatureEnabled(headers_, FEATURE_COMPR, "lz4", config.COMPRESSION)
              ? Compressed::On
              : Compressed::Off)
{
    JLOG(journal_.debug()) << " compression enabled "
                           << (compressionEnabled_ == Compressed::On) << " "
                           << id_;
}

template <typename PeerImplmnt>
P2PeerImp<PeerImplmnt>::~P2PeerImp()
{
}

template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::run()
{
    if (!strand_.running_in_this_thread())
        return post(strand_, std::bind(&P2PeerImp<PeerImplmnt>::run, shared()));

    onEvtRun();

    if (inbound_)
        doAccept();
    else
        doProtocolStart();

    // Anything else that needs to be done with the connection should be
    // done in doProtocolStart
}

//------------------------------------------------------------------------------

template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::send(std::shared_ptr<Message> const& m)
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_, std::bind(&P2PeerImp<PeerImplmnt>::send, shared(), m));
    if (gracefulClose_)
        return;
    if (detaching_)
        return;

    if (static_cast<PeerImplmnt*>(this)->squelched(m))
        return;

    this->overlay_.reportTraffic(
            safe_cast<TrafficCount::category>(m->getCategory()),
            false,
            static_cast<int>(m->getBuffer(compressionEnabled_).size()));

    auto sendq_size = send_queue_.size();

    if (sendq_size < Tuning::targetSendQueue)
    {
        // To detect a peer that does not read from their
        // side of the connection, we expect a peer to have
        // a small senq periodically
        large_sendq_ = 0;
    }
    else if (auto sink = journal_.debug();
             sink && (sendq_size % Tuning::sendQueueLogFreq) == 0)
    {
        std::string const n = name();
        sink << (n.empty() ? remote_address_.to_string() : n)
             << " sendq: " << sendq_size;
    }

    send_queue_.push(m);

    if (sendq_size != 0)
        return;

    boost::asio::async_write(
        stream_,
        boost::asio::buffer(
            send_queue_.front()->getBuffer(compressionEnabled_)),
        bind_executor(
            strand_,
            std::bind(
                &P2PeerImp<PeerImplmnt>::onWriteMessage,
                shared(),
                std::placeholders::_1,
                std::placeholders::_2)));
}

template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::charge(Resource::Charge const& fee)
{
    if ((usage_.charge(fee) == Resource::drop) && usage_.disconnect() &&
        strand_.running_in_this_thread())
    {
        // Sever the connection
        this->overlay_.incPeerDisconnectCharges();
        fail("charge: Resources");
    }
}

//------------------------------------------------------------------------------

template <typename PeerImplmnt>
std::string
P2PeerImp<PeerImplmnt>::getVersion() const
{
    if (inbound_)
        return headers_["User-Agent"].to_string();
    return headers_["Server"].to_string();
}

template <typename PeerImplmnt>
Json::Value
P2PeerImp<PeerImplmnt>::json()
{
    Json::Value ret(Json::objectValue);

    ret[jss::public_key] = toBase58(TokenType::NodePublic, publicKey_);
    ret[jss::address] = remote_address_.to_string();

    if (inbound_)
        ret[jss::inbound] = true;

    if (auto const d = domain(); !d.empty())
        ret[jss::server_domain] = domain();

    if (auto const nid = headers_["Network-ID"].to_string(); !nid.empty())
        ret[jss::network_id] = nid;

    ret[jss::load] = usage_.balance();

    if (auto const version = getVersion(); !version.empty())
        ret[jss::version] = version;

    ret[jss::protocol] = to_string(protocol_);

    {
        std::lock_guard sl(recentLock_);
        if (latency_)
            ret[jss::latency] = static_cast<Json::UInt>(latency_->count());
    }

    ret[jss::metrics] = Json::Value(Json::objectValue);
    ret[jss::metrics][jss::total_bytes_recv] =
        std::to_string(metrics_.recv.total_bytes());
    ret[jss::metrics][jss::total_bytes_sent] =
        std::to_string(metrics_.sent.total_bytes());
    ret[jss::metrics][jss::avg_bps_recv] =
        std::to_string(metrics_.recv.average_bytes());
    ret[jss::metrics][jss::avg_bps_sent] =
        std::to_string(metrics_.sent.average_bytes());

    return ret;
}

//------------------------------------------------------------------------------

template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::close()
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
    {
        onEvtClose();
        detaching_ = true;  // DEPRECATED
        error_code ec;
        socket_.close(ec);
        this->overlay_.incPeerDisconnect();
        if (inbound_)
        {
            JLOG(journal_.debug()) << "Closed";
        }
        else
        {
            JLOG(journal_.info()) << "Closed";
        }
    }
}

template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::fail(std::string const& reason)
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_,
            std::bind(
                (void (P2PeerImp<PeerImplmnt>::*)(std::string const&)) &
                    P2PeerImp<PeerImplmnt>::fail,
                shared(),
                reason));
    if (journal_.active(beast::severities::kWarning) && socket_.is_open())
    {
        std::string const n = name();
        JLOG(journal_.warn()) << (n.empty() ? remote_address_.to_string() : n)
                              << " failed: " << reason;
    }
    close();
}

template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::fail(std::string const& name, error_code ec)
{
    assert(strand_.running_in_this_thread());
    if (socket_.is_open())
    {
        JLOG(journal_.warn())
            << name << " from " << toBase58(TokenType::NodePublic, publicKey_)
            << " at " << remote_address_.to_string() << ": " << ec.message();
    }
    close();
}

template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::gracefulClose()
{
    assert(strand_.running_in_this_thread());
    assert(socket_.is_open());
    assert(!gracefulClose_);
    gracefulClose_ = true;
#if 0
    // Flush messages
while(send_queue_.size() > 1)
    send_queue_.pop_back();
#endif
    if (send_queue_.size() > 0)
        return;
    onEvtGracefulClose();
    stream_.async_shutdown(bind_executor(
        strand_,
        std::bind(
            &P2PeerImp<PeerImplmnt>::onShutdown,
            shared(),
            std::placeholders::_1)));
}

//------------------------------------------------------------------------------

template <typename PeerImplmnt>
std::string
P2PeerImp<PeerImplmnt>::makePrefix(id_t id)
{
    std::stringstream ss;
    ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
    return ss.str();
}

template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::onShutdown(error_code ec)
{
    onEvtShutdown();
    // If we don't get eof then something went wrong
    if (!ec)
    {
        JLOG(journal_.error()) << "onShutdown: expected error condition";
        return close();
    }
    if (ec != boost::asio::error::eof)
        return fail("onShutdown", ec);
    close();
}

//------------------------------------------------------------------------------
template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::doAccept()
{
    assert(read_buffer_.size() == 0);

    JLOG(journal_.debug()) << "doAccept: " << remote_address_;

    auto const sharedValue = makeSharedValue(*stream_ptr_, journal_);

    // This shouldn't fail since we already computed
    // the shared value successfully in OverlayImpl
    if (!sharedValue)
        return fail("makeSharedValue: Unexpected failure");

    JLOG(journal_.info()) << "Protocol: " << to_string(protocol_);
    JLOG(journal_.info()) << "Public Key: "
                          << toBase58(TokenType::NodePublic, publicKey_);

    onEvtAccept();

    this->overlay_.activate(std::static_pointer_cast<PeerImplmnt>(shared()));

    // XXX Set timer: connection is in grace period to be useful.
    // XXX Set timer: connection idle (idle may vary depending on connection
    // type.)

    auto write_buffer = std::make_shared<boost::beast::multi_buffer>();

#if 0 // TBD have to resolve Application dependency issue
    boost::beast::ostream(*write_buffer) << makeResponse(
            !this->overlay_.peerFinder().config().peerPrivate,
            request_,
            this->overlay_.setup().public_ip,
            remote_address_.address(),
            *sharedValue,
            this->overlay_.setup().networkID,
            protocol_,
            app_);
#endif

    // Write the whole buffer and only start protocol when that's done.
    boost::asio::async_write(
        stream_,
        write_buffer->data(),
        boost::asio::transfer_all(),
        bind_executor(
            strand_,
            [this, write_buffer, self = shared()](
                error_code ec, std::size_t bytes_transferred) {
                if (!socket_.is_open())
                    return;
                if (ec == boost::asio::error::operation_aborted)
                    return;
                if (ec)
                    return fail("onWriteResponse", ec);
                if (write_buffer->size() == bytes_transferred)
                    return doProtocolStart();
                return fail("Failed to write header");
            }));
}

template <typename PeerImplmnt>
std::string
P2PeerImp<PeerImplmnt>::name() const
{
    std::shared_lock read_lock{nameMutex_};
    return name_;
}

template <typename PeerImplmnt>
std::string
P2PeerImp<PeerImplmnt>::domain() const
{
    return headers_["Server-Domain"].to_string();
}

//------------------------------------------------------------------------------

// Protocol logic

template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::doProtocolStart()
{
    onReadMessage(error_code(), 0);

    onEvtProtocolStart();
}

// Called repeatedly with protocol message data
template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::onReadMessage(
    error_code ec,
    std::size_t bytes_transferred)
{
    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec == boost::asio::error::eof)
    {
        JLOG(journal_.info()) << "EOF";
        return gracefulClose();
    }
    if (ec)
        return fail("onReadMessage", ec);
    if (auto stream = journal_.trace())
    {
        if (bytes_transferred > 0)
            stream << "onReadMessage: " << bytes_transferred << " bytes";
        else
            stream << "onReadMessage";
    }

    metrics_.recv.add_message(bytes_transferred);

    read_buffer_.commit(bytes_transferred);

    auto hint = Tuning::readBufferBytes;

    while (read_buffer_.size() > 0)
    {
        std::size_t bytes_consumed;
        std::tie(bytes_consumed, ec) = invokeProtocolMessage(
            read_buffer_.data(), static_cast<PeerImplmnt&>(*this), hint);
        if (ec)
            return fail("onReadMessage", ec);
        if (!socket_.is_open())
            return;
        if (gracefulClose_)
            return;
        if (bytes_consumed == 0)
            break;
        read_buffer_.consume(bytes_consumed);
    }

    // Timeout on writes only
    stream_.async_read_some(
        read_buffer_.prepare(std::max(Tuning::readBufferBytes, hint)),
        bind_executor(
            strand_,
            std::bind(
                &P2PeerImp<PeerImplmnt>::onReadMessage,
                shared(),
                std::placeholders::_1,
                std::placeholders::_2)));
}

template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::onWriteMessage(
    error_code ec,
    std::size_t bytes_transferred)
{
    if (!socket_.is_open())
        return;
    if (ec == boost::asio::error::operation_aborted)
        return;
    if (ec)
        return fail("onWriteMessage", ec);
    if (auto stream = journal_.trace())
    {
        if (bytes_transferred > 0)
            stream << "onWriteMessage: " << bytes_transferred << " bytes";
        else
            stream << "onWriteMessage";
    }

    metrics_.sent.add_message(bytes_transferred);

    assert(!send_queue_.empty());
    send_queue_.pop();
    if (!send_queue_.empty())
    {
        // Timeout on writes only
        return boost::asio::async_write(
            stream_,
            boost::asio::buffer(
                send_queue_.front()->getBuffer(compressionEnabled_)),
            bind_executor(
                strand_,
                std::bind(
                    &P2PeerImp<PeerImplmnt>::onWriteMessage,
                    shared(),
                    std::placeholders::_1,
                    std::placeholders::_2)));
    }

    if (gracefulClose_)
    {
        return stream_.async_shutdown(bind_executor(
            strand_,
            std::bind(
                &P2PeerImp<PeerImplmnt>::onShutdown,
                shared(),
                std::placeholders::_1)));
    }
}

template <typename PeerImplmnt>
int
P2PeerImp<PeerImplmnt>::getScore(bool haveItem) const
{
    // Random component of score, used to break ties and avoid
    // overloading the "best" peer
    static const int spRandomMax = 9999;

    // Score for being very likely to have the thing we are
    // look for; should be roughly spRandomMax
    static const int spHaveItem = 10000;

    // Score reduction for each millisecond of latency; should
    // be roughly spRandomMax divided by the maximum reasonable
    // latency
    static const int spLatency = 30;

    // Penalty for unknown latency; should be roughly spRandomMax
    static const int spNoLatency = 8000;

    int score = rand_int(spRandomMax);

    if (haveItem)
        score += spHaveItem;

    boost::optional<std::chrono::milliseconds> latency;
    {
        std::lock_guard sl(recentLock_);
        latency = latency_;
    }

    if (latency)
        score -= latency->count() * spLatency;
    else
        score -= spNoLatency;

    return score;
}

template <typename PeerImplmnt>
bool
P2PeerImp<PeerImplmnt>::isHighLatency() const
{
    std::lock_guard sl(recentLock_);
    return latency_ >= peerHighLatency;
}

template <typename PeerImplmnt>
void
P2PeerImp<PeerImplmnt>::Metrics::add_message(std::uint64_t bytes)
{
    using namespace std::chrono_literals;
    std::unique_lock lock{mutex_};

    totalBytes_ += bytes;
    accumBytes_ += bytes;
    auto const timeElapsed = clock_type::now() - intervalStart_;
    auto const timeElapsedInSecs =
        std::chrono::duration_cast<std::chrono::seconds>(timeElapsed);

    if (timeElapsedInSecs >= 1s)
    {
        auto const avgBytes = accumBytes_ / timeElapsedInSecs.count();
        rollingAvg_.push_back(avgBytes);

        auto const totalBytes =
            std::accumulate(rollingAvg_.begin(), rollingAvg_.end(), 0ull);
        rollingAvgBytes_ = totalBytes / rollingAvg_.size();

        intervalStart_ = clock_type::now();
        accumBytes_ = 0;
    }
}

template <typename PeerImplmnt>
std::uint64_t
P2PeerImp<PeerImplmnt>::Metrics::average_bytes() const
{
    std::shared_lock lock{mutex_};
    return rollingAvgBytes_;
}

template <typename PeerImplmnt>
std::uint64_t
P2PeerImp<PeerImplmnt>::Metrics::total_bytes() const
{
    std::shared_lock lock{mutex_};
    return totalBytes_;
}

}  // namespace ripple

#endif
