#ifndef LUNDI_ENGINE_HEADER_SCAN_HPP
#define LUNDI_ENGINE_HEADER_SCAN_HPP

#include <cstddef>

#ifdef __SSE2__
#include <immintrin.h>
#endif

namespace lundi::engine
{

// Find \r\n\r\n in buffer starting from scan_start.
// Returns position past the \r\n\r\n (i.e. first byte of body), or 0 if not found.
// Only scans new bytes — caller tracks scan_start across reads.
inline size_t find_header_end( const char* data, size_t len,
                               size_t scan_start ) // NOLINT(bugprone-easily-swappable-parameters)
// Need at least 4 bytes from scan point
{
    if( len < 4 )
    {
        return 0;
    }

    // Start scanning from scan_start, but back up 3 bytes
    // in case \r\n\r\n straddles the boundary of old/new data
    size_t i = scan_start > 3 ? scan_start - 3 : 0;

#ifdef __SSE2__
    const __m128i cr = _mm_set1_epi8( '\r' );

    for(; i + 19 < len; i += 16 )
    {
        __m128i chunk = _mm_loadu_si128( reinterpret_cast< const __m128i* >( data + i ) );
        __m128i cmp = _mm_cmpeq_epi8( chunk, cr );
        int mask = _mm_movemask_epi8( cmp );

        while( mask )
        {
            int bit = __builtin_ctz( mask );
            size_t pos = i + bit;
            if( pos + 3 < len && data[ pos + 1 ] == '\n' && data[ pos + 2 ] == '\r' &&
                data[ pos + 3 ] == '\n' )
            {
                return pos + 4;
            }
            mask &= mask - 1;
        }
    }
#endif

    // scalar fallback
    for(; i + 3 < len; ++i )
    {
        if( data[ i ] == '\r' && data[ i + 1 ] == '\n' && data[ i + 2 ] == '\r' && data[ i + 3 ] == '\n' )
        {
            return i + 4;
        }
    }
    return 0;
}

}  // namespace lundi::engine

#endif  // LUNDI_ENGINE_HEADER_SCAN_HPP