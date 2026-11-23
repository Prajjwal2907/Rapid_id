#include "HttpGuard.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace rapidaid::http {

namespace {

bool isValidName(const std::string& name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '.' && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

bool endsWith(const std::string& str, const std::string& suffix) {
    if (str.size() < suffix.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

bool HttpGuard::isHostAllowed(const std::string& hostHeader, int port, bool lanMode) {
    if (hostHeader.empty()) {
        return false;
    }

    std::string s = hostHeader;
    // Trim surrounding whitespace
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        start++;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        end--;
    }
    s = s.substr(start, end - start);
    if (s.empty()) {
        return false;
    }

    std::string hostPart;
    std::string portPart;

    if (s.front() == '[') {
        size_t closeBracket = s.find(']');
        if (closeBracket == std::string::npos) {
            return false;
        }
        hostPart = s.substr(0, closeBracket + 1);
        std::string rest = s.substr(closeBracket + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') {
                return false;
            }
            portPart = rest.substr(1);
        }
    } else {
        size_t colon = s.rfind(':');
        if (colon != std::string::npos) {
            hostPart = s.substr(0, colon);
            portPart = s.substr(colon + 1);
        } else {
            hostPart = s;
        }
    }

    std::string expectedPort = std::to_string(port);
    if (!portPart.empty() && portPart != expectedPort) {
        return false;
    }

    if (lanMode) {
        return true;
    }

    std::transform(hostPart.begin(), hostPart.end(), hostPart.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    bool isLoopback = (hostPart == "localhost" || hostPart == "127.0.0.1" || hostPart == "[::1]");
    return isLoopback;
}

std::string HttpGuard::normalizeStaticPath(const std::string& rawPath) {
    if (rawPath.empty() || rawPath.front() != '/') {
        return "";
    }

    // Check for NUL or backslash before decoding
    if (rawPath.find('\0') != std::string::npos || rawPath.find('\\') != std::string::npos) {
        return "";
    }

    // Strip query string and fragment
    std::string pathOnly = rawPath;
    size_t queryPos = pathOnly.find('?');
    if (queryPos != std::string::npos) {
        pathOnly = pathOnly.substr(0, queryPos);
    }
    size_t fragPos = pathOnly.find('#');
    if (fragPos != std::string::npos) {
        pathOnly = pathOnly.substr(0, fragPos);
    }

    // Single-decode percent encoding, rejecting double-encoding and encoded dots/slashes
    std::string decoded;
    decoded.reserve(pathOnly.size());
    for (size_t i = 0; i < pathOnly.size(); ++i) {
        if (pathOnly[i] == '%') {
            if (i + 2 >= pathOnly.size()) {
                return "";
            }
            char h1 = pathOnly[i + 1];
            char h2 = pathOnly[i + 2];
            if (!std::isxdigit(static_cast<unsigned char>(h1)) ||
                !std::isxdigit(static_cast<unsigned char>(h2))) {
                return "";
            }
            int v1 = std::isdigit(static_cast<unsigned char>(h1))
                         ? (h1 - '0')
                         : (std::tolower(static_cast<unsigned char>(h1)) - 'a' + 10);
            int v2 = std::isdigit(static_cast<unsigned char>(h2))
                         ? (h2 - '0')
                         : (std::tolower(static_cast<unsigned char>(h2)) - 'a' + 10);
            char ch = static_cast<char>((v1 << 4) | v2);

            // Reject double encoding
            if (ch == '%') {
                return "";
            }
            // Reject percent-encoded dots or slashes or NUL
            if (ch == '.' || ch == '/' || ch == '\\' || ch == '\0') {
                return "";
            }

            decoded.push_back(ch);
            i += 2;
        } else {
            decoded.push_back(pathOnly[i]);
        }
    }

    // Reject NUL, backslash, or path traversal ("..")
    if (decoded.find('\0') != std::string::npos ||
        decoded.find('\\') != std::string::npos ||
        decoded.find("..") != std::string::npos) {
        return "";
    }

    if (decoded == "/") {
        return "/index.html";
    }

    return decoded;
}

bool HttpGuard::isPathAllowed(const std::string& normalizedPath) {
    if (normalizedPath.empty() || normalizedPath.front() != '/') {
        return false;
    }

    if (normalizedPath == "/" ||
        normalizedPath == "/index.html" ||
        normalizedPath == "/app.js" ||
        normalizedPath == "/unit.js" ||
        normalizedPath == "/board.js" ||
        normalizedPath == "/dispatch.js" ||
        normalizedPath == "/stats.js" ||
        normalizedPath == "/admin.js" ||
        normalizedPath == "/theme.js" ||
        normalizedPath == "/style.css" ||
        normalizedPath == "/styles.css" ||
        normalizedPath == "/modules.css") {
        return true;
    }

    const std::string assetsPrefix = "/assets/";
    if (normalizedPath.rfind(assetsPrefix, 0) == 0) {
        std::string name = normalizedPath.substr(assetsPrefix.size());
        if (!isValidName(name)) {
            return false;
        }
        if (endsWith(name, ".png") || endsWith(name, ".svg") || endsWith(name, ".ico") ||
            endsWith(name, ".jpg") || endsWith(name, ".webp")) {
            return true;
        }
        return false;
    }

    const std::string dataOutPrefix = "/data/out/";
    if (normalizedPath.rfind(dataOutPrefix, 0) == 0) {
        std::string name = normalizedPath.substr(dataOutPrefix.size());
        if (!isValidName(name)) {
            return false;
        }
        if (endsWith(name, ".js") || endsWith(name, ".geojson") || endsWith(name, ".json")) {
            return true;
        }
        return false;
    }

    return false;
}

std::string HttpGuard::contentTypeFor(const std::string& path) {
    if (endsWith(path, ".html") || path == "/") {
        return "text/html; charset=utf-8";
    }
    if (endsWith(path, ".js")) {
        return "application/javascript; charset=utf-8";
    }
    if (endsWith(path, ".css")) {
        return "text/css; charset=utf-8";
    }
    if (endsWith(path, ".json") || endsWith(path, ".geojson")) {
        return "application/json; charset=utf-8";
    }
    if (endsWith(path, ".png")) {
        return "image/png";
    }
    if (endsWith(path, ".jpg") || endsWith(path, ".jpeg")) {
        return "image/jpeg";
    }
    if (endsWith(path, ".svg")) {
        return "image/svg+xml";
    }
    if (endsWith(path, ".ico")) {
        return "image/x-icon";
    }
    if (endsWith(path, ".webp")) {
        return "image/webp";
    }
    return "application/octet-stream";
}

std::string HttpGuard::contentSecurityPolicy() {
    return "default-src 'self'; script-src 'self' https://unpkg.com; "
           "style-src 'self' 'unsafe-inline' https://unpkg.com; "
           "img-src 'self' data: blob: https://unpkg.com https://*.basemaps.cartocdn.com "
           "https://tile.openstreetmap.org https://*.tile.openstreetmap.org; "
           "connect-src 'self' https://unpkg.com https://photon.komoot.io "
           "https://nominatim.openstreetmap.org; "
           "frame-ancestors 'none'; base-uri 'self'; form-action 'self'";
}

std::string HttpGuard::cacheControlFor(const std::string& path) {
    if (path.empty() || path == "/" || endsWith(path, ".html") || path.rfind("/api/", 0) == 0) {
        return "no-store";
    }
    if (path.rfind("/assets/", 0) == 0 || endsWith(path, ".js") || endsWith(path, ".css")) {
        return "public, max-age=3600";
    }
    return "no-store";
}

std::string HttpGuard::securityHeaders(const std::string& path) {
    std::string headers;
    headers.reserve(512);
    headers += "Content-Security-Policy: " + contentSecurityPolicy() + "\r\n";
    headers += "X-Content-Type-Options: nosniff\r\n";
    headers += "X-Frame-Options: DENY\r\n";
    headers += "Referrer-Policy: no-referrer\r\n";
    headers += "Cache-Control: " + cacheControlFor(path) + "\r\n";
    return headers;
}

} // namespace rapidaid::http
