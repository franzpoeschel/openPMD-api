#pragma once

namespace openPMD
{
enum class AdvanceStatus
{
    OK
};

enum class AdvanceMode
{
    AUTO, // according to accesstype
    READ,
    WRITE
};
} // namespace openPMD