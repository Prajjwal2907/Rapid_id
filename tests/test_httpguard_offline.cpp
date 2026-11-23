#include "HttpGuard.hpp"

#include <iostream>
#include <string>

namespace {

void check(bool cond, const std::string& desc, int& failures) {
    if (cond) {
        std::cout << "[PASS] " << desc << "\n";
    } else {
        std::cout << "[FAIL] " << desc << "\n";
        failures++;
    }
}

} // namespace

int main() {
    using rapidaid::http::HttpGuard;

    int failed = 0;
    std::cout << "Starting HttpGuard Offline Security Tests...\n";

    // =========================================================================
    // 1. Host header validation
    // =========================================================================
    {
        std::cout << "\n--- 1. Host Header Validation ---\n";

        // Allowed local hosts on default port 8080 (lanMode = false)
        check(HttpGuard::isHostAllowed("localhost:8080", 8080, false), "localhost:8080 allowed", failed);
        check(HttpGuard::isHostAllowed("localhost", 8080, false), "localhost without port allowed (defaults to 8080)", failed);
        check(HttpGuard::isHostAllowed("127.0.0.1:8080", 8080, false), "127.0.0.1:8080 allowed", failed);
        check(HttpGuard::isHostAllowed("127.0.0.1", 8080, false), "127.0.0.1 without port allowed", failed);
        check(HttpGuard::isHostAllowed("[::1]:8080", 8080, false), "[::1]:8080 allowed", failed);
        check(HttpGuard::isHostAllowed("[::1]", 8080, false), "[::1] without port allowed", failed);
        check(HttpGuard::isHostAllowed("LOCALHOST:8080", 8080, false), "LOCALHOST:8080 (case insensitive) allowed", failed);

        // Denied external hosts
        check(!HttpGuard::isHostAllowed("evil.com", 8080, false), "evil.com denied", failed);
        check(!HttpGuard::isHostAllowed("evil.com:8080", 8080, false), "evil.com:8080 denied", failed);
        check(!HttpGuard::isHostAllowed("attacker.org:8080", 8080, false), "attacker.org:8080 denied", failed);
        check(!HttpGuard::isHostAllowed("192.168.1.100:8080", 8080, false), "LAN IP denied when lanMode=false", failed);
        check(!HttpGuard::isHostAllowed("", 8080, false), "Empty host header denied", failed);

        // Wrong port denied
        check(!HttpGuard::isHostAllowed("localhost:9090", 8080, false), "localhost:9090 (wrong port) denied", failed);
        check(!HttpGuard::isHostAllowed("127.0.0.1:80", 8080, false), "127.0.0.1:80 (wrong port) denied", failed);
        check(!HttpGuard::isHostAllowed("[::1]:3000", 8080, false), "[::1]:3000 (wrong port) denied", failed);

        // LAN mode (lanMode = true)
        check(HttpGuard::isHostAllowed("192.168.1.50:8080", 8080, true), "LAN mode allows 192.168.1.50:8080", failed);
        check(HttpGuard::isHostAllowed("mydevice.local:8080", 8080, true), "LAN mode allows mydevice.local:8080", failed);
        check(!HttpGuard::isHostAllowed("192.168.1.50:9090", 8080, true), "LAN mode denies wrong port 9090", failed);
    }

    // =========================================================================
    // 2. Static path normalization & traversal rejection
    // =========================================================================
    {
        std::cout << "\n--- 2. Static Path Normalization & Traversal Rejection ---\n";

        // Normal paths
        check(HttpGuard::normalizeStaticPath("/") == "/index.html", "Root '/' normalized to '/index.html'", failed);
        check(HttpGuard::normalizeStaticPath("/app.js") == "/app.js", "Valid path /app.js preserved", failed);
        check(HttpGuard::normalizeStaticPath("/styles.css") == "/styles.css", "Valid path /styles.css preserved", failed);

        // Stripping query strings and fragments
        check(HttpGuard::normalizeStaticPath("/app.js?v=1.2.3") == "/app.js", "Query string stripped", failed);
        check(HttpGuard::normalizeStaticPath("/index.html#hash") == "/index.html", "Fragment stripped", failed);
        check(HttpGuard::normalizeStaticPath("/styles.css?foo=bar#baz") == "/styles.css", "Query and fragment stripped", failed);

        // Directory traversal rejection
        check(HttpGuard::normalizeStaticPath("../secret.txt").empty(), "Direct '..' rejected", failed);
        check(HttpGuard::normalizeStaticPath("/../secret.txt").empty(), "Leading '/..' rejected", failed);
        check(HttpGuard::normalizeStaticPath("/assets/../../secret.txt").empty(), "Embedded '/../../' rejected", failed);
        check(HttpGuard::normalizeStaticPath("/assets/..").empty(), "Trailing '/..' rejected", failed);

        // Backslash traversal rejection
        check(HttpGuard::normalizeStaticPath("..\\secret.txt").empty(), "Backslash traversal '..\\' rejected", failed);
        check(HttpGuard::normalizeStaticPath("/assets/..\\secret.txt").empty(), "Mixed backslash traversal rejected", failed);

        // URL encoded traversal rejection
        check(HttpGuard::normalizeStaticPath("/%2e%2e/secret.txt").empty(), "Encoded %2e%2e rejected", failed);
        check(HttpGuard::normalizeStaticPath("/%2e%2e%2fsecret.txt").empty(), "Encoded %2e%2e%2f rejected", failed);
        check(HttpGuard::normalizeStaticPath("/%252e%252e/secret.txt").empty(), "Double encoded %252e rejected", failed);
        check(HttpGuard::normalizeStaticPath("/assets%2f..%2fsecret.txt").empty(), "Encoded slash traversal rejected", failed);

        // Null byte injection rejection
        check(HttpGuard::normalizeStaticPath("/app.js%00.html").empty(), "Null byte injection rejected", failed);
    }

    // =========================================================================
    // 3. Static allowlist enforcement (Contract D)
    // =========================================================================
    {
        std::cout << "\n--- 3. Static Allowlist Enforcement ---\n";

        // Allowed Contract D static paths
        check(HttpGuard::isPathAllowed("/index.html"), "/index.html is allowed", failed);
        check(HttpGuard::isPathAllowed("/app.js"), "/app.js is allowed", failed);
        check(HttpGuard::isPathAllowed("/unit.js"), "/unit.js is allowed", failed);
        check(HttpGuard::isPathAllowed("/board.js"), "/board.js is allowed", failed);
        check(HttpGuard::isPathAllowed("/dispatch.js"), "/dispatch.js is allowed", failed);
        check(HttpGuard::isPathAllowed("/stats.js"), "/stats.js is allowed", failed);
        check(HttpGuard::isPathAllowed("/admin.js"), "/admin.js is allowed", failed);
        check(HttpGuard::isPathAllowed("/theme.js"), "/theme.js is allowed", failed);
        check(HttpGuard::isPathAllowed("/style.css"), "/style.css is allowed", failed);
        check(HttpGuard::isPathAllowed("/styles.css"), "/styles.css is allowed", failed);
        check(HttpGuard::isPathAllowed("/modules.css"), "/modules.css is allowed", failed);
        check(HttpGuard::isPathAllowed("/data/out/area.json"), "/data/out/area.json is allowed", failed);
        check(HttpGuard::isPathAllowed("/data/out/roads.geojson"), "/data/out/roads.geojson is allowed", failed);
        check(HttpGuard::isPathAllowed("/data/out/places.geojson"), "/data/out/places.geojson is allowed", failed);
        check(HttpGuard::isPathAllowed("/data/out/roads.js"), "/data/out/roads.js is allowed", failed);
        check(HttpGuard::isPathAllowed("/data/out/places.js"), "/data/out/places.js is allowed", failed);

        // Allowed assets matching [A-Za-z0-9._-]+.(png|svg|ico|jpg|webp)
        check(HttpGuard::isPathAllowed("/assets/logo.png"), "/assets/logo.png is allowed", failed);
        check(HttpGuard::isPathAllowed("/assets/marker.svg"), "/assets/marker.svg is allowed", failed);
        check(HttpGuard::isPathAllowed("/assets/favicon.ico"), "/assets/favicon.ico is allowed", failed);
        check(HttpGuard::isPathAllowed("/assets/photo.webp"), "/assets/photo.webp is allowed", failed);

        // Disallowed asset extensions
        check(!HttpGuard::isPathAllowed("/assets/font.woff2"), "woff2 extension in /assets/ is disallowed per Contract D", failed);
        check(!HttpGuard::isPathAllowed("/assets/evil.exe"), "Executable in /assets/ is denied", failed);
        check(!HttpGuard::isPathAllowed("/assets/script.sh"), "Shell script in /assets/ is denied", failed);

        // Disallowed sensitive files
        check(!HttpGuard::isPathAllowed("/data/users.txt"), "Sensitive /data/users.txt is denied", failed);
        check(!HttpGuard::isPathAllowed("/users.txt"), "Sensitive /users.txt is denied", failed);
        check(!HttpGuard::isPathAllowed("/auth.key"), "Sensitive /auth.key is denied", failed);
        check(!HttpGuard::isPathAllowed("/data/units.json"), "Sensitive /data/units.json is denied", failed);
        check(!HttpGuard::isPathAllowed("/data/incidents.json"), "Sensitive /data/incidents.json is denied", failed);
        check(!HttpGuard::isPathAllowed("/Makefile"), "/Makefile is denied", failed);
        check(!HttpGuard::isPathAllowed("/main.cpp"), "/main.cpp is denied", failed);
        check(!HttpGuard::isPathAllowed(""), "Empty path is denied", failed);
    }

    // =========================================================================
    // 4. Content-Type determination
    // =========================================================================
    {
        std::cout << "\n--- 4. Content-Type Determination ---\n";

        check(HttpGuard::contentTypeFor("/index.html") == "text/html; charset=utf-8", "Content-Type for .html", failed);
        check(HttpGuard::contentTypeFor("/app.js") == "application/javascript; charset=utf-8", "Content-Type for .js", failed);
        check(HttpGuard::contentTypeFor("/styles.css") == "text/css; charset=utf-8", "Content-Type for .css", failed);
        check(HttpGuard::contentTypeFor("/data/out/area.json") == "application/json; charset=utf-8", "Content-Type for .json", failed);
        check(HttpGuard::contentTypeFor("/data/out/roads.geojson") == "application/json; charset=utf-8", "Content-Type for .geojson", failed);
        check(HttpGuard::contentTypeFor("/assets/logo.png") == "image/png", "Content-Type for .png", failed);
        check(HttpGuard::contentTypeFor("/assets/pic.jpg") == "image/jpeg", "Content-Type for .jpg", failed);
        check(HttpGuard::contentTypeFor("/assets/pic.jpeg") == "image/jpeg", "Content-Type for .jpeg", failed);
        check(HttpGuard::contentTypeFor("/assets/icon.svg") == "image/svg+xml", "Content-Type for .svg", failed);
        check(HttpGuard::contentTypeFor("/assets/favicon.ico") == "image/x-icon", "Content-Type for .ico", failed);
        check(HttpGuard::contentTypeFor("/assets/photo.webp") == "image/webp", "Content-Type for .webp", failed);
        check(HttpGuard::contentTypeFor("/assets/unknown.bin") == "application/octet-stream", "Content-Type default octet-stream", failed);
    }

    // =========================================================================
    // 5. Security headers
    // =========================================================================
    {
        std::cout << "\n--- 5. Security Headers ---\n";

        std::string htmlHeaders = HttpGuard::securityHeaders("/index.html");
        check(htmlHeaders.find("Content-Security-Policy:") != std::string::npos, "Headers contain CSP", failed);
        std::string csp = HttpGuard::contentSecurityPolicy();
        check(csp.find("https://unpkg.com") != std::string::npos, "CSP allows Leaflet assets from unpkg", failed);
        check(csp.find("connect-src 'self' https://unpkg.com") != std::string::npos, "CSP allows Leaflet source-map connections from unpkg", failed);
        check(csp.find("https://*.tile.openstreetmap.org") != std::string::npos, "CSP allows OpenStreetMap tile hosts", failed);
        check(csp.find("https://*.basemaps.cartocdn.com") != std::string::npos, "CSP allows CARTO tile hosts", failed);
        check(htmlHeaders.find("X-Content-Type-Options: nosniff") != std::string::npos, "Headers contain nosniff", failed);
        check(htmlHeaders.find("X-Frame-Options: DENY") != std::string::npos, "Headers contain DENY frame options", failed);
        check(htmlHeaders.find("Referrer-Policy: no-referrer") != std::string::npos, "Headers contain Referrer-Policy", failed);
        check(htmlHeaders.find("Cache-Control: no-store") != std::string::npos, "HTML headers contain Cache-Control: no-store", failed);

        std::string assetHeaders = HttpGuard::securityHeaders("/assets/logo.png");
        check(assetHeaders.find("Cache-Control: public, max-age=3600") != std::string::npos, "Asset headers contain Cache-Control: public, max-age=3600", failed);

        // Ensure no CORS wildcard leak
        check(htmlHeaders.find("Access-Control-Allow-Origin") == std::string::npos, "Headers do NOT contain CORS wildcard", failed);
    }

    std::cout << "\nHttpGuard Offline Test Summary: "
              << (failed == 0 ? "ALL PASSED (0 failures)" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed == 0 ? 0 : 1;
}
