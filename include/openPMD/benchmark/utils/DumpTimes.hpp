#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <string>
#include <utility>

namespace openPMD
{
// https://en.cppreference.com/w/cpp/named_req/Clock
template< typename Clock = std::chrono::system_clock, bool enable = true >
class DumpTimes
{
public:
    using time_point = typename Clock::time_point;
    using duration = typename Clock::duration;
    using Ret_T = std::pair< time_point, duration >;

    constexpr static char const * ENV_VAR = "BENCHMARK_FILENAME";
    const std::string filename;

    DumpTimes();
    DumpTimes( std::string filename );

    template< typename Duration >
    Ret_T
    now( std::string description, std::string separator = "\t" );

    void
    flush();

private:
    std::fstream outStream;
    time_point lastTimePoint;
};

template< typename Clock, bool enable >
DumpTimes< Clock, enable >::DumpTimes()
    : DumpTimes( std::string( std::getenv( ENV_VAR ) ) )
{
}

template< typename Clock, bool enable >
DumpTimes< Clock, enable >::DumpTimes( std::string _filename )
    : filename( std::move( _filename ) )
    , outStream( filename, std::ios_base::out | std::ios_base::app )
    , lastTimePoint( Clock::now() )
{
}

template< typename Clock, bool enable >
template< typename Duration >
typename DumpTimes< Clock, enable >::Ret_T
DumpTimes< Clock, enable >::now(
    std::string description,
    std::string separator )
{
    auto currentTime = Clock::now();
    auto delta = currentTime - lastTimePoint;
    lastTimePoint = currentTime;
    std::time_t now_c = Clock::to_time_t( currentTime );
    outStream << std::put_time( std::localtime( &now_c ), "%F %T" ) << '.'
              << std::setfill( '0' ) << std::setw( 3 )
              << std::chrono::duration_cast< std::chrono::milliseconds >(
                     currentTime.time_since_epoch() )
                     .count() %
            1000
              << separator
              << std::chrono::duration_cast< Duration >( delta ).count()
              << separator << description << '\n';
    return std::make_pair( currentTime, delta );
}

template< typename Clock, bool enable >
void
DumpTimes< Clock, enable >::flush()
{
    outStream << std::flush;
}

template< typename Clock >
class DumpTimes< Clock, false >
{
public:
    using Ret_T = void;
    DumpTimes()
    {
    }
    DumpTimes( std::string )
    {
    }

    template< typename, typename... Args >
    inline Ret_T
    now( Args &&... )
    {
    }

    template< typename, typename... Args >
    inline void
    flush( Args &&... )
    {
    }
};

} // namespace openPMD
