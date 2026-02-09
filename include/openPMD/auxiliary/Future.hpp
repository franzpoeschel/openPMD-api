#pragma once

#include "openPMD/auxiliary/TypeTraits.hpp"

namespace openPMD::auxiliary
{
template <typename T>
class DeferredComputationI
{
public:
    virtual auto operator()() -> T;
};

template <typename T>
class DeferredComputation
{
    using task_type = std::shared_ptr<DeferredComputationI<T>>;
    task_type m_task;
    bool m_valid = false;

public:
    DeferredComputation(task_type);
    explicit DeferredComputation() = default;
    ~DeferredComputation();

    auto get() -> T;

    [[nodiscard]] auto valid() const noexcept -> bool;
};
} // namespace openPMD::auxiliary
