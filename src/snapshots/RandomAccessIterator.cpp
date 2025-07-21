#include "openPMD/snapshots/RandomAccessIterator.hpp"
#include "openPMD/Error.hpp"
namespace openPMD
{
template <typename iterator_t>
inline RandomAccessIterator<iterator_t>::RandomAccessIterator(iterator_t it)
    : m_it(it)
{}

template <typename iterator_t>
inline RandomAccessIterator<iterator_t>::RandomAccessIterator(
    iterator_t it, iterator_t begin, iterator_t end)
    : m_it(it)
    , m_automaticallyOpenIterations(
          InfoForAutomaticallyOpeningIterations{begin, end})
{
    if constexpr (!is_const())
    {
        while (m_it != end)
        {
            try
            {
                m_it->second.open();
            }
            catch (error::ReadError const &err)
            {
                std::cerr << "[RandomAccessIterator] Failed opening Iteration "
                          << m_it->first << ". Will skip. Original error was:\n"
                          << err.what();
                ++m_it;
                continue;
            }
            break;
        }
    }
}

template <typename iterator_t>
RandomAccessIterator<iterator_t>::~RandomAccessIterator() = default;

template <typename iterator_t>
auto RandomAccessIterator<iterator_t>::operator*() -> value_type &
{
    return *m_it;
}

template <typename iterator_t>
auto RandomAccessIterator<iterator_t>::operator*() const -> value_type const &
{
    return *m_it;
}

template <typename iterator_t>
template <bool boundary_is_inclusive>
auto RandomAccessIterator<iterator_t>::increment_operator_impl(
    iterator_t &(iterator_t::*incr)(),
    iterator_t InfoForAutomaticallyOpeningIterations::*boundary)
    -> RandomAccessIterator &
{
    if (!m_automaticallyOpenIterations.has_value())
    {
        (m_it.*incr)();
        return *this;
    }

    auto &end = (*m_automaticallyOpenIterations).*boundary;

    if constexpr (is_const())
    {
        ++m_it;
        return *this;
    }
    else
    {
        while (true)
        {
            (m_it.*incr)();
            if (m_it == end)
            {
                if constexpr (boundary_is_inclusive)
                {
                    try
                    {
                        m_it->second.open();
                    }
                    catch (error::ReadError const &err)
                    {
                        std::cerr << "[RandomAccessIterator] Failed opening "
                                     "Iteration "
                                  << m_it->first
                                  << ". Will ignore, no iterations are left. "
                                     "Returned Iteration will not be opened. "
                                     "Original error was:\n"
                                  << err.what();
                        continue;
                    }
                }
                break;
            }
            try
            {
                m_it->second.open();
            }
            catch (error::ReadError const &err)
            {
                std::cerr << "[RandomAccessIterator] Failed opening Iteration "
                          << m_it->first << ". Will skip. Original error was:\n"
                          << err.what();
                continue;
            }
            break;
        }
    }
    return *this;
}

template <typename iterator_t>
auto RandomAccessIterator<iterator_t>::operator++() -> RandomAccessIterator &
{
    return increment_operator_impl<false>(
        &iterator_t::operator++, &InfoForAutomaticallyOpeningIterations::m_end);
}

template <typename iterator_t>
auto RandomAccessIterator<iterator_t>::operator--() -> RandomAccessIterator &
{
    return increment_operator_impl<true>(
        &iterator_t::operator--,
        &InfoForAutomaticallyOpeningIterations::m_begin);
}

template <typename iterator_t>
auto RandomAccessIterator<iterator_t>::operator++(int i) -> RandomAccessIterator
{
    return parent_t::default_increment_operator(i);
}

template <typename iterator_t>
auto RandomAccessIterator<iterator_t>::operator--(int i) -> RandomAccessIterator
{
    return parent_t::default_decrement_operator(i);
}

template <typename iterator_t>
auto RandomAccessIterator<iterator_t>::operator==(
    RandomAccessIterator const &other) const -> bool
{
    return m_it == other.m_it;
}

using iterator = Container<Iteration, Iteration::IterationIndex_t>::iterator;
using const_iterator =
    Container<Iteration, Iteration::IterationIndex_t>::const_iterator;
using reverse_iterator =
    Container<Iteration, Iteration::IterationIndex_t>::reverse_iterator;
using const_reverse_iterator =
    Container<Iteration, Iteration::IterationIndex_t>::const_reverse_iterator;
template class RandomAccessIterator<iterator>;
template class RandomAccessIterator<const_iterator>;
template class RandomAccessIterator<reverse_iterator>;
template class RandomAccessIterator<const_reverse_iterator>;
} // namespace openPMD
