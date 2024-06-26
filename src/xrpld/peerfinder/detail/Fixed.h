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

#ifndef RIPPLE_PEERFINDER_FIXED_H_INCLUDED
#define RIPPLE_PEERFINDER_FIXED_H_INCLUDED

#include <xrpld/peerfinder/detail/Tuning.h>

namespace ripple {
namespace PeerFinder {

/** Metadata for a Fixed slot. */
class Fixed
{
public:
    explicit Fixed(clock_type& clock) : m_when(clock.now()), m_failures(0)
    {
    }

    Fixed(Fixed const&) = default;

    /** Returns the time after which we shoud allow a connection attempt. */
    clock_type::time_point const&
    when() const
    {
        return m_when;
    }

    /** Updates metadata to reflect a failed connection. */
    void
    failure(clock_type::time_point const& now)
    {
        m_failures =
            std::min(m_failures + 1, Tuning::connectionBackoff.size() - 1);
        m_when =
            now + std::chrono::minutes(Tuning::connectionBackoff[m_failures]);
    }

    /** Updates metadata to reflect a successful connection. */
    void
    success(clock_type::time_point const& now)
    {
        m_failures = 0;
        m_when = now;
    }

private:
    clock_type::time_point m_when;
    std::size_t m_failures;
};

}  // namespace PeerFinder
}  // namespace ripple

#endif
