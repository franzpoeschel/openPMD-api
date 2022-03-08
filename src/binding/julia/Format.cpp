// IO/Format

#include "defs.hpp"

void define_julia_Format(jlcxx::Module &mod)
{
    mod.add_bits<Format>("Format", jlcxx::julia_type("CppEnum"));
    jlcxx::stl::apply_stl<Format>(mod);

    mod.set_const("FORMAT_HDF5", Format::HDF5);
    mod.set_const("FORMAT_ADIOS1", Format::ADIOS1);
    mod.set_const("FORMAT_ADIOS2", Format::ADIOS2);
    mod.set_const("FORMAT_ADIOS2_SST", Format::ADIOS2_SST);
    mod.set_const("FORMAT_ADIOS2_SSC", Format::ADIOS2_SSC);
    mod.set_const("FORMAT_JSON", Format::JSON);
    mod.set_const("FORMAT_DUMMY", Format::DUMMY);
    mod.method("determine_format", determineFormat);
    mod.method("suffix", suffix);
}
