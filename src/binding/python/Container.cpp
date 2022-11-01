/* Copyright 2018-2021 Axel Huebl
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
 *
 * The function `bind_container` is based on std_bind.h in pybind11
 * Copyright (c) 2016 Sergey Lyskov and Wenzel Jakob
 *
 * BSD-style license, see pybind11 LICENSE file.
 */

#include <pybind11/pybind11.h>

#include "openPMD/Iteration.hpp"
#include "openPMD/Mesh.hpp"
#include "openPMD/ParticlePatches.hpp"
#include "openPMD/ParticleSpecies.hpp"
#include "openPMD/Record.hpp"
#include "openPMD/Series.hpp"
#include "openPMD/backend/BaseRecord.hpp"
#include "openPMD/backend/BaseRecordComponent.hpp"
#include "openPMD/backend/Container.hpp"
#include "openPMD/backend/MeshRecordComponent.hpp"
#include "openPMD/backend/PatchRecord.hpp"
#include "openPMD/backend/PatchRecordComponent.hpp"
#include "openPMD/binding/python/Container.hpp"

namespace py = pybind11;
using namespace openPMD;

using PyIterationContainer = Series::IterationsContainer_t;
using PyMeshContainer = Container<Mesh>;
using PyPartContainer = Container<ParticleSpecies>;
using PyPatchContainer = Container<ParticlePatches>;
using PyRecordContainer = Container<Record>;
using PyPatchRecordContainer = Container<PatchRecord>;
using PyRecordComponentContainer = Container<RecordComponent>;
using PyMeshRecordComponentContainer = Container<MeshRecordComponent>;
using PyPatchRecordComponentContainer = Container<PatchRecordComponent>;
// using PyBaseRecordComponentContainer = Container<BaseRecordComponent>;
PYBIND11_MAKE_OPAQUE(PyIterationContainer)
PYBIND11_MAKE_OPAQUE(PyMeshContainer)
PYBIND11_MAKE_OPAQUE(PyPartContainer)
PYBIND11_MAKE_OPAQUE(PyPatchContainer)
PYBIND11_MAKE_OPAQUE(PyRecordContainer)
PYBIND11_MAKE_OPAQUE(PyPatchRecordContainer)
PYBIND11_MAKE_OPAQUE(PyRecordComponentContainer)
PYBIND11_MAKE_OPAQUE(PyMeshRecordComponentContainer)
PYBIND11_MAKE_OPAQUE(PyPatchRecordComponentContainer)
// PYBIND11_MAKE_OPAQUE(PyBaseRecordComponentContainer)

namespace openPMD::detail
{
template <typename Map>
void wrap_bind_container(py::handle scope, std::string const &name)
{
    create_and_bind_container<Map, Attributable>(std::move(scope), name)
        .def(py::init<Map const &>());
}
} // namespace openPMD::detail

void init_Container(py::module &m)
{
    ::detail::create_and_bind_container<PyIterationContainer, Attributable>(
        m, "Iteration_Container");
    ::detail::create_and_bind_container<PyMeshContainer, Attributable>(
        m, "Mesh_Container");
    ::detail::create_and_bind_container<PyPartContainer, Attributable>(
        m, "Particle_Container");
    ::detail::create_and_bind_container<PyPatchContainer, Attributable>(
        m, "Particle_Patches_Container");
    ::detail::create_and_bind_container<PyRecordContainer, Attributable>(
        m, "Record_Container");
    ::detail::create_and_bind_container<PyPatchRecordContainer, Attributable>(
        m, "Patch_Record_Container");
    ::detail::
        create_and_bind_container<PyRecordComponentContainer, Attributable>(
            m, "Record_Component_Container");
    ::detail::
        create_and_bind_container<PyMeshRecordComponentContainer, Attributable>(
            m, "Mesh_Record_Component_Container");
    ::detail::create_and_bind_container<
        PyPatchRecordComponentContainer,
        Attributable>(m, "Patch_Record_Component_Container");
    // ::detail::create_and_bind_container<PyBaseRecordComponentContainer,
    // Attributable>(
    //     m, "Base_Record_Component_Container");
}
