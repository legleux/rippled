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

#ifndef RIPPLE_CORE_GRPCSERVER_H_INCLUDED
#define RIPPLE_CORE_GRPCSERVER_H_INCLUDED

#include <xrpld/app/main/Application.h>
#include <xrpld/core/JobQueue.h>
#include <xrpld/net/InfoSub.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/GRPCHandlers.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/detail/Handler.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/proto/org/xrpl/rpc/v1/xrp_ledger.grpc.pb.h>
#include <xrpl/resource/Charge.h>

#include <grpcpp/grpcpp.h>

namespace ripple {

// Interface that CallData implements
class Processor
{
public:
    virtual ~Processor() = default;

    Processor() = default;

    Processor(Processor const&) = delete;

    Processor&
    operator=(Processor const&) = delete;

    // process a request that has arrived. Can only be called once per instance
    virtual void
    process() = 0;

    // create a new instance of this CallData object, with the same type
    //(same template parameters) as original. This is called when a CallData
    // object starts processing a request. Creating a new instance allows the
    // server to handle additional requests while the first is being processed
    virtual std::shared_ptr<Processor>
    clone() = 0;

    // true if this object has finished processing the request. Object will be
    // deleted once this function returns true
    virtual bool
    isFinished() = 0;
};

class GRPCServerImpl final
{
private:
    // CompletionQueue returns events that have occurred, or events that have
    // been cancelled
    std::unique_ptr<grpc::ServerCompletionQueue> cq_;

    std::vector<std::shared_ptr<Processor>> requests_;

    // The gRPC service defined by the .proto files
    org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService service_;

    std::unique_ptr<grpc::Server> server_;

    Application& app_;

    std::string serverAddress_;
    std::uint16_t serverPort_ = 0;

    std::vector<boost::asio::ip::address> secureGatewayIPs_;

    beast::Journal journal_;

    // typedef for function to bind a listener
    // This is always of the form:
    // org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService::Request[RPC NAME]
    template <class Request, class Response>
    using BindListener = std::function<void(
        org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService&,
        grpc::ServerContext*,
        Request*,
        grpc::ServerAsyncResponseWriter<Response>*,
        grpc::CompletionQueue*,
        grpc::ServerCompletionQueue*,
        void*)>;

    // typedef for actual handler (that populates a response)
    // handlers are defined in rpc/GRPCHandlers.h
    template <class Request, class Response>
    using Handler = std::function<std::pair<Response, grpc::Status>(
        RPC::GRPCContext<Request>&)>;
    // This implementation is currently limited to v1 of the API
    static unsigned constexpr apiVersion = 1;

    template <class Request, class Response>
    using Forward = std::function<grpc::Status(
        org::xrpl::rpc::v1::XRPLedgerAPIService::Stub*,
        grpc::ClientContext*,
        Request,
        Response*)>;

public:
    explicit GRPCServerImpl(Application& app);

    GRPCServerImpl(GRPCServerImpl const&) = delete;

    GRPCServerImpl&
    operator=(GRPCServerImpl const&) = delete;

    void
    shutdown();

    // setup the server and listeners
    // returns true if server started successfully
    bool
    start();

    // the main event loop
    void
    handleRpcs();

    // Create a CallData object for each RPC. Return created objects in vector
    std::vector<std::shared_ptr<Processor>>
    setupListeners();

    // Obtaining actually binded endpoint (if port 0 was used for server setup).
    boost::asio::ip::tcp::endpoint
    getEndpoint() const;

private:
    // Class encompasing the state and logic needed to serve a request.
    template <class Request, class Response>
    class CallData
        : public Processor,
          public std::enable_shared_from_this<CallData<Request, Response>>
    {
    private:
        // The means of communication with the gRPC runtime for an asynchronous
        // server.
        org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService& service_;

        // The producer-consumer queue for asynchronous server notifications.
        grpc::ServerCompletionQueue& cq_;

        // Context for the rpc, allowing to tweak aspects of it such as the use
        // of compression, authentication, as well as to send metadata back to
        // the client.
        grpc::ServerContext ctx_;

        // true if finished processing request
        // Note, this variable does not need to be atomic, since it is
        // currently only accessed from one thread. However, isFinished(),
        // which returns the value of this variable, is public facing. In the
        // interest of avoiding future concurrency bugs, we make it atomic.
        std::atomic_bool finished_;

        Application& app_;

        // What we get from the client.
        Request request_;

        // The means to get back to the client.
        grpc::ServerAsyncResponseWriter<Response> responder_;

        // Function that creates a listener for specific request type
        BindListener<Request, Response> bindListener_;

        // Function that processes a request
        Handler<Request, Response> handler_;

        // Function to call to forward to another server
        Forward<Request, Response> forward_;

        // Condition required for this RPC
        RPC::Condition requiredCondition_;

        // Load type for this RPC
        Resource::Charge loadType_;

        std::vector<boost::asio::ip::address> const& secureGatewayIPs_;

    public:
        virtual ~CallData() = default;

        // Take in the "service" instance (in this case representing an
        // asynchronous server) and the completion queue "cq" used for
        // asynchronous communication with the gRPC runtime.
        explicit CallData(
            org::xrpl::rpc::v1::XRPLedgerAPIService::AsyncService& service,
            grpc::ServerCompletionQueue& cq,
            Application& app,
            BindListener<Request, Response> bindListener,
            Handler<Request, Response> handler,
            Forward<Request, Response> forward,
            RPC::Condition requiredCondition,
            Resource::Charge loadType,
            std::vector<boost::asio::ip::address> const& secureGatewayIPs);

        CallData(CallData const&) = delete;

        CallData&
        operator=(CallData const&) = delete;

        virtual void
        process() override;

        virtual bool
        isFinished() override;

        std::shared_ptr<Processor>
        clone() override;

    private:
        // process the request. Called inside the coroutine passed to JobQueue
        void
        process(std::shared_ptr<JobQueue::Coro> coro);

        // return load type of this RPC
        Resource::Charge
        getLoadType();

        // return the Role used for this RPC
        Role
        getRole(bool isUnlimited);

        // register endpoint with ResourceManager and return usage
        Resource::Consumer
        getUsage();

        // Returns the ip of the client
        // Empty optional if there was an error decoding the client ip
        std::optional<boost::asio::ip::address>
        getClientIpAddress();

        // Returns the endpoint of the client.
        // Empty optional if there was an error decoding the client
        // endpoint
        std::optional<boost::asio::ip::tcp::endpoint>
        getClientEndpoint();

        // If the request was proxied through
        // another rippled node, returns the ip of the originating client.
        // Empty optional if request was not proxied or there was an error
        // decoding the client ip
        std::optional<boost::asio::ip::address>
        getProxiedClientIpAddress();

        // If the request was proxied through
        // another rippled node, returns the endpoint of the originating client.
        // Empty optional if request was not proxied or there was an error
        // decoding the client endpoint
        std::optional<boost::asio::ip::tcp::endpoint>
        getProxiedClientEndpoint();

        // Returns the user specified in the request. Empty optional if no user
        // was specified
        std::optional<std::string>
        getUser();

        // Sets is_unlimited in response to value of clientIsUnlimited
        // Does nothing if is_unlimited is not a field of the response
        void
        setIsUnlimited(Response& response, bool isUnlimited);

        // True if the client is exempt from resource controls
        bool
        clientIsUnlimited();

        // True if the request was proxied through another rippled node prior
        // to arriving here
        bool
        wasForwarded();

        // forward request to a p2p node
        void
        forwardToP2p(RPC::GRPCContext<Request>& context);

    };  // CallData

};  // GRPCServerImpl

class GRPCServer
{
public:
    explicit GRPCServer(Application& app) : impl_(app)
    {
    }

    GRPCServer(GRPCServer const&) = delete;

    GRPCServer&
    operator=(GRPCServer const&) = delete;

    bool
    start();

    void
    stop();

    ~GRPCServer();

    boost::asio::ip::tcp::endpoint
    getEndpoint() const;

private:
    GRPCServerImpl impl_;
    std::thread thread_;
    bool running_ = false;
};
}  // namespace ripple
#endif
