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

#include <xrpld/app/misc/CanonicalTXSet.h>

namespace ripple {

bool
operator<(CanonicalTXSet::Key const& lhs, CanonicalTXSet::Key const& rhs)
{
    if (lhs.account_ < rhs.account_)
        return true;

    if (lhs.account_ > rhs.account_)
        return false;

    if (lhs.seqProxy_ < rhs.seqProxy_)
        return true;

    if (lhs.seqProxy_ > rhs.seqProxy_)
        return false;

    return lhs.txId_ < rhs.txId_;
}

uint256
CanonicalTXSet::accountKey(AccountID const& account)
{
    uint256 ret = beast::zero;
    memcpy(ret.begin(), account.begin(), account.size());
    ret ^= salt_;
    return ret;
}

void
CanonicalTXSet::insert(std::shared_ptr<STTx const> const& txn)
{
    map_.insert(std::make_pair(
        Key(accountKey(txn->getAccountID(sfAccount)),
            txn->getSeqProxy(),
            txn->getTransactionID()),
        txn));
}

std::shared_ptr<STTx const>
CanonicalTXSet::popAcctTransaction(std::shared_ptr<STTx const> const& tx)
{
    // Determining the next viable transaction for an account with Tickets:
    //
    //  1. Prioritize transactions with Sequences over transactions with
    //     Tickets.
    //
    //  2. For transactions not using Tickets, look for consecutive Sequence
    //     numbers. For transactions using Tickets, don't worry about
    //     consecutive Sequence numbers. Tickets can process out of order.
    //
    //  3. After handling all transactions with Sequences, return Tickets
    //     with the lowest Ticket ID first.
    std::shared_ptr<STTx const> result;
    uint256 const effectiveAccount{accountKey(tx->getAccountID(sfAccount))};

    auto const seqProxy = tx->getSeqProxy();
    Key const after(effectiveAccount, seqProxy, beast::zero);
    auto const itrNext{map_.lower_bound(after)};
    if (itrNext != map_.end() &&
        itrNext->first.getAccount() == effectiveAccount &&
        (!itrNext->second->getSeqProxy().isSeq() ||
         itrNext->second->getSeqProxy().value() == seqProxy.value() + 1))
    {
        result = std::move(itrNext->second);
        map_.erase(itrNext);
    }

    return result;
}

}  // namespace ripple
