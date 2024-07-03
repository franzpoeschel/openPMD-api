#include <openPMD/openPMD.hpp>

namespace filebased_write_test
{
OPENPMDAPI_EXPORT
auto close_and_reopen_iterations(std::string const &filename) -> void;
} // namespace filebased_write_test
namespace close_and_reopen_test
{
OPENPMDAPI_EXPORT
auto close_and_reopen_test() -> void;
} // namespace close_and_reopen_test
