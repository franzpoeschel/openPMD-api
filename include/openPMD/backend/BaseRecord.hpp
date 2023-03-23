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
#include "openPMD/backend/Container.hpp"

#include <array>
#include <stdexcept>
#include <string>

namespace openPMD
{
namespace internal
{
    template <typename T_elem // = T_RecordComponent
              >
    class BaseRecordData final
        : public ContainerData<T_elem>
        , public T_elem::Data_t
    {
    public:
        BaseRecordData();

        BaseRecordData(BaseRecordData const &) = delete;
        BaseRecordData(BaseRecordData &&) = delete;

        BaseRecordData &operator=(BaseRecordData const &) = delete;
        BaseRecordData &operator=(BaseRecordData &&) = delete;
    };
} // namespace internal

template <typename T_elem>
class BaseRecord
    : public Container<T_elem>
    , public T_elem // T_RecordComponent
{
    using T_RecordComponent = T_elem;
    using T_Container = Container<T_elem>;
    using T_Self = BaseRecord<T_elem>;
    friend class Iteration;
    friend class ParticleSpecies;
    friend class PatchRecord;
    friend class Record;
    friend class Mesh;

    static_assert(
        traits::GenerationPolicy<T_RecordComponent>::is_noop,
        "Internal error: Scalar components cannot have generation policies.");

    using Data_t = internal::BaseRecordData<T_elem>;
    std::shared_ptr<Data_t> m_baseRecordData{new Data_t()};

    inline Data_t const &get() const
    {
        return dynamic_cast<Data_t const &>(*this->m_attri);
    }

    inline Data_t &get()
    {
        return dynamic_cast<Data_t &>(*this->m_attri);
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
    using iterator = typename T_Container::iterator;
    using const_iterator = typename T_Container::const_iterator;

    virtual ~BaseRecord() = default;

    mapped_type &operator[](key_type const &key);
    mapped_type &operator[](key_type &&key);
    mapped_type &at(key_type const &key);
    mapped_type const &at(key_type const &key) const;
    size_type erase(key_type const &key);
    iterator erase(iterator res);
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
    void readBase();

private:
    void flush(std::string const &, internal::FlushParams const &) final;
    virtual void
    flush_impl(std::string const &, internal::FlushParams const &) = 0;

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
    template <typename T_elem>
    BaseRecordData<T_elem>::BaseRecordData()
    {
        Attributable impl;
        impl.setData({this, [](auto const *) {}});
        impl.setAttribute(
            "unitDimension",
            std::array<double, 7>{{0., 0., 0., 0., 0., 0., 0.}});
    }
} // namespace internal

template <typename T_elem>
BaseRecord<T_elem>::BaseRecord()
{
    auto baseRecordData = std::make_shared<Data_t>();
    Attributable::setData(std::move(baseRecordData));
}

template <typename T_elem>
inline typename BaseRecord<T_elem>::mapped_type &
BaseRecord<T_elem>::operator[](key_type const &key)
{
    auto it = this->find(key);
    if (it != this->end())
        return it->second;
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

template <typename T_elem>
inline typename BaseRecord<T_elem>::mapped_type &
BaseRecord<T_elem>::operator[](key_type &&key)
{
    auto it = this->find(key);
    if (it != this->end())
        return it->second;
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
                                     : T_Container::operator[](std::move(key));
        return ret;
    }
}

template <typename T_elem>
auto BaseRecord<T_elem>::at(key_type const &key) -> mapped_type &
{
    return const_cast<mapped_type &>(
        static_cast<BaseRecord<T_elem> const *>(this)->at(key));
}

template <typename T_elem>
auto BaseRecord<T_elem>::at(key_type const &key) const -> mapped_type const &
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

template <typename T_elem>
inline typename BaseRecord<T_elem>::size_type
BaseRecord<T_elem>::erase(key_type const &key)
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
        res = get().datasetDefined() ? 1 : 0;
    }

    if (keyScalar)
    {
        this->written() = false;
        this->writable().abstractFilePosition.reset();
        this->get().m_datasetDefined = false;
    }
    return res;
}

template <typename T_elem>
inline typename BaseRecord<T_elem>::iterator
BaseRecord<T_elem>::erase(iterator res)
{
    bool const keyScalar = (res->first == RecordComponent::SCALAR);
    iterator ret;
    if (!keyScalar || (keyScalar && this->at(res->first).constant()))
        ret = Container<T_elem>::erase(res);
    else
    {
        throw std::runtime_error(
            "Unreachable! Iterators do not yet cover scalars (they will in a "
            "later commit).");
    }

    return ret;
}

template <typename T_elem>
inline std::array<double, 7> BaseRecord<T_elem>::unitDimension() const
{
    return this->getAttribute("unitDimension")
        .template get<std::array<double, 7>>();
}

template <typename T_elem>
inline bool BaseRecord<T_elem>::scalar() const
{
    return get().datasetDefined();
}

template <typename T_elem>
inline void BaseRecord<T_elem>::readBase()
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

template <typename T_elem>
inline void BaseRecord<T_elem>::flush(
    std::string const &name, internal::FlushParams const &flushParams)
{
    if (!this->written() && this->T_Container::empty() &&
        !get().datasetDefined())
        throw std::runtime_error(
            "A Record can not be written without any contained "
            "RecordComponents: " +
            name);

    this->flush_impl(name, flushParams);
    // flush_impl must take care to correctly set the dirty() flag so this
    // method doesn't do it
}

template <typename T_elem>
inline bool BaseRecord<T_elem>::dirtyRecursive() const
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
