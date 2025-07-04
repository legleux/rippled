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

#include <xrpl/basics/LocalValue.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/MPTAmount.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/SystemParameters.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/multiprecision/detail/default_ops.hpp>
#include <boost/multiprecision/fwd.hpp>
#include <boost/regex/v5/regbase.hpp>
#include <boost/regex/v5/regex.hpp>
#include <boost/regex/v5/regex_fwd.hpp>
#include <boost/regex/v5/regex_match.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ripple {

namespace {

// Use a static inside a function to help prevent order-of-initialzation issues
LocalValue<bool>&
getStaticSTAmountCanonicalizeSwitchover()
{
    static LocalValue<bool> r{true};
    return r;
}
}  // namespace

bool
getSTAmountCanonicalizeSwitchover()
{
    return *getStaticSTAmountCanonicalizeSwitchover();
}

void
setSTAmountCanonicalizeSwitchover(bool v)
{
    *getStaticSTAmountCanonicalizeSwitchover() = v;
}

static std::uint64_t const tenTo14 = 100000000000000ull;
static std::uint64_t const tenTo14m1 = tenTo14 - 1;
static std::uint64_t const tenTo17 = tenTo14 * 1000;

//------------------------------------------------------------------------------
static std::int64_t
getInt64Value(STAmount const& amount, bool valid, char const* error)
{
    if (!valid)
        Throw<std::runtime_error>(error);
    XRPL_ASSERT(
        amount.exponent() == 0, "ripple::getInt64Value : exponent is zero");

    auto ret = static_cast<std::int64_t>(amount.mantissa());

    XRPL_ASSERT(
        static_cast<std::uint64_t>(ret) == amount.mantissa(),
        "ripple::getInt64Value : mantissa must roundtrip");

    if (amount.negative())
        ret = -ret;

    return ret;
}

static std::int64_t
getSNValue(STAmount const& amount)
{
    return getInt64Value(amount, amount.native(), "amount is not native!");
}

static std::int64_t
getMPTValue(STAmount const& amount)
{
    return getInt64Value(
        amount, amount.holds<MPTIssue>(), "amount is not MPT!");
}

static bool
areComparable(STAmount const& v1, STAmount const& v2)
{
    if (v1.holds<Issue>() && v2.holds<Issue>())
        return v1.native() == v2.native() &&
            v1.get<Issue>().currency == v2.get<Issue>().currency;
    if (v1.holds<MPTIssue>() && v2.holds<MPTIssue>())
        return v1.get<MPTIssue>() == v2.get<MPTIssue>();
    return false;
}

STAmount::STAmount(SerialIter& sit, SField const& name) : STBase(name)
{
    std::uint64_t value = sit.get64();

    // native or MPT
    if ((value & cIssuedCurrency) == 0)
    {
        if ((value & cMPToken) != 0)
        {
            // is MPT
            mOffset = 0;
            mIsNegative = (value & cPositive) == 0;
            mValue = (value << 8) | sit.get8();
            mAsset = sit.get192();
            return;
        }
        // else is XRP
        mAsset = xrpIssue();
        // positive
        if ((value & cPositive) != 0)
        {
            mValue = value & cValueMask;
            mOffset = 0;
            mIsNegative = false;
            return;
        }

        // negative
        if (value == 0)
            Throw<std::runtime_error>("negative zero is not canonical");

        mValue = value & cValueMask;
        mOffset = 0;
        mIsNegative = true;
        return;
    }

    Issue issue;
    issue.currency = sit.get160();

    if (isXRP(issue.currency))
        Throw<std::runtime_error>("invalid native currency");

    issue.account = sit.get160();

    if (isXRP(issue.account))
        Throw<std::runtime_error>("invalid native account");

    // 10 bits for the offset, sign and "not native" flag
    int offset = static_cast<int>(value >> (64 - 10));

    value &= ~(1023ull << (64 - 10));

    if (value)
    {
        bool isNegative = (offset & 256) == 0;
        offset = (offset & 255) - 97;  // center the range

        if (value < cMinValue || value > cMaxValue || offset < cMinOffset ||
            offset > cMaxOffset)
        {
            Throw<std::runtime_error>("invalid currency value");
        }

        mAsset = issue;
        mValue = value;
        mOffset = offset;
        mIsNegative = isNegative;
        canonicalize();
        return;
    }

    if (offset != 512)
        Throw<std::runtime_error>("invalid currency value");

    mAsset = issue;
    mValue = 0;
    mOffset = 0;
    mIsNegative = false;
    canonicalize();
}

STAmount::STAmount(SField const& name, std::int64_t mantissa)
    : STBase(name), mAsset(xrpIssue()), mOffset(0)
{
    set(mantissa);
}

STAmount::STAmount(SField const& name, std::uint64_t mantissa, bool negative)
    : STBase(name)
    , mAsset(xrpIssue())
    , mValue(mantissa)
    , mOffset(0)
    , mIsNegative(negative)
{
    XRPL_ASSERT(
        mValue <= std::numeric_limits<std::int64_t>::max(),
        "ripple::STAmount::STAmount(SField, std::uint64_t, bool) : maximum "
        "mantissa input");
}

STAmount::STAmount(SField const& name, STAmount const& from)
    : STBase(name)
    , mAsset(from.mAsset)
    , mValue(from.mValue)
    , mOffset(from.mOffset)
    , mIsNegative(from.mIsNegative)
{
    XRPL_ASSERT(
        mValue <= std::numeric_limits<std::int64_t>::max(),
        "ripple::STAmount::STAmount(SField, STAmount) : maximum input");
    canonicalize();
}

//------------------------------------------------------------------------------

STAmount::STAmount(std::uint64_t mantissa, bool negative)
    : mAsset(xrpIssue())
    , mValue(mantissa)
    , mOffset(0)
    , mIsNegative(mantissa != 0 && negative)
{
    XRPL_ASSERT(
        mValue <= std::numeric_limits<std::int64_t>::max(),
        "ripple::STAmount::STAmount(std::uint64_t, bool) : maximum mantissa "
        "input");
}

STAmount::STAmount(XRPAmount const& amount)
    : mAsset(xrpIssue()), mOffset(0), mIsNegative(amount < beast::zero)
{
    if (mIsNegative)
        mValue = unsafe_cast<std::uint64_t>(-amount.drops());
    else
        mValue = unsafe_cast<std::uint64_t>(amount.drops());

    canonicalize();
}

std::unique_ptr<STAmount>
STAmount::construct(SerialIter& sit, SField const& name)
{
    return std::make_unique<STAmount>(sit, name);
}

STBase*
STAmount::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STAmount::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

//------------------------------------------------------------------------------
//
// Conversion
//
//------------------------------------------------------------------------------
XRPAmount
STAmount::xrp() const
{
    if (!native())
        Throw<std::logic_error>(
            "Cannot return non-native STAmount as XRPAmount");

    auto drops = static_cast<XRPAmount::value_type>(mValue);
    XRPL_ASSERT(mOffset == 0, "ripple::STAmount::xrp : amount is canonical");

    if (mIsNegative)
        drops = -drops;

    return XRPAmount{drops};
}

IOUAmount
STAmount::iou() const
{
    if (native() || !holds<Issue>())
        Throw<std::logic_error>("Cannot return non-IOU STAmount as IOUAmount");

    auto mantissa = static_cast<std::int64_t>(mValue);
    auto exponent = mOffset;

    if (mIsNegative)
        mantissa = -mantissa;

    return {mantissa, exponent};
}

MPTAmount
STAmount::mpt() const
{
    if (!holds<MPTIssue>())
        Throw<std::logic_error>("Cannot return STAmount as MPTAmount");

    auto value = static_cast<MPTAmount::value_type>(mValue);
    XRPL_ASSERT(mOffset == 0, "ripple::STAmount::mpt : amount is canonical");

    if (mIsNegative)
        value = -value;

    return MPTAmount{value};
}

STAmount&
STAmount::operator=(IOUAmount const& iou)
{
    XRPL_ASSERT(
        native() == false,
        "ripple::STAmount::operator=(IOUAmount) : is not XRP");
    mOffset = iou.exponent();
    mIsNegative = iou < beast::zero;
    if (mIsNegative)
        mValue = static_cast<std::uint64_t>(-iou.mantissa());
    else
        mValue = static_cast<std::uint64_t>(iou.mantissa());
    return *this;
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

STAmount&
STAmount::operator+=(STAmount const& a)
{
    *this = *this + a;
    return *this;
}

STAmount&
STAmount::operator-=(STAmount const& a)
{
    *this = *this - a;
    return *this;
}

STAmount
operator+(STAmount const& v1, STAmount const& v2)
{
    if (!areComparable(v1, v2))
        Throw<std::runtime_error>("Can't add amounts that are't comparable!");

    if (v2 == beast::zero)
        return v1;

    if (v1 == beast::zero)
    {
        // Result must be in terms of v1 currency and issuer.
        return {
            v1.getFName(),
            v1.asset(),
            v2.mantissa(),
            v2.exponent(),
            v2.negative()};
    }

    if (v1.native())
        return {v1.getFName(), getSNValue(v1) + getSNValue(v2)};
    if (v1.holds<MPTIssue>())
        return {v1.mAsset, v1.mpt().value() + v2.mpt().value()};

    if (getSTNumberSwitchover())
    {
        auto x = v1;
        x = v1.iou() + v2.iou();
        return x;
    }

    int ov1 = v1.exponent(), ov2 = v2.exponent();
    std::int64_t vv1 = static_cast<std::int64_t>(v1.mantissa());
    std::int64_t vv2 = static_cast<std::int64_t>(v2.mantissa());

    if (v1.negative())
        vv1 = -vv1;

    if (v2.negative())
        vv2 = -vv2;

    while (ov1 < ov2)
    {
        vv1 /= 10;
        ++ov1;
    }

    while (ov2 < ov1)
    {
        vv2 /= 10;
        ++ov2;
    }

    // This addition cannot overflow an std::int64_t. It can overflow an
    // STAmount and the constructor will throw.

    std::int64_t fv = vv1 + vv2;

    if ((fv >= -10) && (fv <= 10))
        return {v1.getFName(), v1.asset()};

    if (fv >= 0)
        return STAmount{
            v1.getFName(),
            v1.asset(),
            static_cast<std::uint64_t>(fv),
            ov1,
            false};

    return STAmount{
        v1.getFName(), v1.asset(), static_cast<std::uint64_t>(-fv), ov1, true};
}

STAmount
operator-(STAmount const& v1, STAmount const& v2)
{
    return v1 + (-v2);
}

//------------------------------------------------------------------------------

std::uint64_t const STAmount::uRateOne = getRate(STAmount(1), STAmount(1));

void
STAmount::setIssue(Asset const& asset)
{
    mAsset = asset;
}

// Convert an offer into an index amount so they sort by rate.
// A taker will take the best, lowest, rate first.
// (e.g. a taker will prefer pay 1 get 3 over pay 1 get 2.
// --> offerOut: takerGets: How much the offerer is selling to the taker.
// -->  offerIn: takerPays: How much the offerer is receiving from the taker.
// <--    uRate: normalize(offerIn/offerOut)
//             A lower rate is better for the person taking the order.
//             The taker gets more for less with a lower rate.
// Zero is returned if the offer is worthless.
std::uint64_t
getRate(STAmount const& offerOut, STAmount const& offerIn)
{
    if (offerOut == beast::zero)
        return 0;
    try
    {
        STAmount r = divide(offerIn, offerOut, noIssue());
        if (r == beast::zero)  // offer is too good
            return 0;
        XRPL_ASSERT(
            (r.exponent() >= -100) && (r.exponent() <= 155),
            "ripple::getRate : exponent inside range");
        std::uint64_t ret = r.exponent() + 100;
        return (ret << (64 - 8)) | r.mantissa();
    }
    catch (std::exception const&)
    {
    }

    // overflow -- very bad offer
    return 0;
}

/**
 * @brief Safely checks if two STAmount values can be added without overflow,
 * underflow, or precision loss.
 *
 * This function determines whether the addition of two STAmount objects is
 * safe, depending on their type:
 * - For XRP amounts, it checks for integer overflow and underflow.
 * - For IOU amounts, it checks for acceptable precision loss.
 * - For MPT amounts, it checks for overflow and underflow within 63-bit signed
 * integer limits.
 * - If either amount is zero, addition is always considered safe.
 * - If the amounts are of different currencies or types, addition is not
 * allowed.
 *
 * @param a The first STAmount to add.
 * @param b The second STAmount to add.
 * @return true if the addition is safe; false otherwise.
 */
bool
canAdd(STAmount const& a, STAmount const& b)
{
    // cannot add different currencies
    if (!areComparable(a, b))
        return false;

    // special case: adding anything to zero is always fine
    if (a == beast::zero || b == beast::zero)
        return true;

    // XRP case (overflow & underflow check)
    if (isXRP(a) && isXRP(b))
    {
        XRPAmount A = a.xrp();
        XRPAmount B = b.xrp();

        if ((B > XRPAmount{0} &&
             A > XRPAmount{std::numeric_limits<XRPAmount::value_type>::max()} -
                     B) ||
            (B < XRPAmount{0} &&
             A < XRPAmount{std::numeric_limits<XRPAmount::value_type>::min()} -
                     B))
        {
            return false;
        }
        return true;
    }

    // IOU case (precision check)
    if (a.holds<Issue>() && b.holds<Issue>())
    {
        static STAmount const one{IOUAmount{1, 0}, noIssue()};
        static STAmount const maxLoss{IOUAmount{1, -4}, noIssue()};
        STAmount lhs = divide((a - b) + b, a, noIssue()) - one;
        STAmount rhs = divide((b - a) + a, b, noIssue()) - one;
        return ((rhs.negative() ? -rhs : rhs) +
                (lhs.negative() ? -lhs : lhs)) <= maxLoss;
    }

    // MPT (overflow & underflow check)
    if (a.holds<MPTIssue>() && b.holds<MPTIssue>())
    {
        MPTAmount A = a.mpt();
        MPTAmount B = b.mpt();
        if ((B > MPTAmount{0} &&
             A > MPTAmount{std::numeric_limits<MPTAmount::value_type>::max()} -
                     B) ||
            (B < MPTAmount{0} &&
             A < MPTAmount{std::numeric_limits<MPTAmount::value_type>::min()} -
                     B))
        {
            return false;
        }

        return true;
    }
    // LCOV_EXCL_START
    UNREACHABLE("STAmount::canAdd : unexpected STAmount type");
    return false;
    // LCOV_EXCL_STOP
}

/**
 * @brief Determines if it is safe to subtract one STAmount from another.
 *
 * This function checks whether subtracting amount `b` from amount `a` is valid,
 * considering currency compatibility and underflow conditions for specific
 * types.
 *
 * - Subtracting zero is always allowed.
 * - Subtraction is only allowed between comparable currencies.
 * - For XRP amounts, ensures no underflow or overflow occurs.
 * - For IOU amounts, subtraction is always allowed (no underflow).
 * - For MPT amounts, ensures no underflow or overflow occurs.
 *
 * @param a The minuend (amount to subtract from).
 * @param b The subtrahend (amount to subtract).
 * @return true if subtraction is allowed, false otherwise.
 */
bool
canSubtract(STAmount const& a, STAmount const& b)
{
    // Cannot subtract different currencies
    if (!areComparable(a, b))
        return false;

    // Special case: subtracting zero is always fine
    if (b == beast::zero)
        return true;

    // XRP case (underflow & overflow check)
    if (isXRP(a) && isXRP(b))
    {
        XRPAmount A = a.xrp();
        XRPAmount B = b.xrp();
        // Check for underflow
        if (B > XRPAmount{0} && A < B)
            return false;

        // Check for overflow
        if (B < XRPAmount{0} &&
            A > XRPAmount{std::numeric_limits<XRPAmount::value_type>::max()} +
                    B)
            return false;

        return true;
    }

    // IOU case (no underflow)
    if (a.holds<Issue>() && b.holds<Issue>())
    {
        return true;
    }

    // MPT case (underflow & overflow check)
    if (a.holds<MPTIssue>() && b.holds<MPTIssue>())
    {
        MPTAmount A = a.mpt();
        MPTAmount B = b.mpt();

        // Underflow check
        if (B > MPTAmount{0} && A < B)
            return false;

        // Overflow check
        if (B < MPTAmount{0} &&
            A > MPTAmount{std::numeric_limits<MPTAmount::value_type>::max()} +
                    B)
            return false;
        return true;
    }
    // LCOV_EXCL_START
    UNREACHABLE("STAmount::canSubtract : unexpected STAmount type");
    return false;
    // LCOV_EXCL_STOP
}

void
STAmount::setJson(Json::Value& elem) const
{
    elem = Json::objectValue;

    if (!native())
    {
        // It is an error for currency or issuer not to be specified for valid
        // json.
        elem[jss::value] = getText();
        mAsset.setJson(elem);
    }
    else
    {
        elem = getText();
    }
}

//------------------------------------------------------------------------------
//
// STBase
//
//------------------------------------------------------------------------------

SerializedTypeID
STAmount::getSType() const
{
    return STI_AMOUNT;
}

std::string
STAmount::getFullText() const
{
    std::string ret;

    ret.reserve(64);
    ret = getText() + "/" + mAsset.getText();
    return ret;
}

std::string
STAmount::getText() const
{
    // keep full internal accuracy, but make more human friendly if posible
    if (*this == beast::zero)
        return "0";

    std::string const raw_value(std::to_string(mValue));
    std::string ret;

    if (mIsNegative)
        ret.append(1, '-');

    bool const scientific(
        (mOffset != 0) && ((mOffset < -25) || (mOffset > -5)));

    if (native() || mAsset.holds<MPTIssue>() || scientific)
    {
        ret.append(raw_value);

        if (scientific)
        {
            ret.append(1, 'e');
            ret.append(std::to_string(mOffset));
        }

        return ret;
    }

    XRPL_ASSERT(mOffset + 43 > 0, "ripple::STAmount::getText : minimum offset");

    size_t const pad_prefix = 27;
    size_t const pad_suffix = 23;

    std::string val;
    val.reserve(raw_value.length() + pad_prefix + pad_suffix);
    val.append(pad_prefix, '0');
    val.append(raw_value);
    val.append(pad_suffix, '0');

    size_t const offset(mOffset + 43);

    auto pre_from(val.begin());
    auto const pre_to(val.begin() + offset);

    auto const post_from(val.begin() + offset);
    auto post_to(val.end());

    // Crop leading zeroes. Take advantage of the fact that there's always a
    // fixed amount of leading zeroes and skip them.
    if (std::distance(pre_from, pre_to) > pad_prefix)
        pre_from += pad_prefix;

    XRPL_ASSERT(
        post_to >= post_from,
        "ripple::STAmount::getText : first distance check");

    pre_from = std::find_if(pre_from, pre_to, [](char c) { return c != '0'; });

    // Crop trailing zeroes. Take advantage of the fact that there's always a
    // fixed amount of trailing zeroes and skip them.
    if (std::distance(post_from, post_to) > pad_suffix)
        post_to -= pad_suffix;

    XRPL_ASSERT(
        post_to >= post_from,
        "ripple::STAmount::getText : second distance check");

    post_to = std::find_if(
                  std::make_reverse_iterator(post_to),
                  std::make_reverse_iterator(post_from),
                  [](char c) { return c != '0'; })
                  .base();

    // Assemble the output:
    if (pre_from == pre_to)
        ret.append(1, '0');
    else
        ret.append(pre_from, pre_to);

    if (post_to != post_from)
    {
        ret.append(1, '.');
        ret.append(post_from, post_to);
    }

    return ret;
}

Json::Value
STAmount::getJson(JsonOptions) const
{
    Json::Value elem;
    setJson(elem);
    return elem;
}

void
STAmount::add(Serializer& s) const
{
    if (native())
    {
        XRPL_ASSERT(mOffset == 0, "ripple::STAmount::add : zero offset");

        if (!mIsNegative)
            s.add64(mValue | cPositive);
        else
            s.add64(mValue);
    }
    else if (mAsset.holds<MPTIssue>())
    {
        auto u8 = static_cast<unsigned char>(cMPToken >> 56);
        if (!mIsNegative)
            u8 |= static_cast<unsigned char>(cPositive >> 56);
        s.add8(u8);
        s.add64(mValue);
        s.addBitString(mAsset.get<MPTIssue>().getMptID());
    }
    else
    {
        if (*this == beast::zero)
            s.add64(cIssuedCurrency);
        else if (mIsNegative)  // 512 = not native
            s.add64(
                mValue |
                (static_cast<std::uint64_t>(mOffset + 512 + 97) << (64 - 10)));
        else  // 256 = positive
            s.add64(
                mValue |
                (static_cast<std::uint64_t>(mOffset + 512 + 256 + 97)
                 << (64 - 10)));
        s.addBitString(mAsset.get<Issue>().currency);
        s.addBitString(mAsset.get<Issue>().account);
    }
}

bool
STAmount::isEquivalent(STBase const& t) const
{
    STAmount const* v = dynamic_cast<STAmount const*>(&t);
    return v && (*v == *this);
}

bool
STAmount::isDefault() const
{
    return (mValue == 0) && native();
}

//------------------------------------------------------------------------------

// amount = mValue * [10 ^ mOffset]
// Representation range is 10^80 - 10^(-80).
//
// On the wire:
// - high bit is 0 for XRP, 1 for issued currency
// - next bit is 1 for positive, 0 for negative (except 0 issued currency, which
//      is a special case of 0x8000000000000000
// - for issued currencies, the next 8 bits are (mOffset+97).
//   The +97 is so that this value is always positive.
// - The remaining bits are significant digits (mantissa)
//   That's 54 bits for issued currency and 62 bits for native
//   (but XRP only needs 57 bits for the max value of 10^17 drops)
//
// mValue is zero if the amount is zero, otherwise it's within the range
//    10^15 to (10^16 - 1) inclusive.
// mOffset is in the range -96 to +80.
void
STAmount::canonicalize()
{
    if (native() || mAsset.holds<MPTIssue>())
    {
        // native and MPT currency amounts should always have an offset of zero
        // log(2^64,10) ~ 19.2
        if (mValue == 0 || mOffset <= -20)
        {
            mValue = 0;
            mOffset = 0;
            mIsNegative = false;
            return;
        }

        if (getSTAmountCanonicalizeSwitchover())
        {
            // log(cMaxNativeN, 10) == 17
            if (native() && mOffset > 17)
                Throw<std::runtime_error>(
                    "Native currency amount out of range");
            // log(maxMPTokenAmount, 10) ~ 18.96
            if (mAsset.holds<MPTIssue>() && mOffset > 18)
                Throw<std::runtime_error>("MPT amount out of range");
        }

        if (getSTNumberSwitchover() && getSTAmountCanonicalizeSwitchover())
        {
            Number num(
                mIsNegative ? -mValue : mValue, mOffset, Number::unchecked{});
            auto set = [&](auto const& val) {
                mIsNegative = val.value() < 0;
                mValue = mIsNegative ? -val.value() : val.value();
            };
            if (native())
                set(XRPAmount{num});
            else
                set(MPTAmount{num});
            mOffset = 0;
        }
        else
        {
            while (mOffset < 0)
            {
                mValue /= 10;
                ++mOffset;
            }

            while (mOffset > 0)
            {
                if (getSTAmountCanonicalizeSwitchover())
                {
                    // N.B. do not move the overflow check to after the
                    // multiplication
                    if (native() && mValue > cMaxNativeN)
                        Throw<std::runtime_error>(
                            "Native currency amount out of range");
                    else if (!native() && mValue > maxMPTokenAmount)
                        Throw<std::runtime_error>("MPT amount out of range");
                }
                mValue *= 10;
                --mOffset;
            }
        }

        if (native() && mValue > cMaxNativeN)
            Throw<std::runtime_error>("Native currency amount out of range");
        else if (!native() && mValue > maxMPTokenAmount)
            Throw<std::runtime_error>("MPT amount out of range");

        return;
    }

    if (getSTNumberSwitchover())
    {
        *this = iou();
        return;
    }

    if (mValue == 0)
    {
        mOffset = -100;
        mIsNegative = false;
        return;
    }

    while ((mValue < cMinValue) && (mOffset > cMinOffset))
    {
        mValue *= 10;
        --mOffset;
    }

    while (mValue > cMaxValue)
    {
        if (mOffset >= cMaxOffset)
            Throw<std::runtime_error>("value overflow");

        mValue /= 10;
        ++mOffset;
    }

    if ((mOffset < cMinOffset) || (mValue < cMinValue))
    {
        mValue = 0;
        mIsNegative = false;
        mOffset = -100;
        return;
    }

    if (mOffset > cMaxOffset)
        Throw<std::runtime_error>("value overflow");

    XRPL_ASSERT(
        (mValue == 0) || ((mValue >= cMinValue) && (mValue <= cMaxValue)),
        "ripple::STAmount::canonicalize : value inside range");
    XRPL_ASSERT(
        (mValue == 0) || ((mOffset >= cMinOffset) && (mOffset <= cMaxOffset)),
        "ripple::STAmount::canonicalize : offset inside range");
    XRPL_ASSERT(
        (mValue != 0) || (mOffset != -100),
        "ripple::STAmount::canonicalize : value or offset set");
}

void
STAmount::set(std::int64_t v)
{
    if (v < 0)
    {
        mIsNegative = true;
        mValue = static_cast<std::uint64_t>(-v);
    }
    else
    {
        mIsNegative = false;
        mValue = static_cast<std::uint64_t>(v);
    }
}

//------------------------------------------------------------------------------

STAmount
amountFromQuality(std::uint64_t rate)
{
    if (rate == 0)
        return STAmount(noIssue());

    std::uint64_t mantissa = rate & ~(255ull << (64 - 8));
    int exponent = static_cast<int>(rate >> (64 - 8)) - 100;

    return STAmount(noIssue(), mantissa, exponent);
}

STAmount
amountFromString(Asset const& asset, std::string const& amount)
{
    auto const parts = partsFromString(amount);
    if ((asset.native() || asset.holds<MPTIssue>()) && parts.exponent < 0)
        Throw<std::runtime_error>(
            "XRP and MPT must be specified as integral amount.");
    return {asset, parts.mantissa, parts.exponent, parts.negative};
}

STAmount
amountFromJson(SField const& name, Json::Value const& v)
{
    Asset asset;

    Json::Value value;
    Json::Value currencyOrMPTID;
    Json::Value issuer;
    bool isMPT = false;

    if (v.isNull())
    {
        Throw<std::runtime_error>(
            "XRP may not be specified with a null Json value");
    }
    else if (v.isObject())
    {
        if (!validJSONAsset(v))
            Throw<std::runtime_error>("Invalid Asset's Json specification");

        value = v[jss::value];
        if (v.isMember(jss::mpt_issuance_id))
        {
            isMPT = true;
            currencyOrMPTID = v[jss::mpt_issuance_id];
        }
        else
        {
            currencyOrMPTID = v[jss::currency];
            issuer = v[jss::issuer];
        }
    }
    else if (v.isArray())
    {
        value = v.get(Json::UInt(0), 0);
        currencyOrMPTID = v.get(Json::UInt(1), Json::nullValue);
        issuer = v.get(Json::UInt(2), Json::nullValue);
    }
    else if (v.isString())
    {
        std::string val = v.asString();
        std::vector<std::string> elements;
        boost::split(elements, val, boost::is_any_of("\t\n\r ,/"));

        if (elements.size() > 3)
            Throw<std::runtime_error>("invalid amount string");

        value = elements[0];

        if (elements.size() > 1)
            currencyOrMPTID = elements[1];

        if (elements.size() > 2)
            issuer = elements[2];
    }
    else
    {
        value = v;
    }

    bool const native = !currencyOrMPTID.isString() ||
        currencyOrMPTID.asString().empty() ||
        (currencyOrMPTID.asString() == systemCurrencyCode());

    if (native)
    {
        if (v.isObjectOrNull())
            Throw<std::runtime_error>("XRP may not be specified as an object");
        asset = xrpIssue();
    }
    else
    {
        if (isMPT)
        {
            // sequence (32 bits) + account (160 bits)
            uint192 u;
            if (!u.parseHex(currencyOrMPTID.asString()))
                Throw<std::runtime_error>("invalid MPTokenIssuanceID");
            asset = u;
        }
        else
        {
            Issue issue;
            if (!to_currency(issue.currency, currencyOrMPTID.asString()))
                Throw<std::runtime_error>("invalid currency");
            if (!issuer.isString() ||
                !to_issuer(issue.account, issuer.asString()))
                Throw<std::runtime_error>("invalid issuer");
            if (issue.native())
                Throw<std::runtime_error>("invalid issuer");
            asset = issue;
        }
    }

    NumberParts parts;

    if (value.isInt())
    {
        if (value.asInt() >= 0)
        {
            parts.mantissa = value.asInt();
        }
        else
        {
            parts.mantissa = -value.asInt();
            parts.negative = true;
        }
    }
    else if (value.isUInt())
    {
        parts.mantissa = v.asUInt();
    }
    else if (value.isString())
    {
        parts = partsFromString(value.asString());
        // Can't specify XRP or MPT using fractional representation
        if ((asset.native() || asset.holds<MPTIssue>()) && parts.exponent < 0)
            Throw<std::runtime_error>(
                "XRP and MPT must be specified as integral amount.");
    }
    else
    {
        Throw<std::runtime_error>("invalid amount type");
    }

    return {name, asset, parts.mantissa, parts.exponent, parts.negative};
}

bool
amountFromJsonNoThrow(STAmount& result, Json::Value const& jvSource)
{
    try
    {
        result = amountFromJson(sfGeneric, jvSource);
        return true;
    }
    catch (std::exception const& e)
    {
        JLOG(debugLog().warn())
            << "amountFromJsonNoThrow: caught: " << e.what();
    }
    return false;
}

//------------------------------------------------------------------------------
//
// Operators
//
//------------------------------------------------------------------------------

bool
operator==(STAmount const& lhs, STAmount const& rhs)
{
    return areComparable(lhs, rhs) && lhs.negative() == rhs.negative() &&
        lhs.exponent() == rhs.exponent() && lhs.mantissa() == rhs.mantissa();
}

bool
operator<(STAmount const& lhs, STAmount const& rhs)
{
    if (!areComparable(lhs, rhs))
        Throw<std::runtime_error>(
            "Can't compare amounts that are't comparable!");

    if (lhs.negative() != rhs.negative())
        return lhs.negative();

    if (lhs.mantissa() == 0)
    {
        if (rhs.negative())
            return false;
        return rhs.mantissa() != 0;
    }

    // We know that lhs is non-zero and both sides have the same sign. Since
    // rhs is zero (and thus not negative), lhs must, therefore, be strictly
    // greater than zero. So if rhs is zero, the comparison must be false.
    if (rhs.mantissa() == 0)
        return false;

    if (lhs.exponent() > rhs.exponent())
        return lhs.negative();
    if (lhs.exponent() < rhs.exponent())
        return !lhs.negative();
    if (lhs.mantissa() > rhs.mantissa())
        return lhs.negative();
    if (lhs.mantissa() < rhs.mantissa())
        return !lhs.negative();

    return false;
}

STAmount
operator-(STAmount const& value)
{
    if (value.mantissa() == 0)
        return value;
    return STAmount(
        value.getFName(),
        value.asset(),
        value.mantissa(),
        value.exponent(),
        !value.negative(),
        STAmount::unchecked{});
}

//------------------------------------------------------------------------------
//
// Arithmetic
//
//------------------------------------------------------------------------------

// Calculate (a * b) / c when all three values are 64-bit
// without loss of precision:
static std::uint64_t
muldiv(
    std::uint64_t multiplier,
    std::uint64_t multiplicand,
    std::uint64_t divisor)
{
    boost::multiprecision::uint128_t ret;

    boost::multiprecision::multiply(ret, multiplier, multiplicand);
    ret /= divisor;

    if (ret > std::numeric_limits<std::uint64_t>::max())
    {
        Throw<std::overflow_error>(
            "overflow: (" + std::to_string(multiplier) + " * " +
            std::to_string(multiplicand) + ") / " + std::to_string(divisor));
    }

    return static_cast<uint64_t>(ret);
}

static std::uint64_t
muldiv_round(
    std::uint64_t multiplier,
    std::uint64_t multiplicand,
    std::uint64_t divisor,
    std::uint64_t rounding)
{
    boost::multiprecision::uint128_t ret;

    boost::multiprecision::multiply(ret, multiplier, multiplicand);
    ret += rounding;
    ret /= divisor;

    if (ret > std::numeric_limits<std::uint64_t>::max())
    {
        Throw<std::overflow_error>(
            "overflow: ((" + std::to_string(multiplier) + " * " +
            std::to_string(multiplicand) + ") + " + std::to_string(rounding) +
            ") / " + std::to_string(divisor));
    }

    return static_cast<uint64_t>(ret);
}

STAmount
divide(STAmount const& num, STAmount const& den, Asset const& asset)
{
    if (den == beast::zero)
        Throw<std::runtime_error>("division by zero");

    if (num == beast::zero)
        return {asset};

    std::uint64_t numVal = num.mantissa();
    std::uint64_t denVal = den.mantissa();
    int numOffset = num.exponent();
    int denOffset = den.exponent();

    if (num.native() || num.holds<MPTIssue>())
    {
        while (numVal < STAmount::cMinValue)
        {
            // Need to bring into range
            numVal *= 10;
            --numOffset;
        }
    }

    if (den.native() || den.holds<MPTIssue>())
    {
        while (denVal < STAmount::cMinValue)
        {
            denVal *= 10;
            --denOffset;
        }
    }

    // We divide the two mantissas (each is between 10^15
    // and 10^16). To maintain precision, we multiply the
    // numerator by 10^17 (the product is in the range of
    // 10^32 to 10^33) followed by a division, so the result
    // is in the range of 10^16 to 10^15.
    return STAmount(
        asset,
        muldiv(numVal, tenTo17, denVal) + 5,
        numOffset - denOffset - 17,
        num.negative() != den.negative());
}

STAmount
multiply(STAmount const& v1, STAmount const& v2, Asset const& asset)
{
    if (v1 == beast::zero || v2 == beast::zero)
        return STAmount(asset);

    if (v1.native() && v2.native() && asset.native())
    {
        std::uint64_t const minV = std::min(getSNValue(v1), getSNValue(v2));
        std::uint64_t const maxV = std::max(getSNValue(v1), getSNValue(v2));

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Native value overflow");

        return STAmount(v1.getFName(), minV * maxV);
    }
    if (v1.holds<MPTIssue>() && v2.holds<MPTIssue>() && asset.holds<MPTIssue>())
    {
        std::uint64_t const minV = std::min(getMPTValue(v1), getMPTValue(v2));
        std::uint64_t const maxV = std::max(getMPTValue(v1), getMPTValue(v2));

        if (minV > 3037000499ull)  // sqrt(maxMPTokenAmount) ~ 3037000499.98
            Throw<std::runtime_error>("MPT value overflow");

        if (((maxV >> 32) * minV) > 2147483648ull)  // maxMPTokenAmount / 2^32
            Throw<std::runtime_error>("MPT value overflow");

        return STAmount(asset, minV * maxV);
    }

    if (getSTNumberSwitchover())
    {
        auto const r = Number{v1} * Number{v2};
        return STAmount{asset, r.mantissa(), r.exponent()};
    }

    std::uint64_t value1 = v1.mantissa();
    std::uint64_t value2 = v2.mantissa();
    int offset1 = v1.exponent();
    int offset2 = v2.exponent();

    if (v1.native() || v1.holds<MPTIssue>())
    {
        while (value1 < STAmount::cMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    if (v2.native() || v2.holds<MPTIssue>())
    {
        while (value2 < STAmount::cMinValue)
        {
            value2 *= 10;
            --offset2;
        }
    }

    // We multiply the two mantissas (each is between 10^15
    // and 10^16), so their product is in the 10^30 to 10^32
    // range. Dividing their product by 10^14 maintains the
    // precision, by scaling the result to 10^16 to 10^18.
    return STAmount(
        asset,
        muldiv(value1, value2, tenTo14) + 7,
        offset1 + offset2 + 14,
        v1.negative() != v2.negative());
}

// This is the legacy version of canonicalizeRound.  It's been in use
// for years, so it is deeply embedded in the behavior of cross-currency
// transactions.
//
// However in 2022 it was noticed that the rounding characteristics were
// surprising.  When the code converts from IOU-like to XRP-like there may
// be a fraction of the IOU-like representation that is too small to be
// represented in drops.  `canonicalizeRound()` currently does some unusual
// rounding.
//
//  1. If the fractional part is greater than or equal to 0.1, then the
//     number of drops is rounded up.
//
//  2. However, if the fractional part is less than 0.1 (for example,
//     0.099999), then the number of drops is rounded down.
//
// The XRP Ledger has this rounding behavior baked in.  But there are
// situations where this rounding behavior led to undesirable outcomes.
// So an alternative rounding approach was introduced.  You'll see that
// alternative below.
static void
canonicalizeRound(bool native, std::uint64_t& value, int& offset, bool)
{
    if (native)
    {
        if (offset < 0)
        {
            int loops = 0;

            while (offset < -1)
            {
                value /= 10;
                ++offset;
                ++loops;
            }

            value += (loops >= 2) ? 9 : 10;  // add before last divide
            value /= 10;
            ++offset;
        }
    }
    else if (value > STAmount::cMaxValue)
    {
        while (value > (10 * STAmount::cMaxValue))
        {
            value /= 10;
            ++offset;
        }

        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

// The original canonicalizeRound did not allow the rounding direction to
// be specified.  It also ignored some of the bits that could contribute to
// rounding decisions.  canonicalizeRoundStrict() tracks all of the bits in
// the value being rounded.
static void
canonicalizeRoundStrict(
    bool native,
    std::uint64_t& value,
    int& offset,
    bool roundUp)
{
    if (native)
    {
        if (offset < 0)
        {
            bool hadRemainder = false;

            while (offset < -1)
            {
                // It would be better to use std::lldiv than to separately
                // compute the remainder.  But std::lldiv does not support
                // unsigned arguments.
                std::uint64_t const newValue = value / 10;
                hadRemainder |= (value != (newValue * 10));
                value = newValue;
                ++offset;
            }
            value +=
                (hadRemainder && roundUp) ? 10 : 9;  // Add before last divide
            value /= 10;
            ++offset;
        }
    }
    else if (value > STAmount::cMaxValue)
    {
        while (value > (10 * STAmount::cMaxValue))
        {
            value /= 10;
            ++offset;
        }
        value += 9;  // add before last divide
        value /= 10;
        ++offset;
    }
}

namespace {

// We need a class that has an interface similar to NumberRoundModeGuard
// but does nothing.
class DontAffectNumberRoundMode
{
public:
    explicit DontAffectNumberRoundMode(Number::rounding_mode mode) noexcept
    {
    }

    DontAffectNumberRoundMode(DontAffectNumberRoundMode const&) = delete;

    DontAffectNumberRoundMode&
    operator=(DontAffectNumberRoundMode const&) = delete;
};

}  // anonymous namespace

// Pass the canonicalizeRound function pointer as a template parameter.
//
// We might need to use NumberRoundModeGuard.  Allow the caller
// to pass either that or a replacement as a template parameter.
template <
    void (*CanonicalizeFunc)(bool, std::uint64_t&, int&, bool),
    typename MightSaveRound>
static STAmount
mulRoundImpl(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
    bool roundUp)
{
    if (v1 == beast::zero || v2 == beast::zero)
        return {asset};

    bool const xrp = asset.native();

    if (v1.native() && v2.native() && xrp)
    {
        std::uint64_t minV = std::min(getSNValue(v1), getSNValue(v2));
        std::uint64_t maxV = std::max(getSNValue(v1), getSNValue(v2));

        if (minV > 3000000000ull)  // sqrt(cMaxNative)
            Throw<std::runtime_error>("Native value overflow");

        if (((maxV >> 32) * minV) > 2095475792ull)  // cMaxNative / 2^32
            Throw<std::runtime_error>("Native value overflow");

        return STAmount(v1.getFName(), minV * maxV);
    }

    if (v1.holds<MPTIssue>() && v2.holds<MPTIssue>() && asset.holds<MPTIssue>())
    {
        std::uint64_t const minV = std::min(getMPTValue(v1), getMPTValue(v2));
        std::uint64_t const maxV = std::max(getMPTValue(v1), getMPTValue(v2));

        if (minV > 3037000499ull)  // sqrt(maxMPTokenAmount) ~ 3037000499.98
            Throw<std::runtime_error>("MPT value overflow");

        if (((maxV >> 32) * minV) > 2147483648ull)  // maxMPTokenAmount / 2^32
            Throw<std::runtime_error>("MPT value overflow");

        return STAmount(asset, minV * maxV);
    }

    std::uint64_t value1 = v1.mantissa(), value2 = v2.mantissa();
    int offset1 = v1.exponent(), offset2 = v2.exponent();

    if (v1.native() || v1.holds<MPTIssue>())
    {
        while (value1 < STAmount::cMinValue)
        {
            value1 *= 10;
            --offset1;
        }
    }

    if (v2.native() || v2.holds<MPTIssue>())
    {
        while (value2 < STAmount::cMinValue)
        {
            value2 *= 10;
            --offset2;
        }
    }

    bool const resultNegative = v1.negative() != v2.negative();

    // We multiply the two mantissas (each is between 10^15
    // and 10^16), so their product is in the 10^30 to 10^32
    // range. Dividing their product by 10^14 maintains the
    // precision, by scaling the result to 10^16 to 10^18.
    //
    // If the we're rounding up, we want to round up away
    // from zero, and if we're rounding down, truncation
    // is implicit.
    std::uint64_t amount = muldiv_round(
        value1, value2, tenTo14, (resultNegative != roundUp) ? tenTo14m1 : 0);

    int offset = offset1 + offset2 + 14;
    if (resultNegative != roundUp)
    {
        CanonicalizeFunc(xrp, amount, offset, roundUp);
    }
    STAmount result = [&]() {
        // If appropriate, tell Number to round down.  This gives the desired
        // result from STAmount::canonicalize.
        MightSaveRound const savedRound(Number::towards_zero);
        return STAmount(asset, amount, offset, resultNegative);
    }();

    if (roundUp && !resultNegative && !result)
    {
        if (xrp)
        {
            // return the smallest value above zero
            amount = 1;
            offset = 0;
        }
        else
        {
            // return the smallest value above zero
            amount = STAmount::cMinValue;
            offset = STAmount::cMinOffset;
        }
        return STAmount(asset, amount, offset, resultNegative);
    }
    return result;
}

STAmount
mulRound(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
    bool roundUp)
{
    return mulRoundImpl<canonicalizeRound, DontAffectNumberRoundMode>(
        v1, v2, asset, roundUp);
}

STAmount
mulRoundStrict(
    STAmount const& v1,
    STAmount const& v2,
    Asset const& asset,
    bool roundUp)
{
    return mulRoundImpl<canonicalizeRoundStrict, NumberRoundModeGuard>(
        v1, v2, asset, roundUp);
}

// We might need to use NumberRoundModeGuard.  Allow the caller
// to pass either that or a replacement as a template parameter.
template <typename MightSaveRound>
static STAmount
divRoundImpl(
    STAmount const& num,
    STAmount const& den,
    Asset const& asset,
    bool roundUp)
{
    if (den == beast::zero)
        Throw<std::runtime_error>("division by zero");

    if (num == beast::zero)
        return {asset};

    std::uint64_t numVal = num.mantissa(), denVal = den.mantissa();
    int numOffset = num.exponent(), denOffset = den.exponent();

    if (num.native() || num.holds<MPTIssue>())
    {
        while (numVal < STAmount::cMinValue)
        {
            numVal *= 10;
            --numOffset;
        }
    }

    if (den.native() || den.holds<MPTIssue>())
    {
        while (denVal < STAmount::cMinValue)
        {
            denVal *= 10;
            --denOffset;
        }
    }

    bool const resultNegative = (num.negative() != den.negative());

    // We divide the two mantissas (each is between 10^15
    // and 10^16). To maintain precision, we multiply the
    // numerator by 10^17 (the product is in the range of
    // 10^32 to 10^33) followed by a division, so the result
    // is in the range of 10^16 to 10^15.
    //
    // We round away from zero if we're rounding up or
    // truncate if we're rounding down.
    std::uint64_t amount = muldiv_round(
        numVal, tenTo17, denVal, (resultNegative != roundUp) ? denVal - 1 : 0);

    int offset = numOffset - denOffset - 17;

    if (resultNegative != roundUp)
        canonicalizeRound(
            asset.native() || asset.holds<MPTIssue>(), amount, offset, roundUp);

    STAmount result = [&]() {
        // If appropriate, tell Number the rounding mode we are using.
        // Note that "roundUp == true" actually means "round away from zero".
        // Otherwise round toward zero.
        using enum Number::rounding_mode;
        MightSaveRound const savedRound(
            roundUp ^ resultNegative ? upward : downward);
        return STAmount(asset, amount, offset, resultNegative);
    }();

    if (roundUp && !resultNegative && !result)
    {
        if (asset.native() || asset.holds<MPTIssue>())
        {
            // return the smallest value above zero
            amount = 1;
            offset = 0;
        }
        else
        {
            // return the smallest value above zero
            amount = STAmount::cMinValue;
            offset = STAmount::cMinOffset;
        }
        return STAmount(asset, amount, offset, resultNegative);
    }
    return result;
}

STAmount
divRound(
    STAmount const& num,
    STAmount const& den,
    Asset const& asset,
    bool roundUp)
{
    return divRoundImpl<DontAffectNumberRoundMode>(num, den, asset, roundUp);
}

STAmount
divRoundStrict(
    STAmount const& num,
    STAmount const& den,
    Asset const& asset,
    bool roundUp)
{
    return divRoundImpl<NumberRoundModeGuard>(num, den, asset, roundUp);
}

}  // namespace ripple
