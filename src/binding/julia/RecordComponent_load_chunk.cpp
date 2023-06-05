// RecordComponent_load_chunk

#include "defs.hpp"

namespace
{
struct UseType
{
    template <typename T>
    static void call(jlcxx::TypeWrapper<RecordComponent> &type)
    {
        type.method(
            "cxx_load_" + datatypeToString(determineDatatype<T>()),
            overload_cast<std::shared_ptr<T>, Offset, Extent>(
                &RecordComponent::loadChunk<T>));
        type.method(
            "cxx_load_" + datatypeToString(determineDatatype<T>()),
            overload_cast<Offset, Extent>(&RecordComponent::loadChunk<T>));
    }
};
} // namespace

void define_julia_RecordComponent_load_chunk(
    jlcxx::Module & /*mod*/, jlcxx::TypeWrapper<RecordComponent> &type)
{
    forallScalarJuliaTypes<UseType>(type);
}
