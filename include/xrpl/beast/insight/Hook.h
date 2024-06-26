//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_INSIGHT_HOOK_H_INCLUDED
#define BEAST_INSIGHT_HOOK_H_INCLUDED

#include <xrpl/beast/insight/HookImpl.h>

#include <memory>

namespace beast {
namespace insight {

/** A reference to a handler for performing polled collection. */
class Hook final
{
public:
    /** Create a null hook.
        A null hook has no associated handler.
    */
    Hook()
    {
    }

    /** Create a hook referencing the specified implementation.
        Normally this won't be called directly. Instead, call the appropriate
        factory function in the Collector interface.
        @see Collector.
    */
    explicit Hook(std::shared_ptr<HookImpl> const& impl) : m_impl(impl)
    {
    }

    std::shared_ptr<HookImpl> const&
    impl() const
    {
        return m_impl;
    }

private:
    std::shared_ptr<HookImpl> m_impl;
};

}  // namespace insight
}  // namespace beast

#endif
