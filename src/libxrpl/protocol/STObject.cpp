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

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/InnerObjectFormats.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STBlob.h>
#include <xrpl/protocol/STCurrency.h>
#include <xrpl/protocol/STInteger.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/detail/STVar.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ripple {

STObject::STObject(STObject&& other)
    : STBase(other.getFName()), v_(std::move(other.v_)), mType(other.mType)
{
}

STObject::STObject(SField const& name) : STBase(name), mType(nullptr)
{
}

STObject::STObject(SOTemplate const& type, SField const& name) : STBase(name)
{
    set(type);
}

STObject::STObject(SOTemplate const& type, SerialIter& sit, SField const& name)
    : STBase(name)
{
    v_.reserve(type.size());
    set(sit);
    applyTemplate(type);  // May throw
}

STObject::STObject(SerialIter& sit, SField const& name, int depth) noexcept(
    false)
    : STBase(name), mType(nullptr)
{
    if (depth > 10)
        Throw<std::runtime_error>("Maximum nesting depth of STObject exceeded");
    set(sit, depth);
}

STObject
STObject::makeInnerObject(SField const& name)
{
    STObject obj{name};

    // The if is complicated because inner object templates were added in
    // two phases:
    //  1. If there are no available Rules, then always apply the template.
    //  2. fixInnerObjTemplate added templates to two AMM inner objects.
    //  3. fixInnerObjTemplate2 added templates to all remaining inner objects.
    std::optional<Rules> const& rules = getCurrentTransactionRules();
    bool const isAMMObj = name == sfAuctionSlot || name == sfVoteEntry;
    if (!rules || (rules->enabled(fixInnerObjTemplate) && isAMMObj) ||
        (rules->enabled(fixInnerObjTemplate2) && !isAMMObj))
    {
        if (SOTemplate const* elements =
                InnerObjectFormats::getInstance().findSOTemplateBySField(name))
            obj.set(*elements);
    }
    return obj;
}

STBase*
STObject::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STObject::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

SerializedTypeID
STObject::getSType() const
{
    return STI_OBJECT;
}

bool
STObject::isDefault() const
{
    return v_.empty();
}

void
STObject::add(Serializer& s) const
{
    add(s, withAllFields);  // just inner elements
}

STObject&
STObject::operator=(STObject&& other)
{
    setFName(other.getFName());
    mType = other.mType;
    v_ = std::move(other.v_);
    return *this;
}

void
STObject::set(SOTemplate const& type)
{
    v_.clear();
    v_.reserve(type.size());
    mType = &type;

    for (auto const& elem : type)
    {
        if (elem.style() != soeREQUIRED)
            v_.emplace_back(detail::nonPresentObject, elem.sField());
        else
            v_.emplace_back(detail::defaultObject, elem.sField());
    }
}

void
STObject::applyTemplate(SOTemplate const& type)
{
    auto throwFieldErr = [](std::string const& field, char const* description) {
        std::stringstream ss;
        ss << "Field '" << field << "' " << description;
        std::string text{ss.str()};
        JLOG(debugLog().error()) << "STObject::applyTemplate failed: " << text;
        Throw<FieldErr>(text);
    };

    mType = &type;
    decltype(v_) v;
    v.reserve(type.size());
    for (auto const& e : type)
    {
        auto const iter =
            std::find_if(v_.begin(), v_.end(), [&](detail::STVar const& b) {
                return b.get().getFName() == e.sField();
            });
        if (iter != v_.end())
        {
            if ((e.style() == soeDEFAULT) && iter->get().isDefault())
            {
                throwFieldErr(
                    e.sField().fieldName,
                    "may not be explicitly set to default.");
            }
            v.emplace_back(std::move(*iter));
            v_.erase(iter);
        }
        else
        {
            if (e.style() == soeREQUIRED)
            {
                throwFieldErr(e.sField().fieldName, "is required but missing.");
            }
            v.emplace_back(detail::nonPresentObject, e.sField());
        }
    }
    for (auto const& e : v_)
    {
        // Anything left over in the object must be discardable
        if (!e->getFName().isDiscardable())
        {
            throwFieldErr(
                e->getFName().getName(), "found in disallowed location.");
        }
    }
    // Swap the template matching data in for the old data,
    // freeing any leftover junk
    v_.swap(v);
}

void
STObject::applyTemplateFromSField(SField const& sField)
{
    SOTemplate const* elements =
        InnerObjectFormats::getInstance().findSOTemplateBySField(sField);
    if (elements)
        applyTemplate(*elements);  // May throw
}

// return true = terminated with end-of-object
bool
STObject::set(SerialIter& sit, int depth)
{
    bool reachedEndOfObject = false;

    v_.clear();

    // Consume data in the pipe until we run out or reach the end
    while (!sit.empty())
    {
        int type;
        int field;

        // Get the metadata for the next field
        sit.getFieldID(type, field);

        // The object termination marker has been found and the termination
        // marker has been consumed. Done deserializing.
        if (type == STI_OBJECT && field == 1)
        {
            reachedEndOfObject = true;
            break;
        }

        if (type == STI_ARRAY && field == 1)
        {
            JLOG(debugLog().error())
                << "Encountered object with embedded end-of-array marker";
            Throw<std::runtime_error>("Illegal end-of-array marker in object");
        }

        auto const& fn = SField::getField(type, field);

        if (fn.isInvalid())
        {
            JLOG(debugLog().error()) << "Unknown field: field_type=" << type
                                     << ", field_name=" << field;
            Throw<std::runtime_error>("Unknown field");
        }

        // Unflatten the field
        v_.emplace_back(sit, fn, depth + 1);

        // If the object type has a known SOTemplate then set it.
        if (auto const obj = dynamic_cast<STObject*>(&(v_.back().get())))
            obj->applyTemplateFromSField(fn);  // May throw
    }

    // We want to ensure that the deserialized object does not contain any
    // duplicate fields. This is a key invariant:
    auto const sf = getSortedFields(*this, withAllFields);

    auto const dup = std::adjacent_find(
        sf.cbegin(), sf.cend(), [](STBase const* lhs, STBase const* rhs) {
            return lhs->getFName() == rhs->getFName();
        });

    if (dup != sf.cend())
        Throw<std::runtime_error>("Duplicate field detected");

    return reachedEndOfObject;
}

bool
STObject::hasMatchingEntry(STBase const& t)
{
    STBase const* o = peekAtPField(t.getFName());

    if (!o)
        return false;

    return t == *o;
}

std::string
STObject::getFullText() const
{
    std::string ret;
    bool first = true;

    if (getFName().hasName())
    {
        ret = getFName().getName();
        ret += " = {";
    }
    else
        ret = "{";

    for (auto const& elem : v_)
    {
        if (elem->getSType() != STI_NOTPRESENT)
        {
            if (!first)
                ret += ", ";
            else
                first = false;

            ret += elem->getFullText();
        }
    }

    ret += "}";
    return ret;
}

std::string
STObject::getText() const
{
    std::string ret = "{";
    bool first = false;
    for (auto const& elem : v_)
    {
        if (!first)
        {
            ret += ", ";
            first = false;
        }

        ret += elem->getText();
    }
    ret += "}";
    return ret;
}

bool
STObject::isEquivalent(STBase const& t) const
{
    STObject const* v = dynamic_cast<STObject const*>(&t);

    if (!v)
        return false;

    if (mType != nullptr && v->mType == mType)
    {
        return std::equal(
            begin(),
            end(),
            v->begin(),
            v->end(),
            [](STBase const& st1, STBase const& st2) {
                return (st1.getSType() == st2.getSType()) &&
                    st1.isEquivalent(st2);
            });
    }

    auto const sf1 = getSortedFields(*this, withAllFields);
    auto const sf2 = getSortedFields(*v, withAllFields);

    return std::equal(
        sf1.begin(),
        sf1.end(),
        sf2.begin(),
        sf2.end(),
        [](STBase const* st1, STBase const* st2) {
            return (st1->getSType() == st2->getSType()) &&
                st1->isEquivalent(*st2);
        });
}

uint256
STObject::getHash(HashPrefix prefix) const
{
    Serializer s;
    s.add32(prefix);
    add(s, withAllFields);
    return s.getSHA512Half();
}

uint256
STObject::getSigningHash(HashPrefix prefix) const
{
    Serializer s;
    s.add32(prefix);
    add(s, omitSigningFields);
    return s.getSHA512Half();
}

int
STObject::getFieldIndex(SField const& field) const
{
    if (mType != nullptr)
        return mType->getIndex(field);

    int i = 0;
    for (auto const& elem : v_)
    {
        if (elem->getFName() == field)
            return i;
        ++i;
    }
    return -1;
}

STBase const&
STObject::peekAtField(SField const& field) const
{
    int index = getFieldIndex(field);

    if (index == -1)
        throwFieldNotFound(field);

    return peekAtIndex(index);
}

STBase&
STObject::getField(SField const& field)
{
    int index = getFieldIndex(field);

    if (index == -1)
        throwFieldNotFound(field);

    return getIndex(index);
}

SField const&
STObject::getFieldSType(int index) const
{
    return v_[index]->getFName();
}

STBase const*
STObject::peekAtPField(SField const& field) const
{
    int index = getFieldIndex(field);

    if (index == -1)
        return nullptr;

    return peekAtPIndex(index);
}

STBase*
STObject::getPField(SField const& field, bool createOkay)
{
    int index = getFieldIndex(field);

    if (index == -1)
    {
        if (createOkay && isFree())
            return getPIndex(emplace_back(detail::defaultObject, field));

        return nullptr;
    }

    return getPIndex(index);
}

bool
STObject::isFieldPresent(SField const& field) const
{
    int index = getFieldIndex(field);

    if (index == -1)
        return false;

    return peekAtIndex(index).getSType() != STI_NOTPRESENT;
}

STObject&
STObject::peekFieldObject(SField const& field)
{
    return peekField<STObject>(field);
}

STArray&
STObject::peekFieldArray(SField const& field)
{
    return peekField<STArray>(field);
}

bool
STObject::setFlag(std::uint32_t f)
{
    STUInt32* t = dynamic_cast<STUInt32*>(getPField(sfFlags, true));

    if (!t)
        return false;

    t->setValue(t->value() | f);
    return true;
}

bool
STObject::clearFlag(std::uint32_t f)
{
    STUInt32* t = dynamic_cast<STUInt32*>(getPField(sfFlags));

    if (!t)
        return false;

    t->setValue(t->value() & ~f);
    return true;
}

bool
STObject::isFlag(std::uint32_t f) const
{
    return (getFlags() & f) == f;
}

std::uint32_t
STObject::getFlags(void) const
{
    STUInt32 const* t = dynamic_cast<STUInt32 const*>(peekAtPField(sfFlags));

    if (!t)
        return 0;

    return t->value();
}

STBase*
STObject::makeFieldPresent(SField const& field)
{
    int index = getFieldIndex(field);

    if (index == -1)
    {
        if (!isFree())
            throwFieldNotFound(field);

        return getPIndex(emplace_back(detail::nonPresentObject, field));
    }

    STBase* f = getPIndex(index);

    if (f->getSType() != STI_NOTPRESENT)
        return f;

    v_[index] = detail::STVar(detail::defaultObject, f->getFName());
    return getPIndex(index);
}

void
STObject::makeFieldAbsent(SField const& field)
{
    int index = getFieldIndex(field);

    if (index == -1)
        throwFieldNotFound(field);

    STBase const& f = peekAtIndex(index);

    if (f.getSType() == STI_NOTPRESENT)
        return;
    v_[index] = detail::STVar(detail::nonPresentObject, f.getFName());
}

bool
STObject::delField(SField const& field)
{
    int index = getFieldIndex(field);

    if (index == -1)
        return false;

    delField(index);
    return true;
}

void
STObject::delField(int index)
{
    v_.erase(v_.begin() + index);
}

unsigned char
STObject::getFieldU8(SField const& field) const
{
    return getFieldByValue<STUInt8>(field);
}

std::uint16_t
STObject::getFieldU16(SField const& field) const
{
    return getFieldByValue<STUInt16>(field);
}

std::uint32_t
STObject::getFieldU32(SField const& field) const
{
    return getFieldByValue<STUInt32>(field);
}

std::uint64_t
STObject::getFieldU64(SField const& field) const
{
    return getFieldByValue<STUInt64>(field);
}

uint128
STObject::getFieldH128(SField const& field) const
{
    return getFieldByValue<STUInt128>(field);
}

uint160
STObject::getFieldH160(SField const& field) const
{
    return getFieldByValue<STUInt160>(field);
}

uint192
STObject::getFieldH192(SField const& field) const
{
    return getFieldByValue<STUInt192>(field);
}

uint256
STObject::getFieldH256(SField const& field) const
{
    return getFieldByValue<STUInt256>(field);
}

AccountID
STObject::getAccountID(SField const& field) const
{
    return getFieldByValue<STAccount>(field);
}

Blob
STObject::getFieldVL(SField const& field) const
{
    STBlob empty;
    STBlob const& b = getFieldByConstRef<STBlob>(field, empty);
    return Blob(b.data(), b.data() + b.size());
}

STAmount const&
STObject::getFieldAmount(SField const& field) const
{
    static STAmount const empty{};
    return getFieldByConstRef<STAmount>(field, empty);
}

STPathSet const&
STObject::getFieldPathSet(SField const& field) const
{
    static STPathSet const empty{};
    return getFieldByConstRef<STPathSet>(field, empty);
}

STVector256 const&
STObject::getFieldV256(SField const& field) const
{
    static STVector256 const empty{};
    return getFieldByConstRef<STVector256>(field, empty);
}

STArray const&
STObject::getFieldArray(SField const& field) const
{
    static STArray const empty{};
    return getFieldByConstRef<STArray>(field, empty);
}

STCurrency const&
STObject::getFieldCurrency(SField const& field) const
{
    static STCurrency const empty{};
    return getFieldByConstRef<STCurrency>(field, empty);
}

STNumber const&
STObject::getFieldNumber(SField const& field) const
{
    static STNumber const empty{};
    return getFieldByConstRef<STNumber>(field, empty);
}

void
STObject::set(std::unique_ptr<STBase> v)
{
    set(std::move(*v.get()));
}

void
STObject::set(STBase&& v)
{
    auto const i = getFieldIndex(v.getFName());
    if (i != -1)
    {
        v_[i] = std::move(v);
    }
    else
    {
        if (!isFree())
            Throw<std::runtime_error>("missing field in templated STObject");
        v_.emplace_back(std::move(v));
    }
}

void
STObject::setFieldU8(SField const& field, unsigned char v)
{
    setFieldUsingSetValue<STUInt8>(field, v);
}

void
STObject::setFieldU16(SField const& field, std::uint16_t v)
{
    setFieldUsingSetValue<STUInt16>(field, v);
}

void
STObject::setFieldU32(SField const& field, std::uint32_t v)
{
    setFieldUsingSetValue<STUInt32>(field, v);
}

void
STObject::setFieldU64(SField const& field, std::uint64_t v)
{
    setFieldUsingSetValue<STUInt64>(field, v);
}

void
STObject::setFieldH128(SField const& field, uint128 const& v)
{
    setFieldUsingSetValue<STUInt128>(field, v);
}

void
STObject::setFieldH256(SField const& field, uint256 const& v)
{
    setFieldUsingSetValue<STUInt256>(field, v);
}

void
STObject::setFieldV256(SField const& field, STVector256 const& v)
{
    setFieldUsingSetValue<STVector256>(field, v);
}

void
STObject::setAccountID(SField const& field, AccountID const& v)
{
    setFieldUsingSetValue<STAccount>(field, v);
}

void
STObject::setFieldVL(SField const& field, Blob const& v)
{
    setFieldUsingSetValue<STBlob>(field, Buffer(v.data(), v.size()));
}

void
STObject::setFieldVL(SField const& field, Slice const& s)
{
    setFieldUsingSetValue<STBlob>(field, Buffer(s.data(), s.size()));
}

void
STObject::setFieldAmount(SField const& field, STAmount const& v)
{
    setFieldUsingAssignment(field, v);
}

void
STObject::setFieldCurrency(SField const& field, STCurrency const& v)
{
    setFieldUsingAssignment(field, v);
}

void
STObject::setFieldIssue(SField const& field, STIssue const& v)
{
    setFieldUsingAssignment(field, v);
}

void
STObject::setFieldNumber(SField const& field, STNumber const& v)
{
    setFieldUsingAssignment(field, v);
}

void
STObject::setFieldPathSet(SField const& field, STPathSet const& v)
{
    setFieldUsingAssignment(field, v);
}

void
STObject::setFieldArray(SField const& field, STArray const& v)
{
    setFieldUsingAssignment(field, v);
}

Json::Value
STObject::getJson(JsonOptions options) const
{
    Json::Value ret(Json::objectValue);

    for (auto const& elem : v_)
    {
        if (elem->getSType() != STI_NOTPRESENT)
            ret[elem->getFName().getJsonName()] = elem->getJson(options);
    }
    return ret;
}

bool
STObject::operator==(STObject const& obj) const
{
    // This is not particularly efficient, and only compares data elements
    // with binary representations
    int matches = 0;
    for (auto const& t1 : v_)
    {
        if ((t1->getSType() != STI_NOTPRESENT) && t1->getFName().isBinary())
        {
            // each present field must have a matching field
            bool match = false;
            for (auto const& t2 : obj.v_)
            {
                if (t1->getFName() == t2->getFName())
                {
                    if (t2 != t1)
                        return false;

                    match = true;
                    ++matches;
                    break;
                }
            }

            if (!match)
                return false;
        }
    }

    int fields = 0;
    for (auto const& t2 : obj.v_)
    {
        if ((t2->getSType() != STI_NOTPRESENT) && t2->getFName().isBinary())
            ++fields;
    }

    if (fields != matches)
        return false;

    return true;
}

void
STObject::add(Serializer& s, WhichFields whichFields) const
{
    // Depending on whichFields, signing fields are either serialized or
    // not.  Then fields are added to the Serializer sorted by fieldCode.
    std::vector<STBase const*> const fields{
        getSortedFields(*this, whichFields)};

    // insert sorted
    for (STBase const* const field : fields)
    {
        // When we serialize an object inside another object,
        // the type associated by rule with this field name
        // must be OBJECT, or the object cannot be deserialized
        SerializedTypeID const sType{field->getSType()};
        XRPL_ASSERT(
            (sType != STI_OBJECT) ||
                (field->getFName().fieldType == STI_OBJECT),
            "ripple::STObject::add : valid field type");
        field->addFieldID(s);
        field->add(s);
        if (sType == STI_ARRAY || sType == STI_OBJECT)
            s.addFieldID(sType, 1);
    }
}

std::vector<STBase const*>
STObject::getSortedFields(STObject const& objToSort, WhichFields whichFields)
{
    std::vector<STBase const*> sf;
    sf.reserve(objToSort.getCount());

    // Choose the fields that we need to sort.
    for (detail::STVar const& elem : objToSort.v_)
    {
        STBase const& base = elem.get();
        if ((base.getSType() != STI_NOTPRESENT) &&
            base.getFName().shouldInclude(whichFields))
        {
            sf.push_back(&base);
        }
    }

    // Sort the fields by fieldCode.
    std::sort(sf.begin(), sf.end(), [](STBase const* lhs, STBase const* rhs) {
        return lhs->getFName().fieldCode < rhs->getFName().fieldCode;
    });

    return sf;
}

}  // namespace ripple
