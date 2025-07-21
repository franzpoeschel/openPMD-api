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
{}

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
auto RandomAccessIterator<iterator_t>::operator++() -> RandomAccessIterator &
{
    if (!m_automaticallyOpenIterations.has_value())
    {
        ++m_it;
        return *this;
    }

    auto &end = m_automaticallyOpenIterations->m_end;

    if constexpr (std::is_const_v<std::remove_reference_t<decltype(*m_it)>>)
    {
        ++m_it;
        return *this;
    }
    else
    {
        while (true)
        {
            ++m_it;
            if (m_it == end)
            {
                break;
            }
            try
            {
                m_it->second.open();
            }
            catch (error::ReadError const &)
            {
                continue;
            }
            break;
        }
    }
    return *this;
}

template <typename iterator_t>
auto RandomAccessIterator<iterator_t>::operator--() -> RandomAccessIterator &
{
    --m_it;
    return *this;
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
