#include "openPMD/auxiliary/Future.hpp"
#include "openPMD/Error.hpp"
#include "openPMD/RecordComponent.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>

// comment

#include "openPMD/DatatypeMacros.hpp"

namespace openPMD::auxiliary
{

template <>
auto DeferredComputation<void>::get() -> void
{
    std::visit(
        auxiliary::overloaded{
            [](detail::OneTimeTask<void> &task) { std::move(task)(); },
            [](detail::CachedValue<void> &) { return; }},
        this->m_task);
}

template class DeferredComputation<void>;
template class DeferredComputation<RecordComponent::shared_ptr_dataset_types>;
template class DeferredComputation<std::string>; // used in tests

// need this for clang-tidy
#define OPENPMD_ARRAY(type) type[]
#define OPENPMD_APPLY_TEMPLATE(template_, type) template_<type>

#define INSTANTIATE_FUTURE(dtype)                                              \
    template class DeferredComputation<OPENPMD_APPLY_TEMPLATE(                 \
        std::shared_ptr, dtype)>;
#define INSTANTIATE_FUTURE_WITH_AND_WITHOUT_EXTENT(type)                       \
    INSTANTIATE_FUTURE(type) INSTANTIATE_FUTURE(OPENPMD_ARRAY(type))
OPENPMD_FOREACH_NONVECTOR_DATATYPE(INSTANTIATE_FUTURE_WITH_AND_WITHOUT_EXTENT)
#undef INSTANTIATE_FUTURE
#undef INSTANTIATE_FUTURE_WITH_AND_WITHOUT_EXTENT
#undef OPENPMD_ARRAY
#undef OPENPMD_APPLY_TEMPLATE
} // namespace openPMD::auxiliary

#include "openPMD/UndefDatatypeMacros.hpp"
