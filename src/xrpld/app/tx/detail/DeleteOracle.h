//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#ifndef RIPPLE_TX_DELETEORACLE_H_INCLUDED
#define RIPPLE_TX_DELETEORACLE_H_INCLUDED

#include <xrpld/app/tx/detail/Transactor.h>

namespace ripple {

/**
    Price Oracle is a system that acts as a bridge between
    a blockchain network and the external world, providing off-chain price data
    to decentralized applications (dApps) on the blockchain. This implementation
    conforms to the requirements specified in the XLS-47d.

    The DeleteOracle transactor implements the deletion of Oracle objects.
*/

class DeleteOracle : public Transactor
{
public:
    static constexpr ConsequencesFactoryType ConsequencesFactory{Normal};

    explicit DeleteOracle(ApplyContext& ctx) : Transactor(ctx)
    {
    }

    static NotTEC
    preflight(PreflightContext const& ctx);

    static TER
    preclaim(PreclaimContext const& ctx);

    TER
    doApply() override;

    static TER
    deleteOracle(
        ApplyView& view,
        std::shared_ptr<SLE> const& sle,
        AccountID const& account,
        beast::Journal j);
};

using OracleDelete = DeleteOracle;

}  // namespace ripple

#endif  // RIPPLE_TX_DELETEORACLE_H_INCLUDED
