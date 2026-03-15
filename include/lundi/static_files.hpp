#ifndef LUNDI_STATIC_FILES_HPP
#define LUNDI_STATIC_FILES_HPP

/// @file static_files.hpp
/// @brief Static file serving with MIME detection, caching headers, and path traversal protection.
///
/// Supports multiple mount points, ETag/Last-Modified for conditional requests (304 Not Modified),
/// configurable Cache-Control, and index.html fallback for directory requests.

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core.hpp"

#ifdef _WIN32
#include <iomanip>
#include <sstream>

namespace lundi::detail::win32
{
    inline std::tm* gmtime_r(const std::time_t* timep, std::tm* result)
    {
        gmtime_s(result, timep); // MSVC has reversed arg order
        return result;
    }

    inline std::time_t timegm(std::tm* tm)
    {
        return _mkgmtime(tm); // MSVC equivalent
    }

    // strptime doesn't exist on MSVC for now we parse HTTP date via stringstream
    inline char* strptime(const char* s, const char* /*fmt*/, std::tm* tm)
    {
        std::istringstream ss(s);
        ss >> std::get_time(tm, "%a, %d %b %Y %H:%M:%S");
        return ss.fail() ? nullptr : const_cast<char*>(s) + ss.tellg();
    }
}

using lundi::detail::win32::gmtime_r;
using lundi::detail::win32::timegm;
using lundi::detail::win32::strptime;
#endif

namespace lundi
{


// Static file options (per mount point)


/// @brief Configuration for a static file mount point.
struct static_options {
    int max_age = 3600;              ///< Cache-Control max-age in seconds (0 = no-cache)
    bool etag = true;                ///< Send ETag header (based on mtime + size)
    bool last_modified = true;       ///< Send Last-Modified header
    std::string index = "index.html"; ///< Directory index filename (empty = disabled)
    size_t max_file_size = 50 * 1024 * 1024;  ///< Max file size to serve (50 MB default)
};


// MIME type detection

namespace detail
{

/// @brief Detect MIME type from file extension.
inline std::string_view mime_for_extension( std::string_view ext )
{
    // text
    if( ext == ".html" || ext == ".htm" )
    {
        return "text/html; charset=utf-8";
    }
    if( ext == ".css" )
    {
        return "text/css; charset=utf-8";
    }
    if( ext == ".js" || ext == ".mjs" )
    {
        return "application/javascript; charset=utf-8";
    }
    if( ext == ".json" )
    {
        return "application/json; charset=utf-8";
    }
    if( ext == ".xml" )
    {
        return "application/xml; charset=utf-8";
    }
    if( ext == ".txt" )
    {
        return "text/plain; charset=utf-8";
    }
    if( ext == ".csv" )
    {
        return "text/csv; charset=utf-8";
    }
    if( ext == ".md" )
    {
        return "text/markdown; charset=utf-8";
    }

    // images
    if( ext == ".png" )
    {
        return "image/png";
    }
    if( ext == ".jpg" || ext == ".jpeg" )
    {
        return "image/jpeg";
    }
    if( ext == ".gif" )
    {
        return "image/gif";
    }
    if( ext == ".svg" )
    {
        return "image/svg+xml";
    }
    if( ext == ".ico" )
    {
        return "image/x-icon";
    }
    if( ext == ".webp" )
    {
        return "image/webp";
    }
    if( ext == ".avif" )
    {
        return "image/avif";
    }

    // fonts
    if( ext == ".woff2" )
    {
        return "font/woff2";
    }
    if( ext == ".woff" )
    {
        return "font/woff";
    }
    if( ext == ".ttf" )
    {
        return "font/ttf";
    }
    if( ext == ".otf" )
    {
        return "font/otf";
    }
    if( ext == ".eot" )
    {
        return "application/vnd.ms-fontobject";
    }

    // binary / other
    if( ext == ".wasm" )
    {
        return "application/wasm";
    }
    if( ext == ".pdf" )
    {
        return "application/pdf";
    }
    if( ext == ".zip" )
    {
        return "application/zip";
    }
    if( ext == ".gz" )
    {
        return "application/gzip";
    }
    if( ext == ".mp4" )
    {
        return "video/mp4";
    }
    if( ext == ".webm" )
    {
        return "video/webm";
    }
    if( ext == ".mp3" )
    {
        return "audio/mpeg";
    }
    if( ext == ".ogg" )
    {
        return "audio/ogg";
    }
    if( ext == ".map" )
    {
        return "application/json";
    }                                                                     // source maps

    return "application/octet-stream";
}

/// @brief Get lowercase file extension from a path.
inline std::string extension_lower( const std::filesystem::path& p )
{
    auto ext = p.extension().string();
    std::transform( ext.begin(), ext.end(), ext.begin(), ::tolower );
    return ext;
}


// ETag / Last-Modified generation


/// @brief Generate a weak ETag from file modification time and size.
///
/// Format: W/"mtime-size" (hex). Weak because we don't hash the file contents —
/// mtime + size is sufficient for static asset caching and avoids reading the file
/// just to compute the ETag on 304 checks.
inline std::string make_etag( std::uintmax_t size, std::filesystem::file_time_type mtime )
{
    auto since_epoch = mtime.time_since_epoch();
    auto seconds = std::chrono::duration_cast< std::chrono::seconds >( since_epoch ).count();

    char buf[ 64 ];
    int len = std::snprintf( buf, sizeof( buf ), "W/\"%lx-%lx\"",
                             static_cast< unsigned long >( seconds ),
                             static_cast< unsigned long >( size ) );
    return std::string( buf, static_cast< size_t >( len ) );
}

/// @brief Format a file_time_type as an HTTP-date (RFC 7231).
/// Example: "Sun, 06 Nov 1994 08:49:37 GMT"
inline std::string format_http_date( std::filesystem::file_time_type ftime )
{
    auto sctp = std::chrono::time_point_cast< std::chrono::system_clock::duration >(
        ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now() );
    auto tt = std::chrono::system_clock::to_time_t( sctp );
    std::tm gmt{};
    gmtime_r( &tt, &gmt );

    char buf[ 64 ];
    std::strftime( buf, sizeof( buf ), "%a, %d %b %Y %H:%M:%S GMT", &gmt );
    return std::string( buf );
}

/// @brief Parse an HTTP-date string into a time_t. Returns -1 on failure.
inline std::time_t parse_http_date( std::string_view date_str )
{
    std::tm tm{};
    // Try RFC 7231 format: "Sun, 06 Nov 1994 08:49:37 GMT"
    if( strptime( std::string( date_str ).c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm ) )
    {
        return timegm( &tm );
    }
    return static_cast< std::time_t >( -1 );
}

}  // namespace detail


// Static file mount


/// @brief A single static file mount point (url prefix → filesystem directory).
struct static_mount {
    std::string prefix;       ///< URL prefix (e.g. "/" or "/static/")
    std::string directory;    ///< Canonical filesystem directory (always trailing /)
    static_options options;   ///< Caching and serving options
};


// Static file handler


/// @brief Handles static file serving for one or more mount points.
///
/// Usage from lundi::app:
///   server.serve_static("/", "./public");
///   server.serve_static("/assets/", "./dist", { .max_age = 86400 });
///
/// The handler is invoked by the dispatch loop after route matching fails.
/// Mount points are checked in registration order — first match wins.
class static_file_handler
{
public:
    /// @brief Register a mount point.
    ///
    /// @param url_prefix  URL prefix to match (e.g. "/" or "/static/")
    /// @param directory   Filesystem directory to serve from
    /// @param opts        Caching and serving options
    /// @throws std::runtime_error if directory does not exist or is not accessible
    void mount(
        const std::string& url_prefix,  // NOLINT(bugprone-easily-swappable-parameters)
        const std::string& directory,
        static_options opts = {} )
    {
        std::error_code ec;
        auto canonical = std::filesystem::canonical( directory, ec );
        if( ec )
        {
            throw std::runtime_error(
                      "serve_static: cannot resolve directory '" + directory + "': " + ec.message() );
        }

        if( !std::filesystem::is_directory( canonical ) )
        {
            throw std::runtime_error(
                      "serve_static: '" + directory + "' is not a directory" );
        }

        std::string dir_str = canonical.string();
        if( !dir_str.empty() && dir_str.back() != '/' )
        {
            dir_str += '/';
        }

        std::string prefix = url_prefix;
        if( !prefix.empty() && prefix.back() != '/' )
        {
            prefix += '/';
        }

        mounts_.push_back( { std::move( prefix ), std::move( dir_str ), std::move( opts ) } );
    }

    /// @brief True if any mount points are registered.
    bool has_mounts() const
    {
        return !mounts_.empty();
    }

    /// @brief Try to serve a static file for the given request path.
    ///
    /// Checks each mount point in registration order. Returns nullopt if no
    /// mount matches or the file does not exist.
    ///
    /// Path traversal protection (three layers):
    ///   1. req_path is URL-decoded by parse_headers(), so %2f..%2f → /../
    ///   2. ".." substring check catches obvious traversal attempts early.
    ///   3. canonical() resolves symlinks — resolved path must be under the
    ///      mount directory. Even symlinks cannot escape.
    std::optional< response > try_serve(
        const std::string& req_path,
        const request& req ) const
    {
        for( const auto& mount : mounts_ )
        {
            auto result = try_mount( mount, req_path, req );
            if( result )
            {
                return result;
            }
        }
        return std::nullopt;
    }

private:
    std::vector< static_mount > mounts_;

    /// @brief Try to serve a file from a single mount point.
    std::optional< response > try_mount(
        const static_mount& mount,
        const std::string& req_path,
        const request& req ) const
    {
        // --- Prefix matching ---

        // Check if the request path starts with this mount's prefix.
        // Special case: prefix "/" matches everything.
        if( mount.prefix != "/" )
        {
            if( req_path.find( mount.prefix ) != 0 &&
                req_path + "/" != mount.prefix )
            {
                return std::nullopt;
            }
        }

        // Extract the relative path after the prefix
        std::string rel;
        if( mount.prefix == "/" )
        {
            rel = req_path.size() > 1 ? req_path.substr( 1 ) : "";
        }
        else
        {
            rel = req_path.substr( mount.prefix.size() );
        }

        // Directory request → try index file
        if( rel.empty() || rel.back() == '/' )
        {
            if( mount.options.index.empty() )
            {
                return std::nullopt;
            }
            rel += mount.options.index;
        }

        // Strip leading slash
        if( !rel.empty() && rel.front() == '/' )
        {
            rel.erase( 0, 1 );
        }

        // --- Path traversal check (layer 2) ---
        if( rel.find( ".." ) != std::string::npos )
        {
            return std::nullopt;
        }

        // --- Resolve filesystem path ---
        auto full = std::filesystem::path( mount.directory ) / rel;

        std::error_code ec;
        if( !std::filesystem::is_regular_file( full, ec ) )
        {
            return std::nullopt;
        }

        // --- Path traversal check (layer 3: canonical resolution) ---
        auto canonical = std::filesystem::canonical( full, ec );
        if( ec )
        {
            return std::nullopt;
        }
        auto canonical_str = canonical.string();
        if( canonical_str.find( mount.directory ) != 0 )
        {
            return std::nullopt;  // symlink or canonical path escapes mount
        }

        // --- File metadata ---
        auto file_size = std::filesystem::file_size( canonical, ec );
        if( ec || file_size > mount.options.max_file_size )
        {
            return std::nullopt;
        }

        auto mtime = std::filesystem::last_write_time( canonical, ec );
        if( ec )
        {
            return std::nullopt;
        }

        // --- Conditional request: ETag (If-None-Match) ---
        std::string etag;
        if( mount.options.etag )
        {
            etag = detail::make_etag( file_size, mtime );

            auto* inm = req.find_header_ci( "if-none-match" );
            if( inm && *inm == etag )
            {
                response res;
                res.status = 304;
                res.set_header( "ETag", etag );
                if( mount.options.max_age > 0 )
                {
                    res.set_header( "Cache-Control",
                                    "public, max-age=" + std::to_string( mount.options.max_age ) );
                }
                return res;
            }
        }

        // --- Conditional request: Last-Modified (If-Modified-Since) ---
        std::string last_mod;
        if( mount.options.last_modified )
        {
            last_mod = detail::format_http_date( mtime );

            auto* ims = req.find_header_ci( "if-modified-since" );
            if( ims )
            {
                auto client_time = detail::parse_http_date( *ims );
                auto sctp = std::chrono::time_point_cast< std::chrono::system_clock::duration >(
                    mtime - std::filesystem::file_time_type::clock::now()
                    + std::chrono::system_clock::now() );
                auto file_time = std::chrono::system_clock::to_time_t( sctp );

                if( client_time != static_cast< std::time_t >( -1 ) && file_time <= client_time )
                {
                    response res;
                    res.status = 304;
                    if( !last_mod.empty() )
                    {
                        res.set_header( "Last-Modified", last_mod );
                    }
                    if( !etag.empty() )
                    {
                        res.set_header( "ETag", etag );
                    }
                    if( mount.options.max_age > 0 )
                    {
                        res.set_header( "Cache-Control",
                                        "public, max-age=" + std::to_string( mount.options.max_age ) );
                    }
                    return res;
                }
            }
        }

        // --- Read file ---
        std::ifstream file( canonical, std::ios::binary );
        if( !file )
        {
            return std::nullopt;
        }

        std::string body( static_cast< size_t >( file_size ), '\0' );
        file.read( body.data(), static_cast< std::streamsize >( file_size ) );
        if( !file )
        {
            return std::nullopt;
        }

        // --- Build response ---
        auto ext = detail::extension_lower( canonical );
        auto mime = detail::mime_for_extension( ext );

        response res;
        res.status = 200;
        res.body = std::move( body );
        res.set_header( "Content-Type", std::string( mime ) );

        if( !etag.empty() )
        {
            res.set_header( "ETag", etag );
        }
        if( !last_mod.empty() )
        {
            res.set_header( "Last-Modified", last_mod );
        }
        if( mount.options.max_age > 0 )
        {
            res.set_header( "Cache-Control",
                            "public, max-age=" + std::to_string( mount.options.max_age ) );
        }
        else
        {
            res.set_header( "Cache-Control", "no-cache" );
        }

        return res;
    }
};

}  // namespace lundi

#endif  // LUNDI_STATIC_FILES_HPP