/* Copyright 2017-2021 Fabian Koller
 *
 * This file is part of openPMD-api.
 *
 * openPMD-api is free software: you can redistribute it and/or modify
 * it under the terms of of either the GNU General Public License or
 * the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * openPMD-api is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License and the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and the GNU Lesser General Public License along with openPMD-api.
 * If not, see <http://www.gnu.org/licenses/>.
 */
#pragma once

#include "openPMD/RecordComponent.hpp"
#include "openPMD/UnitDimension.hpp"
#include "openPMD/auxiliary/Variant.hpp"
#include "openPMD/backend/Container.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <type_traits> // std::remove_reference_t
#include <utility> // std::declval

namespace openPMD
{
template <typename, typename>
class BaseRecord;
namespace internal
{
    template <
        typename T_elem,
        typename T_RecordComponentData,
        typename T_RecordComponent>
    class BaseRecordData
        : public ContainerData<T_elem>
        , public T_RecordComponentData
    {
    public:
        /*
         * Non-owning iterator, so that we can mock the iterator for
         * the deprecated SCALAR keyword.
         * Non-owning to avoid memory loops.
         * Not nice, but it's deprecated for a reason.
         */
        std::optional<std::pair<std::string const, T_RecordComponent>>
            m_iterator;

        BaseRecordData();

        BaseRecordData(BaseRecordData const &) = delete;
        BaseRecordData(BaseRecordData &&) = delete;

        BaseRecordData &operator=(BaseRecordData const &) = delete;
        BaseRecordData &operator=(BaseRecordData &&) = delete;
    };

    // @todo change T_InternalContainer to direct iterator type
    template <typename T_BaseRecord, typename T_BaseIterator>
    class ScalarIterator
    {
        using T_Container = typename T_BaseRecord::T_Container;
        using T_Value =
            std::remove_reference_t<decltype(*std::declval<T_BaseIterator>())>;
        using Left = T_BaseIterator;
        struct Right
        { /*Empty*/
            constexpr bool operator==(Right const &) const noexcept
            {
                return true;
            }
            constexpr bool operator!=(Right const &) const noexcept
            {
                return false;
            }
        };

        template <typename, typename>
        friend class openPMD::BaseRecord;

        T_BaseRecord *m_baseRecord = nullptr;
        std::variant<Left, Right> m_iterator;

        explicit ScalarIterator() = default;

        ScalarIterator(T_BaseRecord *baseRecord)
            : m_baseRecord(baseRecord), m_iterator(Right())
        {}
        ScalarIterator(T_BaseRecord *baseRecord, Left iterator)
            : m_baseRecord(baseRecord), m_iterator(std::move(iterator))
        {}

    public:
        ScalarIterator &operator++()
        {
            std::visit(
                auxiliary::overloaded{
                    [](Left &left) { ++left; },
                    [this](Right &) {
                        m_iterator = m_baseRecord->container().end();
                    }},
                m_iterator);
            return *this;
        }

        T_Value *operator->()
        {
            return std::visit(
                auxiliary::overloaded{
                    [](Left &left) -> T_Value * { return left.operator->(); },
                    [this](Right &) -> T_Value * {
                        /*
                         * We cannot create this value on the fly since we only
                         * give out a pointer, so that would be use-after-free.
                         * Instead, we just keep one value around inside
                         * BaseRecordData and give it out when needed.
                         */
                        return &m_baseRecord->get().m_iterator.value();
                    }},
                m_iterator);
        }

        T_Value &operator*()
        {
            return *operator->();
        }

        bool operator==(ScalarIterator const &other) const
        {
            return this->m_iterator == other.m_iterator;
        }

        bool operator!=(ScalarIterator const &other) const
        {
            return !operator==(other);
        }
    };
} // namespace internal

template <typename T_elem, typename T_RecordComponent_ = T_elem>
class BaseRecord
    : public Container<T_elem>
    , public T_RecordComponent_
{
    using T_RecordComponent = T_RecordComponent_;
    using T_Container = Container<T_elem>;
    using T_Self = BaseRecord<T_elem, T_RecordComponent>;
    friend class Iteration;
    friend class ParticleSpecies;
    friend class PatchRecord;
    friend class Record;
    friend class Mesh;
    template <typename, typename>
    friend class internal::ScalarIterator;

    static_assert(
        traits::GenerationPolicy<T_RecordComponent>::is_noop,
        "Internal error: Scalar components cannot have generation policies.");

    using Data_t = internal::BaseRecordData<
        T_elem,
        typename T_RecordComponent::Data_t,
        T_RecordComponent>;
    std::shared_ptr<Data_t> m_baseRecordData{new Data_t()};

    inline Data_t &get()
    {
        return *m_baseRecordData;
    }

    inline Data_t const &get() const
    {
        return *m_baseRecordData;
    }

    inline void setData(std::shared_ptr<Data_t> data)
    {
        m_baseRecordData = std::move(data);
        T_Container::setData(m_baseRecordData);
        T_RecordComponent::setData(m_baseRecordData);
    }

    BaseRecord();

public:
    using key_type = typename Container<T_elem>::key_type;
    using mapped_type = typename Container<T_elem>::mapped_type;
    using value_type = typename Container<T_elem>::value_type;
    using size_type = typename Container<T_elem>::size_type;
    using difference_type = typename Container<T_elem>::difference_type;
    using allocator_type = typename Container<T_elem>::allocator_type;
    using reference = typename Container<T_elem>::reference;
    using const_reference = typename Container<T_elem>::const_reference;
    using pointer = typename Container<T_elem>::pointer;
    using const_pointer = typename Container<T_elem>::const_pointer;

    using iterator = internal::ScalarIterator<
        T_Self,
        typename T_Container::InternalContainer::iterator>;
    using const_iterator = internal::ScalarIterator<
        T_Self const,
        typename T_Container::InternalContainer::const_iterator>;

private:
    template <typename... Arg>
    iterator makeIterator(Arg &&...arg)
    {
        return iterator{this, std::forward<Arg>(arg)...};
    }
    template <typename... Arg>
    const_iterator makeIterator(Arg &&...arg) const
    {
        return const_iterator{this, std::forward<Arg>(arg)...};
    }

public:
    iterator begin()
    {
        if (get().m_datasetDefined)
        {
            return makeIterator();
        }
        else
        {
            return makeIterator(T_Container::begin());
        }
    }

    const_iterator begin() const
    {
        if (get().m_datasetDefined)
        {
            return makeIterator();
        }
        else
        {
            return makeIterator(T_Container::begin());
        }
    }

    iterator end()
    {
        return makeIterator(T_Container::end());
    }

    const_iterator end() const
    {
        return makeIterator(T_Container::end());
    }

    virtual ~BaseRecord() = default;

    mapped_type &operator[](key_type const &key)
    {
        auto it = this->find(key);
        if (it != this->end())
        {
            return std::visit(
                auxiliary::overloaded{
                    [](typename iterator::Left &l) -> mapped_type & {
                        return l->second;
                    },
                    [this](typename iterator::Right &) -> mapped_type & {
                        /*
                         * Do not use the iterator result, as it is a non-owning
                         * handle
                         */
                        return static_cast<mapped_type &>(*this);
                    }},
                it.m_iterator);
        }
        else
        {
            bool const keyScalar = (key == RecordComponent::SCALAR);
            if ((keyScalar && !Container<T_elem>::empty() && !scalar()) ||
                (scalar() && !keyScalar))
                throw std::runtime_error(
                    "A scalar component can not be contained at "
                    "the same time as one or more regular components.");

            if (keyScalar)
            {
                /*
                 * This activates the RecordComponent API of this object.
                 */
                T_RecordComponent::get();
            }
            mapped_type &ret = keyScalar ? static_cast<mapped_type &>(*this)
                                         : T_Container::operator[](key);
            return ret;
        }
    }

    mapped_type &operator[](key_type &&key)
    {
        auto it = this->find(key);
        if (it != this->end())
        {
            return std::visit(
                auxiliary::overloaded{
                    [](typename iterator::Left &l) -> mapped_type & {
                        return l->second;
                    },
                    [this](typename iterator::Right &) -> mapped_type & {
                        /*
                         * Do not use the iterator result, as it is a non-owning
                         * handle
                         */
                        return static_cast<mapped_type &>(*this);
                    }},
                it.m_iterator);
        }
        else
        {
            bool const keyScalar = (key == RecordComponent::SCALAR);
            if ((keyScalar && !Container<T_elem>::empty() && !scalar()) ||
                (scalar() && !keyScalar))
                throw std::runtime_error(
                    "A scalar component can not be contained at "
                    "the same time as one or more regular components.");

            if (keyScalar)
            {
                /*
                 * This activates the RecordComponent API of this object.
                 */
                T_RecordComponent::get();
            }
            mapped_type &ret = keyScalar
                ? static_cast<mapped_type &>(*this)
                : T_Container::operator[](std::move(key));
            return ret;
        }
    }

    mapped_type &at(key_type const &key)
    {
        return const_cast<mapped_type &>(
            static_cast<BaseRecord<T_elem, T_RecordComponent> const *>(this)
                ->at(key));
    }

    mapped_type const &at(key_type const &key) const
    {
        bool const keyScalar = (key == RecordComponent::SCALAR);
        if (keyScalar)
        {
            if (!get().m_datasetDefined)
            {
                throw std::out_of_range(
                    "[at()] Requested scalar entry from non-scalar record.");
            }
            return static_cast<mapped_type const &>(*this);
        }
        else
        {
            return T_Container::at(key);
        }
    }

    size_type erase(key_type const &key)
    {
        bool const keyScalar = (key == RecordComponent::SCALAR);
        size_type res;
        if (!keyScalar || (keyScalar && this->at(key).constant()))
            res = Container<T_elem>::erase(key);
        else
        {
            if (this->written())
            {
                Parameter<Operation::DELETE_DATASET> dDelete;
                dDelete.name = ".";
                this->IOHandler()->enqueue(IOTask(this, dDelete));
                this->IOHandler()->flush(internal::defaultFlushParams);
            }
            res = get().m_datasetDefined ? 1 : 0;
        }

        if (keyScalar)
        {
            this->written() = false;
            this->writable().abstractFilePosition.reset();
            this->get().m_datasetDefined = false;
        }
        return res;
    }

    iterator erase(iterator it)
    {
        return std::visit(
            auxiliary::overloaded{
                [this](typename iterator::Left &left) {
                    return makeIterator(T_Container::erase(left));
                },
                [this](typename iterator::Right &) {
                    if (this->written())
                    {
                        Parameter<Operation::DELETE_DATASET> dDelete;
                        dDelete.name = ".";
                        this->IOHandler()->enqueue(IOTask(this, dDelete));
                        this->IOHandler()->flush(internal::defaultFlushParams);
                        this->written() = false;
                    }
                    this->writable().abstractFilePosition.reset();
                    this->get().m_datasetDefined = false;
                    return end();
                }},
            it.m_iterator);
    }

    bool empty() const noexcept
    {
        return !scalar() && T_Container::empty();
    }

    iterator find(key_type const &key)
    {
        auto &r = get();
        if (key == RecordComponent::SCALAR && get().m_datasetDefined)
        {
            if (r.m_datasetDefined)
            {
                return begin();
            }
            else
            {
                return end();
            }
        }
        else
        {
            return makeIterator(r.m_container.find(key));
        }
    }

    const_iterator find(key_type const &key) const
    {
        auto &r = get();
        if (key == RecordComponent::SCALAR && get().m_datasetDefined)
        {
            if (r.m_datasetDefined)
            {
                return begin();
            }
            else
            {
                return end();
            }
        }
        else
        {
            return makeIterator(r.m_container.find(key));
        }
    }

    size_type count(key_type const &key) const
    {
        if (key == RecordComponent::SCALAR)
        {
            return get().m_datasetDefined ? 1 : 0;
        }
        else
        {
            return T_Container::count(key);
        }
    }

    size_type size() const
    {
        if (scalar())
        {
            return 1;
        }
        else
        {
            return T_Container::size();
        }
    }

    //! @todo add also, as soon as added in Container:
    // iterator erase(const_iterator first, const_iterator last) override;

    /** Return the physical dimension (quantity) of a record
     *
     * Annotating the physical dimension of a record allows us to read data
     * sets with arbitrary names and understand their purpose simply by
     * dimensional analysis. The dimensional base quantities in openPMD are
     * in order: length (L), mass (M), time (T), electric current (I),
     * thermodynamic temperature (theta), amount of substance (N),
     * luminous intensity (J) after the international system of quantities
     * (ISQ).
     *
     * @see https://en.wikipedia.org/wiki/Dimensional_analysis
     * @see
     * https://en.wikipedia.org/wiki/International_System_of_Quantities#Base_quantities
     * @see
     * https://github.com/openPMD/openPMD-standard/blob/1.1.0/STANDARD.md#required-for-each-record
     *
     * @return powers of the 7 base measures in the order specified above
     */
    std::array<double, 7> unitDimension() const;

    /** Returns true if this record only contains a single component
     *
     * @return true if a record with only a single component
     */
    bool scalar() const;

protected:
    // BaseRecord(internal::BaseRecordData<T_elem> *);
    void readBase();

private:
    void flush(std::string const &, internal::FlushParams const &) final;
    virtual void
    flush_impl(std::string const &, internal::FlushParams const &) = 0;
    // virtual void read() = 0;

    /**
     * @brief Check recursively whether this BaseRecord is dirty.
     *        It is dirty if any attribute or dataset is read from or written to
     *        the backend.
     *
     * @return true If dirty.
     * @return false Otherwise.
     */
    bool dirtyRecursive() const;
}; // BaseRecord

// implementation

namespace internal
{

    template <
        typename T_elem,
        typename T_RecordComponentData,
        typename T_RecordComponent>
    BaseRecordData<T_elem, T_RecordComponentData, T_RecordComponent>::
        BaseRecordData()
    {
        Attributable impl;
        impl.setData({this, [](auto const *) {}});
        impl.setAttribute(
            "unitDimension",
            std::array<double, 7>{{0., 0., 0., 0., 0., 0., 0.}});
    }
} // namespace internal

template <typename T_elem, typename T_RecordComponent>
BaseRecord<T_elem, T_RecordComponent>::BaseRecord()
{
    T_Container::setData(m_baseRecordData);
    T_RecordComponent::setData(m_baseRecordData);
    T_RecordComponent rc;
    auto rawRCData = &*this->m_recordComponentData;
    rc.setData(std::shared_ptr<typename T_RecordComponent::Data_t>{
        rawRCData, [](auto const *) {}});
    m_baseRecordData->m_iterator.emplace(
        std::make_pair(RecordComponent::SCALAR, std::move(rc)));
}

template <typename T_elem, typename T_RecordComponent>
inline std::array<double, 7>
BaseRecord<T_elem, T_RecordComponent>::unitDimension() const
{
    return this->getAttribute("unitDimension")
        .template get<std::array<double, 7>>();
}

template <typename T_elem, typename T_RecordComponent>
inline bool BaseRecord<T_elem, T_RecordComponent>::scalar() const
{
    return get().m_datasetDefined;
}

template <typename T_elem, typename T_RecordComponent>
inline void BaseRecord<T_elem, T_RecordComponent>::readBase()
{
    using DT = Datatype;
    Parameter<Operation::READ_ATT> aRead;

    aRead.name = "unitDimension";
    this->IOHandler()->enqueue(IOTask(this, aRead));
    this->IOHandler()->flush(internal::defaultFlushParams);
    if (auto val =
            Attribute(*aRead.resource).getOptional<std::array<double, 7>>();
        val.has_value())
        this->setAttribute("unitDimension", val.value());
    else
        throw std::runtime_error(
            "Unexpected Attribute datatype for 'unitDimension'");

    aRead.name = "timeOffset";
    this->IOHandler()->enqueue(IOTask(this, aRead));
    this->IOHandler()->flush(internal::defaultFlushParams);
    if (*aRead.dtype == DT::FLOAT)
        this->setAttribute(
            "timeOffset", Attribute(*aRead.resource).get<float>());
    else if (*aRead.dtype == DT::DOUBLE)
        this->setAttribute(
            "timeOffset", Attribute(*aRead.resource).get<double>());
    // conversion cast if a backend reports an integer type
    else if (auto val = Attribute(*aRead.resource).getOptional<double>();
             val.has_value())
        this->setAttribute("timeOffset", val.value());
    else
        throw std::runtime_error(
            "Unexpected Attribute datatype for 'timeOffset'");
}

template <typename T_elem, typename T_RecordComponent>
inline void BaseRecord<T_elem, T_RecordComponent>::flush(
    std::string const &name, internal::FlushParams const &flushParams)
{
    if (!this->written() && this->empty() && !get().m_datasetDefined)
        throw std::runtime_error(
            "A Record can not be written without any contained "
            "RecordComponents: " +
            name);

    this->flush_impl(name, flushParams);
    // flush_impl must take care to correctly set the dirty() flag so this
    // method doesn't do it
}

template <typename T_elem, typename T_RecordComponent>
inline bool BaseRecord<T_elem, T_RecordComponent>::dirtyRecursive() const
{
    if (this->dirty())
    {
        return true;
    }
    for (auto const &pair : *this)
    {
        if (pair.second.dirtyRecursive())
        {
            return true;
        }
    }
    return false;
}
} // namespace openPMD
