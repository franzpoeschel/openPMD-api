// RecordComponent_load_chunk

#include "defs.hpp"

void define_julia_RecordComponent_load_chunk(
    jlcxx::Module & /*mod*/, jlcxx::TypeWrapper<RecordComponent> &type)
{
#define USE_TYPE(NAME, ENUM, TYPE)                                             \
    type.method(                                                               \
        "cxx_load_chunk_" NAME,                                                \
        static_cast<void (RecordComponent::*)(                                 \
            std::shared_ptr<TYPE>, Offset, Extent)>(                           \
            &RecordComponent::loadChunk<TYPE>));                               \
    type.method(                                                               \
        "cxx_load_chunk_" NAME,                                                \
        static_cast<std::shared_ptr<TYPE> (RecordComponent::*)(                \
            Offset, Extent)>(&RecordComponent::loadChunk<TYPE>));
    {
        FORALL_SCALAR_OPENPMD_TYPES(USE_TYPE)
    }
#undef USE_TYPE
}
