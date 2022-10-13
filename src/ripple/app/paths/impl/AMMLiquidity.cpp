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
#include <ripple/app/paths/AMMLiquidity.h>
#include <ripple/app/paths/AMMOffer.h>

namespace ripple {

AMMLiquidity::AMMLiquidity(
    ReadView const& view,
    AccountID const& ammAccountID,
    std::uint32_t tradingFee,
    Issue const& in,
    Issue const& out,
    AMMContext& offerCounter,
    beast::Journal j)
    : offerCounter_(offerCounter)
    , ammAccountID_(ammAccountID)
    , tradingFee_(tradingFee)
    , initialBalances_{STAmount{in}, STAmount{out}}
    , j_(j)
{
    initialBalances_ = fetchBalances(view);
}

Amounts
AMMLiquidity::fetchBalances(ReadView const& view) const
{
    auto const assetIn =
        ammAccountHolds(view, ammAccountID_, initialBalances_.in.issue());
    auto const assetOut =
        ammAccountHolds(view, ammAccountID_, initialBalances_.out.issue());
    // This should not happen.
    if (assetIn < beast::zero || assetOut < beast::zero)
        Throw<std::runtime_error>("AMMLiquidity: invalid balances");

    return Amounts(assetIn, assetOut);
}

Amounts
AMMLiquidity::generateFibSeqOffer(const Amounts& balances) const
{
    auto const n = offerCounter_.curIters();
    Amounts cur{};
    Number x{};
    Number y{};

    cur.in = toSTAmount(
        balances.in.issue(),
        (Number(5) / 20000) * initialBalances_.in,
        Number::rounding_mode::upward);
    cur.out = swapAssetIn(initialBalances_, cur.in, tradingFee_);
    y = cur.out;
    if (n == 0)
        return cur;

    std::uint16_t i = 0;
    auto const total = [&]() {
        Number total{};
        do
        {
            total = x + y;
            x = y;
            y = total;
        } while (++i < n);
        return total;
    }();
    cur.out = toSTAmount(
        balances.out.issue(), total, Number::rounding_mode::downward);
    cur.in = swapAssetOut(balances, cur.out, tradingFee_);
    return cur;
}

template <typename TIn, typename TOut>
std::optional<AMMOffer<TIn, TOut>>
AMMLiquidity::getOffer(
    ReadView const& view,
    std::optional<Quality> const& clobQuality) const
{
    // Can't generate more offers. Only applies if generating
    // based on Fibonacci sequence.
    if (offerCounter_.maxItersReached())
        return std::nullopt;

    auto const balances = fetchBalances(view);

    JLOG(j_.debug()) << "AMMLiquidity::getOffer balances "
                     << initialBalances_.in << " " << initialBalances_.out
                     << " new balances " << balances.in << " " << balances.out;

    // Can't generate AMM with a better quality than CLOB's
    // quality if AMM's Spot Price quality is less than CLOB quality.
    if (clobQuality && Quality{balances} < *clobQuality)
    {
        JLOG(j_.debug()) << "AMMLiquidity::getOffer, higher clob quality";
        return std::nullopt;
    }

    auto offer = [&]() -> std::optional<AMMOffer<TIn, TOut>> {
        if (offerCounter_.multiPath())
        {
            auto const offer = generateFibSeqOffer(balances);
            if (clobQuality && Quality{offer} < *clobQuality)
                return std::nullopt;
            return AMMOffer<TIn, TOut>(
                *this,
                {get<TIn>(offer.in), get<TOut>(offer.out)},
                std::nullopt);
        }
        else if (
            auto const offer = clobQuality
                ? changeSpotPriceQuality(balances, *clobQuality, tradingFee_)
                : balances)
        {
            return AMMOffer<TIn, TOut>(
                *this,
                {get<TIn>(offer->in), get<TOut>(offer->out)},
                {{get<TIn>(balances.in), get<TOut>(balances.out)}});
        }
        return std::nullopt;
    }();

    if (offer && offer->amount().in > beast::zero &&
        offer->amount().out > beast::zero)
    {
        JLOG(j_.debug()) << "AMMLiquidity::getOffer, created "
                         << toSTAmount(offer->amount().in, balances.in.issue())
                         << " "
                         << toSTAmount(
                                offer->amount().out, balances.out.issue());
        // The new pool product must be greater or equal to the original pool
        // product. Swap in/out formulas are used in case of one-path, which by
        // design maintain the product invariant. The FibSeq is also generated
        // with the swap in/out formulas except when the offer has to
        // be reduced, in which case it is changed proportionally to
        // the original offer quality. It can be shown that in this case
        // the new pool product is greater than the original pool product.
        // Since the result for XRP is fractional, round downward
        // out amount and round upward in amount to maintain the invariant.
        // This is done in Number/STAmount conversion.
        return offer;
    }
    else
    {
        JLOG(j_.debug()) << "AMMLiquidity::getOffer, failed "
                         << offer.has_value();
    }

    return std::nullopt;
}

template std::optional<AMMOffer<STAmount, STAmount>>
AMMLiquidity::getOffer<STAmount, STAmount>(
    ReadView const& view,
    std::optional<Quality> const& clobQuality) const;
template std::optional<AMMOffer<IOUAmount, IOUAmount>>
AMMLiquidity::getOffer<IOUAmount, IOUAmount>(
    ReadView const& view,
    std::optional<Quality> const& clobQuality) const;
template std::optional<AMMOffer<XRPAmount, IOUAmount>>
AMMLiquidity::getOffer<XRPAmount, IOUAmount>(
    ReadView const& view,
    std::optional<Quality> const& clobQuality) const;
template std::optional<AMMOffer<IOUAmount, XRPAmount>>
AMMLiquidity::getOffer<IOUAmount, XRPAmount>(
    ReadView const& view,
    std::optional<Quality> const& clobQuality) const;

}  // namespace ripple