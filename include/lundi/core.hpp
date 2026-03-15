#ifndef LUNDI_CORE_HPP
#define LUNDI_CORE_HPP

/// @file core.hpp
/// @brief Request/response types, HTTP parser, handler_fn, and supporting utilities.

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>
#include <thread>

// Engine components for fast serialization
#include "engine/buffer.hpp"
#include "engine/date_cache.hpp"
#include "engine/fast_itoa.hpp"

namespace lundi
{


// Path parameter value


/// @brief A route parameter value — either an int or a string.
using param_value = std::variant< int, std::string >;

/// @brief Move-only type-erased callable. Replaces std::function for handler dispatch.
///
/// No SBO bookkeeping, no copy support, no RTTI. Heap-allocates the callable once
/// at registration time. Invocation is a single indirect function pointer call.
template< typename Sig >
class handler_fn;

template< typename R, typename ... Args >
class handler_fn< R( Args... ) >
{
using invoke_t = R ( * )( void*, Args... );
invoke_t invoke_ = nullptr;
void* data_ = nullptr;
void (*destroy_)( void* ) = nullptr;

public:
    handler_fn() = default;

    handler_fn( const handler_fn& ) = delete;
    handler_fn& operator=( const handler_fn& ) = delete;

    /// @brief Move constructor. Transfers ownership; source becomes empty.
    handler_fn( handler_fn&& o ) noexcept : invoke_( o.invoke_ ), data_( o.data_ ), destroy_( o.destroy_ )
    {
        o.invoke_ = nullptr;
        o.data_ = nullptr;
        o.destroy_ = nullptr;
    }

    /// @brief Move assignment. Destroys current callable, takes ownership of source.
    handler_fn& operator=( handler_fn&& o ) noexcept
    {
        if( this != &o )
        {
            if( destroy_ )
            {
                destroy_( data_ );
            }
            invoke_ = o.invoke_;
            data_ = o.data_;
            destroy_ = o.destroy_;
            o.invoke_ = nullptr;
            o.data_ = nullptr;
            o.destroy_ = nullptr;
        }
        return *this;
    }

    /// @brief Construct from any callable (lambda, function object, etc.).
    template< typename F, typename = std::enable_if_t< !std::is_same_v< std::decay_t< F >, handler_fn > > >
    handler_fn( F&& f )    // NOLINT(google-explicit-constructor)
    {
        using DecayF = std::decay_t< F >;
        auto* p = new DecayF( std::forward< F >( f ) );
        data_ = p;
        invoke_ = []( void* d, Args... args ) -> R {
            return ( *static_cast< DecayF* >( d ) )( std::forward< Args >( args )... );
        };
        destroy_ = []( void* d ) {
            delete static_cast< DecayF* >( d );
        };
    }

    /// @brief Invoke the stored callable.
    R operator()( Args... args ) const
    {
        return invoke_( data_, std::forward< Args >( args )... );
    }

    /// @brief True if a callable is stored.
    explicit operator bool() const { return invoke_ != nullptr; }

    ~handler_fn()
    {
        if( destroy_ )
        {
            destroy_( data_ );
        }
    }
};


// URL decoding

namespace detail
{

inline int hex_val( char c )
{
    if( c >= '0' && c <= '9' )
    {
        return c - '0';
    }
    if( c >= 'A' && c <= 'F' )
    {
        return c - 'A' + 10;
    }
    if( c >= 'a' && c <= 'f' )
    {
        return c - 'a' + 10;
    }
    return -1;
}

inline std::string url_decode( std::string_view sv )
{
    std::string out;
    out.reserve( sv.size() );
    for( size_t i = 0; i < sv.size(); ++i )
    {
        if( sv[ i ] == '%' && i + 2 < sv.size() )
        {
            int hi = hex_val( sv[ i + 1 ] );
            int lo = hex_val( sv[ i + 2 ] );
            if( hi >= 0 && lo >= 0 )
            {
                out += static_cast< char >( hi * 16 + lo );
                i += 2;
                continue;
            }
        }
        if( sv[ i ] == '+' )
        {
            out += ' ';
            continue;
        }
        out += sv[ i ];
    }
    return out;
}

inline std::string to_lower( std::string_view sv )
{
    std::string out( sv );
    std::transform( out.begin(), out.end(), out.begin(), ::tolower );
    return out;
}

/// @brief A parsed query string parameter (name=value pair).
struct query_entry {
    std::string name;   ///< Parameter name (URL-decoded)
    std::string value;  ///< Parameter value (URL-decoded)
};

inline std::vector< query_entry > parse_query_string( std::string_view qs )
{
    std::vector< query_entry > params;
    while( !qs.empty() )
    {
        auto amp = qs.find( '&' );
        auto pair = qs.substr( 0, amp );
        if( !pair.empty() )
        {
            auto eq = pair.find( '=' );
            if( eq != std::string_view::npos )
            {
                params.push_back( { url_decode( pair.substr( 0, eq ) ), url_decode( pair.substr( eq + 1 ) ) } );
            }
            else
            {
                params.push_back( { url_decode( pair ), "" } );
            }
        }
        if( amp == std::string_view::npos )
        {
            break;
        }
        qs = qs.substr( amp + 1 );
    }
    return params;
}

}  // namespace detail


// Cookie parsing

namespace detail
{

inline std::unordered_map< std::string, std::string > parse_cookies( std::string_view cookie_header )
{
    std::unordered_map< std::string, std::string > cookies;
    while( !cookie_header.empty() )
    {
        while( !cookie_header.empty() && cookie_header[ 0 ] == ' ' )
        {
            cookie_header.remove_prefix( 1 );
        }

        auto semi = cookie_header.find( ';' );
        auto pair = cookie_header.substr( 0, semi );

        if( !pair.empty() )
        {
            auto eq = pair.find( '=' );
            if( eq != std::string_view::npos )
            {
                auto name = pair.substr( 0, eq );
                auto val = pair.substr( eq + 1 );
                while( !name.empty() && name.back() == ' ' )
                {
                    name.remove_suffix( 1 );
                }
                cookies[ std::string( name ) ] = std::string( val );
            }
        }

        if( semi == std::string_view::npos )
        {
            break;
        }
        cookie_header = cookie_header.substr( semi + 1 );
    }
    return cookies;
}

}  // namespace detail


// Multipart form data


/// @brief An uploaded file from a multipart/form-data request.
struct form_file {
    std::string filename;      ///< Original filename from the upload
    std::string content_type;  ///< MIME type (defaults to application/octet-stream)
    std::string data;          ///< Raw file contents
};

/// @brief Parsed multipart/form-data — text fields and uploaded files.
struct form_data {
    std::unordered_map< std::string, std::string > fields;  ///< Text fields (name → value)
    std::unordered_map< std::string, form_file > files;     ///< Uploaded files (name → form_file)

    /// @brief Get a text field value, or a default if not present.
    std::string field( const std::string& name,  // NOLINT(bugprone-easily-swappable-parameters)
                       const std::string& default_val = "" ) const
    {
        auto it = fields.find( name );
        return it != fields.end() ? it->second : default_val;
    }

    /// @brief Get a pointer to an uploaded file, or nullptr if not present.
    const form_file* file( const std::string& name ) const
    {
        auto it = files.find( name );
        return it != files.end() ? &it->second : nullptr;
    }
};

namespace detail
{

inline form_data parse_multipart(
    std::string_view body,  // NOLINT(bugprone-easily-swappable-parameters)
    std::string_view boundary )
{
    form_data result;
    std::string delim = "--";
    delim += boundary;

    size_t pos = body.find( delim );
    if( pos == std::string_view::npos )
    {
        return result;
    }

    while( true )
    {
        pos += delim.size();
        if( pos + 2 > body.size() )
        {
            break;
        }
        if( body[ pos ] == '-' && body[ pos + 1 ] == '-' )
        {
            break;
        }
        if( body[ pos ] == '\r' )
        {
            pos += 2;
        }

        auto header_end = body.find( "\r\n\r\n", pos );
        if( header_end == std::string_view::npos )
        {
            break;
        }

        auto part_headers = body.substr( pos, header_end - pos );
        pos = header_end + 4;

        auto next = body.find( delim, pos );
        if( next == std::string_view::npos )
        {
            break;
        }

        size_t body_end = next;
        if( body_end >= 2 && body[ body_end - 2 ] == '\r' && body[ body_end - 1 ] == '\n' )
        {
            body_end -= 2;
        }
        auto part_body = body.substr( pos, body_end - pos );
        pos = next;

        std::string name, filename, content_type;

        auto name_pos = part_headers.find( "name=\"" );
        if( name_pos != std::string_view::npos )
        {
            name_pos += 6;
            auto name_end = part_headers.find( '"', name_pos );
            if( name_end != std::string_view::npos )
            {
                name = std::string( part_headers.substr( name_pos, name_end - name_pos ) );
            }
        }

        auto fn_pos = part_headers.find( "filename=\"" );
        if( fn_pos != std::string_view::npos )
        {
            fn_pos += 10;
            auto fn_end = part_headers.find( '"', fn_pos );
            if( fn_end != std::string_view::npos )
            {
                filename = std::string( part_headers.substr( fn_pos, fn_end - fn_pos ) );
            }
        }

        auto ct_pos = part_headers.find( "Content-Type: " );
        if( ct_pos == std::string_view::npos )
        {
            ct_pos = part_headers.find( "content-type: " );
        }
        if( ct_pos != std::string_view::npos )
        {
            ct_pos += 14;
            auto ct_end = part_headers.find( "\r\n", ct_pos );
            if( ct_end == std::string_view::npos )
            {
                ct_end = part_headers.size();
            }
            content_type = std::string( part_headers.substr( ct_pos, ct_end - ct_pos ) );
        }

        if( name.empty() )
        {
            continue;
        }

        if( !filename.empty() )
        {
            result.files[ name ] = form_file{
                std::move( filename ),
                content_type.empty() ? "application/octet-stream" : std::move( content_type ),
                std::string( part_body ) };
        }
        else
        {
            result.fields[ name ] = std::string( part_body );
        }
    }

    return result;
}

inline std::string extract_boundary( std::string_view content_type )
{
    auto bp = content_type.find( "boundary=" );
    if( bp == std::string_view::npos )
    {
        return "";
    }
    bp += 9;
    if( bp < content_type.size() && content_type[ bp ] == '"' )
    {
        bp++;
        auto end = content_type.find( '"', bp );
        if( end == std::string_view::npos )
        {
            return "";
        }
        return std::string( content_type.substr( bp, end - bp ) );
    }
    auto end = content_type.find( ';', bp );
    if( end == std::string_view::npos )
    {
        end = content_type.size();
    }
    while( end > bp && ( content_type[ end - 1 ] == ' ' || content_type[ end - 1 ] == '\r' ) )
    {
        --end;
    }
    return std::string( content_type.substr( bp, end - bp ) );
}

}  // namespace detail


// Request


/// @brief An HTTP request. Header views point into the connection's read buffer.
///
/// Created by detail::parse_headers(). Header string_views are valid until the
/// connection buffer is consumed at the end of each request cycle.
struct request {
    std::string_view method;    ///< HTTP method ("GET", "POST", etc.)
    std::string path;           ///< URL-decoded path (owned — decoding may allocate)
    std::string_view raw_path;  ///< Raw path including query string (view into buffer)
    std::string_view body;      ///< Request body (view into buffer, empty for GET)

    /// @brief A request header (name and value are views into the read buffer).
    struct header_entry {
        std::string_view name;   ///< Header name (original case from client)
        std::string_view value;  ///< Header value (whitespace-trimmed)
    };
    static constexpr size_t MAX_HEADERS = 32;  ///< Maximum number of headers stored
    header_entry headers_arr[ MAX_HEADERS ];     ///< Fixed header array (zero heap allocation)
    size_t num_headers = 0;                    ///< Number of headers parsed

    /// @brief A route parameter extracted from the URL pattern.
    struct param_entry {
        std::string name;   ///< Parameter name from pattern (e.g. "id" from `<int:id>`)
        param_value value;  ///< Extracted value (int or string)
    };
    std::vector< param_entry > params;                ///< Route parameters (populated by router)
    std::vector< detail::query_entry > query_params;  ///< Query string parameters

    /// @brief Get an integer route parameter by name. Returns 0 if not found.
    int param_int( const std::string& name ) const
    {
        for( const auto& p : params )
        {
            if( p.name == name )
            {
                return std::get< int >( p.value );
            }
        }
        return 0;
    }

    /// @brief Get a string route parameter by name. Returns empty string if not found.
    const std::string& param_str( const std::string& name ) const
    {
        for( const auto& p : params )
        {
            if( p.name == name )
            {
                return std::get< std::string >( p.value );
            }
        }
        static const std::string empty;
        return empty;
    }

    /// @brief Get a query parameter by name. Returns nullopt if not found.
    std::optional< std::string > query( const std::string& name ) const
    {
        for( const auto& q : query_params )
        {
            if( q.name == name )
            {
                return q.value;
            }
        }
        return std::nullopt;
    }

    /// @brief Get a query parameter by name with a default fallback.
    std::string query( const std::string& name,  // NOLINT(bugprone-easily-swappable-parameters)
                       const std::string& default_val ) const
    {
        for( const auto& q : query_params )
        {
            if( q.name == name )
            {
                return q.value;
            }
        }
        return default_val;
    }

    /// @brief Get a query parameter as an integer. Returns nullopt if missing or non-numeric.
    std::optional< int > query_int( const std::string& name ) const
    {
        for( const auto& q : query_params )
        {
            if( q.name == name )
            {
                int val = 0;
                auto [ ptr, ec ] =
                    std::from_chars( q.value.data(), q.value.data() + q.value.size(), val );
                if( ec == std::errc{} )
                {
                    return val;
                }
            }
        }
        return std::nullopt;
    }

    /// @brief Get a query parameter as an integer with a default fallback.
    int query_int( const std::string& name, int default_val ) const
    {
        return query_int( name ).value_or( default_val );
    }

    /// @brief Look up a header value by name (case-insensitive). Returns empty if not found.
    std::string_view header( std::string_view name ) const
    {
        auto* val = find_header_ci( name );
        return val ? *val : std::string_view{};
    }

    /// @brief Check if a header has a specific value (case-insensitive on both name and value).
    bool header_equals( std::string_view name,  // NOLINT(bugprone-easily-swappable-parameters)
                        std::string_view value ) const
    {
        auto* val = find_header_ci( name );
        if( !val )
        {
            return false;
        }
        if( val->size() != value.size() )
        {
            return false;
        }
        for( size_t i = 0; i < value.size(); ++i )
        {
            if( std::tolower( static_cast< unsigned char >( ( *val )[ i ] ) ) !=
                std::tolower( static_cast< unsigned char >( value[ i ] ) ) )
            {
                return false;
            }
        }
        return true;
    }

    /// @brief Find a header by name (case-insensitive). Returns pointer to value or nullptr.
    const std::string_view* find_header_ci( std::string_view name ) const
    {
        for( size_t i = 0; i < num_headers; ++i )
        {
            if( headers_arr[ i ].name.size() == name.size() )
            {
                bool match = true;
                for( size_t j = 0; j < name.size(); ++j )
                {
                    if( std::tolower( static_cast< unsigned char >( headers_arr[ i ].name[ j ] ) ) !=
                        std::tolower( static_cast< unsigned char >( name[ j ] ) ) )
                    {
                        match = false;
                        break;
                    }
                }
                if( match )
                {
                    return &headers_arr[ i ].value;
                }
            }
        }
        return nullptr;
    }

    /// @brief Alias for find_header_ci (backwards compatibility).
    const std::string_view* find_header( std::string_view lowercase_name ) const
    {
        return find_header_ci( lowercase_name );
    }

    /// @brief Get a cookie value by name. Returns empty string if not found. Lazy-parsed.
    std::string cookie( const std::string& name ) const
    {
        ensure_cookies_parsed();
        if( !cookies_ )
        {
            return "";
        }
        auto it = cookies_->find( name );
        return it != cookies_->end() ? it->second : "";
    }

    /// @brief Check if a cookie exists. Lazy-parsed.
    bool has_cookie( const std::string& name ) const
    {
        ensure_cookies_parsed();
        return cookies_ && cookies_->find( name ) != cookies_->end();
    }

    /// @brief Get all cookies as a map. Lazy-parsed.
    const std::unordered_map< std::string, std::string >& cookies() const
    {
        ensure_cookies_parsed();
        static const std::unordered_map< std::string, std::string > empty;
        return cookies_ ? *cookies_ : empty;
    }

    /// @brief Get the parsed multipart form data. Lazy-parsed.
    const form_data& form() const
    {
        ensure_form_parsed();
        return form_;
    }

    /// @brief Get a form text field by name with optional default. Lazy-parsed.
    std::string form_field( const std::string& name, const std::string& default_val = "" ) const
    {
        ensure_form_parsed();
        return form_.field( name, default_val );
    }

    /// @brief Get a pointer to an uploaded file by name. Returns nullptr if not found. Lazy-parsed.
    const form_file* form_file_ptr( const std::string& name ) const
    {
        ensure_form_parsed();
        return form_.file( name );
    }

    private:
        mutable std::optional< std::unordered_map< std::string, std::string > > cookies_;
        mutable bool cookies_parsed_ = false;
        mutable form_data form_;
        mutable bool form_parsed_ = false;

        void ensure_cookies_parsed() const
        {
            if( !cookies_parsed_ )
            {
                auto* val = find_header_ci( "cookie" );
                if( val )
                {
                    cookies_ = detail::parse_cookies( *val );
                }
                cookies_parsed_ = true;
            }
        }

        void ensure_form_parsed() const
        {
            if( !form_parsed_ )
            {
                auto* ct = find_header_ci( "content-type" );
                if( ct && ct->find( "multipart/form-data" ) != std::string_view::npos )
                {
                    auto boundary = detail::extract_boundary( *ct );
                    if( !boundary.empty() )
                    {
                        form_ = detail::parse_multipart( body, boundary );
                    }
                }
                form_parsed_ = true;
            }
        }
};


// Response


/// @brief An HTTP response. Constructed via factory methods (text, json, html)
/// or directly for custom responses.
struct response {
    int status = 200;  ///< HTTP status code

    /// @brief Pre-set content types (avoids string allocation for common types).
    enum class preset_content_type : std::uint8_t { none, text, html, json };
    preset_content_type preset_ct = preset_content_type::none;  ///< Active preset (if any)

    /// @brief A response header (owned strings).
    struct header_entry {
        std::string name;   ///< Header name
        std::string value;  ///< Header value
    };
    std::vector< header_entry > headers;  ///< Custom response headers
    size_t num_headers = 0;             ///< Number of custom headers set

    std::string body;  ///< Response body

    /// @brief Add a custom response header.
    void set_header( const std::string& name, const std::string& value )
    {
        headers.push_back( { name, value } );
        num_headers++;
    }

    /// @brief Create a text/plain response.
    static response text( const std::string& body, int status = 200 )
    {
        response r;
        r.status = status;
        r.body = body;
        r.preset_ct = preset_content_type::text;
        return r;
    }

    /// @brief Create a text/html response.
    static response html( const std::string& body, int status = 200 )
    {
        response r;
        r.status = status;
        r.body = body;
        r.preset_ct = preset_content_type::html;
        return r;
    }

    /// @brief Create an application/json response.
    static response json( const std::string& body, int status = 200 )
    {
        response r;
        r.status = status;
        r.body = body;
        r.preset_ct = preset_content_type::json;
        return r;
    }

    /// @brief Serialize the response into a pre-allocated write buffer (zero-alloc hot path).
    void serialize_into( engine::write_buffer& buf ) const
    {
        engine::fast_itoa itoa;

        if( status == 200 ) [[likely]] {
            buf.append( "HTTP/1.1 200 OK\r\n", 17 );
        }
        else
        {
            buf.append( "HTTP/1.1 ", 9 );
            auto* s = itoa.format( static_cast< uint64_t >( status ) );
            buf.append( s, itoa.length() );
            buf.push( ' ' );
            auto msg = status_text( status );
            buf.append( msg.data(), msg.size() );
            buf.append( "\r\n", 2 );
        }

        buf.append( "Server: Lundi\r\nDate: ", 21 );
        auto& dc = engine::global_date_cache();
        buf.append( dc.get(), dc.length() );
        buf.append( "\r\n", 2 );

        if( preset_ct == preset_content_type::text )
        {
            buf.append( "Content-Type: text/plain; charset=utf-8\r\n", 41 );
        }
        else if( preset_ct == preset_content_type::json )
        {
            buf.append( "Content-Type: application/json; charset=utf-8\r\n", 47 );
        }
        else if( preset_ct == preset_content_type::html )
        {
            buf.append( "Content-Type: text/html; charset=utf-8\r\n", 40 );
        }

        bool has_cl = false;
        bool has_conn = false;

        for( size_t i = 0; i < num_headers; ++i )
        {
            const auto& k = headers[ i ].name;
            const auto& v = headers[ i ].value;

            if( k == "Content-Length" || k == "content-length" )
            {
                has_cl = true;
            }
            if( k == "Connection" || k == "connection" )
            {
                has_conn = true;
            }
            buf.append( k.data(), k.size() );
            buf.append( ": ", 2 );
            buf.append( v.data(), v.size() );
            buf.append( "\r\n", 2 );
        }

        if( !has_cl )
        {
            buf.append( "Content-Length: ", 16 );
            auto* cl = itoa.format( body.size() );
            buf.append( cl, itoa.length() );
            buf.append( "\r\n", 2 );
        }
        if( !has_conn )
        {
            buf.append( "Connection: keep-alive\r\n", 24 );
        }

        buf.append( "\r\n", 2 );
        if( !body.empty() )
        {
            buf.append( body.data(), body.size() );
        }
    }

    /// @brief Serialize to a std::string (allocates — use serialize_into for hot path).
    std::string serialize() const
    {
        engine::write_buffer buf( 256 + body.size() );
        serialize_into( buf );
        return std::string( buf.data(), buf.size() );
    }

    private:
        static std::string_view status_text( int code )
        {
            switch( code )
            {
                case 101:
                    return "Switching Protocols";
                case 200:
                    return "OK";
                case 201:
                    return "Created";
                case 204:
                    return "No Content";
                case 301:
                    return "Moved Permanently";
                case 302:
                    return "Found";
                case 304:
                    return "Not Modified";
                case 400:
                    return "Bad Request";
                case 401:
                    return "Unauthorized";
                case 403:
                    return "Forbidden";
                case 404:
                    return "Not Found";
                case 405:
                    return "Method Not Allowed";
                case 408:
                    return "Request Timeout";
                case 413:
                    return "Payload Too Large";
                case 429:
                    return "Too Many Requests";
                case 500:
                    return "Internal Server Error";
                case 502:
                    return "Bad Gateway";
                case 503:
                    return "Service Unavailable";
                default:
                    return "Unknown";
            }
        }
};


// HTTP/1.1 header parser (headers only — body handled by caller)

namespace detail
{


/// @brief RAII thread wrapper that joins on destruction, matching std::jthread behaviour.
/// Used instead of std::jthread for Apple Clang compatibility.
class joining_thread
{
public:
    template< typename F >
    explicit joining_thread( F&& f ) : thread_( std::forward< F >( f ) ) {}

    joining_thread( joining_thread&& ) = default;
    joining_thread& operator=( joining_thread&& ) = default;

    joining_thread( const joining_thread& ) = delete;
    joining_thread& operator=( const joining_thread& ) = delete;

    ~joining_thread()
    {
        if( thread_.joinable() ) thread_.join();
    }

private:
    std::thread thread_;
};

inline request parse_headers( std::string_view header_data )
{
    request req;

    auto line_end = header_data.find( "\r\n" );
    if( line_end == std::string_view::npos )
    {
        return req;
    }

    auto line = header_data.substr( 0, line_end );
    auto sp1 = line.find( ' ' );
    auto sp2 = line.find( ' ', sp1 + 1 );
    if( sp1 != std::string_view::npos && sp2 != std::string_view::npos )
    {
        req.method = line.substr( 0, sp1 );
        req.raw_path = line.substr( sp1 + 1, sp2 - sp1 - 1 );

        auto qmark = req.raw_path.find( '?' );
        if( qmark != std::string_view::npos )
        {
            auto raw = req.raw_path.substr( 0, qmark );
            req.path =
                ( raw.find( '%' ) == std::string_view::npos && raw.find( '+' ) == std::string_view::npos )
                    ? std::string( raw )
                    : url_decode( raw );
            req.query_params = parse_query_string( req.raw_path.substr( qmark + 1 ) );
        }
        else
        {
            req.path = ( req.raw_path.find( '%' ) == std::string_view::npos &&
                         req.raw_path.find( '+' ) == std::string_view::npos )
                           ? std::string( req.raw_path )
                           : url_decode( req.raw_path );
        }
    }

    auto pos = line_end + 2;
    while( pos < header_data.size() )
    {
        auto next_end = header_data.find( "\r\n", pos );
        if( next_end == std::string_view::npos || next_end == pos )
        {
            break;
        }

        auto hline = header_data.substr( pos, next_end - pos );
        auto colon = hline.find( ':' );

        if( colon != std::string_view::npos && req.num_headers < request::MAX_HEADERS )
        {
            std::string_view key = hline.substr( 0, colon );
            auto val_start = hline.find_first_not_of( " \t", colon + 1 );
            std::string_view val =
                val_start != std::string_view::npos ? hline.substr( val_start ) : std::string_view{};

            req.headers_arr[ req.num_headers++ ] = { key, val };
        }
        pos = next_end + 2;
    }

    return req;
}

}  // namespace detail

}  // namespace lundi

#endif  // LUNDI_CORE_HPP