#pragma once
#include <future>
#include <memory>
#include <utility>

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

    public:
        ConsumingFuture( std::packaged_task< A( Args &&... ) > task ) :
            std::future< A >( task.get_future() ),
            m_task( std::move( task ) )
        {
        }

        void
        operator()( Args &&... args )
        {
            m_task( std::forward< Args >( args )... );
        }
    };

    namespace detail
    {
        template< typename A, typename B >
        struct AvoidVoid
        {
            using type = B( A );
            static void
            run_task(
                std::future< A > & first,
                std::packaged_task< type > & second )
            {
                second( first.get() );
            }
        };

        template< typename B >
        struct AvoidVoid< void, B >
        {
            using type = B();
            static void
            run_task(
                std::future< void > &,
                std::packaged_task< type > & second )
            {
                second();
            }
        };
    } // namespace detail

    template< typename A, typename B >
    ConsumingFuture< B >
    chain_futures(
        std::future< A > first,
        std::packaged_task< typename detail::AvoidVoid< A, B >::type > second )
    {
        auto first_ptr =
            std::make_shared< decltype( first ) >( std::move( first ) );
        auto second_ptr =
            std::make_shared< decltype( second ) >( std::move( second ) );
        std::packaged_task< B() > ptask( [first_ptr, second_ptr]() {
            if( first_ptr->valid() )
                first_ptr->wait();
            auto future = second_ptr->get_future();
            detail::AvoidVoid< A, B >::run_task( *first_ptr, *second_ptr );
            future.wait();
            return future.get();
        } );
        return ConsumingFuture< B >( std::move( ptask ) );
    }
} // namespace auxiliary
} // namespace openPMD