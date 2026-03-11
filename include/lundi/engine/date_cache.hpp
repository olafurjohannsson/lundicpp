#ifndef LUNDI_ENGINE_DATE_CACHE_HPP
#define LUNDI_ENGINE_DATE_CACHE_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <ctime>

namespace lundi::engine
{

/// @brief Pre-formatted HTTP Date header, updated once per second
///
/// HTTP/1.1 requires a Date header on every response, this class caches the result and
/// only reformats when the epoch second changes.
/// Thread-safe via double-buffering + atomic index swap.
class date_cache
{
public:
    /// @brief Length of an HTTP date string ("Thu, 01 Jan 1970 00:00:00 GMT").
    static constexpr size_t DATE_LEN = 29;

    date_cache()
    {
        update();
    }

    /// @brief Reformat the cached date if the current second has changed.
    /// Call this once per sec
    void update() noexcept
    {
        auto now = std::chrono::system_clock::now();
        auto epoch = std::chrono::system_clock::to_time_t( now );

        if( epoch == last_epoch_ )
        {
            return;  // already up to date
        }
        last_epoch_ = epoch;

        // Write to the inactive buffer
        int next = 1 - active_.load( std::memory_order_relaxed );
        struct tm tm_buf;
#ifdef _MSC_VER
        gmtime_s( &tm_buf, &epoch );
#else
        gmtime_r( &epoch, &tm_buf );
#endif
        strftime( bufs_[ next ], sizeof( bufs_[ next ] ), "%a, %d %b %Y %H:%M:%S GMT", &tm_buf );

        // Swap active buffer atomically
        active_.store( next, std::memory_order_release );
    }

    /// @brief Pointer to the current formatted date string lock-free rea
    const char* get() const noexcept
    {
        return bufs_[ active_.load( std::memory_order_acquire ) ];
    }

    /// @brief Length of the cached date string (always 29).
    size_t length() const noexcept
    {
        return DATE_LEN;
    }

private:
    char bufs_[ 2 ][ 32 ]{};  // double-buffered
    std::atomic< int > active_{ 0 };
    std::time_t last_epoch_{ 0 };
};

/// @brief Global singleton one per process, all threads share it.
inline date_cache& global_date_cache()
{
    static date_cache instance;
    return instance;
}

}  // namespace lundi::engine

#endif  // LUNDI_ENGINE_DATE_CACHE_HPP