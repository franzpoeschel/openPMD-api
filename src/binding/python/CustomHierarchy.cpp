

#include "openPMD/CustomHierarchy.hpp"
#include "openPMD/ParticleSpecies.hpp"
#include "openPMD/RecordComponent.hpp"
#include "openPMD/backend/Attributable.hpp"
#include <pybind11/pybind11.h>

namespace py = pybind11;
using namespace openPMD;

void init_CustomHierarchy(py::module &m)
{
    py::class_<CustomHierarchy, Container<CustomHierarchy>, Attributable>(
        m, "CustomHierarchy")
        .def(
            "as_container_of_datasets",
            &CustomHierarchy::asContainerOf<RecordComponent>)
        .def("as_container_of_meshes", &CustomHierarchy::asContainerOf<Mesh>)
        .def(
            "as_container_of_particles",
            &CustomHierarchy::asContainerOf<ParticleSpecies>)

        .def_readwrite(
            "meshes",
            &CustomHierarchy::meshes,
            py::return_value_policy::copy,
            // garbage collection: return value must be freed before Iteration
            py::keep_alive<1, 0>())
        .def_readwrite(
            "particles",
            &CustomHierarchy::particles,
            py::return_value_policy::copy,
            // garbage collection: return value must be freed before Iteration
            py::keep_alive<1, 0>());
}
