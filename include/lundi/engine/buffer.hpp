#ifndef LUNDI_ENGINE_BUFFER_HPP
#define LUNDI_ENGINE_BUFFER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace lundi::engine
{

/// @brief Reusable byte buffer for building HTTP responses.
///
/// Modeled after Rust's BytesMut — clear() resets length but keeps capacity,
/// avoiding repeated heap allocations on keep-alive connections. One instance
/// is created per connection in handle_connection and reused across requests.
///
/// @note Not thread-safe. Each connection has its own buffer on its worker thread.
class write_buffer
{
public:
    /// @param initial_capacity Pre-allocated size in bytes (default 32KB)
    explicit write_buffer( size_t initial_capacity = 32768 ) : data_( initial_capacity )
    {
    }

    /// @brief Append raw bytes to the buffer.
    /// @param data Pointer to source bytes
    /// @param len  Number of bytes to copy
    void append( const void* data, size_t len )
    {
        ensure( len );
        std::memcpy( data_.data() + size_, data, len );
        size_ += len;
    }

    /// @brief Append a string_view.
    void append( std::string_view sv )
    {
        append( sv.data(), sv.size() );
    }

    /// @brief Append a null-terminated C string.
    void append( const char* s )
    {
        append( s, std::strlen( s ) );
    }

    /// @brief Append a single byte.
    void push( char c )
    {
        ensure( 1 );
        data_[ size_++ ] = c;
    }

    /// @brief Direct write access for callers that fill the buffer manually.
    /// @warning Caller must call ensure() first to guarantee space.
    char* write_pos()
    {
        return data_.data() + size_;
    }

    /// @brief Advance the write position after a manual write.
    /// @param n Number of bytes written at write_pos()
    void advance( size_t n )
    {
        size_ += n;
    }

    /// @brief Pointer to the beginning of buffered data.
    const char* data() const
    {
        return data_.data();
    }

    /// @brief Number of bytes currently in the buffer.
    size_t size() const
    {
        return size_;
    }

    /// @brief True if the buffer contains no data.
    bool empty() const
    {
        return size_ == 0;
    }

    /// @brief Reset length to zero. Keeps allocated memory for reuse.
    void clear()
    {
        size_ = 0;
    }

    /// @brief Remove the first @p n bytes, shifting remaining data forward.
    ///
    /// Used when consuming a processed request from a pipelined buffer.
    /// Prefer clear() when all data has been consumed (avoids memmove).
    void consume( size_t n )
    {
        if( n >= size_ )
        {
            size_ = 0;
        }
        else
        {
            std::memmove( data_.data(), data_.data() + n, size_ - n );
            size_ -= n;
        }
    }

    /// @brief Ensure at least @p additional bytes of write space.
    ///
    /// Doubles capacity if the current size is insufficient.
    void ensure( size_t additional )
    {
        if( size_ + additional > data_.size() )
        {
            data_.resize( std::max( data_.size() * 2, size_ + additional ) );
        }
    }

private:
    std::vector< char > data_;
    size_t size_ = 0;
};

/// @brief Read buffer with position tracking for parsing pipelined HTTP requests.
///
/// Manages a single contiguous memory region where socket reads append to the end
/// and parsed requests are consumed from the front.
///
/// @note Not thread-safe. Each connection has its own buffer.
class read_buffer
{
public:
    /// @param initial_capacity Pre-allocated size in bytes (default 32KB)
    explicit read_buffer( size_t initial_capacity = 32768 ) : data_( initial_capacity )
    {
    }

    /// @brief Pointer to the next writable position for socket reads.
    char* write_pos()
    {
        return data_.data() + size_;
    }

    /// @brief Number of bytes available for writing before the buffer is full.
    size_t write_capacity() const
    {
        return data_.size() - size_;
    }

    /// @brief Advance write position after a successful socket read.
    /// @param n Number of bytes received
    void advance_write( size_t n )
    {
        size_ += n;
    }

    /// @brief Ensure at least @p n bytes of write space, growing if needed.
    void ensure_write( size_t n )
    {
        if( write_capacity() < n )
        {
            data_.resize( std::max( data_.size() * 2, size_ + n ) );
        }
    }

    /// @brief Pointer to the beginning of unprocessed data.
    const char* data() const
    {
        return data_.data();
    }

    /// @brief Number of unprocessed bytes in the buffer.
    size_t size() const
    {
        return size_;
    }

    /// @brief True if no unprocessed data remains.
    bool empty() const
    {
        return size_ == 0;
    }

    /// @brief Remove the first @p n bytes after processing a request.
    ///
    /// Shifts remaining bytes to the front via memmove. For the common case
    /// where all data is consumed, prefer clear() to avoid the shift.
    void consume( size_t n )
    {
        if( n >= size_ )
        {
            size_ = 0;
        }
        else
        {
            std::memmove( data_.data(), data_.data() + n, size_ - n );
            size_ -= n;
        }
    }

    /// @brief Discard all data. Keeps allocated memory.
    void clear()
    {
        size_ = 0;
    }

    /// @brief Scan for the HTTP header terminator (\\r\\n\\r\\n)
    ///
    /// Uses glibc's memmem() on Linux using SIMD, and a byte-by-byte fallback
    ///
    /// @return Pointer to the first byte after \\r\\n\\r\\n (start of body),
    ///         or nullptr if the terminator was not found.
    const char* find_header_end() const
    {
        if( size_ < 4 )
        {
            return nullptr;
        }
#if defined( __GLIBC__ ) || defined( __linux__ )
        static constexpr char needle[] = "\r\n\r\n";
        auto* found = static_cast< const char* >( ::memmem( data_.data(), size_, needle, 4 ) );
        return found ? found + 4 : nullptr;
#else
        const char* p = data_.data();
        const char* end = p + size_ - 3;
        while( p < end )
        {
            if( p[ 0 ] == '\r' && p[ 1 ] == '\n' && p[ 2 ] == '\r' && p[ 3 ] == '\n' )
                return p + 4;
            ++p;
        }
        return nullptr;
#endif
    }

private:
    std::vector< char > data_;
    size_t size_ = 0;
};

}  // namespace lundi::engine

#endif  // LUNDI_ENGINE_BUFFER_HPP