

#include "openPMD/CustomHierarchy.hpp"
#include "openPMD/backend/Attributable.hpp"
#include <pybind11/pybind11.h>

namespace py = pybind11;
using namespace openPMD;

void init_CustomHierarchy(py::module &m)
{
    py::class_<CustomHierarchy, Container<CustomHierarchy>, Attributable>(
        m, "CustomHierarchy");
}
