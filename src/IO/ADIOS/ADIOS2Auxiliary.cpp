#include "openPMD/config.hpp"
#if openPMD_HAVE_ADIOS2
#include "openPMD/IO/ADIOS/ADIOS2Auxiliary.hpp"
#include "openPMD/Datatype.hpp"

namespace openPMD
{
namespace detail
{
    template < typename T > std::string ToDatatypeHelper< T >::type( )
    {
        return adios2::GetType< T >( );
    }

    template < typename T >
    std::string ToDatatypeHelper< std::vector< T > >::type( )
    {
        return

            adios2::GetType< T >( );
    }

    template < typename T, size_t n >
    std::string ToDatatypeHelper< std::array< T, n > >::type( )
    {
        return

            adios2::GetType< T >( );
    }

    std::string ToDatatypeHelper< bool >::type( )
    {
        return ToDatatypeHelper< bool_representation >::type( );
    }

    template < typename T > std::string ToDatatype::operator( )( )
    {
        return ToDatatypeHelper< T >::type( );
    }

    template < int n > std::string ToDatatype::operator( )( )
    {
        return "";
    }

    Datatype fromADIOS2Type( std::string const & dt )
    {
        static std::map< std::string, Datatype > map{
            {"string", Datatype::STRING},
            {"char", Datatype::CHAR},
            {"signed char", Datatype::CHAR},
            {"unsigned char", Datatype::UCHAR},
            {"short", Datatype::SHORT},
            {"unsigned short", Datatype::USHORT},
            {"int", Datatype::INT},
            {"unsigned int", Datatype::UINT},
            {"long int", Datatype::LONG},
            {"unsigned long int", Datatype::ULONG},
            {"long long int", Datatype::LONGLONG},
            {"unsigned long long int", Datatype::ULONGLONG},
            {"float", Datatype::FLOAT},
            {"double", Datatype::DOUBLE},
            {"long double", Datatype::LONG_DOUBLE}};
        auto it = map.find( dt );
        if ( it != map.end( ) )
        {
            return it->second;
        }
        else
        {
            return Datatype::UNDEFINED;
        }
    }

    template < typename T >
    typename std::vector< T >::size_type
    AttributeInfoHelper< T >::getSize( adios2::IO & IO,
                                       std::string const & attributeName )
    {
        auto attribute = IO.InquireAttribute< T >( attributeName );
        if ( !attribute )
        {
            throw std::runtime_error(
                "Internal error: Attribute not present." );
        }
        return attribute.Data( ).size( );
    }

    template < typename T >
    typename std::vector< T >::size_type
    AttributeInfoHelper< std::vector< T > >::getSize(
        adios2::IO & IO, std::string const & attributeName )
    {
        return AttributeInfoHelper< T >::getSize( IO, attributeName );
    }

    typename std::vector< bool_representation >::size_type
    AttributeInfoHelper< bool >::getSize( adios2::IO & IO,
                                          std::string const & attributeName )
    {
        return AttributeInfoHelper< bool_representation >::getSize(
            IO, attributeName );
    }

    template < typename T >
    typename std::vector< T >::size_type AttributeInfo::
    operator( )( adios2::IO & IO, std::string const & attributeName )
    {
        return AttributeInfoHelper< T >::getSize( IO, attributeName );
    }

    template < int n, typename... Params >
    size_t AttributeInfo::operator( )( Params &&... )
    {
        return 0;
    }

    Datatype attributeInfo( adios2::IO & IO, std::string const & attributeName )
    {
        std::string type = IO.AttributeType( attributeName );
        if ( type.empty( ) )
        {
            return Datatype::UNDEFINED;
        }
        else
        {
            static AttributeInfo ai;
            Datatype basicType = fromADIOS2Type( type );
            auto size =
                switchType< size_t >( basicType, ai, IO, attributeName );
            Datatype openPmdType = size == 1
                ? basicType
                : size == 7 && basicType == Datatype::DOUBLE
                    ? Datatype::ARR_DBL_7
                    : toVectorType( basicType );
            return openPmdType;
        }
    }
} // namespace detail
} // namespace openPMD
#endif
