//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_UNIT_TEST_AMOUNT_HPP
#define BEAST_UNIT_TEST_AMOUNT_HPP

#include <cstddef>
#include <ostream>
#include <string>

namespace beast {
namespace unit_test {

/** Utility for producing nicely composed output of amounts with units. */
class amount
{
private:
    std::size_t n_;
    std::string const& what_;

public:
    amount(amount const&) = default;
    amount&
    operator=(amount const&) = delete;

    template <class = void>
    amount(std::size_t n, std::string const& what);

    friend std::ostream&
    operator<<(std::ostream& s, amount const& t);
};

template <class>
amount::amount(std::size_t n, std::string const& what) : n_(n), what_(what)
{
}

inline std::ostream&
operator<<(std::ostream& s, amount const& t)
{
    s << t.n_ << " " << t.what_ << ((t.n_ != 1) ? "s" : "");
    return s;
}

}  // namespace unit_test
}  // namespace beast

#endif
