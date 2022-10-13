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

#ifndef RIPPLE_APP_MISC_AMM_FORMULAE_H_INCLUDED
#define RIPPLE_APP_MISC_AMM_FORMULAE_H_INCLUDED

#include <ripple/basics/IOUAmount.h>
#include <ripple/basics/Number.h>
#include <ripple/protocol/Issue.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STAmount.h>

#include <type_traits>

namespace ripple {

namespace {

/** RAII to save/restore rounding mode on swap in/out.
 * AMM pool invariant - the product (A * B) after swap in/out has to remain
 * at least the same: (A + in) * (A - out) >= A * B
 * XRP round-off may result in a smaller product after swap in/out.
 * To fix this:
 *     - if on swapIn the out is XRP then the amount is round-off
 *       downward, making the product slightly larger since out
 *       value is reduced.
 *     - if on swapOut the in is XRP then the amount is round-off
 *       upward, making the product slightly larger since in
 *       value is increased.
 */
class RoundingMode
{
    Number::rounding_mode mode_;

public:
    template <typename Amt>
    RoundingMode(Amt const& a, Number::rounding_mode mode)
        : mode_(Number::getround())
    {
        if constexpr (!std::is_same_v<Amt, IOUAmount>)
            if (isXRP(a))
                Number::setround(mode);
    }
    RoundingMode(Number::rounding_mode mode) : mode_(Number::getround())
    {
        Number::setround(mode);
    }
    ~RoundingMode()
    {
        Number::setround(mode_);
    }
};

}  // namespace

inline STAmount
toSTAmount(
    Issue const& issue,
    Number const& n,
    Number::rounding_mode mode = Number::rounding_mode::to_nearest)
{
    if (isXRP(issue))
    {
        RoundingMode rm(mode);
        return STAmount{issue, static_cast<std::int64_t>(n)};
    }
    return STAmount{issue, n.mantissa(), n.exponent()};
}

template <typename A>
STAmount
toSTAmount(A const& a, Issue const& issue)
{
    if constexpr (std::is_same_v<IOUAmount, A>)
        return toSTAmount(a, issue);
    else if constexpr (std::is_same_v<XRPAmount, A>)
        return toSTAmount(a);
    else
        return a;
}

/** Calculate LP Tokens given AMM pool reserves.
 * @param asset1 AMM one side of the pool reserve
 * @param asset2 AMM another side of the pool reserve
 * @return LP Tokens as IOU
 */
STAmount
calcAMMLPT(
    STAmount const& asset1,
    STAmount const& asset2,
    Issue const& lptIssue);

/** Convert to the fee from the basis points
 * @param tfee  trading fee in basis points
 */
inline Number
getFee(std::uint16_t tfee)
{
    return Number{tfee} / Number{100000};
}

/** Get fee multiplier (1 - tfee)
 * @tfee trading fee in basis points
 */
inline Number
feeMult(std::uint16_t tfee)
{
    return 1 - getFee(tfee);
}

/** Get fee multiplier (1 - tfee / 2)
 * @tfee trading fee in basis points
 */
inline Number
feeMultHalf(std::uint16_t tfee)
{
    return 1 - getFee(tfee) / 2;
}

/** Calculate LP Tokens given asset's deposit amount.
 * @param asset1Balance current AMM asset1 balance
 * @param asset1Deposit requested asset1 deposit amount
 * @param lpTokensBalance LP Tokens balance
 * @param tfee trading fee in basis points
 * @return tokens
 */
STAmount
calcLPTokensIn(
    STAmount const& asset1Balance,
    STAmount const& asset1Deposit,
    STAmount const& lpTokensBalance,
    std::uint16_t tfee);

/** Calculate asset deposit given LP Tokens.
 * @param asset1Balance current AMM asset1 balance
 * @param lpTokensBalance LP Tokens balance
 * @param ammTokensBalance AMM LPT balance
 * @param tfee trading fee in basis points
 * @return
 */
STAmount
calcAssetIn(
    STAmount const& asset1Balance,
    STAmount const& lpTokensBalance,
    STAmount const& ammTokensBalance,
    std::uint16_t tfee);

/** Calculate LP Tokens given asset's withdraw amount. Return 0
 * if can't calculate.
 * @param asset1Balance current AMM asset1 balance
 * @param asset1Withdraw requested asset1 withdraw amount
 * @param lpTokensBalance LP Tokens balance
 * @param tfee trading fee in basis points
 * @return tokens out amount
 */
STAmount
calcLPTokensOut(
    STAmount const& asset1Balance,
    STAmount const& asset1Withdraw,
    STAmount const& lpTokensBalance,
    std::uint16_t tfee);

/** Calculate asset withdrawal by tokens
 * @param assetBalance balance of the asset being withdrawn
 * @param lptAMMBalance total AMM Tokens balance
 * @param lpTokens LP Tokens balance
 * @param tfee trading fee in basis points
 * @return calculated asset amount
 */
STAmount
calcWithdrawalByTokens(
    STAmount const& assetBalance,
    STAmount const& lptAMMBalance,
    STAmount const& lpTokens,
    std::uint32_t tfee);

/** Calculate AMM's Spot Price
 * @param asset1Balance current AMM asset1 balance
 * @param asset2Balance current AMM asset2 balance
 * @param tfee trading fee in basis points
 * @return spot price
 */
STAmount
calcSpotPrice(
    STAmount const& asset1Balance,
    STAmount const& asset2Balance,
    std::uint16_t tfee);

/** Get asset2 amount based on new AMM's Spot Price.
 * @param asset1Balance current AMM asset1 balance
 * @param asset2Balance current AMM asset2 balance
 * @param newSP requested SP of asset1 relative to asset2
 * @param tfee trading fee in basis points
 * @return
 */
std::optional<STAmount>
changeSpotPrice(
    STAmount const& assetInBalance,
    STAmount const& assetOuBalance,
    STAmount const& newSP,
    std::uint16_t tfee);

/** Find in/out amounts to change the spot price quality to the requested
 * quality.
 * @param pool AMM pool balances
 * @param quality requested quality
 * @param tfee trading fee in basis points
 * @return seated in/out amounts if the quality can be changed
 */
std::optional<Amounts>
changeSpotPriceQuality(
    Amounts const& pool,
    Quality const& quality,
    std::uint32_t tfee);

/** Swap assetIn into the pool and swap out a proportional amount
 * of the other asset.
 * @param pool current AMM pool balances
 * @param assetIn amount to swap in
 * @param tfee trading fee in basis points
 * @return
 */
template <typename TIn, typename TOut>
TOut
swapAssetIn(
    TAmounts<TIn, TOut> const& pool,
    TIn const& assetIn,
    std::uint16_t tfee)
{
    auto const res =
        pool.out - (pool.in * pool.out) / (pool.in + assetIn * feeMult(tfee));
    {
        RoundingMode rm(pool.out, Number::rounding_mode::downward);
        if constexpr (std::is_same_v<TOut, IOUAmount>)
            return IOUAmount(res);
        if constexpr (std::is_same_v<TOut, XRPAmount>)
            return XRPAmount(static_cast<std::int64_t>(res));
        if constexpr (std::is_same_v<TOut, STAmount>)
        {
            if (isXRP(pool.out))
                return STAmount(
                    pool.out.issue(), static_cast<std::int64_t>(res));
            return STAmount(pool.out.issue(), res.mantissa(), res.exponent());
        }
    }
}

/** Swap assetOut out of the pool and swap in a proportional amount
 * of the other asset.
 * @param pool current AMM pool balances
 * @param assetOut amount to swap out
 * @param tfee trading fee in basis points
 * @return
 */
template <typename TIn, typename TOut>
TIn
swapAssetOut(
    TAmounts<TIn, TOut> const& pool,
    TOut const& assetOut,
    std::uint16_t tfee)
{
    auto const res = ((pool.in * pool.out) / (pool.out - assetOut) - pool.in) /
        feeMult(tfee);
    {
        RoundingMode rm(pool.in, Number::rounding_mode::upward);
        if constexpr (std::is_same_v<TIn, IOUAmount>)
            return IOUAmount(res);
        if constexpr (std::is_same_v<TIn, XRPAmount>)
            return XRPAmount((std::int64_t)res);
        if constexpr (std::is_same_v<TIn, STAmount>)
        {
            if (isXRP(pool.in))
                return STAmount(
                    pool.in.issue(), static_cast<std::int64_t>(res));
            return STAmount(pool.in.issue(), res.mantissa(), res.exponent());
        }
    }
}

/** Swap assetIn into the pool and swap out a proportional amount
 * of the other asset.
 * @param pool current AMM pool balances
 * @param assetIn amount to swap in
 * @param tfee trading fee in basis points
 * @return
 */
template <typename TIn>
STAmount
swapAssetInCvt(Amounts const& pool, TIn const& assetIn, std::uint16_t tfee)
{
    auto const res = toSTAmount(
        pool.out.issue(),
        pool.out - (pool.in * pool.out) / (pool.in + assetIn * feeMult(tfee)),
        Number::rounding_mode::downward);
    return res;
}

/** Swap assetOut out of the pool and swap in a proportional amount
 * of the other asset.
 * @param pool current AMM pool balances
 * @param assetOut amount to swap out
 * @param tfee trading fee in basis points
 * @return
 */
template <typename TOut>
STAmount
swapAssetOutCvt(Amounts const& pool, TOut const& assetOut, std::uint16_t tfee)
{
    auto const res = toSTAmount(
        pool.in.issue(),
        ((pool.in * pool.out) / (pool.out - assetOut) - pool.in) /
            feeMult(tfee),
        Number::rounding_mode::upward);
    return res;
}

/** Get T amount
 */
template <typename T>
T
get(STAmount const& a)
{
    if constexpr (std::is_same_v<T, IOUAmount>)
        return a.iou();
    else if constexpr (std::is_same_v<T, XRPAmount>)
        return a.xrp();
    else
        return a;
}

/** Return square of n.
 */
Number
square(Number const& n);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_AMM_FORMULAE_H_INCLUDED
