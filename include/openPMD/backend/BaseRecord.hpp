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
#include "openPMD/auxiliary/TypeTraits.hpp"
#include "openPMD/backend/Container.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <type_traits> // std::remove_reference_t
#include <utility> // std::declval

namespace openPMD
{
namespace internal
{
    template <typename T_elem, typename T_RecordComponentData>
    class BaseRecordData
        : public ContainerData<T_elem>
        , public T_RecordComponentData
    {
    public:
        /**
         * True if this Record contains a scalar record component.
         * If so, then that record component is the only component contained,
         * and the last hierarchical layer is skipped (i.e. only one OPEN_PATH
         * task for Record and RecordComponent).
         */
        bool m_containsScalar = false;

        BaseRecordData();

        BaseRecordData(BaseRecordData const &) = delete;
        BaseRecordData(BaseRecordData &&) = delete;

        BaseRecordData &operator=(BaseRecordData const &) = delete;
        BaseRecordData &operator=(BaseRecordData &&) = delete;
    };
} // namespace internal

/*
 * We want to specify different child classes for BaseRecord, depending on
 * the position in the openPMD object hierarchy that is being described.
 * The child class might be RecordComponent or MeshRecordComponent.
 * But also, we want to be able to have BaseRecord recursively as a child class.
 * This would be an infinite type BaseRecord<BaseRecord<BaseRecord<...>>>, so
 * we need to use a little trick.
 * We specify the above concept by BaseRecord<void> instead and use the
 * auxiliary::OkOr class template to treat the void type specially.
 */
template <
    typename T_elem_maybe_void,
    typename T_RecordComponent_ = T_elem_maybe_void>
class BaseRecord
    : public Container<
          typename auxiliary::OkOr<T_elem_maybe_void, BaseRecord<void> >::type>
    , public T_RecordComponent_
{
protected:
    // make it available for child classes

private:
    using T_RecordComponent = T_RecordComponent_;
    using T_elem =
        typename auxiliary::OkOr<T_elem_maybe_void, BaseRecord<void> >::type;
    using T_Container = Container<T_elem>;
    using T_RecordComponentData = std::remove_reference_t<
        decltype(*std::declval<T_RecordComponent>().m_recordComponentData)>;
    friend class Iteration;
    friend class ParticleSpecies;
    friend class PatchRecord;
    friend class Record;
    friend class Mesh;

    std::shared_ptr<internal::BaseRecordData<T_elem, T_RecordComponentData> >
        m_baseRecordData{
            new internal::BaseRecordData<T_elem, T_RecordComponentData>()};
    using Data_t = internal::BaseRecordData<T_elem, T_RecordComponentData>;

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
    using iterator = typename Container<T_elem>::iterator;
    using const_iterator = typename Container<T_elem>::const_iterator;

    // this avoids object slicing
    operator T_RecordComponent() const
    {
        T_RecordComponent res;
        res.setData(m_baseRecordData);
        return res;
    }

    virtual ~BaseRecord() = default;

    mapped_type &operator[](key_type const &key) override;
    mapped_type &operator[](key_type &&key) override;
    mapped_type &at(key_type const &key) override;
    mapped_type const &at(key_type const &key) const override;
    size_type erase(key_type const &key) override;
    iterator erase(iterator res) override;
    bool empty() const noexcept;
    size_type count(key_type const &key) const override;

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

    void datasetDefined() override;

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
    template <typename T_elem, typename T_RecordComponentData>
    BaseRecordData<T_elem, T_RecordComponentData>::BaseRecordData()
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
}

template <typename T_elem, typename T_RecordComponent>
inline typename BaseRecord<T_elem, T_RecordComponent>::mapped_type &
BaseRecord<T_elem, T_RecordComponent>::operator[](key_type const &key)
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
            datasetDefined();
        }
        mapped_type &ret = keyScalar ? static_cast<mapped_type &>(*this)
                                     : T_Container::operator[](key);
        return ret;
    }
}

template <typename T_elem, typename T_RecordComponent>
inline typename BaseRecord<T_elem, T_RecordComponent>::mapped_type &
BaseRecord<T_elem, T_RecordComponent>::operator[](key_type &&key)
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
            datasetDefined();
        }
        /*
         * datasetDefined() inits the container entry
         * is this object slicing???? JA
         */
        mapped_type &ret = keyScalar ? static_cast<mapped_type &>(*this)
                                     : T_Container::operator[](std::move(key));
        return ret;
    }
}

template <typename T_elem, typename T_RecordComponent>
inline auto BaseRecord<T_elem, T_RecordComponent>::at(key_type const &key)
    -> mapped_type &
{
    return const_cast<mapped_type &>(
        static_cast<BaseRecord<T_elem, T_RecordComponent> const *>(this)->at(
            key));
}

template <typename T_elem, typename T_RecordComponent>
inline auto BaseRecord<T_elem, T_RecordComponent>::at(key_type const &key) const
    -> mapped_type const &
{
    bool const keyScalar = (key == RecordComponent::SCALAR);
    if (keyScalar)
    {
        if (!get().m_containsScalar)
        {
            throw std::out_of_range(
                "[at()] Requested scalar entry from non-scalar record.");
        }
        return static_cast<mapped_type const &>(*this);
    }
    else
    {
        return at(key);
    }
}

template <typename T_elem, typename T_RecordComponent>
inline typename BaseRecord<T_elem, T_RecordComponent>::size_type
BaseRecord<T_elem, T_RecordComponent>::erase(key_type const &key)
{
    bool const keyScalar = (key == RecordComponent::SCALAR);
    size_type res;
    if (!keyScalar || (keyScalar && this->at(key).constant()))
        res = Container<T_elem>::erase(key);
    else
    {
        mapped_type &rc = this->find(RecordComponent::SCALAR)->second;
        if (rc.written())
        {
            Parameter<Operation::DELETE_DATASET> dDelete;
            dDelete.name = ".";
            this->IOHandler()->enqueue(IOTask(&rc, dDelete));
            this->IOHandler()->flush(internal::defaultFlushParams);
        }
        res = Container<T_elem>::erase(key);
    }

    if (keyScalar)
    {
        this->written() = false;
        this->writable().abstractFilePosition.reset();
        this->get().m_containsScalar = false;
    }
    return res;
}

template <typename T_elem, typename T_RecordComponent>
inline typename BaseRecord<T_elem, T_RecordComponent>::iterator
BaseRecord<T_elem, T_RecordComponent>::erase(iterator res)
{
    bool const keyScalar = (res->first == RecordComponent::SCALAR);
    iterator ret;
    if (!keyScalar || (keyScalar && this->at(res->first).constant()))
        ret = Container<T_elem>::erase(res);
    else
    {
        mapped_type &rc = this->find(RecordComponent::SCALAR)->second;
        if (rc.written())
        {
            Parameter<Operation::DELETE_DATASET> dDelete;
            dDelete.name = ".";
            this->IOHandler()->enqueue(IOTask(&rc, dDelete));
            this->IOHandler()->flush(internal::defaultFlushParams);
        }
        ret = Container<T_elem>::erase(res);
    }

    if (keyScalar)
    {
        this->written() = false;
        this->writable().abstractFilePosition.reset();
        this->get().m_containsScalar = false;
    }
    return ret;
}

template <typename T_elem, typename T_RecordComponent>
bool BaseRecord<T_elem, T_RecordComponent>::empty() const noexcept
{
    return T_Container::empty();
}

template <typename T_elem, typename T_RecordComponent>
auto BaseRecord<T_elem, T_RecordComponent>::count(key_type const &key) const
    -> size_type
{
    if (key == RecordComponent::SCALAR)
    {
        return get().m_containsScalar ? 1 : 0;
    }
    else
    {
        return T_Container::count(key);
    }
}

template <typename T_elem, typename T_RecordComponent>
inline std::array<double, 7>
BaseRecord<T_elem, T_RecordComponent>::unitDimension() const
{
    return this->getAttribute("unitDimension")
        .template get<std::array<double, 7> >();
}

template <typename T_elem, typename T_RecordComponent>
inline bool BaseRecord<T_elem, T_RecordComponent>::scalar() const
{
    return get().m_containsScalar;
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
            Attribute(*aRead.resource).getOptional<std::array<double, 7> >();
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
    if (!this->written() && this->empty() && !get().m_containsScalar)
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

template <typename T_elem, typename T_RecordComponent>
void BaseRecord<T_elem, T_RecordComponent>::datasetDefined()
{
    // If the RecordComponent API of this object has been used, then the record
    // is a scalar one
    T_RecordComponent &rc = *this;
    /*
     * No need to do any of the hierarchy linking business,
     * rc and *this are the same object, so everything is linked already.
     * Just need to init the RC-specific stuff.
     */
    traits::GenerationPolicy<T_RecordComponent> gen;
    gen(rc);
    get().m_containsScalar = true;
    T_RecordComponent::datasetDefined();
}
} // namespace openPMD
