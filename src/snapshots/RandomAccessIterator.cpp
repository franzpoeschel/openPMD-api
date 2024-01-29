#include "openPMD/snapshots/RandomAccessIterator.hpp"
namespace openPMD
{
template <typename iterator_t>
inline RandomAccessIterator<iterator_t>::RandomAccessIterator(iterator_t it)
    : m_it(it)
{}

template <typename iterator_t>
auto RandomAccessIterator<iterator_t>::operator*() const -> value_type const &
{
    return *m_it;
}

template <typename iterator_t>
auto RandomAccessIterator<iterator_t>::operator++() -> RandomAccessIterator &
{
    ++m_it;
    return *this;
}

template <typename iterator_t>
auto RandomAccessIterator<iterator_t>::operator--() -> RandomAccessIterator &
{
    --m_it;
    return *this;
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
template class RandomAccessIterator<iterator>;
template class RandomAccessIterator<const_iterator>;
} // namespace openPMD
