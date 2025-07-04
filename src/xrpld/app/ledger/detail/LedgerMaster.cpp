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

#include <xrpld/app/consensus/RCLValidations.h>
#include <xrpld/app/ledger/Ledger.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerReplayer.h>
#include <xrpld/app/ledger/OpenLedger.h>
#include <xrpld/app/ledger/OrderBookDB.h>
#include <xrpld/app/ledger/PendingSaves.h>
#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/AmendmentTable.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/app/misc/NetworkOPs.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/app/misc/Transaction.h>
#include <xrpld/app/misc/TxQ.h>
#include <xrpld/app/misc/ValidatorList.h>
#include <xrpld/app/paths/PathRequests.h>
#include <xrpld/app/rdb/RelationalDatabase.h>
#include <xrpld/core/TimeKeeper.h>
#include <xrpld/overlay/Overlay.h>
#include <xrpld/overlay/Peer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/basics/MathUtilities.h>
#include <xrpl/basics/UptimeClock.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/basics/scope.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/BuildInfo.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/resource/Fees.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <vector>

namespace ripple {

// Don't catch up more than 100 ledgers (cannot exceed 256)
static constexpr int MAX_LEDGER_GAP{100};

// Don't acquire history if ledger is too old
static constexpr std::chrono::minutes MAX_LEDGER_AGE_ACQUIRE{1};

// Don't acquire history if write load is too high
static constexpr int MAX_WRITE_LOAD_ACQUIRE{8192};

// Helper function for LedgerMaster::doAdvance()
// Return true if candidateLedger should be fetched from the network.
static bool
shouldAcquire(
    std::uint32_t const currentLedger,
    std::uint32_t const ledgerHistory,
    std::optional<LedgerIndex> const minimumOnline,
    std::uint32_t const candidateLedger,
    beast::Journal j)
{
    bool const ret = [&]() {
        // Fetch ledger if it may be the current ledger
        if (candidateLedger >= currentLedger)
            return true;

        // Or if it is within our configured history range:
        if (currentLedger - candidateLedger <= ledgerHistory)
            return true;

        // Or if greater than or equal to a specific minimum ledger.
        // Do nothing if the minimum ledger to keep online is unknown.
        return minimumOnline.has_value() && candidateLedger >= *minimumOnline;
    }();

    JLOG(j.trace()) << "Missing ledger " << candidateLedger
                    << (ret ? " should" : " should NOT") << " be acquired";
    return ret;
}

LedgerMaster::LedgerMaster(
    Application& app,
    Stopwatch& stopwatch,
    beast::insight::Collector::ptr const& collector,
    beast::Journal journal)
    : app_(app)
    , m_journal(journal)
    , mLedgerHistory(collector, app)
    , standalone_(app_.config().standalone())
    , fetch_depth_(
          app_.getSHAMapStore().clampFetchDepth(app_.config().FETCH_DEPTH))
    , ledger_history_(app_.config().LEDGER_HISTORY)
    , ledger_fetch_size_(app_.config().getValueFor(SizedItem::ledgerFetch))
    , fetch_packs_(
          "FetchPack",
          65536,
          std::chrono::seconds{45},
          stopwatch,
          app_.journal("TaggedCache"))
    , m_stats(std::bind(&LedgerMaster::collect_metrics, this), collector)
{
}

LedgerIndex
LedgerMaster::getCurrentLedgerIndex()
{
    return app_.openLedger().current()->info().seq;
}

LedgerIndex
LedgerMaster::getValidLedgerIndex()
{
    return mValidLedgerSeq;
}

bool
LedgerMaster::isCompatible(
    ReadView const& view,
    beast::Journal::Stream s,
    char const* reason)
{
    auto validLedger = getValidatedLedger();

    if (validLedger && !areCompatible(*validLedger, view, s, reason))
    {
        return false;
    }

    {
        std::lock_guard sl(m_mutex);

        if ((mLastValidLedger.second != 0) &&
            !areCompatible(
                mLastValidLedger.first,
                mLastValidLedger.second,
                view,
                s,
                reason))
        {
            return false;
        }
    }

    return true;
}

std::chrono::seconds
LedgerMaster::getPublishedLedgerAge()
{
    using namespace std::chrono_literals;
    std::chrono::seconds pubClose{mPubLedgerClose.load()};
    if (pubClose == 0s)
    {
        JLOG(m_journal.debug()) << "No published ledger";
        return weeks{2};
    }

    std::chrono::seconds ret = app_.timeKeeper().closeTime().time_since_epoch();
    ret -= pubClose;
    ret = (ret > 0s) ? ret : 0s;
    static std::chrono::seconds lastRet = -1s;

    if (ret != lastRet)
    {
        JLOG(m_journal.trace()) << "Published ledger age is " << ret.count();
        lastRet = ret;
    }
    return ret;
}

std::chrono::seconds
LedgerMaster::getValidatedLedgerAge()
{
    using namespace std::chrono_literals;

    std::chrono::seconds valClose{mValidLedgerSign.load()};
    if (valClose == 0s)
    {
        JLOG(m_journal.debug()) << "No validated ledger";
        return weeks{2};
    }

    std::chrono::seconds ret = app_.timeKeeper().closeTime().time_since_epoch();
    ret -= valClose;
    ret = (ret > 0s) ? ret : 0s;
    static std::chrono::seconds lastRet = -1s;

    if (ret != lastRet)
    {
        JLOG(m_journal.trace()) << "Validated ledger age is " << ret.count();
        lastRet = ret;
    }
    return ret;
}

bool
LedgerMaster::isCaughtUp(std::string& reason)
{
    using namespace std::chrono_literals;

    if (getPublishedLedgerAge() > 3min)
    {
        reason = "No recently-published ledger";
        return false;
    }
    std::uint32_t validClose = mValidLedgerSign.load();
    std::uint32_t pubClose = mPubLedgerClose.load();
    if (!validClose || !pubClose)
    {
        reason = "No published ledger";
        return false;
    }
    if (validClose > (pubClose + 90))
    {
        reason = "Published ledger lags validated ledger";
        return false;
    }
    return true;
}

void
LedgerMaster::setValidLedger(std::shared_ptr<Ledger const> const& l)
{
    std::vector<NetClock::time_point> times;
    std::optional<uint256> consensusHash;

    if (!standalone_)
    {
        auto validations = app_.validators().negativeUNLFilter(
            app_.getValidations().getTrustedForLedger(
                l->info().hash, l->info().seq));
        times.reserve(validations.size());
        for (auto const& val : validations)
            times.push_back(val->getSignTime());

        if (!validations.empty())
            consensusHash = validations.front()->getConsensusHash();
    }

    NetClock::time_point signTime;

    if (!times.empty() && times.size() >= app_.validators().quorum())
    {
        // Calculate the sample median
        std::sort(times.begin(), times.end());
        auto const t0 = times[(times.size() - 1) / 2];
        auto const t1 = times[times.size() / 2];
        signTime = t0 + (t1 - t0) / 2;
    }
    else
    {
        signTime = l->info().closeTime;
    }

    mValidLedger.set(l);
    mValidLedgerSign = signTime.time_since_epoch().count();
    XRPL_ASSERT(
        mValidLedgerSeq || !app_.getMaxDisallowedLedger() ||
            l->info().seq + max_ledger_difference_ >
                app_.getMaxDisallowedLedger(),
        "ripple::LedgerMaster::setValidLedger : valid ledger sequence");
    (void)max_ledger_difference_;
    mValidLedgerSeq = l->info().seq;

    app_.getOPs().updateLocalTx(*l);
    app_.getSHAMapStore().onLedgerClosed(getValidatedLedger());
    mLedgerHistory.validatedLedger(l, consensusHash);
    app_.getAmendmentTable().doValidatedLedger(l);
    if (!app_.getOPs().isBlocked())
    {
        if (app_.getAmendmentTable().hasUnsupportedEnabled())
        {
            JLOG(m_journal.error()) << "One or more unsupported amendments "
                                       "activated: server blocked.";
            app_.getOPs().setAmendmentBlocked();
        }
        else if (!app_.getOPs().isAmendmentWarned() || l->isFlagLedger())
        {
            // Amendments can lose majority, so re-check periodically (every
            // flag ledger), and clear the flag if appropriate. If an unknown
            // amendment gains majority log a warning as soon as it's
            // discovered, then again every flag ledger until the operator
            // upgrades, the amendment loses majority, or the amendment goes
            // live and the node gets blocked. Unlike being amendment blocked,
            // this message may be logged more than once per session, because
            // the node will otherwise function normally, and this gives
            // operators an opportunity to see and resolve the warning.
            if (auto const first =
                    app_.getAmendmentTable().firstUnsupportedExpected())
            {
                JLOG(m_journal.error()) << "One or more unsupported amendments "
                                           "reached majority. Upgrade before "
                                        << to_string(*first)
                                        << " to prevent your server from "
                                           "becoming amendment blocked.";
                app_.getOPs().setAmendmentWarned();
            }
            else
                app_.getOPs().clearAmendmentWarned();
        }
    }
}

void
LedgerMaster::setPubLedger(std::shared_ptr<Ledger const> const& l)
{
    mPubLedger = l;
    mPubLedgerClose = l->info().closeTime.time_since_epoch().count();
    mPubLedgerSeq = l->info().seq;
}

void
LedgerMaster::addHeldTransaction(
    std::shared_ptr<Transaction> const& transaction)
{
    std::lock_guard ml(m_mutex);
    mHeldTransactions.insert(transaction->getSTransaction());
}

// Validate a ledger's close time and sequence number if we're considering
// jumping to that ledger. This helps defend against some rare hostile or
// diverged majority scenarios.
bool
LedgerMaster::canBeCurrent(std::shared_ptr<Ledger const> const& ledger)
{
    XRPL_ASSERT(ledger, "ripple::LedgerMaster::canBeCurrent : non-null input");

    // Never jump to a candidate ledger that precedes our
    // last validated ledger

    auto validLedger = getValidatedLedger();
    if (validLedger && (ledger->info().seq < validLedger->info().seq))
    {
        JLOG(m_journal.trace())
            << "Candidate for current ledger has low seq " << ledger->info().seq
            << " < " << validLedger->info().seq;
        return false;
    }

    // Ensure this ledger's parent close time is within five minutes of
    // our current time. If we already have a known fully-valid ledger
    // we perform this check. Otherwise, we only do it if we've built a
    // few ledgers as our clock can be off when we first start up

    auto closeTime = app_.timeKeeper().closeTime();
    auto ledgerClose = ledger->info().parentCloseTime;

    using namespace std::chrono_literals;
    if ((validLedger || (ledger->info().seq > 10)) &&
        ((std::max(closeTime, ledgerClose) - std::min(closeTime, ledgerClose)) >
         5min))
    {
        JLOG(m_journal.warn())
            << "Candidate for current ledger has close time "
            << to_string(ledgerClose) << " at network time "
            << to_string(closeTime) << " seq " << ledger->info().seq;
        return false;
    }

    if (validLedger)
    {
        // Sequence number must not be too high. We allow ten ledgers
        // for time inaccuracies plus a maximum run rate of one ledger
        // every two seconds. The goal is to prevent a malicious ledger
        // from increasing our sequence unreasonably high

        LedgerIndex maxSeq = validLedger->info().seq + 10;

        if (closeTime > validLedger->info().parentCloseTime)
            maxSeq += std::chrono::duration_cast<std::chrono::seconds>(
                          closeTime - validLedger->info().parentCloseTime)
                          .count() /
                2;

        if (ledger->info().seq > maxSeq)
        {
            JLOG(m_journal.warn())
                << "Candidate for current ledger has high seq "
                << ledger->info().seq << " > " << maxSeq;
            return false;
        }

        JLOG(m_journal.trace())
            << "Acceptable seq range: " << validLedger->info().seq
            << " <= " << ledger->info().seq << " <= " << maxSeq;
    }

    return true;
}

void
LedgerMaster::switchLCL(std::shared_ptr<Ledger const> const& lastClosed)
{
    XRPL_ASSERT(lastClosed, "ripple::LedgerMaster::switchLCL : non-null input");
    if (!lastClosed->isImmutable())
        LogicError("mutable ledger in switchLCL");

    if (lastClosed->open())
        LogicError("The new last closed ledger is open!");

    {
        std::lock_guard ml(m_mutex);
        mClosedLedger.set(lastClosed);
    }

    if (standalone_)
    {
        setFullLedger(lastClosed, true, false);
        tryAdvance();
    }
    else
    {
        checkAccept(lastClosed);
    }
}

bool
LedgerMaster::fixIndex(LedgerIndex ledgerIndex, LedgerHash const& ledgerHash)
{
    return mLedgerHistory.fixIndex(ledgerIndex, ledgerHash);
}

bool
LedgerMaster::storeLedger(std::shared_ptr<Ledger const> ledger)
{
    bool validated = ledger->info().validated;
    // Returns true if we already had the ledger
    return mLedgerHistory.insert(std::move(ledger), validated);
}

/** Apply held transactions to the open ledger
    This is normally called as we close the ledger.
    The open ledger remains open to handle new transactions
    until a new open ledger is built.
*/
void
LedgerMaster::applyHeldTransactions()
{
    CanonicalTXSet const set = [this]() {
        std::lock_guard sl(m_mutex);
        // VFALCO NOTE The hash for an open ledger is undefined so we use
        // something that is a reasonable substitute.
        CanonicalTXSet set(app_.openLedger().current()->info().parentHash);
        std::swap(mHeldTransactions, set);
        return set;
    }();

    if (!set.empty())
        app_.getOPs().processTransactionSet(set);
}

std::shared_ptr<STTx const>
LedgerMaster::popAcctTransaction(std::shared_ptr<STTx const> const& tx)
{
    std::lock_guard sl(m_mutex);

    return mHeldTransactions.popAcctTransaction(tx);
}

void
LedgerMaster::setBuildingLedger(LedgerIndex i)
{
    mBuildingLedgerSeq.store(i);
}

bool
LedgerMaster::haveLedger(std::uint32_t seq)
{
    std::lock_guard sl(mCompleteLock);
    return boost::icl::contains(mCompleteLedgers, seq);
}

void
LedgerMaster::clearLedger(std::uint32_t seq)
{
    std::lock_guard sl(mCompleteLock);
    mCompleteLedgers.erase(seq);
}

bool
LedgerMaster::isValidated(ReadView const& ledger)
{
    if (ledger.open())
        return false;

    if (ledger.info().validated)
        return true;

    auto const seq = ledger.info().seq;
    try
    {
        // Use the skip list in the last validated ledger to see if ledger
        // comes before the last validated ledger (and thus has been
        // validated).
        auto const hash = walkHashBySeq(seq, InboundLedger::Reason::GENERIC);

        if (!hash || ledger.info().hash != *hash)
        {
            // This ledger's hash is not the hash of the validated ledger
            if (hash)
            {
                XRPL_ASSERT(
                    hash->isNonZero(),
                    "ripple::LedgerMaster::isValidated : nonzero hash");
                uint256 valHash =
                    app_.getRelationalDatabase().getHashByIndex(seq);
                if (valHash == ledger.info().hash)
                {
                    // SQL database doesn't match ledger chain
                    clearLedger(seq);
                }
            }
            return false;
        }
    }
    catch (SHAMapMissingNode const& mn)
    {
        JLOG(m_journal.warn()) << "Ledger #" << seq << ": " << mn.what();
        return false;
    }

    // Mark ledger as validated to save time if we see it again.
    ledger.info().validated = true;
    return true;
}

// returns Ledgers we have all the nodes for
bool
LedgerMaster::getFullValidatedRange(
    std::uint32_t& minVal,
    std::uint32_t& maxVal)
{
    // Validated ledger is likely not stored in the DB yet so we use the
    // published ledger which is.
    maxVal = mPubLedgerSeq.load();

    if (!maxVal)
        return false;

    std::optional<std::uint32_t> maybeMin;
    {
        std::lock_guard sl(mCompleteLock);
        maybeMin = prevMissing(mCompleteLedgers, maxVal);
    }

    if (maybeMin == std::nullopt)
        minVal = maxVal;
    else
        minVal = 1 + *maybeMin;

    return true;
}

// Returns Ledgers we have all the nodes for and are indexed
bool
LedgerMaster::getValidatedRange(std::uint32_t& minVal, std::uint32_t& maxVal)
{
    if (!getFullValidatedRange(minVal, maxVal))
        return false;

    // Remove from the validated range any ledger sequences that may not be
    // fully updated in the database yet

    auto const pendingSaves = app_.pendingSaves().getSnapshot();

    if (!pendingSaves.empty() && ((minVal != 0) || (maxVal != 0)))
    {
        // Ensure we shrink the tips as much as possible. If we have 7-9 and
        // 8,9 are invalid, we don't want to see the 8 and shrink to just 9
        // because then we'll have nothing when we could have 7.
        while (pendingSaves.count(maxVal) > 0)
            --maxVal;
        while (pendingSaves.count(minVal) > 0)
            ++minVal;

        // Best effort for remaining exclusions
        for (auto v : pendingSaves)
        {
            if ((v.first >= minVal) && (v.first <= maxVal))
            {
                if (v.first > ((minVal + maxVal) / 2))
                    maxVal = v.first - 1;
                else
                    minVal = v.first + 1;
            }
        }

        if (minVal > maxVal)
            minVal = maxVal = 0;
    }

    return true;
}

// Get the earliest ledger we will let peers fetch
std::uint32_t
LedgerMaster::getEarliestFetch()
{
    // The earliest ledger we will let people fetch is ledger zero,
    // unless that creates a larger range than allowed
    std::uint32_t e = getClosedLedger()->info().seq;

    if (e > fetch_depth_)
        e -= fetch_depth_;
    else
        e = 0;
    return e;
}

void
LedgerMaster::tryFill(std::shared_ptr<Ledger const> ledger)
{
    std::uint32_t seq = ledger->info().seq;
    uint256 prevHash = ledger->info().parentHash;

    std::map<std::uint32_t, LedgerHashPair> ledgerHashes;

    std::uint32_t minHas = seq;
    std::uint32_t maxHas = seq;

    NodeStore::Database& nodeStore{app_.getNodeStore()};
    while (!app_.getJobQueue().isStopping() && seq > 0)
    {
        {
            std::lock_guard ml(m_mutex);
            minHas = seq;
            --seq;

            if (haveLedger(seq))
                break;
        }

        auto it(ledgerHashes.find(seq));

        if (it == ledgerHashes.end())
        {
            if (app_.isStopping())
                return;

            {
                std::lock_guard ml(mCompleteLock);
                mCompleteLedgers.insert(range(minHas, maxHas));
            }
            maxHas = minHas;
            ledgerHashes = app_.getRelationalDatabase().getHashesByIndex(
                (seq < 500) ? 0 : (seq - 499), seq);
            it = ledgerHashes.find(seq);

            if (it == ledgerHashes.end())
                break;

            if (!nodeStore.fetchNodeObject(
                    ledgerHashes.begin()->second.ledgerHash,
                    ledgerHashes.begin()->first))
            {
                // The ledger is not backed by the node store
                JLOG(m_journal.warn()) << "SQL DB ledger sequence " << seq
                                       << " mismatches node store";
                break;
            }
        }

        if (it->second.ledgerHash != prevHash)
            break;

        prevHash = it->second.parentHash;
    }

    {
        std::lock_guard ml(mCompleteLock);
        mCompleteLedgers.insert(range(minHas, maxHas));
    }
    {
        std::lock_guard ml(m_mutex);
        mFillInProgress = 0;
        tryAdvance();
    }
}

/** Request a fetch pack to get to the specified ledger
 */
void
LedgerMaster::getFetchPack(LedgerIndex missing, InboundLedger::Reason reason)
{
    LedgerIndex const ledgerIndex = missing + 1;

    auto const haveHash{getLedgerHashForHistory(ledgerIndex, reason)};
    if (!haveHash || haveHash->isZero())
    {
        JLOG(m_journal.error())
            << "No hash for fetch pack. Missing Index " << missing;
        return;
    }

    // Select target Peer based on highest score.  The score is randomized
    // but biased in favor of Peers with low latency.
    std::shared_ptr<Peer> target;
    {
        int maxScore = 0;
        auto peerList = app_.overlay().getActivePeers();
        for (auto const& peer : peerList)
        {
            if (peer->hasRange(missing, missing + 1))
            {
                int score = peer->getScore(true);
                if (!target || (score > maxScore))
                {
                    target = peer;
                    maxScore = score;
                }
            }
        }
    }

    if (target)
    {
        protocol::TMGetObjectByHash tmBH;
        tmBH.set_query(true);
        tmBH.set_type(protocol::TMGetObjectByHash::otFETCH_PACK);
        tmBH.set_ledgerhash(haveHash->begin(), 32);
        auto packet = std::make_shared<Message>(tmBH, protocol::mtGET_OBJECTS);

        target->send(packet);
        JLOG(m_journal.trace()) << "Requested fetch pack for " << missing;
    }
    else
        JLOG(m_journal.debug()) << "No peer for fetch pack";
}

void
LedgerMaster::fixMismatch(ReadView const& ledger)
{
    int invalidate = 0;
    std::optional<uint256> hash;

    for (std::uint32_t lSeq = ledger.info().seq - 1; lSeq > 0; --lSeq)
    {
        if (haveLedger(lSeq))
        {
            try
            {
                hash = hashOfSeq(ledger, lSeq, m_journal);
            }
            catch (std::exception const& ex)
            {
                JLOG(m_journal.warn())
                    << "fixMismatch encounters partial ledger. Exception: "
                    << ex.what();
                clearLedger(lSeq);
                return;
            }

            if (hash)
            {
                // try to close the seam
                auto otherLedger = getLedgerBySeq(lSeq);

                if (otherLedger && (otherLedger->info().hash == *hash))
                {
                    // we closed the seam
                    if (invalidate != 0)
                    {
                        JLOG(m_journal.warn())
                            << "Match at " << lSeq << ", " << invalidate
                            << " prior ledgers invalidated";
                    }

                    return;
                }
            }

            clearLedger(lSeq);
            ++invalidate;
        }
    }

    // all prior ledgers invalidated
    if (invalidate != 0)
    {
        JLOG(m_journal.warn())
            << "All " << invalidate << " prior ledgers invalidated";
    }
}

void
LedgerMaster::setFullLedger(
    std::shared_ptr<Ledger const> const& ledger,
    bool isSynchronous,
    bool isCurrent)
{
    // A new ledger has been accepted as part of the trusted chain
    JLOG(m_journal.debug()) << "Ledger " << ledger->info().seq
                            << " accepted :" << ledger->info().hash;
    XRPL_ASSERT(
        ledger->stateMap().getHash().isNonZero(),
        "ripple::LedgerMaster::setFullLedger : nonzero ledger state hash");

    ledger->setValidated();
    ledger->setFull();

    if (isCurrent)
        mLedgerHistory.insert(ledger, true);

    {
        // Check the SQL database's entry for the sequence before this
        // ledger, if it's not this ledger's parent, invalidate it
        uint256 prevHash =
            app_.getRelationalDatabase().getHashByIndex(ledger->info().seq - 1);
        if (prevHash.isNonZero() && prevHash != ledger->info().parentHash)
            clearLedger(ledger->info().seq - 1);
    }

    pendSaveValidated(app_, ledger, isSynchronous, isCurrent);

    {
        std::lock_guard ml(mCompleteLock);
        mCompleteLedgers.insert(ledger->info().seq);
    }

    {
        std::lock_guard ml(m_mutex);

        if (ledger->info().seq > mValidLedgerSeq)
            setValidLedger(ledger);
        if (!mPubLedger)
        {
            setPubLedger(ledger);
            app_.getOrderBookDB().setup(ledger);
        }

        if (ledger->info().seq != 0 && haveLedger(ledger->info().seq - 1))
        {
            // we think we have the previous ledger, double check
            auto prevLedger = getLedgerBySeq(ledger->info().seq - 1);

            if (!prevLedger ||
                (prevLedger->info().hash != ledger->info().parentHash))
            {
                JLOG(m_journal.warn())
                    << "Acquired ledger invalidates previous ledger: "
                    << (prevLedger ? "hashMismatch" : "missingLedger");
                fixMismatch(*ledger);
            }
        }
    }
}

void
LedgerMaster::failedSave(std::uint32_t seq, uint256 const& hash)
{
    clearLedger(seq);
    app_.getInboundLedgers().acquire(hash, seq, InboundLedger::Reason::GENERIC);
}

// Check if the specified ledger can become the new last fully-validated
// ledger.
void
LedgerMaster::checkAccept(uint256 const& hash, std::uint32_t seq)
{
    std::size_t valCount = 0;

    if (seq != 0)
    {
        // Ledger is too old
        if (seq < mValidLedgerSeq)
            return;

        auto validations = app_.validators().negativeUNLFilter(
            app_.getValidations().getTrustedForLedger(hash, seq));
        valCount = validations.size();
        if (valCount >= app_.validators().quorum())
        {
            std::lock_guard ml(m_mutex);
            if (seq > mLastValidLedger.second)
                mLastValidLedger = std::make_pair(hash, seq);
        }

        if (seq == mValidLedgerSeq)
            return;

        // Ledger could match the ledger we're already building
        if (seq == mBuildingLedgerSeq)
            return;
    }

    auto ledger = mLedgerHistory.getLedgerByHash(hash);

    if (!ledger)
    {
        if ((seq != 0) && (getValidLedgerIndex() == 0))
        {
            // Set peers converged early if we can
            if (valCount >= app_.validators().quorum())
                app_.overlay().checkTracking(seq);
        }

        // FIXME: We may not want to fetch a ledger with just one
        // trusted validation
        ledger = app_.getInboundLedgers().acquire(
            hash, seq, InboundLedger::Reason::GENERIC);
    }

    if (ledger)
        checkAccept(ledger);
}

/**
 * Determines how many validations are needed to fully validate a ledger
 *
 * @return Number of validations needed
 */
std::size_t
LedgerMaster::getNeededValidations()
{
    return standalone_ ? 0 : app_.validators().quorum();
}

void
LedgerMaster::checkAccept(std::shared_ptr<Ledger const> const& ledger)
{
    // Can we accept this ledger as our new last fully-validated ledger

    if (!canBeCurrent(ledger))
        return;

    // Can we advance the last fully-validated ledger? If so, can we
    // publish?
    std::lock_guard ml(m_mutex);

    if (ledger->info().seq <= mValidLedgerSeq)
        return;

    auto const minVal = getNeededValidations();
    auto validations = app_.validators().negativeUNLFilter(
        app_.getValidations().getTrustedForLedger(
            ledger->info().hash, ledger->info().seq));
    auto const tvc = validations.size();
    if (tvc < minVal)  // nothing we can do
    {
        JLOG(m_journal.trace())
            << "Only " << tvc << " validations for " << ledger->info().hash;
        return;
    }

    JLOG(m_journal.info()) << "Advancing accepted ledger to "
                           << ledger->info().seq << " with >= " << minVal
                           << " validations";

    ledger->setValidated();
    ledger->setFull();
    setValidLedger(ledger);
    if (!mPubLedger)
    {
        pendSaveValidated(app_, ledger, true, true);
        setPubLedger(ledger);
        app_.getOrderBookDB().setup(ledger);
    }

    std::uint32_t const base = app_.getFeeTrack().getLoadBase();
    auto fees = app_.getValidations().fees(ledger->info().hash, base);
    {
        auto fees2 =
            app_.getValidations().fees(ledger->info().parentHash, base);
        fees.reserve(fees.size() + fees2.size());
        std::copy(fees2.begin(), fees2.end(), std::back_inserter(fees));
    }
    std::uint32_t fee;
    if (!fees.empty())
    {
        std::sort(fees.begin(), fees.end());
        if (auto stream = m_journal.debug())
        {
            std::stringstream s;
            s << "Received fees from validations: (" << fees.size() << ") ";
            for (auto const fee1 : fees)
            {
                s << " " << fee1;
            }
            stream << s.str();
        }
        fee = fees[fees.size() / 2];  // median
    }
    else
    {
        fee = base;
    }

    app_.getFeeTrack().setRemoteFee(fee);

    tryAdvance();

    if (ledger->seq() % 256 == 0)
    {
        // Check if the majority of validators run a higher version rippled
        // software. If so print a warning.
        //
        // Once the HardenedValidations amendment is enabled, validators include
        // their rippled software version in the validation messages of every
        // (flag - 1) ledger. We wait for one ledger time before checking the
        // version information to accumulate more validation messages.

        auto currentTime = app_.timeKeeper().now();
        bool needPrint = false;

        // The variable upgradeWarningPrevTime_ will be set when and only when
        // the warning is printed.
        if (upgradeWarningPrevTime_ == TimeKeeper::time_point())
        {
            // Have not printed the warning before, check if need to print.
            auto const vals = app_.getValidations().getTrustedForLedger(
                ledger->info().parentHash, ledger->info().seq - 1);
            std::size_t higherVersionCount = 0;
            std::size_t rippledCount = 0;
            for (auto const& v : vals)
            {
                if (v->isFieldPresent(sfServerVersion))
                {
                    auto version = v->getFieldU64(sfServerVersion);
                    higherVersionCount +=
                        BuildInfo::isNewerVersion(version) ? 1 : 0;
                    rippledCount +=
                        BuildInfo::isRippledVersion(version) ? 1 : 0;
                }
            }
            // We report only if (1) we have accumulated validation messages
            // from 90% validators from the UNL, (2) 60% of validators
            // running the rippled implementation have higher version numbers,
            // and (3) the calculation won't cause divide-by-zero.
            if (higherVersionCount > 0 && rippledCount > 0)
            {
                constexpr std::size_t reportingPercent = 90;
                constexpr std::size_t cutoffPercent = 60;
                auto const unlSize{
                    app_.validators().getQuorumKeys().second.size()};
                needPrint = unlSize > 0 &&
                    calculatePercent(vals.size(), unlSize) >=
                        reportingPercent &&
                    calculatePercent(higherVersionCount, rippledCount) >=
                        cutoffPercent;
            }
        }
        // To throttle the warning messages, instead of printing a warning
        // every flag ledger, we print every week.
        else if (currentTime - upgradeWarningPrevTime_ >= weeks{1})
        {
            // Printed the warning before, and assuming most validators
            // do not downgrade, we keep printing the warning
            // until the local server is restarted.
            needPrint = true;
        }

        if (needPrint)
        {
            upgradeWarningPrevTime_ = currentTime;
            auto const upgradeMsg =
                "Check for upgrade: "
                "A majority of trusted validators are "
                "running a newer version.";
            std::cerr << upgradeMsg << std::endl;
            JLOG(m_journal.error()) << upgradeMsg;
        }
    }
}

/** Report that the consensus process built a particular ledger */
void
LedgerMaster::consensusBuilt(
    std::shared_ptr<Ledger const> const& ledger,
    uint256 const& consensusHash,
    Json::Value consensus)
{
    // Because we just built a ledger, we are no longer building one
    setBuildingLedger(0);

    // No need to process validations in standalone mode
    if (standalone_)
        return;

    mLedgerHistory.builtLedger(ledger, consensusHash, std::move(consensus));

    if (ledger->info().seq <= mValidLedgerSeq)
    {
        auto stream = app_.journal("LedgerConsensus").info();
        JLOG(stream) << "Consensus built old ledger: " << ledger->info().seq
                     << " <= " << mValidLedgerSeq;
        return;
    }

    // See if this ledger can be the new fully-validated ledger
    checkAccept(ledger);

    if (ledger->info().seq <= mValidLedgerSeq)
    {
        auto stream = app_.journal("LedgerConsensus").debug();
        JLOG(stream) << "Consensus ledger fully validated";
        return;
    }

    // This ledger cannot be the new fully-validated ledger, but
    // maybe we saved up validations for some other ledger that can be

    auto validations = app_.validators().negativeUNLFilter(
        app_.getValidations().currentTrusted());

    // Track validation counts with sequence numbers
    class valSeq
    {
    public:
        valSeq() : valCount_(0), ledgerSeq_(0)
        {
            ;
        }

        void
        mergeValidation(LedgerIndex seq)
        {
            valCount_++;

            // If we didn't already know the sequence, now we do
            if (ledgerSeq_ == 0)
                ledgerSeq_ = seq;
        }

        std::size_t valCount_;
        LedgerIndex ledgerSeq_;
    };

    // Count the number of current, trusted validations
    hash_map<uint256, valSeq> count;
    for (auto const& v : validations)
    {
        valSeq& vs = count[v->getLedgerHash()];
        vs.mergeValidation(v->getFieldU32(sfLedgerSequence));
    }

    auto const neededValidations = getNeededValidations();
    auto maxSeq = mValidLedgerSeq.load();
    auto maxLedger = ledger->info().hash;

    // Of the ledgers with sufficient validations,
    // find the one with the highest sequence
    for (auto& v : count)
        if (v.second.valCount_ > neededValidations)
        {
            // If we still don't know the sequence, get it
            if (v.second.ledgerSeq_ == 0)
            {
                if (auto l = getLedgerByHash(v.first))
                    v.second.ledgerSeq_ = l->info().seq;
            }

            if (v.second.ledgerSeq_ > maxSeq)
            {
                maxSeq = v.second.ledgerSeq_;
                maxLedger = v.first;
            }
        }

    if (maxSeq > mValidLedgerSeq)
    {
        auto stream = app_.journal("LedgerConsensus").debug();
        JLOG(stream) << "Consensus triggered check of ledger";
        checkAccept(maxLedger, maxSeq);
    }
}

std::optional<LedgerHash>
LedgerMaster::getLedgerHashForHistory(
    LedgerIndex index,
    InboundLedger::Reason reason)
{
    // Try to get the hash of a ledger we need to fetch for history
    std::optional<LedgerHash> ret;
    auto const& l{mHistLedger};

    if (l && l->info().seq >= index)
    {
        ret = hashOfSeq(*l, index, m_journal);
        if (!ret)
            ret = walkHashBySeq(index, l, reason);
    }

    if (!ret)
        ret = walkHashBySeq(index, reason);

    return ret;
}

std::vector<std::shared_ptr<Ledger const>>
LedgerMaster::findNewLedgersToPublish(
    std::unique_lock<std::recursive_mutex>& sl)
{
    std::vector<std::shared_ptr<Ledger const>> ret;

    JLOG(m_journal.trace()) << "findNewLedgersToPublish<";

    // No valid ledger, nothing to do
    if (mValidLedger.empty())
    {
        JLOG(m_journal.trace()) << "No valid journal, nothing to publish.";
        return {};
    }

    if (!mPubLedger)
    {
        JLOG(m_journal.info())
            << "First published ledger will be " << mValidLedgerSeq;
        return {mValidLedger.get()};
    }

    if (mValidLedgerSeq > (mPubLedgerSeq + MAX_LEDGER_GAP))
    {
        JLOG(m_journal.warn()) << "Gap in validated ledger stream "
                               << mPubLedgerSeq << " - " << mValidLedgerSeq - 1;

        auto valLedger = mValidLedger.get();
        ret.push_back(valLedger);
        setPubLedger(valLedger);
        app_.getOrderBookDB().setup(valLedger);

        return {valLedger};
    }

    if (mValidLedgerSeq <= mPubLedgerSeq)
    {
        JLOG(m_journal.trace()) << "No valid journal, nothing to publish.";
        return {};
    }

    int acqCount = 0;

    auto pubSeq = mPubLedgerSeq + 1;  // Next sequence to publish
    auto valLedger = mValidLedger.get();
    std::uint32_t valSeq = valLedger->info().seq;

    scope_unlock sul{sl};
    try
    {
        for (std::uint32_t seq = pubSeq; seq <= valSeq; ++seq)
        {
            JLOG(m_journal.trace())
                << "Trying to fetch/publish valid ledger " << seq;

            std::shared_ptr<Ledger const> ledger;
            // This can throw
            auto hash = hashOfSeq(*valLedger, seq, m_journal);
            // VFALCO TODO Restructure this code so that zero is not
            // used.
            if (!hash)
                hash = beast::zero;  // kludge
            if (seq == valSeq)
            {
                // We need to publish the ledger we just fully validated
                ledger = valLedger;
            }
            else if (hash->isZero())
            {
                JLOG(m_journal.fatal()) << "Ledger: " << valSeq
                                        << " does not have hash for " << seq;
                UNREACHABLE(
                    "ripple::LedgerMaster::findNewLedgersToPublish : ledger "
                    "not found");
            }
            else
            {
                ledger = mLedgerHistory.getLedgerByHash(*hash);
            }

            if (!app_.config().LEDGER_REPLAY)
            {
                // Can we try to acquire the ledger we need?
                if (!ledger && (++acqCount < ledger_fetch_size_))
                    ledger = app_.getInboundLedgers().acquire(
                        *hash, seq, InboundLedger::Reason::GENERIC);
            }

            // Did we acquire the next ledger we need to publish?
            if (ledger && (ledger->info().seq == pubSeq))
            {
                ledger->setValidated();
                ret.push_back(ledger);
                ++pubSeq;
            }
        }

        JLOG(m_journal.trace())
            << "ready to publish " << ret.size() << " ledgers.";
    }
    catch (std::exception const& ex)
    {
        JLOG(m_journal.error())
            << "Exception while trying to find ledgers to publish: "
            << ex.what();
    }

    if (app_.config().LEDGER_REPLAY)
    {
        /* Narrow down the gap of ledgers, and try to replay them.
         * When replaying a ledger gap, if the local node has
         * the start ledger, it saves an expensive InboundLedger
         * acquire. If the local node has the finish ledger, it
         * saves a skip list acquire.
         */
        auto const& startLedger = ret.empty() ? mPubLedger : ret.back();
        auto finishLedger = valLedger;
        while (startLedger->seq() + 1 < finishLedger->seq())
        {
            if (auto const parent = mLedgerHistory.getLedgerByHash(
                    finishLedger->info().parentHash);
                parent)
            {
                finishLedger = parent;
            }
            else
            {
                auto numberLedgers =
                    finishLedger->seq() - startLedger->seq() + 1;
                JLOG(m_journal.debug())
                    << "Publish LedgerReplays " << numberLedgers
                    << " ledgers, from seq=" << startLedger->info().seq << ", "
                    << startLedger->info().hash
                    << " to seq=" << finishLedger->info().seq << ", "
                    << finishLedger->info().hash;
                app_.getLedgerReplayer().replay(
                    InboundLedger::Reason::GENERIC,
                    finishLedger->info().hash,
                    numberLedgers);
                break;
            }
        }
    }

    return ret;
}

void
LedgerMaster::tryAdvance()
{
    std::lock_guard ml(m_mutex);

    // Can't advance without at least one fully-valid ledger
    mAdvanceWork = true;
    if (!mAdvanceThread && !mValidLedger.empty())
    {
        mAdvanceThread = true;
        app_.getJobQueue().addJob(jtADVANCE, "advanceLedger", [this]() {
            std::unique_lock sl(m_mutex);

            XRPL_ASSERT(
                !mValidLedger.empty() && mAdvanceThread,
                "ripple::LedgerMaster::tryAdvance : has valid ledger");

            JLOG(m_journal.trace()) << "advanceThread<";

            try
            {
                doAdvance(sl);
            }
            catch (std::exception const& ex)
            {
                JLOG(m_journal.fatal()) << "doAdvance throws: " << ex.what();
            }

            mAdvanceThread = false;
            JLOG(m_journal.trace()) << "advanceThread>";
        });
    }
}

void
LedgerMaster::updatePaths()
{
    {
        std::lock_guard ml(m_mutex);
        if (app_.getOPs().isNeedNetworkLedger())
        {
            --mPathFindThread;
            mPathLedger.reset();
            JLOG(m_journal.debug()) << "Need network ledger for updating paths";
            return;
        }
    }

    while (!app_.getJobQueue().isStopping())
    {
        JLOG(m_journal.debug()) << "updatePaths running";
        std::shared_ptr<ReadView const> lastLedger;
        {
            std::lock_guard ml(m_mutex);

            if (!mValidLedger.empty() &&
                (!mPathLedger || (mPathLedger->info().seq != mValidLedgerSeq)))
            {  // We have a new valid ledger since the last full pathfinding
                mPathLedger = mValidLedger.get();
                lastLedger = mPathLedger;
            }
            else if (mPathFindNewRequest)
            {  // We have a new request but no new ledger
                lastLedger = app_.openLedger().current();
            }
            else
            {  // Nothing to do
                --mPathFindThread;
                mPathLedger.reset();
                JLOG(m_journal.debug()) << "Nothing to do for updating paths";
                return;
            }
        }

        if (!standalone_)
        {  // don't pathfind with a ledger that's more than 60 seconds old
            using namespace std::chrono;
            auto age = time_point_cast<seconds>(app_.timeKeeper().closeTime()) -
                lastLedger->info().closeTime;
            if (age > 1min)
            {
                JLOG(m_journal.debug())
                    << "Published ledger too old for updating paths";
                std::lock_guard ml(m_mutex);
                --mPathFindThread;
                mPathLedger.reset();
                return;
            }
        }

        try
        {
            auto& pathRequests = app_.getPathRequests();
            {
                std::lock_guard ml(m_mutex);
                if (!pathRequests.requestsPending())
                {
                    --mPathFindThread;
                    mPathLedger.reset();
                    JLOG(m_journal.debug())
                        << "No path requests found. Nothing to do for updating "
                           "paths. "
                        << mPathFindThread << " jobs remaining";
                    return;
                }
            }
            JLOG(m_journal.debug()) << "Updating paths";
            pathRequests.updateAll(lastLedger);

            std::lock_guard ml(m_mutex);
            if (!pathRequests.requestsPending())
            {
                JLOG(m_journal.debug())
                    << "No path requests left. No need for further updating "
                       "paths";
                --mPathFindThread;
                mPathLedger.reset();
                return;
            }
        }
        catch (SHAMapMissingNode const& mn)
        {
            JLOG(m_journal.info()) << "During pathfinding: " << mn.what();
            if (lastLedger->open())
            {
                // our parent is the problem
                app_.getInboundLedgers().acquire(
                    lastLedger->info().parentHash,
                    lastLedger->info().seq - 1,
                    InboundLedger::Reason::GENERIC);
            }
            else
            {
                // this ledger is the problem
                app_.getInboundLedgers().acquire(
                    lastLedger->info().hash,
                    lastLedger->info().seq,
                    InboundLedger::Reason::GENERIC);
            }
        }
    }
}

bool
LedgerMaster::newPathRequest()
{
    std::unique_lock ml(m_mutex);
    mPathFindNewRequest = newPFWork("pf:newRequest", ml);
    return mPathFindNewRequest;
}

bool
LedgerMaster::isNewPathRequest()
{
    std::lock_guard ml(m_mutex);
    bool const ret = mPathFindNewRequest;
    mPathFindNewRequest = false;
    return ret;
}

// If the order book is radically updated, we need to reprocess all
// pathfinding requests.
bool
LedgerMaster::newOrderBookDB()
{
    std::unique_lock ml(m_mutex);
    mPathLedger.reset();

    return newPFWork("pf:newOBDB", ml);
}

/** A thread needs to be dispatched to handle pathfinding work of some kind.
 */
bool
LedgerMaster::newPFWork(
    char const* name,
    std::unique_lock<std::recursive_mutex>&)
{
    if (!app_.isStopping() && mPathFindThread < 2 &&
        app_.getPathRequests().requestsPending())
    {
        JLOG(m_journal.debug())
            << "newPFWork: Creating job. path find threads: "
            << mPathFindThread;
        if (app_.getJobQueue().addJob(
                jtUPDATE_PF, name, [this]() { updatePaths(); }))
        {
            ++mPathFindThread;
        }
    }
    // If we're stopping don't give callers the expectation that their
    // request will be fulfilled, even if it may be serviced.
    return mPathFindThread > 0 && !app_.isStopping();
}

std::recursive_mutex&
LedgerMaster::peekMutex()
{
    return m_mutex;
}

// The current ledger is the ledger we believe new transactions should go in
std::shared_ptr<ReadView const>
LedgerMaster::getCurrentLedger()
{
    return app_.openLedger().current();
}

std::shared_ptr<Ledger const>
LedgerMaster::getValidatedLedger()
{
    return mValidLedger.get();
}

Rules
LedgerMaster::getValidatedRules()
{
    // Once we have a guarantee that there's always a last validated
    // ledger then we can dispense with the if.

    // Return the Rules from the last validated ledger.
    if (auto const ledger = getValidatedLedger())
        return ledger->rules();

    return Rules(app_.config().features);
}

// This is the last ledger we published to clients and can lag the validated
// ledger.
std::shared_ptr<ReadView const>
LedgerMaster::getPublishedLedger()
{
    std::lock_guard lock(m_mutex);
    return mPubLedger;
}

std::string
LedgerMaster::getCompleteLedgers()
{
    std::lock_guard sl(mCompleteLock);
    return to_string(mCompleteLedgers);
}

std::optional<NetClock::time_point>
LedgerMaster::getCloseTimeBySeq(LedgerIndex ledgerIndex)
{
    uint256 hash = getHashBySeq(ledgerIndex);
    return hash.isNonZero() ? getCloseTimeByHash(hash, ledgerIndex)
                            : std::nullopt;
}

std::optional<NetClock::time_point>
LedgerMaster::getCloseTimeByHash(
    LedgerHash const& ledgerHash,
    std::uint32_t index)
{
    auto nodeObject = app_.getNodeStore().fetchNodeObject(ledgerHash, index);
    if (nodeObject && (nodeObject->getData().size() >= 120))
    {
        SerialIter it(
            nodeObject->getData().data(), nodeObject->getData().size());
        if (safe_cast<HashPrefix>(it.get32()) == HashPrefix::ledgerMaster)
        {
            it.skip(
                4 + 8 + 32 +   // seq drops parentHash
                32 + 32 + 4);  // txHash acctHash parentClose
            return NetClock::time_point{NetClock::duration{it.get32()}};
        }
    }

    return std::nullopt;
}

uint256
LedgerMaster::getHashBySeq(std::uint32_t index)
{
    uint256 hash = mLedgerHistory.getLedgerHash(index);

    if (hash.isNonZero())
        return hash;

    return app_.getRelationalDatabase().getHashByIndex(index);
}

std::optional<LedgerHash>
LedgerMaster::walkHashBySeq(std::uint32_t index, InboundLedger::Reason reason)
{
    std::optional<LedgerHash> ledgerHash;

    if (auto referenceLedger = mValidLedger.get())
        ledgerHash = walkHashBySeq(index, referenceLedger, reason);

    return ledgerHash;
}

std::optional<LedgerHash>
LedgerMaster::walkHashBySeq(
    std::uint32_t index,
    std::shared_ptr<ReadView const> const& referenceLedger,
    InboundLedger::Reason reason)
{
    if (!referenceLedger || (referenceLedger->info().seq < index))
    {
        // Nothing we can do. No validated ledger.
        return std::nullopt;
    }

    // See if the hash for the ledger we need is in the reference ledger
    auto ledgerHash = hashOfSeq(*referenceLedger, index, m_journal);
    if (ledgerHash)
        return ledgerHash;

    // The hash is not in the reference ledger. Get another ledger which can
    // be located easily and should contain the hash.
    LedgerIndex refIndex = getCandidateLedger(index);
    auto const refHash = hashOfSeq(*referenceLedger, refIndex, m_journal);
    XRPL_ASSERT(refHash, "ripple::LedgerMaster::walkHashBySeq : found ledger");
    if (refHash)
    {
        // Try the hash and sequence of a better reference ledger just found
        auto ledger = mLedgerHistory.getLedgerByHash(*refHash);

        if (ledger)
        {
            try
            {
                ledgerHash = hashOfSeq(*ledger, index, m_journal);
            }
            catch (SHAMapMissingNode const&)
            {
                ledger.reset();
            }
        }

        // Try to acquire the complete ledger
        if (!ledger)
        {
            if (auto const l = app_.getInboundLedgers().acquire(
                    *refHash, refIndex, reason))
            {
                ledgerHash = hashOfSeq(*l, index, m_journal);
                XRPL_ASSERT(
                    ledgerHash,
                    "ripple::LedgerMaster::walkHashBySeq : has complete "
                    "ledger");
            }
        }
    }
    return ledgerHash;
}

std::shared_ptr<Ledger const>
LedgerMaster::getLedgerBySeq(std::uint32_t index)
{
    if (index <= mValidLedgerSeq)
    {
        // Always prefer a validated ledger
        if (auto valid = mValidLedger.get())
        {
            if (valid->info().seq == index)
                return valid;

            try
            {
                auto const hash = hashOfSeq(*valid, index, m_journal);

                if (hash)
                    return mLedgerHistory.getLedgerByHash(*hash);
            }
            catch (std::exception const&)
            {
                // Missing nodes are already handled
            }
        }
    }

    if (auto ret = mLedgerHistory.getLedgerBySeq(index))
        return ret;

    auto ret = mClosedLedger.get();
    if (ret && (ret->info().seq == index))
        return ret;

    clearLedger(index);
    return {};
}

std::shared_ptr<Ledger const>
LedgerMaster::getLedgerByHash(uint256 const& hash)
{
    if (auto ret = mLedgerHistory.getLedgerByHash(hash))
        return ret;

    auto ret = mClosedLedger.get();
    if (ret && (ret->info().hash == hash))
        return ret;

    return {};
}

void
LedgerMaster::setLedgerRangePresent(std::uint32_t minV, std::uint32_t maxV)
{
    std::lock_guard sl(mCompleteLock);
    mCompleteLedgers.insert(range(minV, maxV));
}

void
LedgerMaster::sweep()
{
    mLedgerHistory.sweep();
    fetch_packs_.sweep();
}

float
LedgerMaster::getCacheHitRate()
{
    return mLedgerHistory.getCacheHitRate();
}

void
LedgerMaster::clearPriorLedgers(LedgerIndex seq)
{
    std::lock_guard sl(mCompleteLock);
    if (seq > 0)
        mCompleteLedgers.erase(range(0u, seq - 1));
}

void
LedgerMaster::clearLedgerCachePrior(LedgerIndex seq)
{
    mLedgerHistory.clearLedgerCachePrior(seq);
}

void
LedgerMaster::takeReplay(std::unique_ptr<LedgerReplay> replay)
{
    replayData = std::move(replay);
}

std::unique_ptr<LedgerReplay>
LedgerMaster::releaseReplay()
{
    return std::move(replayData);
}

void
LedgerMaster::fetchForHistory(
    std::uint32_t missing,
    bool& progress,
    InboundLedger::Reason reason,
    std::unique_lock<std::recursive_mutex>& sl)
{
    scope_unlock sul{sl};
    if (auto hash = getLedgerHashForHistory(missing, reason))
    {
        XRPL_ASSERT(
            hash->isNonZero(),
            "ripple::LedgerMaster::fetchForHistory : found ledger");
        auto ledger = getLedgerByHash(*hash);
        if (!ledger)
        {
            if (!app_.getInboundLedgers().isFailure(*hash))
            {
                ledger =
                    app_.getInboundLedgers().acquire(*hash, missing, reason);
                if (!ledger && missing != fetch_seq_ &&
                    missing > app_.getNodeStore().earliestLedgerSeq())
                {
                    JLOG(m_journal.trace())
                        << "fetchForHistory want fetch pack " << missing;
                    fetch_seq_ = missing;
                    getFetchPack(missing, reason);
                }
                else
                    JLOG(m_journal.trace())
                        << "fetchForHistory no fetch pack for " << missing;
            }
            else
                JLOG(m_journal.debug())
                    << "fetchForHistory found failed acquire";
        }
        if (ledger)
        {
            auto seq = ledger->info().seq;
            XRPL_ASSERT(
                seq == missing,
                "ripple::LedgerMaster::fetchForHistory : sequence match");
            JLOG(m_journal.trace()) << "fetchForHistory acquired " << seq;
            setFullLedger(ledger, false, false);
            int fillInProgress;
            {
                std::lock_guard lock(m_mutex);
                mHistLedger = ledger;
                fillInProgress = mFillInProgress;
            }
            if (fillInProgress == 0 &&
                app_.getRelationalDatabase().getHashByIndex(seq - 1) ==
                    ledger->info().parentHash)
            {
                {
                    // Previous ledger is in DB
                    std::lock_guard lock(m_mutex);
                    mFillInProgress = seq;
                }
                app_.getJobQueue().addJob(
                    jtADVANCE, "tryFill", [this, ledger]() {
                        tryFill(ledger);
                    });
            }
            progress = true;
        }
        else
        {
            std::uint32_t fetchSz;
            // Do not fetch ledger sequences lower
            // than the earliest ledger sequence
            fetchSz = app_.getNodeStore().earliestLedgerSeq();
            fetchSz = missing >= fetchSz
                ? std::min(ledger_fetch_size_, (missing - fetchSz) + 1)
                : 0;
            try
            {
                for (std::uint32_t i = 0; i < fetchSz; ++i)
                {
                    std::uint32_t seq = missing - i;
                    if (auto h = getLedgerHashForHistory(seq, reason))
                    {
                        XRPL_ASSERT(
                            h->isNonZero(),
                            "ripple::LedgerMaster::fetchForHistory : "
                            "prefetched ledger");
                        app_.getInboundLedgers().acquire(*h, seq, reason);
                    }
                }
            }
            catch (std::exception const& ex)
            {
                JLOG(m_journal.warn())
                    << "Threw while prefetching: " << ex.what();
            }
        }
    }
    else
    {
        JLOG(m_journal.fatal())
            << "Can't find ledger following prevMissing " << missing;
        JLOG(m_journal.fatal())
            << "Pub:" << mPubLedgerSeq << " Val:" << mValidLedgerSeq;
        JLOG(m_journal.fatal())
            << "Ledgers: " << app_.getLedgerMaster().getCompleteLedgers();
        JLOG(m_journal.fatal())
            << "Acquire reason: "
            << (reason == InboundLedger::Reason::HISTORY ? "HISTORY"
                                                         : "NOT HISTORY");
        clearLedger(missing + 1);
        progress = true;
    }
}

// Try to publish ledgers, acquire missing ledgers
void
LedgerMaster::doAdvance(std::unique_lock<std::recursive_mutex>& sl)
{
    do
    {
        mAdvanceWork = false;  // If there's work to do, we'll make progress
        bool progress = false;

        auto const pubLedgers = findNewLedgersToPublish(sl);
        if (pubLedgers.empty())
        {
            if (!standalone_ && !app_.getFeeTrack().isLoadedLocal() &&
                (app_.getJobQueue().getJobCount(jtPUBOLDLEDGER) < 10) &&
                (mValidLedgerSeq == mPubLedgerSeq) &&
                (getValidatedLedgerAge() < MAX_LEDGER_AGE_ACQUIRE) &&
                (app_.getNodeStore().getWriteLoad() < MAX_WRITE_LOAD_ACQUIRE))
            {
                // We are in sync, so can acquire
                InboundLedger::Reason reason = InboundLedger::Reason::HISTORY;
                std::optional<std::uint32_t> missing;
                {
                    std::lock_guard sll(mCompleteLock);
                    missing = prevMissing(
                        mCompleteLedgers,
                        mPubLedger->info().seq,
                        app_.getNodeStore().earliestLedgerSeq());
                }
                if (missing)
                {
                    JLOG(m_journal.trace())
                        << "tryAdvance discovered missing " << *missing;
                    if ((mFillInProgress == 0 || *missing > mFillInProgress) &&
                        shouldAcquire(
                            mValidLedgerSeq,
                            ledger_history_,
                            app_.getSHAMapStore().minimumOnline(),
                            *missing,
                            m_journal))
                    {
                        JLOG(m_journal.trace())
                            << "advanceThread should acquire";
                    }
                    else
                        missing = std::nullopt;
                }
                if (missing)
                {
                    fetchForHistory(*missing, progress, reason, sl);
                    if (mValidLedgerSeq != mPubLedgerSeq)
                    {
                        JLOG(m_journal.debug())
                            << "tryAdvance found last valid changed";
                        progress = true;
                    }
                }
            }
            else
            {
                mHistLedger.reset();
                JLOG(m_journal.trace()) << "tryAdvance not fetching history";
            }
        }
        else
        {
            JLOG(m_journal.trace()) << "tryAdvance found " << pubLedgers.size()
                                    << " ledgers to publish";
            for (auto const& ledger : pubLedgers)
            {
                {
                    scope_unlock sul{sl};
                    JLOG(m_journal.debug())
                        << "tryAdvance publishing seq " << ledger->info().seq;
                    setFullLedger(ledger, true, true);
                }

                setPubLedger(ledger);

                {
                    scope_unlock sul{sl};
                    app_.getOPs().pubLedger(ledger);
                }
            }

            app_.getOPs().clearNeedNetworkLedger();
            progress = newPFWork("pf:newLedger", sl);
        }
        if (progress)
            mAdvanceWork = true;
    } while (mAdvanceWork);
}

void
LedgerMaster::addFetchPack(uint256 const& hash, std::shared_ptr<Blob> data)
{
    fetch_packs_.canonicalize_replace_client(hash, data);
}

std::optional<Blob>
LedgerMaster::getFetchPack(uint256 const& hash)
{
    Blob data;
    if (fetch_packs_.retrieve(hash, data))
    {
        fetch_packs_.del(hash, false);
        if (hash == sha512Half(makeSlice(data)))
            return data;
    }
    return std::nullopt;
}

void
LedgerMaster::gotFetchPack(bool progress, std::uint32_t seq)
{
    if (!mGotFetchPackThread.test_and_set(std::memory_order_acquire))
    {
        app_.getJobQueue().addJob(jtLEDGER_DATA, "gotFetchPack", [&]() {
            app_.getInboundLedgers().gotFetchPack();
            mGotFetchPackThread.clear(std::memory_order_release);
        });
    }
}

/** Populate a fetch pack with data from the map the recipient wants.

    A recipient may or may not have the map that they are asking for. If
    they do, we can optimize the transfer by not including parts of the
    map that they are already have.

    @param have The map that the recipient already has (if any).
    @param cnt The maximum number of nodes to return.
    @param into The protocol object into which we add information.
    @param seq The sequence number of the ledger the map is a part of.
    @param withLeaves True if leaf nodes should be included.

    @note: The withLeaves parameter is configurable even though the
           code, so far, only ever sets the parameter to true.

           The rationale is that for transaction trees, it may make
           sense to not include the leaves if the fetch pack is being
           constructed for someone attempting to get a recent ledger
           for which they already have the transactions.

           However, for historical ledgers, which is the only use we
           have for fetch packs right now, it makes sense to include
           the transactions because the caller is unlikely to have
           them.
 */
static void
populateFetchPack(
    SHAMap const& want,
    SHAMap const* have,
    std::uint32_t cnt,
    protocol::TMGetObjectByHash* into,
    std::uint32_t seq,
    bool withLeaves = true)
{
    XRPL_ASSERT(cnt, "ripple::populateFetchPack : nonzero count input");

    Serializer s(1024);

    want.visitDifferences(
        have,
        [&s, withLeaves, &cnt, into, seq](SHAMapTreeNode const& n) -> bool {
            if (!withLeaves && n.isLeaf())
                return true;

            s.erase();
            n.serializeWithPrefix(s);

            auto const& hash = n.getHash().as_uint256();

            protocol::TMIndexedObject* obj = into->add_objects();
            obj->set_ledgerseq(seq);
            obj->set_hash(hash.data(), hash.size());
            obj->set_data(s.getDataPtr(), s.getLength());

            return --cnt != 0;
        });
}

void
LedgerMaster::makeFetchPack(
    std::weak_ptr<Peer> const& wPeer,
    std::shared_ptr<protocol::TMGetObjectByHash> const& request,
    uint256 haveLedgerHash,
    UptimeClock::time_point uptime)
{
    using namespace std::chrono_literals;
    if (UptimeClock::now() > uptime + 1s)
    {
        JLOG(m_journal.info()) << "Fetch pack request got stale";
        return;
    }

    if (app_.getFeeTrack().isLoadedLocal() || (getValidatedLedgerAge() > 40s))
    {
        JLOG(m_journal.info()) << "Too busy to make fetch pack";
        return;
    }

    auto peer = wPeer.lock();

    if (!peer)
        return;

    auto have = getLedgerByHash(haveLedgerHash);

    if (!have)
    {
        JLOG(m_journal.info())
            << "Peer requests fetch pack for ledger we don't have: " << have;
        peer->charge(Resource::feeRequestNoReply, "get_object ledger");
        return;
    }

    if (have->open())
    {
        JLOG(m_journal.warn())
            << "Peer requests fetch pack from open ledger: " << have;
        peer->charge(Resource::feeMalformedRequest, "get_object ledger open");
        return;
    }

    if (have->info().seq < getEarliestFetch())
    {
        JLOG(m_journal.debug()) << "Peer requests fetch pack that is too early";
        peer->charge(Resource::feeMalformedRequest, "get_object ledger early");
        return;
    }

    auto want = getLedgerByHash(have->info().parentHash);

    if (!want)
    {
        JLOG(m_journal.info())
            << "Peer requests fetch pack for ledger whose predecessor we "
            << "don't have: " << have;
        peer->charge(
            Resource::feeRequestNoReply, "get_object ledger no parent");
        return;
    }

    try
    {
        Serializer hdr(128);

        protocol::TMGetObjectByHash reply;
        reply.set_query(false);

        if (request->has_seq())
            reply.set_seq(request->seq());

        reply.set_ledgerhash(request->ledgerhash());
        reply.set_type(protocol::TMGetObjectByHash::otFETCH_PACK);

        // Building a fetch pack:
        //  1. Add the header for the requested ledger.
        //  2. Add the nodes for the AccountStateMap of that ledger.
        //  3. If there are transactions, add the nodes for the
        //     transactions of the ledger.
        //  4. If the FetchPack now contains at least 512 entries then stop.
        //  5. If not very much time has elapsed, then loop back and repeat
        //     the same process adding the previous ledger to the FetchPack.
        do
        {
            std::uint32_t lSeq = want->info().seq;

            {
                // Serialize the ledger header:
                hdr.erase();

                hdr.add32(HashPrefix::ledgerMaster);
                addRaw(want->info(), hdr);

                // Add the data
                protocol::TMIndexedObject* obj = reply.add_objects();
                obj->set_hash(
                    want->info().hash.data(), want->info().hash.size());
                obj->set_data(hdr.getDataPtr(), hdr.getLength());
                obj->set_ledgerseq(lSeq);
            }

            populateFetchPack(
                want->stateMap(), &have->stateMap(), 16384, &reply, lSeq);

            // We use nullptr here because transaction maps are per ledger
            // and so the requestor is unlikely to already have it.
            if (want->info().txHash.isNonZero())
                populateFetchPack(want->txMap(), nullptr, 512, &reply, lSeq);

            if (reply.objects().size() >= 512)
                break;

            have = std::move(want);
            want = getLedgerByHash(have->info().parentHash);
        } while (want && UptimeClock::now() <= uptime + 1s);

        auto msg = std::make_shared<Message>(reply, protocol::mtGET_OBJECTS);

        JLOG(m_journal.info())
            << "Built fetch pack with " << reply.objects().size() << " nodes ("
            << msg->getBufferSize() << " bytes)";

        peer->send(msg);
    }
    catch (std::exception const& ex)
    {
        JLOG(m_journal.warn())
            << "Exception building fetch pach. Exception: " << ex.what();
    }
}

std::size_t
LedgerMaster::getFetchPackCacheSize() const
{
    return fetch_packs_.getCacheSize();
}

// Returns the minimum ledger sequence in SQL database, if any.
std::optional<LedgerIndex>
LedgerMaster::minSqlSeq()
{
    return app_.getRelationalDatabase().getMinLedgerSeq();
}

std::optional<uint256>
LedgerMaster::txnIdFromIndex(uint32_t ledgerSeq, uint32_t txnIndex)
{
    uint32_t first = 0, last = 0;

    if (!getValidatedRange(first, last) || last < ledgerSeq)
        return {};

    auto const lgr = getLedgerBySeq(ledgerSeq);
    if (!lgr || lgr->txs.empty())
        return {};

    for (auto it = lgr->txs.begin(); it != lgr->txs.end(); ++it)
        if (it->first && it->second &&
            it->second->isFieldPresent(sfTransactionIndex) &&
            it->second->getFieldU32(sfTransactionIndex) == txnIndex)
            return it->first->getTransactionID();

    return {};
}

}  // namespace ripple
