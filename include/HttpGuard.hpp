#ifndef RAPIDAID_HTTP_HTTPGUARD_HPP
#define RAPIDAID_HTTP_HTTPGUARD_HPP

#include <string>

namespace rapidaid::http {

/**
 * @class HttpGuard
 * @brief Pure, offline-testable HTTP validation and security policy helper.
 */
class HttpGuard {
public:
    /**
     * @brief Validates if the Host header is allowed based on origin policies and network mode.
     * @param hostHeader Host header value from client (e.g. "localhost:8080").
     * @param port Server listening port.
     * @param lanMode True if server is running in LAN mode (--lan).
     * @return True if host is authorized; false otherwise.
     * Failure behavior: Returns false on empty host or mismatched host/port when lanMode is false. Never throws.
     */
    static bool isHostAllowed(const std::string& hostHeader, int port, bool lanMode);

    /**
     * @brief Normalizes a raw request path, performing single-decode, traversal checks, and query trimming.
     * @param rawPath Raw HTTP request URI path (e.g. "/index.html?v=1", "/assets/logo.png").
     * @return Normalized path string starting with '/', or empty string if invalid/unsafe.
     * Failure behavior: Returns empty string ("") if path contains NUL, backslashes, double-encoding,
     *                   percent-encoded dots or slashes, or path traversal (".."). Never throws.
     */
    static std::string normalizeStaticPath(const std::string& rawPath);

    /**
     * @brief Verifies whether the normalized path is in the Contract D static file allowlist.
     * @param normalizedPath Safe, normalized path returned by normalizeStaticPath.
     * @return True if the path is permitted to be served statically; false otherwise.
     * Failure behavior: Returns false for any unlisted path. Never throws.
     */
    static bool isPathAllowed(const std::string& normalizedPath);

    /**
     * @brief Determines the appropriate MIME content-type header for a file path based on its extension.
     * @param path File path or normalized URI path.
     * @return MIME type string (e.g. "text/html; charset=utf-8", "application/javascript; charset=utf-8").
     * Failure behavior: Returns "application/octet-stream" if extension is unknown. Never throws.
     */
    static std::string contentTypeFor(const std::string& path);

    /**
     * @brief Generates standard security response headers formatted with CRLF line endings.
     * @param path Normalized request path (used to determine appropriate Cache-Control policy).
     * @return Formatted HTTP headers string ending with CRLF.
     * Failure behavior: Pure string generation. Never throws.
     */
    static std::string securityHeaders(const std::string& path = "");

    /**
     * @brief Returns the Content-Security-Policy header value string.
     * Failure behavior: Pure accessor. Never throws.
     */
    static std::string contentSecurityPolicy();

    /**
     * @brief Returns the Cache-Control header value appropriate for the specified path.
     * @param path Request path.
     * @return Cache-Control value string.
     * Failure behavior: Returns "no-store" for unrecognized or dynamic paths. Never throws.
     */
    static std::string cacheControlFor(const std::string& path);
};

} // namespace rapidaid::http

#endif // RAPIDAID_HTTP_HTTPGUARD_HPP
