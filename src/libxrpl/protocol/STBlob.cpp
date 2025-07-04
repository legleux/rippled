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

#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/Serializer.h>

#include <cstddef>
#include <string>
#include <utility>

namespace ripple {

STBlob::STBlob(SerialIter& st, SField const& name)
    : STBase(name), value_(st.getVLBuffer())
{
}

STBase*
STBlob::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STBlob::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STBlob::getSType() const
{
    return STI_VL;
}

std::string
STBlob::getText() const
{
    return strHex(value_);
}

void
STBlob::add(Serializer& s) const
{
    XRPL_ASSERT(getFName().isBinary(), "ripple::STBlob::add : field is binary");
    XRPL_ASSERT(
        (getFName().fieldType == STI_VL) ||
            (getFName().fieldType == STI_ACCOUNT),
        "ripple::STBlob::add : valid field type");
    s.addVL(value_.data(), value_.size());
}

bool
STBlob::isEquivalent(STBase const& t) const
{
    STBlob const* v = dynamic_cast<STBlob const*>(&t);
    return v && (value_ == v->value_);
}

bool
STBlob::isDefault() const
{
    return value_.empty();
}

}  // namespace ripple
