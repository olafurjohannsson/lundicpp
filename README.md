# Lundi

> A blazing-fast, header-only, asynchronous C++20 web framework. Named after the Icelandic puffin.

[![CI](https://github.com/olafurjohannsson/lundicpp/actions/workflows/ci.yml/badge.svg)](https://github.com/olafurjohannsson/lundicpp/actions/workflows/ci.yml)

```cpp
#include <lundi.hpp>

int main() {
    lundi::app server;

    server.get("/hello", [](lundi::request&) -> asio::awaitable<lundi::response> {
        co_return lundi::response::text("Hello from Lundi!");
    });

    server.get("/users/<int:id>", [](lundi::request& req) -> asio::awaitable<lundi::response> {
        int id = req.param_int("id");
        co_return lundi::response::json(R"({"id": )" + std::to_string(id) + "}");
    });

    server.listen({.port = 8080});
}
```

```bash
$ curl http://localhost:8080/hello
Hello from Lundi!

$ curl http://localhost:8080/users/42
{"id": 42}
```

## Why Lundi?

One `#include`, no Boost, no external dependencies, 1.4M req/s, C++20 coroutines give you async I/O that reads like synchronous code.

| Framework | Req/s | Latency (avg) | Language |
|-----------|-------|---------------|----------|
| **Lundi** | **1.4M** | **1.86ms** | C++20 |
| Express.js | 17,630 | 49ms | Node.js |
| FastAPI (uvicorn) | 8,166 | 49ms | Python |

## Installation

Lundi is header-only. Requires a C++20 compiler (GCC 12+, Clang 14+).

**CMake FetchContent (recommended):**

```cmake
include(FetchContent)

FetchContent_Declare(
    lundi
    GIT_REPOSITORY https://github.com/olafurjohannsson/lundi.git
    GIT_TAG main
)
FetchContent_MakeAvailable(lundi)

target_link_libraries(your_app PRIVATE lundi)
```

**Or clone and build:**

```bash
git clone https://github.com/olafurjohannsson/lundi.git
cd lundi && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./examples/hello/hello
```

## Routing

```cpp
// Path parameters
server.get("/users/<int:id>", [](lundi::request& req) -> asio::awaitable<lundi::response> {
    int id = req.param_int("id");
    co_return lundi::response::json(R"({"id": )" + std::to_string(id) + "}");
});

// String parameters
server.get("/files/<string:name>", [](lundi::request& req) -> asio::awaitable<lundi::response> {
    auto name = req.param_str("name");
    co_return lundi::response::text("File: " + name);
});

// Query strings
server.get("/search", [](lundi::request& req) -> asio::awaitable<lundi::response> {
    auto q = req.query("q", "");
    int limit = req.query_int("limit", 10);
    co_return lundi::response::json(
        R"({"query":")" + q + R"(","limit":)" + std::to_string(limit) + "}");
});

// All HTTP methods
server.post("/data", handler);
server.put("/data/<int:id>", handler);
server.del("/data/<int:id>", handler);
server.patch("/data/<int:id>", handler);
```

## Route Groups

```cpp
auto api = server.group("/api/v1");
api.get("/users", list_users);
api.get("/users/<int:id>", get_user);
api.post("/users", create_user);

// Nested groups
auto admin = api.group("/admin");
admin.get("/stats", get_stats);  // GET /api/v1/admin/stats
```

## Middleware

**Global middleware** runs on every request:

```cpp
server.use(lundi::logger());  // logs: GET /hello → 200 (15μs)
server.use(lundi::cors());    // adds CORS headers, handles OPTIONS preflight
```

**Per-route middleware** runs only on specific routes:

```cpp
auto auth = [](lundi::request& req, lundi::next_t& next) -> asio::awaitable<lundi::response> {
    if (req.header("authorization") != "Bearer secret123") {
        co_return lundi::response::json(R"({"error":"unauthorized"})", 401);
    }
    co_return co_await next(req);
};

server.get("/api/secret", {auth}, handler);            // protected
server.get("/api/public", handler);                     // not protected
server.post("/api/admin", {auth, rate_limit}, handler); // multiple middlewares
```

## WebSocket

```cpp
server.ws("/chat", [](lundi::websocket ws) -> asio::awaitable<void> {
    co_await ws.send("welcome");

    while (auto msg = co_await ws.read()) {
        if (msg->is_text()) {
            co_await ws.send("echo: " + msg->data);
        }
    }
});
```

Supports text and binary messages, ping/pong, graceful close with status codes. SHA-1 handshake is self-contained, no OpenSSL dependency (TLS on the way).

## Server-Sent Events (SSE)

```cpp
server.sse("/events", [](lundi::sse_stream stream) -> asio::awaitable<void> {
    int counter = 0;
    for (;;) {
        co_await stream.send(std::to_string(counter++), "tick");
        co_await stream.sleep(1);
    }
});
```

Ideal for LLM token streaming, live dashboards, and real-time notifications. Supports named events, event IDs (for reconnection), and heartbeats.

## Chunked Streaming

```cpp
server.stream("/download", [](lundi::request& req, lundi::chunked_writer writer)
    -> asio::awaitable<void> {
    co_await writer.write("chunk 1\n");
    co_await writer.sleep_ms(100);
    co_await writer.write("chunk 2\n");
    co_await writer.finish();
}, "text/plain");
```

## Cookies

```cpp
server.get("/dashboard", [](lundi::request& req) -> asio::awaitable<lundi::response> {
    auto session = req.cookie("session_id");
    bool logged_in = req.has_cookie("session_id");
    auto all = req.cookies();
    co_return lundi::response::text("session: " + session);
});
```

Cookies are lazily parsed, if your handler never reads cookies, zero overhead.

## File Uploads (Multipart)

```cpp
server.post("/upload", [](lundi::request& req) -> asio::awaitable<lundi::response> {
    auto title = req.form_field("title", "untitled");
    auto* file = req.form_file_ptr("document");

    if (file) {
        // file->filename, file->content_type, file->data
        save_to_disk(file->filename, file->data);
    }

    co_return lundi::response::json(R"({"status":"uploaded"})");
});
```

## Static Files

```cpp
server.serve_static("/", "./public");
```

Automatic MIME detection for html, css, js, json, png, jpg, gif, svg, ico, woff2, wasm, and more. Path traversal protection via canonical path checks.

## Server Options

```cpp
server.listen({
    .port                   = 8080,
    .address                = "0.0.0.0",
    .threads                = 0,                 // 0 = auto-detect cores
    .max_body_size          = 10 * 1024 * 1024,  // 10 MB
    .max_header_size        = 64 * 1024,         // 64 KB
    .max_keepalive_requests = 100,
    .idle_timeout_seconds   = 30,
});
```

## Architecture

- **Main thread** runs the TCP acceptor, signal handler, and date cache timer.
- **Worker threads** (one per core) each have their own `io_context`. Connections distributed via least-connections balancing.
- **No shared state** between workers. No mutexes in the hot path.
- **Graceful shutdown**: `SIGINT`/`SIGTERM` closes the acceptor, stops all workers, drains active connections.

## Features

- C++20 coroutines throughout, no callbacks
- Worker-per-thread, least-connections balancing
- Zero-alloc route matching and response serialization
- Path parameters (`<int:id>`, `<string:name>`) and query strings
- Composable middleware (global + per-route), ships with CORS and logger
- WebSocket (RFC 6455) with built-in SHA-1 — no OpenSSL
- Server-Sent Events with named events, IDs, heartbeats
- Chunked transfer encoding
- Multipart form data and file uploads
- Lazy cookie parsing
- Static file serving with MIME detection and path traversal protection
- Idle timeouts, header/body size limits, keep-alive limits
- Header-only with single `#include <lundi.hpp>`
- 269 Catch2 tests, 600 assertions

## Running Tests

```bash
mkdir build && cd build
cmake .. -DLUNDI_BUILD_TESTS=ON
make -j$(nproc)
./tests/lundi_tests
```

## Project Structure

```
include/
├── lundi.hpp                    ← single include
└── lundi/
    ├── chunked.hpp              ← chunked transfer encoding
    ├── core.hpp                 ← request, response, parsers, cookies, multipart
    ├── middleware.hpp           ← composable chain, cors, logger
    ├── router.hpp               ← pattern matching, zero-alloc path splitting
    ├── server.hpp               ← app, workers, accept loop, dispatch
    ├── sse.hpp                  ← Server-Sent Events
    ├── static_files.hpp         ← Static file handling
    ├── ws.hpp                   ← WebSocket (SHA-1, frames, ping/pong)
    
    └── engine/
        ├── buffer.hpp           ← reusable read/write buffers
        ├── date_cache.hpp       ← cached HTTP Date header
        ├── fast_itoa.hpp        ← stack-based integer-to-string
        ├── header_scan.hpp      ← stack-based integer-to-string
```

## License

MIT
