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

#ifndef RIPPLE_NET_RPCCALL_H_INCLUDED
#define RIPPLE_NET_RPCCALL_H_INCLUDED

#include <xrpld/core/Config.h>

#include <xrpl/basics/Log.h>
#include <xrpl/json/json_value.h>

#include <boost/asio/io_service.hpp>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ripple {

// This a trusted interface, the user is expected to provide valid input to
// perform valid requests. Error catching and reporting is not a requirement of
// the command line interface.
//
// Improvements to be more strict and to provide better diagnostics are welcome.

/** Processes Ripple RPC calls. */
namespace RPCCall {

int
fromCommandLine(
    Config const& config,
    std::vector<std::string> const& vCmd,
    Logs& logs);

void
fromNetwork(
    boost::asio::io_service& io_service,
    std::string const& strIp,
    std::uint16_t const iPort,
    std::string const& strUsername,
    std::string const& strPassword,
    std::string const& strPath,
    std::string const& strMethod,
    Json::Value const& jvParams,
    bool const bSSL,
    bool quiet,
    Logs& logs,
    std::function<void(Json::Value const& jvInput)> callbackFuncP =
        std::function<void(Json::Value const& jvInput)>(),
    std::unordered_map<std::string, std::string> headers = {});
}  // namespace RPCCall

Json::Value
rpcCmdToJson(
    std::vector<std::string> const& args,
    Json::Value& retParams,
    unsigned int apiVersion,
    beast::Journal j);

/** Internal invocation of RPC client.
 *  Used by both rippled command line as well as rippled unit tests
 */
std::pair<int, Json::Value>
rpcClient(
    std::vector<std::string> const& args,
    Config const& config,
    Logs& logs,
    unsigned int apiVersion,
    std::unordered_map<std::string, std::string> const& headers = {});

}  // namespace ripple

#endif
