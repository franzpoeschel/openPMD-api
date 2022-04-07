#pragma once

#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace openPMD
{
/**
 * @brief Base class for all openPMD-specific error types.
 *
 * Should not be instantiated directly, but only by implementing child classes
 * in the openPMD::error namespace.
 *
 */
class Error : public std::exception
{
private:
    std::string m_what;

protected:
    Error(std::string what) : m_what(what)
    {}

public:
    virtual const char *what() const noexcept;

    Error(Error const &) = default;
    Error(Error &&) = default;

    Error &operator=(Error const &) = default;
    Error &operator=(Error &&) = default;

    virtual ~Error() noexcept = default;
};

namespace error
{
    /**
     * @brief An operation was requested that is not supported in a specific
     *     backend.
     *
     * Example: Append mode is not available in ADIOS1.
     */
    class OperationUnsupportedInBackend : public Error
    {
    public:
        std::string backend;
        OperationUnsupportedInBackend(std::string backend_in, std::string what);
    };

    /**
     * @brief The API was used in an illegal way.
     *
     * Example: File-based iteration encoding is selected without specifying an
     *     expansion pattern.
     */
    class WrongAPIUsage : public Error
    {
    public:
        WrongAPIUsage(std::string what);
    };

    class BackendConfigSchema : public Error
    {
    public:
        std::vector<std::string> errorLocation;

        BackendConfigSchema(std::vector<std::string>, std::string what);
    };

    /**
     * @brief Internal errors that should not happen. Please report.
     *
     * Example: A nullpointer is observed somewhere.
     */
    class Internal : public Error
    {
    public:
        Internal(std::string const &what);
    };

    enum class AffectedObject
    {
        Attribute,
        Dataset,
        Group,
        Other
    };

    enum class Reason
    {
        NotFound,
        CannotRead,
        UnexpectedContent,
        Inaccessible,
        Other
    };

    /*
     * Read error concerning a specific object.
     */
    class ReadError : public Error
    {
    public:
        AffectedObject affectedObject;
        Reason reason;
        // If empty, then the error is thrown by the frontend
        std::optional<std::string> backend;
        std::string description; // object path, further details, ...

        ReadError(
            AffectedObject,
            Reason,
            std::optional<std::string> backend_in,
            std::string description_in);
    };

    /*
     * Inrecoverable parse error from the frontend.
     */
    class ParseError : public Error
    {
    public:
        ParseError(std::string what);
    };
} // namespace error
} // namespace openPMD
