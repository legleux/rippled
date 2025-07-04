//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <xrpld/app/ledger/AcceptedLedger.h>
#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/nodestore/Database.h>
#include <xrpld/rpc/Context.h>

#include <xrpl/basics/UptimeClock.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

static void
textTime(
    std::string& text,
    UptimeClock::time_point& seconds,
    char const* unitName,
    std::chrono::seconds unitVal)
{
    auto i = seconds.time_since_epoch() / unitVal;

    if (i == 0)
        return;

    seconds -= unitVal * i;

    if (!text.empty())
        text += ", ";

    text += std::to_string(i);
    text += " ";
    text += unitName;

    if (i > 1)
        text += "s";
}

Json::Value
getCountsJson(Application& app, int minObjectCount)
{
    auto objectCounts = CountedObjects::getInstance().getCounts(minObjectCount);

    Json::Value ret(Json::objectValue);

    for (auto const& [k, v] : objectCounts)
    {
        ret[k] = v;
    }

    if (app.config().useTxTables())
    {
        auto const db =
            dynamic_cast<SQLiteDatabase*>(&app.getRelationalDatabase());

        if (!db)
            Throw<std::runtime_error>("Failed to get relational database");

        auto dbKB = db->getKBUsedAll();

        if (dbKB > 0)
            ret[jss::dbKBTotal] = dbKB;

        dbKB = db->getKBUsedLedger();

        if (dbKB > 0)
            ret[jss::dbKBLedger] = dbKB;

        dbKB = db->getKBUsedTransaction();

        if (dbKB > 0)
            ret[jss::dbKBTransaction] = dbKB;

        {
            std::size_t c = app.getOPs().getLocalTxCount();
            if (c > 0)
                ret[jss::local_txs] = static_cast<Json::UInt>(c);
        }
    }

    ret[jss::write_load] = app.getNodeStore().getWriteLoad();

    ret[jss::historical_perminute] =
        static_cast<int>(app.getInboundLedgers().fetchRate());
    ret[jss::SLE_hit_rate] = app.cachedSLEs().rate();
    ret[jss::ledger_hit_rate] = app.getLedgerMaster().getCacheHitRate();
    ret[jss::AL_size] = Json::UInt(app.getAcceptedLedgerCache().size());
    ret[jss::AL_hit_rate] = app.getAcceptedLedgerCache().getHitRate();

    ret[jss::fullbelow_size] =
        static_cast<int>(app.getNodeFamily().getFullBelowCache()->size());
    ret[jss::treenode_cache_size] =
        app.getNodeFamily().getTreeNodeCache()->getCacheSize();
    ret[jss::treenode_track_size] =
        app.getNodeFamily().getTreeNodeCache()->getTrackSize();

    std::string uptime;
    auto s = UptimeClock::now();
    using namespace std::chrono_literals;
    textTime(uptime, s, "year", 365 * 24h);
    textTime(uptime, s, "day", 24h);
    textTime(uptime, s, "hour", 1h);
    textTime(uptime, s, "minute", 1min);
    textTime(uptime, s, "second", 1s);
    ret[jss::uptime] = uptime;

    app.getNodeStore().getCountsJson(ret);

    return ret;
}

// {
//   min_count: <number>  // optional, defaults to 10
// }
Json::Value
doGetCounts(RPC::JsonContext& context)
{
    int minCount = 10;

    if (context.params.isMember(jss::min_count))
        minCount = context.params[jss::min_count].asUInt();

    return getCountsJson(context.app, minCount);
}

}  // namespace ripple
