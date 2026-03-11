#ifndef LUNDI_ENGINE_FAST_ITOA_HPP
#define LUNDI_ENGINE_FAST_ITOA_HPP

#include <cstddef>
#include <cstdint>

namespace lundi::engine
{

/// @brief Stack-based uint64_t to ASCII converter. No heap allocation.
class fast_itoa
{
public:
    // @brief Convert a uint64_t to its decimal string representation.
    /// @param val Value to convert
    /// @return Pointer to the first character of the result (inside internal buffer)
    const char* format( uint64_t val ) noexcept
    {
        char* p = buf_ + sizeof( buf_ ) - 1;
        *p = '\0';
        if( val == 0 )
        {
            *--p = '0';
        }
        else
        {
            while( val > 0 )
            {
                *--p = static_cast< char >( '0' + static_cast< int >( val % 10 ) );
                val /= 10;
            }
        }
        len_ = static_cast< size_t >( ( buf_ + sizeof( buf_ ) - 1 ) - p );
        return p;
    }

    /// @brief Length of the last formatted string.
    size_t length() const noexcept
    {
        return len_;
    }

private:
    char buf_[ 21 ];  // max uint64_t = 20 digits + null
    size_t len_ = 0;
};

}  // namespace lundi::engine

#endif  // LUNDI_ENGINE_FAST_ITOA_HPP