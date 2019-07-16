#pragma once
#include <future>
#include <memory>
#include <utility>
#include <thread>

namespace openPMD
{
namespace auxiliary
{
    /**
     * Subclass template for std::future<A> that wraps
     * the std::packaged_task<…> creating the future
     * Useful to extend the lifetime of the task for
     * exactly as long as the future lives.
     */
    template< typename A, typename... Args >
    class ConsumingFuture : public std::future< A >
    {
    private:
        std::packaged_task< A( Args &&... ) > m_task;
        std::unique_ptr< std::thread > m_thread;

    public:
        ConsumingFuture( std::packaged_task< A( Args &&... ) > task ) :
            std::future< A >( task.get_future() ),
            m_task( std::move( task ) )
        {
        }

        ConsumingFuture ( ConsumingFuture && ) = default;

        ~ConsumingFuture( )
        {
            if ( m_thread )
            {
                m_thread->join( );
            }
        }

        void
        operator()( Args &&... args )
        {
            m_task( std::forward< Args >( args )... );
        }

        void
        run_as_thread( Args &&... args )
        {
            m_thread = std::unique_ptr< std::thread >(
                new std::thread(
                    std::move( m_task ),
                    std::forward< Args >( args )...
                )
            );
        }
    };

    namespace detail
    {
        template< typename A, typename B >
        struct AvoidVoid
        {
            using type = B( A );

            template< typename Future >
            static void
            run_task(
                Future & first,
                std::packaged_task< type > & second )
            {
                second( first.get() );
            }
        };

        template< typename B >
        struct AvoidVoid< void, B >
        {
            using type = B();

            template< typename Future >
            static void
            run_task(
                Future &,
                std::packaged_task< type > & second )
            {
                second();
            }
        };
    } // namespace detail

    template< 
        typename A, 
        typename B, 
        typename Future = std::future< A > >
    ConsumingFuture< B >
    chain_futures(
        Future first,
        std::packaged_task< typename detail::AvoidVoid< A, B >::type > second )
    {
        auto first_ptr =
            std::make_shared< Future >( std::move( first ) );
        auto second_ptr =
            std::make_shared< decltype( second ) >( std::move( second ) );
        std::packaged_task< B() > ptask( [first_ptr, second_ptr]() {
            if( first_ptr->valid() )
                first_ptr->wait();
            auto future = second_ptr->get_future();
            detail::AvoidVoid< A, B >::template run_task< Future >( 
                *first_ptr, *second_ptr );
            future.wait();
            return future.get();
        } );
        return ConsumingFuture< B >( std::move( ptask ) );
    }
} // namespace auxiliary
} // namespace openPMD