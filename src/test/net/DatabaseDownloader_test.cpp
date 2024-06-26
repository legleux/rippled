//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright 2019 Ripple Labs Inc.

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

#include <test/jtx.h>
#include <test/jtx/TrustedPublisherServer.h>
#include <test/unit_test/FileDirGuard.h>
#include <xrpld/net/DatabaseDownloader.h>
#include <boost/filesystem/operations.hpp>
#include <condition_variable>
#include <mutex>

namespace ripple {
namespace test {

#define REPORT_FAILURE(D) reportFailure(D, __FILE__, __LINE__)

class DatabaseDownloader_test : public beast::unit_test::suite
{
    std::shared_ptr<TrustedPublisherServer>
    createServer(jtx::Env& env, bool ssl = true)
    {
        std::vector<TrustedPublisherServer::Validator> list;
        list.push_back(TrustedPublisherServer::randomValidator());
        return make_TrustedPublisherServer(
            env.app().getIOService(),
            list,
            env.timeKeeper().now() + std::chrono::seconds{3600},
            // No future VLs
            {},
            ssl);
    }

    struct DownloadCompleter
    {
        std::mutex m;
        std::condition_variable cv;
        bool called = false;
        boost::filesystem::path dest;

        void
        operator()(boost::filesystem::path dst)
        {
            std::unique_lock<std::mutex> lk(m);
            called = true;
            dest = std::move(dst);
            cv.notify_one();
        };

        bool
        waitComplete()
        {
            std::unique_lock<std::mutex> lk(m);

            auto stat = cv.wait_for(
                lk, std::chrono::seconds(10), [this] { return called; });

            called = false;
            return stat;
        };
    };
    DownloadCompleter cb;

    struct Downloader
    {
        test::StreamSink sink_;
        beast::Journal journal_;
        std::shared_ptr<DatabaseDownloader> ptr_;

        Downloader(jtx::Env& env)
            : journal_{sink_}
            , ptr_{make_DatabaseDownloader(
                  env.app().getIOService(),
                  env.app().config(),
                  journal_)}
        {
        }

        ~Downloader()
        {
            ptr_->stop();
        }

        DatabaseDownloader*
        operator->()
        {
            return ptr_.get();
        }

        DatabaseDownloader const*
        operator->() const
        {
            return ptr_.get();
        }
    };

    void
    reportFailure(Downloader const& dl, char const* file, int line)
    {
        std::stringstream ss;
        ss << "Failed. LOGS:\n"
           << dl.sink_.messages().str()
           << "\nDownloadCompleter failure."
              "\nDatabaseDownloader session active? "
           << std::boolalpha << dl->sessionIsActive()
           << "\nDatabaseDownloader is stopping? " << std::boolalpha
           << dl->isStopping();

        fail(ss.str(), file, line);
    }

    void
    testDownload(bool verify)
    {
        testcase << std::string("Basic download - SSL ") +
                (verify ? "Verify" : "No Verify");

        using namespace jtx;

        ripple::test::detail::FileDirGuard cert{
            *this, "_cert", "ca.pem", TrustedPublisherServer::ca_cert()};

        Env env{*this, envconfig([&cert, &verify](std::unique_ptr<Config> cfg) {
                    if ((cfg->SSL_VERIFY = verify))  // yes, this is assignment
                        cfg->SSL_VERIFY_FILE = cert.file().string();
                    return cfg;
                })};

        Downloader dl{env};

        // create a TrustedPublisherServer as a simple HTTP
        // server to request from. Use the /textfile endpoint
        // to get a simple text file sent as response.
        auto server = createServer(env);
        log << "Downloading DB from " << server->local_endpoint() << std::endl;

        ripple::test::detail::FileDirGuard const data{
            *this, "downloads", "data", "", false, false};
        // initiate the download and wait for the callback
        // to be invoked
        auto stat = dl->download(
            server->local_endpoint().address().to_string(),
            std::to_string(server->local_endpoint().port()),
            "/textfile",
            11,
            data.file(),
            std::function<void(boost::filesystem::path)>{std::ref(cb)});
        if (!BEAST_EXPECT(stat))
        {
            REPORT_FAILURE(dl);
            return;
        }
        if (!BEAST_EXPECT(cb.waitComplete()))
        {
            REPORT_FAILURE(dl);
            return;
        }
        BEAST_EXPECT(cb.dest == data.file());
        if (!BEAST_EXPECT(boost::filesystem::exists(data.file())))
            return;
        BEAST_EXPECT(boost::filesystem::file_size(data.file()) > 0);
    }

    void
    testFailures()
    {
        testcase("Error conditions");

        using namespace jtx;

        Env env{*this};

        {
            // bad hostname
            boost::system::error_code ec;
            boost::asio::ip::tcp::resolver resolver{env.app().getIOService()};
            auto const results = resolver.resolve("badhostname", "443", ec);
            // we require an error in resolving this name in order
            // for this test to pass. Some networks might have DNS hijacking
            // that prevent NXDOMAIN, in which case the failure is not
            // possible, so we skip the test.
            if (ec)
            {
                Downloader dl{env};
                ripple::test::detail::FileDirGuard const datafile{
                    *this, "downloads", "data", "", false, false};
                BEAST_EXPECT(dl->download(
                    "badhostname",
                    "443",
                    "",
                    11,
                    datafile.file(),
                    std::function<void(boost::filesystem::path)>{
                        std::ref(cb)}));
                if (!BEAST_EXPECT(cb.waitComplete()))
                {
                    REPORT_FAILURE(dl);
                }
                BEAST_EXPECT(!boost::filesystem::exists(datafile.file()));
                BEAST_EXPECTS(
                    dl.sink_.messages().str().find("async_resolve") !=
                        std::string::npos,
                    dl.sink_.messages().str());
            }
        }
        {
            // can't connect
            Downloader dl{env};
            ripple::test::detail::FileDirGuard const datafile{
                *this, "downloads", "data", "", false, false};
            auto server = createServer(env);
            auto host = server->local_endpoint().address().to_string();
            auto port = std::to_string(server->local_endpoint().port());
            log << "Downloading DB from " << server->local_endpoint()
                << std::endl;
            server->stop();
            BEAST_EXPECT(dl->download(
                host,
                port,
                "",
                11,
                datafile.file(),
                std::function<void(boost::filesystem::path)>{std::ref(cb)}));
            if (!BEAST_EXPECT(cb.waitComplete()))
            {
                REPORT_FAILURE(dl);
            }
            BEAST_EXPECT(!boost::filesystem::exists(datafile.file()));
            BEAST_EXPECTS(
                dl.sink_.messages().str().find("async_connect") !=
                    std::string::npos,
                dl.sink_.messages().str());
        }
        {
            // not ssl (failed handlshake)
            Downloader dl{env};
            ripple::test::detail::FileDirGuard const datafile{
                *this, "downloads", "data", "", false, false};
            auto server = createServer(env, false);
            log << "Downloading DB from " << server->local_endpoint()
                << std::endl;
            BEAST_EXPECT(dl->download(
                server->local_endpoint().address().to_string(),
                std::to_string(server->local_endpoint().port()),
                "",
                11,
                datafile.file(),
                std::function<void(boost::filesystem::path)>{std::ref(cb)}));
            if (!BEAST_EXPECT(cb.waitComplete()))
            {
                REPORT_FAILURE(dl);
            }
            BEAST_EXPECT(!boost::filesystem::exists(datafile.file()));
            BEAST_EXPECTS(
                dl.sink_.messages().str().find("async_handshake") !=
                    std::string::npos,
                dl.sink_.messages().str());
        }
        {
            // huge file (content length)
            Downloader dl{env};
            ripple::test::detail::FileDirGuard const datafile{
                *this, "downloads", "data", "", false, false};
            auto server = createServer(env);
            log << "Downloading DB from " << server->local_endpoint()
                << std::endl;
            BEAST_EXPECT(dl->download(
                server->local_endpoint().address().to_string(),
                std::to_string(server->local_endpoint().port()),
                "/textfile/huge",
                11,
                datafile.file(),
                std::function<void(boost::filesystem::path)>{std::ref(cb)}));
            if (!BEAST_EXPECT(cb.waitComplete()))
            {
                REPORT_FAILURE(dl);
            }
            BEAST_EXPECT(!boost::filesystem::exists(datafile.file()));
            BEAST_EXPECTS(
                dl.sink_.messages().str().find("Insufficient disk space") !=
                    std::string::npos,
                dl.sink_.messages().str());
        }
    }

public:
    void
    run() override
    {
        testDownload(true);
        testDownload(false);
        testFailures();
    }
};

#undef REPORT_FAILURE

BEAST_DEFINE_TESTSUITE(DatabaseDownloader, net, ripple);
}  // namespace test
}  // namespace ripple
