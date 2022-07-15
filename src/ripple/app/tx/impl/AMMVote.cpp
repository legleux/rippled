//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2022 Ripple Labs Inc.

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

#include <ripple/app/misc/AMM.h>
#include <ripple/app/tx/impl/AMMVote.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {

TxConsequences
AMMVote::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx};
}

NotTEC
AMMVote::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureAMM))
        return temDISABLED;

    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    if (ctx.tx.getFlags() & tfUniversalMask)
    {
        JLOG(ctx.j.debug()) << "AMM Vote: invalid flags.";
        return temINVALID_FLAG;
    }

    if (ctx.tx[sfFeeVal] > 65000)
    {
        JLOG(ctx.j.debug()) << "AMM Vote: invalid trading fee.";
        return temBAD_FEE;
    }

    return preflight2(ctx);
}

TER
AMMVote::preclaim(PreclaimContext const& ctx)
{
    if (!ctx.view.read(keylet::account(ctx.tx[sfAccount])))
    {
        JLOG(ctx.j.debug()) << "AMM Vote: Invalid account.";
        return terNO_ACCOUNT;
    }

    if (!getAMMSle(ctx.view, ctx.tx[sfAMMHash]))
    {
        JLOG(ctx.j.debug()) << "AMM Vote: Invalid AMM account.";
        return terNO_ACCOUNT;
    }

    return tesSUCCESS;
}

void
AMMVote::preCompute()
{
    return Transactor::preCompute();
}

std::pair<TER, bool>
AMMVote::applyGuts(Sandbox& sb)
{
    auto const feeNew = ctx_.tx[sfFeeVal];
    auto const amm = getAMMSle(sb, ctx_.tx[sfAMMHash]);
    assert(amm);
    auto const ammAccount = amm->getAccountID(sfAMMAccount);
    auto const lptAMMBalance = amm->getFieldAmount(sfLPTokenBalance);
    auto const lpTokensNew = lpHolds(sb, ammAccount, account_, ctx_.journal);
    if (lpTokensNew == beast::zero)
    {
        JLOG(ctx_.journal.debug()) << "AMM Vote: account is not LP.";
        return {tecAMM_INVALID_TOKENS, false};
    }

    std::optional<STAmount> minTokens{};
    std::size_t minPos{0};
    STArray updatedVoteEntries;
    Number num{0};
    Number den{0};
    bool foundAccount = false;
    for (auto const& entry : amm->getFieldArray(sfVoteEntries))
    {
        auto const account = entry.getAccountID(sfAccount);
        auto lpTokens = lpHolds(sb, ammAccount, account, ctx_.journal);
        if (lpTokens == beast::zero)
            continue;
        auto feeVal = entry.getFieldU32(sfFeeVal);
        STObject newEntry{sfVoteEntry};
        if (account == account_)
        {
            lpTokens = lpTokensNew;
            feeVal = feeNew;
            foundAccount = true;
        }
        num += feeVal * lpTokens;
        den += lpTokens;
        newEntry.setAccountID(sfAccount, account);
        newEntry.setFieldU32(sfFeeVal, feeVal);
        newEntry.setFieldU32(
            sfVoteWeight,
            (std::int64_t)(
                Number(lpTokens) * 100000 / lptAMMBalance + Number(1) / 2));
        if (!minTokens || lpTokens < *minTokens)
        {
            minTokens = lpTokens;
            minPos = updatedVoteEntries.size();
        }
        updatedVoteEntries.emplace_back(newEntry);
    }

    if (!foundAccount)
    {
        auto update = [&]() {
            STObject newEntry{sfVoteEntry};
            newEntry.setFieldU32(sfFeeVal, feeNew);
            newEntry.setFieldU32(
                sfVoteWeight,
                (std::int64_t)(
                    Number(lpTokensNew) * 100000 / lptAMMBalance +
                    Number(1) / 2));
            newEntry.setAccountID(sfAccount, account_);
            num += feeNew * lpTokensNew;
            den += lpTokensNew;
            updatedVoteEntries.emplace_back(newEntry);
        };
        if (updatedVoteEntries.size() < 8)
            update();
        else if (lpTokensNew > *minTokens)
        {
            auto const entry = updatedVoteEntries.begin() + minPos;
            num -= entry->getFieldU32(sfFeeVal) * *minTokens;
            den -= *minTokens;
            updatedVoteEntries.erase(updatedVoteEntries.begin() + minPos);
            update();
        }
    }

    amm->setFieldArray(sfVoteEntries, updatedVoteEntries);
    amm->setFieldU32(sfTradingFee, (std::int64_t)(num / den + Number(1) / 2));
    sb.update(amm);

    return {tesSUCCESS, true};
}

TER
AMMVote::doApply()
{
    // This is the ledger view that we work against. Transactions are applied
    // as we go on processing transactions.
    Sandbox sb(&ctx_.view());

    // This is a ledger with just the fees paid and any unfunded or expired
    // offers we encounter removed. It's used when handling Fill-or-Kill offers,
    // if the order isn't going to be placed, to avoid wasting the work we did.
    Sandbox sbCancel(&ctx_.view());

    auto const result = applyGuts(sb);
    if (result.second)
        sb.apply(ctx_.rawView());
    else
        sbCancel.apply(ctx_.rawView());

    return result.first;
}

}  // namespace ripple