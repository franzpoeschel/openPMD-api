/* Copyright 2017-2019 Fabian Koller and Franz Poeschel.
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
#include "openPMD/IO/ADIOS/ADIOS2IOHandler.hpp"

#include <iostream>
#include <set>
#include <list>
#include <string>
#include <algorithm>

#include "openPMD/Datatype.hpp"
#include "openPMD/IO/ADIOS/ADIOS2FilePosition.hpp"
#include "openPMD/IO/ADIOS/ADIOS2IOHandler.hpp"
#include "openPMD/auxiliary/Environment.hpp"
#include "openPMD/auxiliary/Filesystem.hpp"
#include "openPMD/auxiliary/StringManip.hpp"
#include <type_traits>

namespace openPMD
{
#if openPMD_USE_VERIFY
#define VERIFY( CONDITION, TEXT )                                              \
    {                                                                          \
        if ( !( CONDITION ) )                                                  \
            throw std::runtime_error( ( TEXT ) );                              \
    }
#else
#define VERIFY( CONDITION, TEXT )                                              \
    do                                                                         \
    {                                                                          \
        (void)sizeof( CONDITION );                                             \
    } while ( 0 );
#endif

#define VERIFY_ALWAYS( CONDITION, TEXT )                                       \
    {                                                                          \
        if ( !( CONDITION ) )                                                  \
            throw std::runtime_error( ( TEXT ) );                              \
    }

#if openPMD_HAVE_ADIOS2

#    if openPMD_HAVE_MPI

ADIOS2IOHandlerImpl::ADIOS2IOHandlerImpl(
    AbstractIOHandler * handler,
    MPI_Comm communicator,
    nlohmann::json config )
    : AbstractIOHandlerImplCommon( handler )
    , m_comm{ communicator }
    , m_ADIOS{ communicator, ADIOS2_DEBUG_MODE }
{
    init( std::move( config ) );
}

#    endif // openPMD_HAVE_MPI

ADIOS2IOHandlerImpl::ADIOS2IOHandlerImpl(
    AbstractIOHandler * handler,
    nlohmann::json config )
    : AbstractIOHandlerImplCommon( handler ), m_ADIOS{ ADIOS2_DEBUG_MODE }
{
    init( std::move( config ) );
}


void
ADIOS2IOHandlerImpl::init( nlohmann::json config )
{
    if( config.contains( "adios2" ) )
    {
        m_config = std::move( config[ "adios2" ] );
    }
}

ADIOS2IOHandlerImpl::~ADIOS2IOHandlerImpl( )
{
    this->flush( );
}

std::future< void > ADIOS2IOHandlerImpl::flush( )
{
    auto res = AbstractIOHandlerImpl::flush( );
    for ( auto & p : m_fileData )
    {
        if ( m_dirty.find( p.first ) != m_dirty.end( ) )
        {
            p.second->flush( );
        }
        else
        {
            p.second->drop( );
        }
    }
    return res;
}

void ADIOS2IOHandlerImpl::createFile(
    Writable * writable,
    Parameter< Operation::CREATE_FILE > const & parameters )
{
    VERIFY_ALWAYS( m_handler->accessTypeBackend != AccessType::READ_ONLY,
                   "Creating a file in read-only mode is not possible." );

    if ( !writable->written )
    {
        std::string name = parameters.name;
        if ( !auxiliary::ends_with( name, ".bp" ) )
        {
            name += ".bp";
        }

        auto res_pair = getPossiblyExisting( name );
        InvalidatableFile shared_name = InvalidatableFile( name );
        VERIFY_ALWAYS(
            !( m_handler->accessTypeBackend == AccessType::READ_WRITE &&
               ( !std::get< PE_NewlyCreated >( res_pair ) ||
                 auxiliary::file_exists( fullPath(
                     std::get< PE_InvalidatableFile >( res_pair ) ) ) ) ),
            "Can only overwrite existing file in CREATE mode." );

        if ( !std::get< PE_NewlyCreated >( res_pair ) )
        {
            auto file = std::get< PE_InvalidatableFile >( res_pair );
            m_dirty.erase( file );
            dropFileData( file );
            file.invalidate( );
        }

        std::string const dir( m_handler->directory );
        if ( !auxiliary::directory_exists( dir ) )
        {
            auto success = auxiliary::create_directories( dir );
            VERIFY( success, "Could not create directory." );
        }

        associateWithFile( writable, shared_name );
        this->m_dirty.emplace( shared_name );
        getFileData( shared_name ).m_mode = adios2::Mode::Write; // WORKAROUND
        // ADIOS2 does not yet implement ReadWrite Mode

        writable->written = true;
        writable->abstractFilePosition =
            std::make_shared< ADIOS2FilePosition >( );
    }
}

void ADIOS2IOHandlerImpl::createPath(
    Writable * writable,
    const Parameter< Operation::CREATE_PATH > & parameters )
{
    std::string path;
    refreshFileFromParent( writable );

    /* Sanitize path */
    if ( !auxiliary::starts_with( parameters.path, '/' ) )
    {
        path = filePositionToString( setAndGetFilePosition( writable ) ) + "/" +
            auxiliary::removeSlashes( parameters.path );
    }
    else
    {
        path = "/" + auxiliary::removeSlashes( parameters.path );
    }

    /* ADIOS has no concept for explicitly creating paths.
     * They are implicitly created with the paths of variables/attributes. */

    writable->written = true;
    writable->abstractFilePosition = std::make_shared< ADIOS2FilePosition >(
        path, ADIOS2FilePosition::GD::GROUP );
}

void ADIOS2IOHandlerImpl::createDataset(
    Writable * writable,
    const Parameter< Operation::CREATE_DATASET > & parameters )
{
    if ( m_handler->accessTypeBackend == AccessType::READ_ONLY )
    {
        throw std::runtime_error( "Creating a dataset in a file opened as read "
                                  "only is not possible." );
    }
    if ( !writable->written )
    {
        /* Sanitize name */
        std::string name = auxiliary::removeSlashes( parameters.name );

        auto file = refreshFileFromParent( writable );
        auto filePos = setAndGetFilePosition( writable, name );
        filePos->gd = ADIOS2FilePosition::GD::DATASET;
        auto varName = filePositionToString( filePos );
        /*
         * @brief std::optional would be more idiomatic, but it's not in
         *        the C++11 standard
         * @todo replace with std::optional upon switching to C++17
         */
        std::unique_ptr< adios2::Operator > compression;
        if ( !parameters.compression.empty( ) )
        {
            compression = getCompressionOperator( parameters.compression );
        }
        switchType( parameters.dtype, detail::VariableDefiner( ),
                    getFileData( file ).m_IO, varName,
                    std::move( compression ), parameters.extent );
        writable->written = true;
        m_dirty.emplace( file );
    }
}

void ADIOS2IOHandlerImpl::extendDataset(
    Writable *, const Parameter< Operation::EXTEND_DATASET > & )
{
    throw std::runtime_error(
        "Dataset extension not implemented in ADIOS backend" );
}

void ADIOS2IOHandlerImpl::openFile(
    Writable * writable, const Parameter< Operation::OPEN_FILE > & parameters )
{
    if ( !auxiliary::directory_exists( m_handler->directory ) )
    {
        throw no_such_file_error( "Supplied directory is not valid: " +
                                  m_handler->directory );
    }

    std::string name = parameters.name;
    if ( !auxiliary::ends_with( name, ".bp" ) )
    {
        name += ".bp";
    }

    auto file = std::get< PE_InvalidatableFile >( getPossiblyExisting( name ) );

    associateWithFile( writable, file );

    writable->written = true;
    writable->abstractFilePosition = std::make_shared< ADIOS2FilePosition >( );
}

void ADIOS2IOHandlerImpl::openPath(
    Writable * writable, const Parameter< Operation::OPEN_PATH > & parameters )
{
    /* Sanitize path */
    refreshFileFromParent( writable );
    std::string prefix =
        filePositionToString( setAndGetFilePosition( writable->parent ) );
    std::string suffix = auxiliary::removeSlashes( parameters.path );
    std::string infix = auxiliary::ends_with( prefix, '/' ) ? "" : "/";

    /* ADIOS has no concept for explicitly creating paths.
     * They are implicitly created with the paths of variables/attributes. */

    writable->abstractFilePosition = std::make_shared< ADIOS2FilePosition >(
        prefix + infix + suffix, ADIOS2FilePosition::GD::GROUP );
    writable->written = true;
}

void ADIOS2IOHandlerImpl::openDataset(
    Writable * writable, Parameter< Operation::OPEN_DATASET > & parameters )
{
    auto name = auxiliary::removeSlashes( parameters.name );
    writable->abstractFilePosition.reset( );
    auto pos = setAndGetFilePosition( writable, name );
    pos->gd = ADIOS2FilePosition::GD::DATASET;
    auto file = refreshFileFromParent( writable );
    auto varName = filePositionToString( pos );
    *parameters.dtype = detail::fromADIOS2Type(
        getFileData( file ).m_IO.VariableType( varName ) );
    switchType( *parameters.dtype, detail::DatasetOpener( this ), file, varName,
                parameters );
    writable->written = true;
}

void ADIOS2IOHandlerImpl::deleteFile(
    Writable *, const Parameter< Operation::DELETE_FILE > & )
{
    throw std::runtime_error( "ADIOS2 backend does not support deletion." );
}

void ADIOS2IOHandlerImpl::deletePath(
    Writable *, const Parameter< Operation::DELETE_PATH > & )
{
    throw std::runtime_error( "ADIOS2 backend does not support deletion." );
}

void ADIOS2IOHandlerImpl::deleteDataset(
    Writable *, const Parameter< Operation::DELETE_DATASET > & )
{
    throw std::runtime_error( "ADIOS2 backend does not support deletion." );
}

void ADIOS2IOHandlerImpl::deleteAttribute(
    Writable *, const Parameter< Operation::DELETE_ATT > & )
{
    throw std::runtime_error( "ADIOS2 backend does not support deletion." );
}

void ADIOS2IOHandlerImpl::writeDataset(
    Writable * writable,
    const Parameter< Operation::WRITE_DATASET > & parameters )
{
    VERIFY_ALWAYS( m_handler->accessTypeBackend != AccessType::READ_ONLY,
                   "Cannot write data in read-only mode." );
    setAndGetFilePosition( writable );
    auto file = refreshFileFromParent( writable );
    detail::BufferedActions & ba = getFileData( file );
    detail::BufferedPut bp;
    bp.name = nameOfVariable( writable );
    bp.param = parameters;
    ba.enqueue( std::move( bp ) );
    m_dirty.emplace( std::move( file ) );
    writable->written = true; // TODO erst nach dem Schreiben?
}

void ADIOS2IOHandlerImpl::writeAttribute(
    Writable * writable, const Parameter< Operation::WRITE_ATT > & parameters )
{
    switchType( parameters.dtype, detail::AttributeWriter( ), this, writable,
                parameters );
}

void ADIOS2IOHandlerImpl::readDataset(
    Writable * writable, Parameter< Operation::READ_DATASET > & parameters )
{
    setAndGetFilePosition( writable );
    auto file = refreshFileFromParent( writable );
    detail::BufferedActions & ba = getFileData( file );
    detail::BufferedGet bg;
    bg.name = nameOfVariable( writable );
    bg.param = parameters;
    ba.enqueue( std::move( bg ) );
    m_dirty.emplace( std::move( file ) );
}

void ADIOS2IOHandlerImpl::readAttribute(
    Writable * writable, Parameter< Operation::READ_ATT > & parameters )
{
    auto file = refreshFileFromParent( writable );
    auto pos = setAndGetFilePosition( writable );
    detail::BufferedActions & ba = getFileData( file );
    detail::BufferedAttributeRead bar;
    bar.name = nameOfAttribute( writable, parameters.name );
    bar.param = parameters;
    ba.enqueue( std::move( bar ) );
    m_dirty.emplace( std::move( file ) );
}

void ADIOS2IOHandlerImpl::listPaths(
    Writable * writable, Parameter< Operation::LIST_PATHS > & parameters )
{
    VERIFY_ALWAYS(
        writable->written,
        "Internal error: Writable not marked written during path listing" );
    auto file = refreshFileFromParent( writable );
    auto pos = setAndGetFilePosition( writable );
    // adios2::Engine & engine = getEngine( file );
    std::string myName = filePositionToString( pos );
    if ( !auxiliary::ends_with( myName, '/' ) )
    {
        myName = myName + '/';
    }

    /*
     * since ADIOS does not have a concept of paths, restore them
     * from variables and attributes.
     * yes.
     */

    auto & IO = getFileData( file ).m_IO;

    std::unordered_set< std::string > subdirs;
    /*
     * When reading an attribute, we cannot distinguish
     * whether its containing "folder" is a group or a
     * dataset. If we stumble upon a dataset at the current
     * level (which can be distinguished via variables),
     * we put in in the list 'delete_me' to remove them
     * again later on.
     * Note that the method 'listDatasets' does not have
     * this problem since datasets can be restored solely
     * from variables – attributes don't even need to be
     * inspected.
     */
    std::vector< std::string > delete_me;
    auto f = [myName, &subdirs, &delete_me](
                 std::vector< std::string > & varsOrAttrs, bool variables ) {
        for ( auto var : varsOrAttrs )
        {
            if ( auxiliary::starts_with( var, myName ) )
            {
                var = auxiliary::replace_first( var, myName, "" );
                auto firstSlash = var.find_first_of( '/' );
                if ( firstSlash != std::string::npos )
                {
                    var = var.substr( 0, firstSlash );
                    subdirs.emplace( std::move( var ) );
                }
                else if ( variables )
                { // var is a dataset at the current level
                    delete_me.push_back( std::move( var ) );
                }
            }
        }
    };
    std::vector< std::string > vars;
    for ( auto const & p : IO.AvailableVariables( ) )
    {
        vars.emplace_back( p.first );
    }

    std::vector< std::string > attrs;
    for ( auto const & p : IO.AvailableAttributes( ) )
    {
        attrs.emplace_back( p.first );
    }
    f( vars, true );
    f( attrs, false );
    for ( auto & d : delete_me )
    {
        subdirs.erase( d );
    }
    for ( auto & path : subdirs )
    {
        parameters.paths->emplace_back( std::move( path ) );
    }
}

void ADIOS2IOHandlerImpl::listDatasets(
    Writable * writable, Parameter< Operation::LIST_DATASETS > & parameters )
{
    VERIFY_ALWAYS(
        writable->written,
        "Internal error: Writable not marked written during path listing" );
    auto file = refreshFileFromParent( writable );
    auto pos = setAndGetFilePosition( writable );
    // adios2::Engine & engine = getEngine( file );
    std::string myName = filePositionToString( pos );
    if ( !auxiliary::ends_with( myName, '/' ) )
    {
        myName = myName + '/';
    }

    /*
     * since ADIOS does not have a concept of paths, restore them
     * from variables and attributes.
     * yes.
     */

    std::map< std::string, adios2::Params > vars =
        getFileData( file ).m_IO.AvailableVariables( );

    std::unordered_set< std::string > subdirs;
    for ( auto & pair : vars )
    {
        std::string var = pair.first;
        if ( auxiliary::starts_with( var, myName ) )
        {
            var = auxiliary::replace_first( var, myName, "" );
            auto firstSlash = var.find_first_of( '/' );
            if ( firstSlash == std::string::npos )
            {
                subdirs.emplace( std::move( var ) );
            } // else: var is a path or a dataset in a group below the current
            // group
        }
    }
    for ( auto & dataset : subdirs )
    {
        parameters.datasets->emplace_back( std::move( dataset ) );
    }
}

void ADIOS2IOHandlerImpl::listAttributes(
    Writable * writable, Parameter< Operation::LIST_ATTS > & parameters )
{
    VERIFY_ALWAYS( writable->written,
                   "Internal error: Writable not marked "
                   "written during attribute writing" );
    auto file = refreshFileFromParent( writable );
    auto pos = setAndGetFilePosition( writable );
    auto attributePrefix = filePositionToString( pos );
    if ( attributePrefix == "/" )
    {
        attributePrefix = "";
    }
    auto & ba = getFileData( file );
    ba.getEngine( ); // make sure that the attributes are present
    auto attrs = ba.m_IO.AvailableAttributes( attributePrefix );
    for ( auto & pair : attrs )
    {
        auto attr = auxiliary::removeSlashes( pair.first );
        if ( attr.find_last_of( '/' ) == std::string::npos )
        {
            parameters.attributes->push_back( std::move( attr ) );
        }
    }
}

void
ADIOS2IOHandlerImpl::advance(
    Writable * writable,
    Parameter< Operation::ADVANCE > & parameters )
{
    auto file = refreshFileFromParent( writable );
    auto & ba = getFileData( file );
    *parameters.task = ba.advance( parameters.mode );
}

void
ADIOS2IOHandlerImpl::availableChunks(
    Writable * writable,
    Parameter< Operation::AVAILABLE_CHUNKS > & parameters )
{
    setAndGetFilePosition( writable );
    auto file = refreshFileFromParent( writable );
    detail::BufferedActions & ba = getFileData( file );
    detail::BufferedTableRead btr;
    btr.name = nameOfVariable( writable );
    btr.param = parameters;
    ba.enqueue( std::move( btr ) );
}

adios2::Mode ADIOS2IOHandlerImpl::adios2Accesstype( )
{
    switch ( m_handler->accessTypeBackend )
    {
    case AccessType::CREATE:
        return adios2::Mode::Write;
    case AccessType::READ_ONLY:
        return adios2::Mode::Read;
    case AccessType::READ_WRITE:
        std::cerr << "ADIOS2 does currently not yet implement ReadWrite "
                     "(Append) mode."
                  << "Replacing with Read mode." << std::endl;
        return adios2::Mode::Read;
    default:
        return adios2::Mode::Undefined;
    }
}

nlohmann::json ADIOS2IOHandlerImpl::nullvalue = nlohmann::json();

std::string ADIOS2IOHandlerImpl::filePositionToString(
    std::shared_ptr< ADIOS2FilePosition > filepos )
{
    return filepos->location;
}

std::shared_ptr< ADIOS2FilePosition > ADIOS2IOHandlerImpl::extendFilePosition(
    std::shared_ptr< ADIOS2FilePosition > const & oldPos, std::string s )
{
    auto path = filePositionToString( oldPos );
    if ( !auxiliary::ends_with( path, '/' ) &&
         !auxiliary::starts_with( s, '/' ) )
    {
        path = path + "/";
    }
    else if ( auxiliary::ends_with( path, '/' ) &&
              auxiliary::starts_with( s, '/' ) )
    {
        path = auxiliary::replace_last( path, "/", "" );
    }
    return std::make_shared< ADIOS2FilePosition >( path + std::move( s ),
                                                   oldPos->gd );
}

std::unique_ptr< adios2::Operator >
ADIOS2IOHandlerImpl::getCompressionOperator( std::string const & compression )
{
    adios2::Operator res;
    auto it = m_operators.find( compression );
    if ( it == m_operators.end( ) )
    {
        try {
            res = m_ADIOS.DefineOperator( compression, compression );
        }
        catch ( std::invalid_argument const & )
        {
            std::cerr << "Warning: ADIOS2 backend does not support compression "
                "method " << compression << ". Continuing without compression."
                << std::endl;
            return std::unique_ptr< adios2::Operator >( );
        }
        m_operators.emplace( compression, res );

    }
    else
    {
        res = it->second;
    }
    return std::unique_ptr< adios2::Operator >(
        new adios2::Operator( res ) );
}

std::string ADIOS2IOHandlerImpl::nameOfVariable( Writable * writable )
{
    return filePositionToString( setAndGetFilePosition( writable ) );
}

std::string ADIOS2IOHandlerImpl::nameOfAttribute( Writable * writable,
                                                  std::string attribute )
{
    auto pos = setAndGetFilePosition( writable );
    return filePositionToString(
        extendFilePosition( pos, auxiliary::removeSlashes( attribute ) ) );
}

ADIOS2FilePosition::GD
ADIOS2IOHandlerImpl::groupOrDataset( Writable * writable )
{
    return setAndGetFilePosition( writable )->gd;
}

detail::BufferedActions &
ADIOS2IOHandlerImpl::getFileData( InvalidatableFile file )
{
    VERIFY_ALWAYS( file.valid( ),
                   "Cannot retrieve file data for a file that has "
                   "been overwritten or deleted." )
    auto it = m_fileData.find( file );
    if ( it == m_fileData.end( ) )
    {
        return *m_fileData
                    .emplace( std::move( file ),
                              std::unique_ptr< detail::BufferedActions >{
                                  new detail::BufferedActions{*this, file}} )
                    .first->second;
    }
    else
    {
        return *it->second;
    }
}

void ADIOS2IOHandlerImpl::dropFileData( InvalidatableFile file )
{
    auto it = m_fileData.find( file );
    if ( it != m_fileData.end( ) )
    {
        it->second->drop( );
        m_fileData.erase( it );
    }
}

template < typename T >
adios2::Variable< T >
ADIOS2IOHandlerImpl::verifyDataset( Offset const & offset,
                                    Extent const & extent, adios2::IO & IO,
                                    std::string const & varName )
{
    {
        auto requiredType = adios2::GetType< T >( );
        auto actualType = IO.VariableType( varName );
        VERIFY_ALWAYS( requiredType == actualType,
                       "Trying to access a dataset with wrong type (trying to "
                       "access dataset with type " +
                           requiredType + ", but has type " + actualType + ")" )
    }
    adios2::Variable< T > var = IO.InquireVariable< T >( varName );
    VERIFY_ALWAYS( var.operator bool( ),
                   "Internal error: Failed opening ADIOS2 variable." )
    // TODO leave this check to ADIOS?
    adios2::Dims shape = var.Shape( );
    auto actualDim = shape.size( );
    {
        auto requiredDim = extent.size( );
        VERIFY_ALWAYS( requiredDim == actualDim,
                       "Trying to access a dataset with wrong dimensionality "
                       "(trying to access dataset with dimensionality " +
                           std::to_string( requiredDim ) +
                           ", but has dimensionality " +
                           std::to_string( actualDim ) + ")" )
    }
    for ( unsigned int i = 0; i < actualDim; i++ )
    {
        VERIFY_ALWAYS( offset[i] + extent[i] <= shape[i],
                       "Dataset access out of bounds." )
    }
    var.SetSelection( {offset, extent} );
    return var;
}

namespace detail
{
    DatasetReader::DatasetReader( openPMD::ADIOS2IOHandlerImpl * impl )
    : m_impl{impl}
    {
    }

    template < typename T >
    void DatasetReader::operator( )( detail::BufferedGet & bp, adios2::IO & IO,
                                     adios2::Engine & engine,
                                     std::string const & fileName )
    {
        DatasetHelper< T >{m_impl}.readDataset( bp, IO, engine, fileName );
    }

    template < int n, typename... Params >
    void DatasetReader::operator( )( Params &&... )
    {
        throw std::runtime_error(
            "Internal error: Unknown datatype trying to read a dataset." );
    }

    template < typename T >
    Datatype AttributeReader::
    operator( )( adios2::IO & IO, std::string name,
                 std::shared_ptr< Attribute::resource > resource )
    {
        /*
         * If we store an attribute of boolean type, we store an additional
         * attribute prefixed with '__is_boolean__' to indicate this information
         * that would otherwise be lost. Check whether this has been done.
         */
        using rep = AttributeTypes<bool>::rep;
#if __cplusplus > 201402L
        constexpr
#endif
        if( std::is_same< T, rep >::value )
        {
            std::string metaAttr = "__is_boolean__" + name;
            auto type = attributeInfo( IO, "__is_boolean__" + name );
            if ( type == determineDatatype<rep>() )
            {
                auto attr = IO.InquireAttribute< rep >( metaAttr );
                if (attr.Data().size() == 1 && attr.Data()[0] == 1)
                {
                    AttributeTypes< bool >::readAttribute( IO, name, resource );
                    return determineDatatype< bool >();
                }
            }
        }
        AttributeTypes< T >::readAttribute( IO, name, resource );
        return determineDatatype< T >();
    }

    template < int n, typename... Params >
    Datatype AttributeReader::operator( )( Params &&... )
    {
        throw std::runtime_error( "Internal error: Unknown datatype while "
                                  "trying to read an attribute." );
    }

    template < typename T >
    void AttributeWriter::
    operator( )( ADIOS2IOHandlerImpl * impl, Writable * writable,
                 const Parameter< Operation::WRITE_ATT > & parameters )
    {

        VERIFY_ALWAYS( impl->m_handler->accessTypeBackend !=
                           AccessType::READ_ONLY,
                       "Cannot write attribute in read-only mode." );
        auto pos = impl->setAndGetFilePosition( writable );
        auto file = impl->refreshFileFromParent( writable );
        auto fullName = impl->nameOfAttribute( writable, parameters.name );
        auto prefix = impl->filePositionToString( pos );

        adios2::IO IO = impl->getFileData( file ).m_IO;
        impl->m_dirty.emplace( std::move( file ) );

        std::string t = IO.AttributeType( fullName );
        if ( !t.empty( ) ) // an attribute is present <=> it has a type
        {
            IO.RemoveAttribute( fullName );
        }
        typename AttributeTypes< T >::Attr attr =
            AttributeTypes< T >::createAttribute(
                IO, fullName, variantSrc::get< T >( parameters.resource ) );
        VERIFY_ALWAYS( attr, "Failed creating attribute." )
    }

    template < int n, typename... Params >
    void AttributeWriter::operator( )( Params &&... )
    {
        throw std::runtime_error( "Internal error: Unknown datatype while "
                                  "trying to write an attribute." );
    }

    DatasetOpener::DatasetOpener( ADIOS2IOHandlerImpl * impl ) : m_impl{impl}
    {
    }

    template < typename T >
    void DatasetOpener::
    operator( )( InvalidatableFile file, const std::string & varName,
                 Parameter< Operation::OPEN_DATASET > & parameters )
    {
        DatasetHelper< T >{m_impl}.openDataset( file, varName, parameters );
    }

    template < int n, typename... Params >
    void DatasetOpener::operator( )( Params &&... )
    {
        throw std::runtime_error(
            "Unknown datatype while trying to open dataset." );
    }

    WriteDataset::WriteDataset( ADIOS2IOHandlerImpl * handlerImpl )
    : m_handlerImpl{handlerImpl}
    {
    }

    template < typename T >
    void WriteDataset::operator( )( detail::BufferedPut & bp, adios2::IO & IO,
                                    adios2::Engine & engine )
    {
        DatasetHelper< T > dh{m_handlerImpl};
        dh.writeDataset( bp, IO, engine );
    }

    template < int n, typename... Params >
    void WriteDataset::operator( )( Params &&... )
    {
        throw std::runtime_error( "WRITE_DATASET: Invalid datatype." );
    }

    template < typename T >
    void VariableDefiner::
    operator( )( adios2::IO & IO, const std::string & name,
                 std::unique_ptr< adios2::Operator > compression,
                 const adios2::Dims & shape, const adios2::Dims & start,
                 const adios2::Dims & count, const bool constantDims )
    {
        DatasetHelper< T >::defineVariable( IO, name, std::move( compression ),
                                            shape, start, count, constantDims );
    }

    template < int n, typename... Params >
    void VariableDefiner::operator( )( adios2::IO &, Params &&... )
    {
        throw std::runtime_error( "Defining a variable with undefined type." );
    }



    template < typename T >
    typename AttributeTypes< T >::Attr
    AttributeTypes< T >::createAttribute( adios2::IO & IO, std::string name,
                                          const T value )
    {
        auto attr = IO.DefineAttribute( name, value );
        if ( !attr )
        {
            throw std::runtime_error(
                "Internal error: Failed defining attribute '" + name + "'." );
        }
        return attr;
    }

    template < typename T >
    void AttributeTypes< T >::readAttribute(
        adios2::IO & IO, std::string name,
        std::shared_ptr< Attribute::resource > resource )
    {
        auto attr = IO.InquireAttribute< BasicType >( name );
        if ( !attr )
        {
            throw std::runtime_error(
                "Internal error: Failed reading attribute '" + name + "'." );
        }
        *resource = attr.Data( )[0];
    }

    template < typename T >
    typename AttributeTypes< std::vector< T > >::Attr
    AttributeTypes< std::vector< T > >::createAttribute(
        adios2::IO & IO, std::string name, const std::vector< T > & value )
    {
        auto attr = IO.DefineAttribute( name, value.data( ), value.size( ) );
        if ( !attr )
        {
            throw std::runtime_error(
                "Internal error: Failed defining attribute '" + name + "'." );
        }
        return attr;
    }

    template < typename T >
    void AttributeTypes< std::vector< T > >::readAttribute(
        adios2::IO & IO, std::string name,
        std::shared_ptr< Attribute::resource > resource )
    {
        auto attr = IO.InquireAttribute< BasicType >( name );
        if ( !attr )
        {
            throw std::runtime_error(
                "Internal error: Failed reading attribute '" + name + "'." );
        }
        *resource = attr.Data( );
    }

    template < typename T, size_t n >
    typename AttributeTypes< std::array< T, n > >::Attr
    AttributeTypes< std::array< T, n > >::createAttribute(
        adios2::IO & IO, std::string name, const std::array< T, n > & value )
    {
        auto attr = IO.DefineAttribute( name, value.data( ), n );
        if ( !attr )
        {
            throw std::runtime_error(
                "Internal error: Failed defining attribute '" + name + "'." );
        }
        return attr;
    }

    template < typename T, size_t n >
    void AttributeTypes< std::array< T, n > >::readAttribute(
        adios2::IO & IO, std::string name,
        std::shared_ptr< Attribute::resource > resource )
    {
        auto attr = IO.InquireAttribute< BasicType >( name );
        if ( !attr )
        {
            throw std::runtime_error(
                "Internal error: Failed reading attribute '" + name + "'." );
        }
        auto data = attr.Data( );
        std::array< T, n > res;
        for ( size_t i = 0; i < n; i++ )
        {
            res[i] = data[i];
        }
        *resource = res;
    }

    typename AttributeTypes< bool >::Attr
    AttributeTypes< bool >::createAttribute( adios2::IO & IO, std::string name,
                                             const bool value )
    {
        IO.DefineAttribute< bool_representation >( "__is_boolean__" + name, 1 );
        return AttributeTypes< bool_representation >::createAttribute(
            IO, name, toRep( value ) );
    }

    void AttributeTypes< bool >::readAttribute(
        adios2::IO & IO, std::string name,
        std::shared_ptr< Attribute::resource > resource )
    {
        auto attr = IO.InquireAttribute< BasicType >( name );
        if ( !attr )
        {
            throw std::runtime_error(
                "Internal error: Failed reading attribute '" + name + "'." );
        }
        *resource = fromRep( attr.Data( )[0] );
    }

    template < typename T >
    DatasetHelper<
        T, typename std::enable_if< DatasetTypes< T >::validType >::type >::
        DatasetHelper( openPMD::ADIOS2IOHandlerImpl * impl )
    : m_impl{impl}
    {
    }

    template < typename T >
    void DatasetHelper<
        T, typename std::enable_if< DatasetTypes< T >::validType >::type >::
        openDataset( InvalidatableFile file, const std::string & varName,
                     Parameter< Operation::OPEN_DATASET > & parameters )
    {
        auto & IO = m_impl->getFileData( file ).m_IO;
        adios2::Variable< T > var = IO.InquireVariable< T >( varName );
        if ( !var )
        {
            throw std::runtime_error(
                "Failed retrieving ADIOS2 Variable with name '" + varName +
                "' from file " + *file + "." );
        }
        *parameters.extent = var.Shape( );
    }

    template < typename T >
    void DatasetHelper<
        T, typename std::enable_if< DatasetTypes< T >::validType >::type >::
        readDataset( detail::BufferedGet & bp, adios2::IO & IO,
                     adios2::Engine & engine, std::string const & fileName )
    {
        adios2::Variable< T > var = m_impl->verifyDataset< T >(
            bp.param.offset, bp.param.extent, IO, bp.name );
        if ( !var )
        {
            throw std::runtime_error(
                "Failed retrieving ADIOS2 Variable with name '" + bp.name +
                "' from file " + fileName + "." );
        }
        auto ptr = std::static_pointer_cast< T >( bp.param.data ).get( );
        engine.Get( var, ptr );
    }

    template < typename T >
    void DatasetHelper<
        T, typename std::enable_if< DatasetTypes< T >::validType >::type >::
        defineVariable( adios2::IO & IO, const std::string & name,
                        std::unique_ptr< adios2::Operator > compression,
                        const adios2::Dims & shape, const adios2::Dims & start,
                        const adios2::Dims & count, const bool constantDims )
    {
        adios2::Variable< T > var =
            IO.DefineVariable< T >( name, shape, start, count, constantDims );
        if ( !var )
        {
            throw std::runtime_error(
                "Internal error: Could not create Variable '" + name + "'." );
        }
        // check whether the unique_ptr has an element
        // and whether the held operator is valid
        if ( compression && *compression )
        {
            var.AddOperation( *compression );
        }
    }

    template < typename T >
    void DatasetHelper<
        T, typename std::enable_if< DatasetTypes< T >::validType >::type >::
        writeDataset( detail::BufferedPut & bp, adios2::IO & IO,
                      adios2::Engine & engine )
    {
        VERIFY_ALWAYS( m_impl->m_handler->accessTypeBackend !=
                           AccessType::READ_ONLY,
                       "Cannot write data in read-only mode." );

        auto ptr = std::static_pointer_cast< const T >( bp.param.data ).get( );

        adios2::Variable< T > var = m_impl->verifyDataset< T >(
            bp.param.offset, bp.param.extent, IO, bp.name );

        engine.Put( var, ptr );
    }

    template < typename T >
    DatasetHelper<
        T, typename std::enable_if< !DatasetTypes< T >::validType >::type >::
        DatasetHelper( openPMD::ADIOS2IOHandlerImpl * )
    {
    }

    template < typename T >
    void DatasetHelper< T,
                        typename std::enable_if<
                            !DatasetTypes< T >::validType >::type >::throwErr( )
    {
        throw std::runtime_error(
            "Trying to access dataset with unallowed datatype: " +
            datatypeToString( determineDatatype< T >( ) ) );
    }

    template < typename T >
    template < typename... Params >
    void DatasetHelper<
        T, typename std::enable_if< !DatasetTypes< T >::validType >::type >::
        openDataset( Params &&... )
    {
        throwErr( );
    }

    template < typename T >
    template < typename... Params >
    void DatasetHelper<
        T, typename std::enable_if< !DatasetTypes< T >::validType >::type >::
        readDataset( Params &&... )
    {
        throwErr( );
    }

    template < typename T >
    template < typename... Params >
    void DatasetHelper<
        T, typename std::enable_if< !DatasetTypes< T >::validType >::type >::
        defineVariable( Params &&... )
    {
        throwErr( );
    }

    template < typename T >
    template < typename... Params >
    void DatasetHelper<
        T, typename std::enable_if< !DatasetTypes< T >::validType >::type >::
        writeDataset( Params &&... )
    {
        throwErr( );
    }

    void BufferedGet::run( BufferedActions & ba )
    {
        switchType( param.dtype, ba.m_readDataset, *this, ba.m_IO,
                    ba.getEngine( ), ba.m_file );
    }

    void BufferedPut::run( BufferedActions & ba )
    {
        switchType( param.dtype, ba.m_writeDataset, *this, ba.m_IO,
                    ba.getEngine( ) );
        //std::cout << "stored to " << name << std::endl;
        decltype(BufferedActions::writtenChunks)::mapped_type * tuple;
        auto it = ba.writtenChunks.find( name );
        if( it == ba.writtenChunks.end() )
        {
            tuple = & ba.writtenChunks[ name ];
            auto & arr = std::get< 1 >( *tuple );
            arr[ 0 ] = 1; // we have just written the first chunk
            arr[ 1 ] = param.extent.size(); // the dimensionality of the dataset
        }
        else
        {
            tuple = & it->second;
            auto & arr = std::get< 1 >( *tuple );
            VERIFY(
                std::get<0>(*tuple).size() == arr[ 0 ] * arr[ 1 ] * 2,
                "Internal error: Dimension table written so far contains "
                "an unexpected number of elements" );
            arr[ 0 ]++; // next chunk has just been written
            VERIFY(
                arr[ 1 ] == param.extent.size(),
                "Internal error: Dataset dimensionality has changed." );
        }
        //std::cout << "offsets: ";
        for( auto offset : param.offset )
        {
            //std::cout << offset << ", ";
            std::get<0>(*tuple).push_back( offset );
        }
        //std::cout << "\nextents: ";
        for( auto extent: param.extent )
        {
            //std::cout << extent << ", ";
            std::get<0>(*tuple).push_back( extent );
        }
        //std::cout << std::endl;
    }

    void BufferedAttributeRead::run( BufferedActions & ba )
    {
        auto type = attributeInfo( ba.m_IO, name );

        if ( type == Datatype::UNDEFINED )
        {
            throw std::runtime_error( "Requested attribute (" + name +
                                      ") not found in backend." );
        }

        Datatype ret = switchType< Datatype >(
            type, detail::AttributeReader{}, ba.m_IO, name, param.resource );
        *param.dtype = ret;
    }

    void detail::BufferedTableRead::run( BufferedActions & ba )
    {
        using var_t = adios2::Variable< BufferedActions::extent_t >;
        std::vector< var_t > tables
            = ba.availabeTablesPerRank( name );
        TableReadPostprocessing trp;
        trp.data = decltype( TableReadPostprocessing::data )( tables.size() );
        trp.param = param;
        trp.shapes = 
                decltype( TableReadPostprocessing::shapes)( tables.size() );
        adios2::Engine & engine = ba.getEngine();
        for( size_t rank = 0; rank < tables.size(); rank++ )
        {
            var_t & table = tables[ rank ];
            auto dims = table.Shape();
            VERIFY_ALWAYS(
                dims.size() == 3,
                "ADIOS2 error: Step table for variable " +
                name + " has an unexpected shape." );
            size_t length = dims[0] * dims[1] * dims[2];
            trp.data[ rank ] = std::vector< Extent::value_type >( length );
            engine.Get( table, trp.data[ rank ].data() );
            trp.shapes[ rank ] = std::move( dims );
        }
        ba.enqueue( std::move( trp ), ba.m_bufferAfterFlush );
    }

    void detail::TableReadPostprocessing::run( BufferedActions & )
    {
        ChunkTable res( shapes.size() );
        for( size_t rank = 0; rank < shapes.size(); rank++ )
        {
            ChunkTable::T_perRank & currentRank = res.chunkTable[ rank ];
            auto const & shape = shapes[ rank ];
            auto number_of_chunks = shape[ 0 ];
            auto dimensionality = shape[ 2 ];
            auto const & rankData = data[ rank ];
            for( size_t chunk = 0; chunk < number_of_chunks; chunk++ )
            {
                Offset offset( dimensionality );
                Extent extent( dimensionality );
                size_t idx = chunk * dimensionality * 2;
                std::copy_n(
                    &rankData[ idx ],
                    dimensionality,
                    offset.begin() );
                std::copy_n(
                    &rankData[ idx + dimensionality ],
                    dimensionality,
                    extent.begin() );
                currentRank.push_back(
                    std::make_pair(
                        std::move( offset ),
                        std::move( extent ) ) );
            }
        }
        *param.chunks = res;
    }

    BufferedActions::BufferedActions( ADIOS2IOHandlerImpl & impl,
                                      InvalidatableFile file )
    : m_file( impl.fullPath( std::move( file ) ) ),
      m_IO( impl.m_ADIOS.DeclareIO( std::to_string( impl.nameCounter++ ) ) ),
      m_mode( impl.adios2Accesstype( ) ), m_writeDataset( &impl ),
      m_readDataset( &impl ), m_attributeReader( ), m_impl( impl )
    {
#if openPMD_HAVE_MPI
        MPI_Comm_rank( impl.m_comm, &mpi_rank );
        MPI_Comm_size( impl.m_comm, &mpi_size );
#else
        mpi_rank = 0;
        mpi_size = 1;
#endif
        if ( !m_IO )
        {
            throw std::runtime_error(
                "Internal error: Failed declaring ADIOS2 IO object for file " +
                m_file );
        }
        else
        {
            configure_IO(impl);
        }
    }

    BufferedActions::~BufferedActions()
    {
        // if write accessing, ensure that the engine is opened
        if( !m_engine && m_mode != adios2::Mode::Read )
        {
            getEngine();
        }
        if( m_engine )
        {
            if (duringStep)
            {
                writeChunkTables();
                m_engine->EndStep();
            }
            m_engine->Close();
        }
    }

    void
    BufferedActions::configure_IO( ADIOS2IOHandlerImpl & impl )
    {
        std::set< std::string > alreadyConfigured;
        auto & engine = impl.config( detail::str_engine );
        if( !engine.is_null() )
        {
            m_IO.SetEngine( impl.config( detail::str_type, engine ) );
            auto & params = impl.config( detail::str_params, engine );
            if( params.is_object() )
            {
                for( auto it = params.begin(); it != params.end(); it++ )
                {
                    m_IO.SetParameter( it.key(), it.value() );
                    alreadyConfigured.emplace( it.key() );
                }
            }
        }

        auto notYetConfigured =
            [&alreadyConfigured]( std::string const & param ) {
                auto it = alreadyConfigured.find( param );
                return it == alreadyConfigured.end();
            };

        // read parameters from environment
        if( notYetConfigured( "CollectiveMetadata" ) )
        {
            if( 1 ==
                auxiliary::getEnvNum( "OPENPMD_ADIOS2_HAVE_METADATA_FILE", 1 ) )
            {
                m_IO.SetParameter( "CollectiveMetadata", "On" );
            }
            else
            {
                m_IO.SetParameter( "CollectiveMetadata", "Off" );
            }
        }
        if( notYetConfigured( "Profile" ) )
        {
            if( 1 ==
                    auxiliary::getEnvNum(
                        "OPENPMD_ADIOS2_HAVE_PROFILING", 1 ) &&
                notYetConfigured( "Profile" ) )
            {
                m_IO.SetParameter( "Profile", "On" );
            }
            else
            {
                m_IO.SetParameter( "Profile", "Off" );
            }
        }
#if openPMD_HAVE_MPI
        {
            auto num_substreams =
                auxiliary::getEnvNum( "OPENPMD_ADIOS2_NUM_SUBSTREAMS", 0 );
            if( notYetConfigured( "SubStreams" ) && 0 != num_substreams )
            {
                m_IO.SetParameter(
                    "SubStreams", std::to_string( num_substreams ) );
            }
        }
#endif
    }

    adios2::Engine &
    BufferedActions::getEngine()
    {
        if ( !m_engine )
        {
            m_engine = std::unique_ptr< adios2::Engine >(
                new adios2::Engine( m_IO.Open( m_file, m_mode ) ) );
            if ( !m_engine )
            {
                throw std::runtime_error( "Failed opening ADIOS2 Engine." );
            }
        }
        return *m_engine;
    }

    template < typename BA > void BufferedActions::enqueue( BA && ba )
    {
        enqueue< BA >( std::forward< BA >( ba ), m_buffer );
    }

    template < typename BA > void BufferedActions::enqueue(
        BA && ba,
        decltype( m_buffer ) & buffer )
    {
        using _BA = typename std::remove_reference< BA >::type;
        buffer.emplace_back( std::unique_ptr< BufferedAction >(
            new _BA( std::forward< BA >( ba ) ) ) );
    }

    void BufferedActions::flush( )
    {
        auto & eng = getEngine( );
        if ( !duringStep )
        {
            eng.BeginStep();
            duringStep = true;
        }
        {
            for ( auto & ba : m_buffer )
            {
                ba->run( *this );
            }
            // Flush() does not necessarily perform
            // deferred actions....
            switch ( m_mode )
            {
            case adios2::Mode::Write:
                eng.PerformPuts( );
                break;
            case adios2::Mode::Read:
                eng.PerformGets( );
                break;
            case adios2::Mode::Append:
                // TODO order?
                eng.PerformGets( );
                eng.PerformPuts( );
                break;
            default:
                break;
            }
        }
        m_buffer.clear( );
        for ( auto & ba : m_bufferAfterFlush )
        {
            ba->run( *this );
        }
        m_bufferAfterFlush.clear();
    }

    std::packaged_task< AdvanceStatus() >
    BufferedActions::advance( AdvanceMode mode )
    {
        switch (mode) {
        case AdvanceMode::WRITE:
        {
            /*
             * This also forces a Engine::BeginStep() if no step
             * is currently active, ensuring that the next step
             * will start.
             */
            flush();
            writeChunkTables();
            getEngine().EndStep();
            writtenChunks.clear();
            currentStep++;
            duringStep = false;
            return std::packaged_task< AdvanceStatus() >(
                []() {
                    return AdvanceStatus::OK;
                } );
        }
        case AdvanceMode::READ:
        {
            if ( duringStep )
            {
                flush();
                getEngine().EndStep();
            }
            currentStep++;
            AdvanceStatus status;
            switch ( getEngine().BeginStep() )
            {
            case adios2::StepStatus::EndOfStream:
                status = AdvanceStatus::OVER;
                break;
            default:
                status = AdvanceStatus::OK;
                break;
            }
            duringStep = true;
            return std::packaged_task< AdvanceStatus() >(
                [status]() { return status; } );
        }
        case AdvanceMode::AUTO:
            throw std::runtime_error(
                    "Internal error: Advance mode should be explicitly"
                    " chosen by the front-end.");
        }
    }

    void BufferedActions::drop( )
    {
        m_buffer.clear( );
    }
    
    std::vector< adios2::Variable< BufferedActions::extent_t > > 
    BufferedActions::availabeTablesPerRank( std::string dataset )
    {
        std::list< std::string > variableNames;
        std::string prefix = chunkTablePrefix( dataset, false);
        for( auto & pair : m_IO.AvailableVariables() )
        {
            if( auxiliary::starts_with(pair.first, prefix ) )
            {
                variableNames.emplace_back( pair.first );
            }
        }
        auto writer_mpi_size = variableNames.size();
        std::vector< adios2::Variable< extent_t > > res( writer_mpi_size );
        for( std::string const & var : variableNames )
        {
            adios2::Variable< extent_t > table = 
                m_IO.InquireVariable< extent_t >( var );
            adios2::Attribute< int > rankAttr =
                m_IO.InquireAttribute< int >( "rank", var );
            int rank = rankAttr.Data()[ 0 ];
            VERIFY_ALWAYS( 
                table.operator bool() 
                && rankAttr.operator bool() 
                && rank < static_cast< int >( writer_mpi_size ), 
                "ADIOS2 backend: failed trying to load chunk tables for "
                "dataset " + dataset );
            res[ rank ] = std::move( table );
        }
        VERIFY_ALWAYS(
            std::all_of(
                res.begin(),
                res.end(),
                []( adios2::Variable< extent_t > & var ){
                    return var.operator bool(); 
                }),
            "ADIOS2 backend: 2 failed trying to load chunk tables for "
            "dataset " + dataset );
        return res;
    }

    std::string
    BufferedActions::chunkTablePrefix( std::string dataset, bool includeRank )
    {
        if( !auxiliary::starts_with(dataset, '/') )
        {
            dataset = '/' + dataset;
        }
        std::string res =  "/openPMD_internal/chunkTablesPerStepAndRank/"
            + std::to_string( currentStep )
            + dataset;
        if( includeRank )
            res += "/" + std::to_string( mpi_rank );
        return res;
    }
    
    adios2::Variable< BufferedActions::extent_t >
    BufferedActions::createChunkTable( std::string dataset )
    {
        std::string table = chunkTablePrefix( dataset, true );
        auto attr = m_IO.DefineAttribute( "rank", mpi_rank, table );
        auto res = m_IO.DefineVariable< extent_t >( table );
        VERIFY_ALWAYS(
            res && attr,
            "ADIOS2: Failed creating attribute/variable." );
        return res;
    }


    void BufferedActions::writeChunkTables()
    {
        for( auto & pair : writtenChunks )
        {
            adios2::Variable< extent_t > table = createChunkTable( pair.first );
            std::array< extent_t, 2 > & arr = std::get< 1 >( pair.second );
            extent_t number_of_chunks = arr[ 0 ];
            extent_t dimensionality = arr[ 1 ];
            adios2::Dims dims{ number_of_chunks, 2, dimensionality };
            table.SetShape( dims );
            table.SetSelection( { adios2::Dims{ 0, 0, 0 }, dims } );
            getEngine().Put( table, std::get< 0 >( pair.second ).data() );
        }
    }

} // namespace detail

#if openPMD_HAVE_MPI

ADIOS2IOHandler::ADIOS2IOHandler(
    std::string path,
    openPMD::AccessType at,
    MPI_Comm comm,
    nlohmann::json options )
    : AbstractIOHandler( std::move( path ), at, comm )
    , m_impl{ this, comm, std::move( options )

    }
{
}

#endif

ADIOS2IOHandler::ADIOS2IOHandler(
    std::string path,
    AccessType at,
    nlohmann::json options )
    : AbstractIOHandler( std::move( path ), at )
    , m_impl{ this, std::move( options ) }
{
}


ADIOS2IOHandler::~ADIOS2IOHandler( )
{
    this->flush( );
}

std::future< void > ADIOS2IOHandler::flush( )
{
    return m_impl.flush( );
}

#else // openPMD_HAVE_ADIOS2

#if openPMD_HAVE_MPI
ADIOS2IOHandler::ADIOS2IOHandler( std::string path, AccessType at,
                                  MPI_Comm comm )
: AbstractIOHandler( std::move( path ), at, comm )
{
}

#endif

ADIOS2IOHandler::ADIOS2IOHandler( std::string path, AccessType at )
: AbstractIOHandler( std::move( path ), at )
{
}

std::future< void > ADIOS2IOHandler::flush( )
{
    return std::future< void >( );
}

#endif

} // namespace openPMD
