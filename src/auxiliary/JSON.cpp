/* Copyright 2020-2021 Franz Poeschel
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
 */

#include "openPMD/auxiliary/JSON.hpp"

#include "openPMD/auxiliary/Filesystem.hpp"
#include "openPMD/auxiliary/Option.hpp"
#include "openPMD/auxiliary/StringManip.hpp"

#include <algorithm>
#include <cctype> // std::isspace
#include <fstream>
#include <iostream> // std::cerr
#include <map>
#include <sstream>
#include <utility> // std::forward
#include <vector>

namespace openPMD
{
namespace json
{
    TracingJSON::TracingJSON() : TracingJSON( nlohmann::json() )
    {
    }

    TracingJSON::TracingJSON( nlohmann::json originalJSON )
        : m_originalJSON(
              std::make_shared< nlohmann::json >( std::move( originalJSON ) ) ),
          m_shadow( std::make_shared< nlohmann::json >() ),
          m_positionInOriginal( &*m_originalJSON ),
          m_positionInShadow( &*m_shadow )
    {
    }

    nlohmann::json const & TracingJSON::getShadow() const
    {
        return *m_positionInShadow;
    }

    nlohmann::json TracingJSON::invertShadow() const
    {
        nlohmann::json inverted = *m_positionInOriginal;
        invertShadow( inverted, *m_positionInShadow );
        return inverted;
    }

    void TracingJSON::invertShadow(
        nlohmann::json & result, nlohmann::json const & shadow ) const
    {
        if( !shadow.is_object() )
        {
            return;
        }
        std::vector< std::string > toRemove;
        for( auto it = shadow.begin(); it != shadow.end(); ++it )
        {
            nlohmann::json & partialResult = result[ it.key() ];
            if( partialResult.is_object() )
            {
                invertShadow( partialResult, it.value() );
                if( partialResult.size() == 0 )
                {
                    toRemove.emplace_back( it.key() );
                }
            }
            else
            {
                toRemove.emplace_back( it.key() );
            }
        }
        for( auto const & key : toRemove )
        {
            result.erase( key );
        }
    }

    void
    TracingJSON::declareFullyRead()
    {
        if( m_trace )
        {
            // copy over
            *m_positionInShadow = *m_positionInOriginal;
        }
    }

    TracingJSON::TracingJSON(
        std::shared_ptr< nlohmann::json > originalJSON,
        std::shared_ptr< nlohmann::json > shadow,
        nlohmann::json * positionInOriginal,
        nlohmann::json * positionInShadow,
        bool trace )
        : m_originalJSON( std::move( originalJSON ) ),
          m_shadow( std::move( shadow ) ),
          m_positionInOriginal( positionInOriginal ),
          m_positionInShadow( positionInShadow ),
          m_trace( trace )
    {
    }

    namespace {
    auxiliary::Option< std::string >
    extractFilename( std::string const & unparsed )
    {
        std::string trimmed = auxiliary::trim(
            unparsed, []( char c ) { return std::isspace( c ); } );
        if( trimmed.at( 0 ) == '@' )
        {
            trimmed = trimmed.substr( 1 );
            trimmed = auxiliary::trim(
                trimmed, []( char c ) { return std::isspace( c ); } );
            return auxiliary::makeOption( trimmed );
        }
        else
        {
            return auxiliary::Option< std::string >{};
        }
    }
    }

    nlohmann::json
    parseOptions( std::string const & options )
    {
        auto filename = extractFilename( options );
        if( filename.has_value() )
        {
            std::fstream handle;
            handle.open( filename.get(), std::ios_base::in );
            nlohmann::json res;
            handle >> res;
            if( !handle.good() )
            {
                throw std::runtime_error(
                    "Failed reading JSON config from file " + filename.get() +
                    "." );
            }
            lowerCase( res );
            return res;
        }
        else
        {
            auto res = nlohmann::json::parse( options );
            lowerCase( res );
            return res;
        }
    }

#if openPMD_HAVE_MPI
    nlohmann::json
    parseOptions( std::string const & options, MPI_Comm comm )
    {
        auto filename = extractFilename( options );
        if( filename.has_value() )
        {
            auto res = nlohmann::json::parse(
                auxiliary::collective_file_read( filename.get(), comm ) );
            lowerCase( res );
            return res;
        }
        else
        {
            auto res = nlohmann::json::parse( options );
            lowerCase( res );
            return res;
        }
    }
#endif

    static nlohmann::json &
    lowerCase( nlohmann::json & json, std::vector< std::string > & currentPath )
    {
        if( json.is_object() )
        {
            auto & val = json.get_ref< nlohmann::json::object_t & >();
            // somekey -> SomeKey
            std::map< std::string, std::string > originalKeys;
            for( auto & pair : val )
            {
                std::string lower =
                    auxiliary::lowerCase( std::string( pair.first ) );
                auto findEntry = originalKeys.find( lower );
                if( findEntry != originalKeys.end() )
                {
                    // double entry found
                    std::vector< std::string > copyCurrentPath{ currentPath };
                    copyCurrentPath.push_back( lower );
                    throw error::BackendConfigSchema(
                        std::move( copyCurrentPath ),
                        "JSON config: duplicate keys." );
                }
                originalKeys.emplace_hint(
                    findEntry, std::move( lower ), pair.first );
            }

            nlohmann::json::object_t newObject;
            for( auto & pair : originalKeys )
            {
                newObject[ pair.first ] = std::move( val[ pair.second ] );
            }
            val = newObject;

            // now recursively
            for( auto & pair : val )
            {
                currentPath.push_back( pair.first );
                lowerCase( pair.second, currentPath );
                currentPath.pop_back();
            }
        }
        else if( json.is_array() )
        {
            for( auto & val : json )
            {
                lowerCase( val );
            }
        }
        return json;
    }

    nlohmann::json & lowerCase( nlohmann::json & json )
    {
        std::vector< std::string > currentPath;
        // that's as deep as our config currently goes, +1 for good measure
        currentPath.reserve( 6 );
        return lowerCase( json, currentPath );
    }

    auxiliary::Option< std::string >
    asStringDynamic( nlohmann::json const & value )
    {
        if( value.is_string() )
        {
            return value.get< std::string >();
        }
        else if( value.is_number_integer() )
        {
            return std::to_string( value.get< long long >() );
        }
        else if( value.is_number_float() )
        {
            return std::to_string( value.get< long double >() );
        }
        else if( value.is_boolean() )
        {
            return std::string( value.get< bool >() ? "1" : "0" );
        }
        return auxiliary::Option< std::string >{};
    }

    auxiliary::Option< std::string >
    asLowerCaseStringDynamic( nlohmann::json const & value )
    {
        auto maybeString = asStringDynamic( value );
        if( maybeString.has_value() )
        {
            auxiliary::lowerCase( maybeString.get() );
        }
        return maybeString;
    }

    std::vector< std::string > backendKeys{
        "adios1", "adios2", "json", "hdf5" };

    void warnGlobalUnusedOptions( TracingJSON const & config )
    {
        auto shadow = config.invertShadow();
        // The backends are supposed to deal with this
        // Only global options here
        for( auto const & backendKey : json::backendKeys )
        {
            shadow.erase( backendKey );
        }
        if( shadow.size() > 0 )
        {
            std::cerr
                << "[Series] The following parts of the global JSON config "
                   "remains unused:\n"
                << shadow.dump() << std::endl;
        }
    }
} // namespace json
} // namespace openPMD
