//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2022 Ripple Labs Inc.

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
#include <ripple/app/misc/AMM_formulae.h>
#include <ripple/app/tx/impl/AMMWithdraw.h>
#include <ripple/basics/Number.h>
#include <ripple/ledger/Sandbox.h>
#include <ripple/ledger/View.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/STAccount.h>

namespace ripple {

TxConsequences
AMMWithdraw::makeTxConsequences(PreflightContext const& ctx)
{
    return TxConsequences{ctx.tx};
}

NotTEC
AMMWithdraw::preflight(PreflightContext const& ctx)
{
    if (!ctx.rules.enabled(featureAMM))
        return temDISABLED;

    auto const ret = preflight1(ctx);
    if (!isTesSuccess(ret))
        return ret;

    auto const asset1Out = ctx.tx[~sfAsset1Out];
    auto const asset2Out = ctx.tx[~sfAsset2Out];
    auto const maxSP = ctx.tx[~sfMaxSP];
    auto const lpTokens = ctx.tx[~sfLPTokens];
    // Valid combinations are:
    //   LPTokens
    //   Asset1Out
    //   Asset1Out and Asset2Out
    //   Asset1Out and LPTokens
    //   Asset1Out and MaxSP
    if ((!lpTokens && !asset1Out) || (lpTokens && (asset2Out || maxSP)) ||
        (asset1Out &&
         ((asset2Out && (lpTokens || maxSP)) ||
          (maxSP && (asset2Out || lpTokens)))))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid combination of "
                               "deposit fields.";
        return temBAD_AMM_OPTIONS;
    }
    if (lpTokens && *lpTokens == beast::zero)
    {
        JLOG(ctx.j.debug()) << "Withdraw all tokens";
    }
    if (auto const res = validAmount(asset1Out, lpTokens.has_value()))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid Asset1Out";
        return *res;
    }
    else if (auto const res = validAmount(asset2Out))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid Asset2OutAmount";
        return *res;
    }
    else if (auto const res = validAmount(maxSP))
    {
        JLOG(ctx.j.debug()) << "Malformed transaction: invalid MaxSP";
        return *res;
    }

    return preflight2(ctx);
}

TER
AMMWithdraw::preclaim(PreclaimContext const& ctx)
{
    auto const sleAMM = getAMMSle(ctx.view, ctx.tx[sfAMMHash]);
    if (!sleAMM)
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw: Invalid AMM account";
        return temBAD_SRC_ACCOUNT;
    }
    auto const asset1Out = ctx.tx[~sfAsset1Out];
    auto const asset2Out = ctx.tx[~sfAsset2Out];
    auto const maxSP = ctx.tx[~sfMaxSP];
    auto const ammAccountID = sleAMM->getAccountID(sfAMMAccount);
    auto const [asset1, asset2, lptBalance] = getAMMBalances(
        ctx.view,
        ammAccountID,
        ctx.tx[sfAccount],
        asset1Out ? std::optional<Issue>(asset1Out->issue()) : std::nullopt,
        asset2Out ? std::optional<Issue>(asset2Out->issue()) : std::nullopt,
        ctx.j);
    auto const lpTokens = [&]() -> std::optional<STAmount> {
        auto const tokens = ctx.tx[~sfLPTokens];
        // special case - withdraw all tokens
        if (tokens && *tokens == beast::zero)
            return getLPTokens(
                ctx.view, ammAccountID, ctx.tx[sfAccount], ctx.j);
        return tokens;
    }();
    if (asset1 <= beast::zero || asset2 <= beast::zero ||
        lptBalance <= beast::zero)
    {
        JLOG(ctx.j.error())
            << "AMM Withdraw: reserves or tokens balance is zero";
        return tecAMM_BALANCE;
    }
    if (lpTokens && *lpTokens > lptBalance)
    {
        JLOG(ctx.j.error()) << "AMM Withdraw: invalid tokens balance";
        return tecAMM_BALANCE;
    }
    if (asset1Out && *asset1Out > asset1)
    {
        JLOG(ctx.j.error()) << "AMM Withdraw: invalid asset1 balance";
        return tecAMM_BALANCE;
    }
    if (asset2Out && *asset2Out > asset2)
    {
        JLOG(ctx.j.error()) << "AMM Withdraw: invalid asset2 balance";
        return tecAMM_BALANCE;
    }
    if (isFrozen(ctx.view, ctx.tx[~sfAsset1Out]) ||
        isFrozen(ctx.view, ctx.tx[~sfAsset2Out]))
    {
        JLOG(ctx.j.debug()) << "AMM Withdraw involves frozen asset";
        return tecFROZEN;
    }
    return tesSUCCESS;
}

void
AMMWithdraw::preCompute()
{
    return Transactor::preCompute();
}

std::pair<TER, bool>
AMMWithdraw::applyGuts(Sandbox& sb)
{
    auto const asset1Out = ctx_.tx[~sfAsset1Out];
    auto const asset2Out = ctx_.tx[~sfAsset2Out];
    auto const maxSP = ctx_.tx[~sfMaxSP];
    auto const sleAMM = ctx_.view().peek(keylet::amm(ctx_.tx[sfAMMHash]));
    assert(sleAMM);
    auto const ammAccountID = sleAMM->getAccountID(sfAMMAccount);
    auto const lpTokens = [&]() -> std::optional<STAmount> {
        auto const tokens = ctx_.tx[~sfLPTokens];
        // special case - withdraw all tokens
        if (tokens && *tokens == beast::zero)
            return getLPTokens(sb, ammAccountID, account_, ctx_.journal);
        return tokens;
    }();
    auto const [asset1, asset2, lptAMMBalance] = getAMMBalances(
        sb,
        ammAccountID,
        std::nullopt,
        asset1Out ? asset1Out->issue() : std::optional<Issue>{},
        asset2Out ? asset2Out->issue() : std::optional<Issue>{},
        ctx_.journal);

    auto const tfee = sleAMM->getFieldU32(sfTradingFee);
    auto const weight = sleAMM->getFieldU8(sfAssetWeight);

    TER result = tesSUCCESS;

    if (asset1Out)
    {
        if (asset2Out)
            result = equalWithdrawalLimit(
                sb,
                ammAccountID,
                asset1,
                asset2,
                lptAMMBalance,
                *asset1Out,
                *asset2Out);
        else if (lpTokens)
            result = singleWithdrawalTokens(
                sb,
                ammAccountID,
                asset1,
                lptAMMBalance,
                *asset1Out,
                *lpTokens,
                weight,
                tfee);
        else if (maxSP)
            result = singleWithdrawMaxSP(
                sb,
                ammAccountID,
                asset1,
                asset2,
                lptAMMBalance,
                *asset1Out,
                *maxSP,
                weight,
                tfee);
        else
            result = singleWithdrawal(
                sb,
                ammAccountID,
                asset1,
                lptAMMBalance,
                *asset1Out,
                weight,
                tfee);
    }
    else if (lpTokens)
        result = equalWithdrawalTokens(
            sb, ammAccountID, asset1, asset2, lptAMMBalance, *lpTokens);
    return {result, result == tesSUCCESS};
}

TER
AMMWithdraw::doApply()
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

TER
AMMWithdraw::deleteAccount(Sandbox& view, AccountID const& ammAccountID)
{
    return tesSUCCESS;
    auto sleAMMRoot = view.peek(keylet::account(ammAccountID));
    assert(sleAMMRoot);
    auto sleAMM = view.peek(keylet::amm(ctx_.tx[sfAMMHash]));
    assert(sleAMM);

    if (!sleAMMRoot || !sleAMM)
        return tefBAD_LEDGER;

    // Delete all of the entries in the account directory.
    Keylet const ownerDirKeylet{keylet::ownerDir(ammAccountID)};
    std::shared_ptr<SLE> sleDirNode{};
    unsigned int uDirEntry{0};
    uint256 dirEntry{beast::zero};

    if (view.exists(ownerDirKeylet) &&
        dirFirst(view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry))
    {
        do
        {
            // Choose the right way to delete each directory node.
            auto sleItem = view.peek(keylet::child(dirEntry));
            if (!sleItem)
            {
                // Directory node has an invalid index.  Bail out.
                JLOG(j_.fatal())
                    << "DeleteAccount: Directory node in ledger " << view.seq()
                    << " has index to object that is missing: "
                    << to_string(dirEntry);
                return tefBAD_LEDGER;
            }

            assert(uDirEntry == 1);
            if (uDirEntry != 1)
            {
                JLOG(j_.error())
                    << "DeleteAccount iterator re-validation failed.";
                return tefBAD_LEDGER;
            }
            uDirEntry = 0;

        } while (
            dirNext(view, ownerDirKeylet.key, sleDirNode, uDirEntry, dirEntry));
    }

    // If there's still an owner directory associated with the source account
    // delete it.
    if (view.exists(ownerDirKeylet) && !view.emptyDirDelete(ownerDirKeylet))
    {
        JLOG(j_.error()) << "DeleteAccount cannot delete root dir node of "
                         << toBase58(account_);
        return tecHAS_OBLIGATIONS;
    }

    this->view().erase(sleAMM);
    this->view().erase(sleAMMRoot);

    return tesSUCCESS;
}

TER
AMMWithdraw::withdraw(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1,
    std::optional<STAmount> const& asset2,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens)
{
    if (!validLPTokens(lptAMMBalance, lpTokens))
        return tecAMM_INVALID_TOKENS;

    // Withdraw asset1
    auto res = accountSend(view, ammAccount, account_, asset1, ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Instance: failed to withdraw " << asset1;
        return res;
    }

    // Withdraw asset2
    if (asset2)
    {
        res = accountSend(view, ammAccount, account_, *asset2, ctx_.journal);
        if (res != tesSUCCESS)
        {
            JLOG(ctx_.journal.debug())
                << "AMM Instance: failed to withdraw " << *asset2;
            return res;
        }
    }

    // Withdraw LP tokens
    res = redeemIOU(view, account_, lpTokens, lpTokens.issue(), ctx_.journal);
    if (res != tesSUCCESS)
    {
        JLOG(ctx_.journal.debug())
            << "AMM Instance: failed to withdraw LPTokens";
        return res;
    }

    auto const [asset1Rem, asset2Rem, lptAMMRem] = getAMMBalances(
        view,
        ammAccount,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        ctx_.journal);
    // TODO delete AMM account and all related objects if
    // the tokens balance is 0. Must handle cases
    // if tokens are 0 but balances are not and the other way
    // around.
    if (lptAMMRem == beast::zero)
        return deleteAccount(view, ammAccount);

    return tesSUCCESS;
}

TER
AMMWithdraw::equalWithdrawalTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& tokens)
{
    auto const frac = divide(tokens, lptAMMBalance, noIssue());
    return withdraw(
        view,
        ammAccount,
        multiply(asset1Balance, frac, asset1Balance.issue()),
        multiply(asset2Balance, frac, asset2Balance.issue()),
        lptAMMBalance,
        tokens);
}

TER
AMMWithdraw::equalWithdrawalLimit(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1Out,
    STAmount const& asset2Out)
{
    auto frac = Number{asset1Out} / asset1Balance;
    auto const asset2Withdraw = asset2Balance * frac;
    if (asset2Withdraw <= asset2Out)
        return withdraw(
            view,
            ammAccount,
            asset1Out,
            toSTAmount(asset2Out.issue(), asset2Withdraw),
            lptAMMBalance,
            toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac));
    frac = Number{asset2Out} / asset2Balance;
    auto const asset1Withdraw = asset1Balance * frac;
    return withdraw(
        view,
        ammAccount,
        toSTAmount(asset1Out.issue(), asset1Withdraw),
        asset2Out,
        lptAMMBalance,
        toSTAmount(lptAMMBalance.issue(), lptAMMBalance * frac));
}

TER
AMMWithdraw::singleWithdrawal(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1Out,
    std::uint8_t weight,
    std::uint16_t tfee)
{
    auto const tokens =
        calcLPTokensOut(asset1Balance, asset1Out, lptAMMBalance, weight, tfee);
    return withdraw(
        view, ammAccount, asset1Out, std::nullopt, lptAMMBalance, tokens);
}

TER
AMMWithdraw::singleWithdrawalTokens(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1Out,
    STAmount const& tokens,
    std::uint8_t weight,
    std::uint16_t tfee)
{
    auto const asset1Withdraw = asset1Balance *
        (1 - power(1 - tokens / lptAMMBalance, 100, weight)) *
        feeMult(tfee, weight);
    if (asset1Out == beast::zero || asset1Withdraw >= asset1Out)
        return withdraw(
            view,
            ammAccount,
            toSTAmount(asset1Out.issue(), asset1Withdraw),
            std::nullopt,
            lptAMMBalance,
            tokens);
    return tecAMM_FAILED_WITHDRAW;
}

TER
AMMWithdraw::singleWithdrawMaxSP(
    Sandbox& view,
    AccountID const& ammAccount,
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    STAmount const& lptAMMBalance,
    STAmount const& asset1Out,
    STAmount const& maxSP,
    std::uint8_t weight1,
    std::uint16_t tfee)
{
#if 0
    auto const asset1BalanceUpd = asset1Balance - asset1Out;
    auto const sp =
        calcSpotPrice(asset1BalanceUpd, asset2Balance, weight1, tfee);
    auto const asset1Deposit = [&]() -> std::optional<STAmount> {
        if (sp <= STAmount{noIssue(), maxSP.mantissa(), maxSP.exponent()})
            return asset1Out;
        return changeSpotPrice(
            asset1Balance, asset2Balance, maxSP, weight1, tfee);
    }();
    if (!asset1Deposit)
        return tecAMM_FAILED_DEPOSIT;
    auto const tokens = calcLPTokensOut(
        asset1Balance, *asset1Deposit, lptAMMBalance, weight1, tfee);
    if (!tokens)
        return tecAMM_FAILED_DEPOSIT;
    return withdraw(
        view, ammAccount, *asset1Deposit, std::nullopt, lptAMMBalance, tokens);
#endif
    return tecAMM_FAILED_DEPOSIT;
}

}  // namespace ripple