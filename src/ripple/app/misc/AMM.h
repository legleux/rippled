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
#ifndef RIPPLE_APP_MISC_AMM_H_INLCUDED
#define RIPPLE_APP_MISC_AMM_H_INLCUDED

#include <ripple/beast/utility/Journal.h>
#include <ripple/protocol/Quality.h>
#include <ripple/protocol/STAmount.h>
#include <ripple/protocol/TER.h>
#include <ripple/protocol/digest.h>

namespace ripple {

class ReadView;
class STLedgerEntry;

/** Calculate AMM account ID.
 * @return AMM account id
 */
template <typename... Args>
AccountID
calcAccountID(Args const&... args)
{
    // TODO use private/public keys to generate account id
    ripesha_hasher rsh;
    auto hash = sha512Half(args...);
    rsh(hash.data(), hash.size());
    return AccountID{static_cast<ripesha_hasher::result_type>(rsh)};
}

/** Calculate AMM's unique hash.
 * Issues are sorted in canonical order.
 * @param weight1 weight of the issue1
 * @param issue1 issue of one side of the AMM instance
 * @param issue2 issue of the other side of the AMM instance
 * @return hash
 */
uint256
calcAMMHash(std::uint8_t weight1, Issue const& issue1, Issue const& issue2);

/** Same as above. Returns weight in the issues
 * canonical order in addition to the hash.
 * weight is weight1 if issue1 < issue2, otherwise
 * it's 100 - weight1.
 */
std::pair<uint256, std::uint8_t>
calcAMMHashAndWeight(int weight1, Issue const& issue1, Issue const& issue2);

/** Calculate Liquidity Provider Token (LPT) Currency.
 * @param ammAccountID AMM's instance account id
 * @return LPT Currency
 */
Currency
calcLPTCurrency(AccountID const& ammAccountID);

/** Calculate LPT Issue.
 * @param ammAccountID AMM's instance account id
 * @return LPT Issue
 */
Issue
calcLPTIssue(AccountID const& ammAccountID);

/** Get AMM pool balances in/out. AMM trades in both
 * directions but given specific BookStep
 * the direction is set to in/out.
 */
std::pair<STAmount, STAmount>
getAMMPoolBalances(
    ReadView const& view,
    AccountID const& ammAccountID,
    Issue const& in,
    Issue const& out,
    beast::Journal const j);

/** Get AMM pool and LPT balances.
 * If issue1 is set then return reserves
 * in the order issue1 first. If in addition
 * issue2 is set then validate it matches
 * the issue of the second asset. This
 * method is needed for AMMTrade functionality
 * when trade options don't include both issues.
 */
std::tuple<STAmount, STAmount, STAmount>
getAMMBalances(
    ReadView const& view,
    AccountID const& ammAccountID,
    std::optional<AccountID> const& lpAccountID,
    std::optional<Issue> const& issue1,
    std::optional<Issue> const& issue2,
    beast::Journal const j);

/** Return the balance of LP tokens.
 */
STAmount
getLPTokens(
    ReadView const& view,
    AccountID const& ammAccountID,
    AccountID const& lpAccount,
    beast::Journal const j);

/** Validate the amount.
 */
std::optional<TEMcodes>
validAmount(std::optional<STAmount> const& a, bool zero = false);

/** Check if the line is frozen from the issuer.
 */
bool
isFrozen(ReadView const& view, std::optional<STAmount> const& a);

/** Validate deposit/withdraw lpt amount. The amount must not
 * exceed 30% of the pool share and must not be 0.
 * @param lptAMMBalance current AMM LPT balance
 * @param tokens requested LPT amount to deposit/withdraw
 * @return
 */
bool
validLPTokens(STAmount const& lptAMMBalance, STAmount const& tokens);

/** Get AMM SLE and verify that the AMM account exists.
 * Return null if SLE not found or AMM account doesn't exist.
 * @param view
 * @param ammHash
 * @return
 */
std::shared_ptr<SLE const>
getAMMSle(ReadView const& view, uint256 ammHash);

}  // namespace ripple

#endif  // RIPPLE_APP_MISC_AMM_H_INLCUDED