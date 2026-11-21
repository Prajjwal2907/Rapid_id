#include "DataCache.hpp"
#include "FileStore.hpp"
#include "HttpGuard.hpp"
#include "Login.hpp"
#include "UiBridge.hpp"
#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#define closesocket close
#define SOCKET int
#define INVALID_SOCKET -1
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace {

std::atomic<int> g_activeConnections{0};

void openBrowser(const std::string& url) {
#if defined(_WIN32)
    std::string cmd = "start \"\" \"" + url + "\"";
    std::system(cmd.c_str());
#elif defined(__APPLE__)
    std::string cmd = "open \"" + url + "\"";
    std::system(cmd.c_str());
#else
    std::string cmd = "xdg-open \"" + url + "\"";
    std::system(cmd.c_str());
#endif
}

void sendHttpResponse(SOCKET client, const std::string& status, const std::string& contentType, const std::string& body, const std::string& path = "") {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << rapidaid::http::HttpGuard::securityHeaders(path)
        << "\r\n"
        << body;
    std::string resp = oss.str();
#if defined(MSG_NOSIGNAL)
    send(client, resp.c_str(), static_cast<int>(resp.size()), MSG_NOSIGNAL);
#else
    send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
#endif
}

void handleClient(SOCKET client, Auth& auth, rapidaid::data::DataCache& cache, int port, bool lanMode) {
    // Cap concurrent connections at 64
    if (g_activeConnections.fetch_add(1) >= 64) {
        g_activeConnections.fetch_sub(1);
        sendHttpResponse(client, "503 Service Unavailable", "text/plain", "Service Unavailable", "/");
        closesocket(client);
        return;
    }
    struct ConnGuard {
        ~ConnGuard() { g_activeConnections.fetch_sub(1); }
    } connGuard;

    // Set socket receive timeout to 10 seconds
#if defined(_WIN32)
    DWORD rcvTimeout = 10000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout), sizeof(rcvTimeout));
#else
    struct timeval rcvTimeout;
    rcvTimeout.tv_sec = 10;
    rcvTimeout.tv_usec = 0;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout, sizeof(rcvTimeout));
#endif

    std::string req;
    char buf[4096];
    size_t headerEnd = std::string::npos;
    size_t contentLength = 0;
    bool hasContentLength = false;

    // Read full request headers (max 16 KB) and Content-Length body (max 1 MB)
    while (true) {
        int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, static_cast<std::size_t>(n));

        if (headerEnd == std::string::npos) {
            headerEnd = req.find("\r\n\r\n");
            if (headerEnd == std::string::npos && req.size() > 16384) { // reject headers > 16 KB
                sendHttpResponse(client, "431 Request Header Fields Too Large", "text/plain", "Request Header Fields Too Large", "/");
                closesocket(client);
                return;
            }
            if (headerEnd != std::string::npos) {
                std::string headers = req.substr(0, headerEnd);
                std::string clTag = "content-length:";
                std::string lowerHeaders = headers;
                std::transform(lowerHeaders.begin(), lowerHeaders.end(), lowerHeaders.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                auto pos = lowerHeaders.find(clTag);
                if (pos != std::string::npos) {
                    auto lineEnd = headers.find("\r\n", pos);
                    std::string clVal = headers.substr(pos + clTag.size(),
                        lineEnd == std::string::npos ? std::string::npos : lineEnd - (pos + clTag.size()));
                    contentLength = std::strtoul(clVal.c_str(), nullptr, 10);
                    hasContentLength = true;
                }
            }
        }

        if (hasContentLength) {
            if (contentLength > 1024 * 1024) { // reject bodies > 1 MB
                sendHttpResponse(client, "413 Payload Too Large", "text/plain", "Payload Too Large", "/");
                closesocket(client);
                return;
            }
            if (req.size() >= headerEnd + 4 + contentLength) {
                break;
            }
        } else if (headerEnd != std::string::npos) {
            break;
        }
    }

    if (req.empty() || headerEnd == std::string::npos) {
        closesocket(client);
        return;
    }

    std::string headers = req.substr(0, headerEnd);

    // Host header validation
    std::string hostHeader;
    {
        std::string lowerHeaders = headers;
        std::transform(lowerHeaders.begin(), lowerHeaders.end(), lowerHeaders.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string hostTag = "host:";
        auto pos = lowerHeaders.find(hostTag);
        if (pos != std::string::npos) {
            auto lineEnd = headers.find("\r\n", pos);
            std::string hVal = headers.substr(pos + hostTag.size(),
                lineEnd == std::string::npos ? std::string::npos : lineEnd - (pos + hostTag.size()));
            size_t s = 0;
            while (s < hVal.size() && std::isspace(static_cast<unsigned char>(hVal[s]))) s++;
            size_t e = hVal.size();
            while (e > s && std::isspace(static_cast<unsigned char>(hVal[e - 1]))) e--;
            hostHeader = hVal.substr(s, e - s);
        }
    }

    if (hostHeader.empty() || !rapidaid::http::HttpGuard::isHostAllowed(hostHeader, port, lanMode)) {
        sendHttpResponse(client, "403 Forbidden", "text/plain", "Forbidden: Invalid Host header", "/");
        closesocket(client);
        return;
    }

    std::istringstream iss(req);
    std::string method, path;
    iss >> method >> path;

    if (method == "OPTIONS") {
        sendHttpResponse(client, "405 Method Not Allowed", "text/plain", "Method Not Allowed", path);
        closesocket(client);
        return;
    }

    if (method != "GET" && method != "POST") {
        sendHttpResponse(client, "405 Method Not Allowed", "text/plain", "Method Not Allowed", path);
        closesocket(client);
        return;
    }

    if (method == "GET" && path.rfind("/api/", 0) == 0) {
        sendHttpResponse(client, "405 Method Not Allowed", "text/plain", "Method Not Allowed", path);
        closesocket(client);
        return;
    }

    if (method == "POST" && path.rfind("/api/", 0) != 0) {
        sendHttpResponse(client, "405 Method Not Allowed", "text/plain", "Method Not Allowed", path);
        closesocket(client);
        return;
    }

    if (method == "GET") {
        std::string normPath = rapidaid::http::HttpGuard::normalizeStaticPath(path);
        if (normPath.empty() || !rapidaid::http::HttpGuard::isPathAllowed(normPath)) {
            sendHttpResponse(client, "404 Not Found", "text/plain", "Not Found", normPath);
            closesocket(client);
            return;
        }

        std::string contentType = rapidaid::http::HttpGuard::contentTypeFor(normPath);
        if (normPath == "/index.html") {
            std::string html = rapidaid::io::FileStore::load("index.html");
            if (html.empty()) {
                std::string msg = "500 Internal Server Error: Failed to load index.html in directory " +
                                  std::filesystem::current_path().string();
                sendHttpResponse(client, "500 Internal Server Error", "text/plain", msg, normPath);
            } else {
                sendHttpResponse(client, "200 OK", contentType, html, normPath);
            }
        } else {
            std::string relPath = normPath.substr(1);
            std::string content = rapidaid::io::FileStore::load(relPath);
            if (content.empty()) {
                sendHttpResponse(client, "404 Not Found", "text/plain", "Not Found", normPath);
            } else {
                sendHttpResponse(client, "200 OK", contentType, content, normPath);
            }
        }
    } else { // POST /api/*
        if (path == "/api/bridge") {
            std::string body = req.substr(headerEnd + 4);
            std::string result = rapidaid::ui::handleBridgeMessage(auth, cache, body, "data/out");
            sendHttpResponse(client, "200 OK", "application/json; charset=utf-8", result, path);
        } else if (path == "/api/create_area" || path == "/api/pipeline") {
            std::string body = req.substr(headerEnd + 4);
            std::string token;
            try {
                nlohmann::json j = nlohmann::json::parse(body);
                if (j.contains("token") && j["token"].is_string()) {
                    token = j["token"].get<std::string>();
                }
            } catch (...) {}

            if (!rapidaid::ui::isTokenAuthorized(token, "dispatcher")) {
                sendHttpResponse(client, "403 Forbidden", "application/json; charset=utf-8", "{\"ok\":false,\"error\":\"Not allowed\"}", path);
                closesocket(client);
                return;
            }

            std::string result = rapidaid::ui::handleCreateAreaRequest(cache, body, "data/out");
            sendHttpResponse(client, "200 OK", "application/json; charset=utf-8", result, path);
        } else {
            sendHttpResponse(client, "404 Not Found", "text/plain", "Not Found", path);
        }
    }

    closesocket(client);
}

} // anonymous namespace

int main(int argc, char* argv[]) {
#if !defined(_WIN32)
    signal(SIGPIPE, SIG_IGN);
#endif

    bool lanMode = false;
    std::string jsonArg;
    std::string rootDir;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--lan") {
            lanMode = true;
        } else if (arg == "--root" && i + 1 < argc) {
            rootDir = argv[++i];
        } else if (!arg.empty() && (arg[0] == '{' || arg[0] == '[')) {
            jsonArg = arg;
        }
    }

    std::cout << "Current working directory: " << std::filesystem::current_path().string() << "\n";
    if (!rootDir.empty()) {
        std::error_code ec;
        std::filesystem::current_path(rootDir, ec);
    } else {
#if defined(_WIN32)
        char exePath[MAX_PATH];
        if (GetModuleFileNameA(NULL, exePath, MAX_PATH) != 0) {
            std::filesystem::path p(exePath);
            std::filesystem::path projectRoot = p.parent_path().parent_path();
            std::error_code ec;
            std::filesystem::current_path(projectRoot, ec);
        }
#elif defined(__APPLE__)
        char exePath[1024];
        uint32_t size = sizeof(exePath);
        if (_NSGetExecutablePath(exePath, &size) == 0) {
            std::filesystem::path p(exePath);
            std::filesystem::path projectRoot = p.parent_path().parent_path();
            std::error_code ec;
            std::filesystem::current_path(projectRoot, ec);
        }
#else
        char exePath[1024];
        ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
        if (len != -1) {
            exePath[len] = '\0';
            std::filesystem::path p(exePath);
            std::filesystem::path projectRoot = p.parent_path().parent_path();
            std::error_code ec;
            std::filesystem::current_path(projectRoot, ec);
        }
#endif
    }
    std::cout << "Project working directory: " << std::filesystem::current_path().string() << "\n";

    std::error_code ec;
    std::filesystem::create_directories("data", ec);

    Auth auth("users.txt");
    if (!auth.ready()) {
        std::cerr << "Auth not ready: " << auth.notReadyReason() << "\n";
        return 1;
    }

    // Mode 1: Command-line single JSON argument
    if (!jsonArg.empty()) {
        rapidaid::data::DataCache cache("data", false);
        std::cout << rapidaid::ui::handleBridgeMessage(auth, cache, jsonArg, "data/out") << "\n";
        return 0;
    }

    // Sync pre-existing GeoJSON files to JavaScript wrappers
    std::string placesGeo = rapidaid::io::FileStore::load("data/out/places.geojson");
    if (!placesGeo.empty()) {
        rapidaid::io::FileStore::save("data/out/places.js", "window.RAPIDAID_PLACES = " + placesGeo + ";");
    }
    std::string roadsGeo = rapidaid::io::FileStore::load("data/out/roads.geojson");
    if (!roadsGeo.empty()) {
        rapidaid::io::FileStore::save("data/out/roads.js", "window.RAPIDAID_ROADS = " + roadsGeo + ";");
    }

#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed. Opening index.html directly.\n";
        openBrowser("index.html");
        return 1;
    }
#endif

    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "Socket creation failed. Opening index.html directly.\n";
        openBrowser("index.html");
#if defined(_WIN32)
        WSACleanup();
#endif
        return 1;
    }

#if defined(_WIN32)
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#endif

    int port = 8080;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (lanMode) {
        std::cout << "Warning: Binding to all network interfaces (--lan). Passwords travel unencrypted over HTTP.\n";
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        port = 8081;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            std::cerr << "Bind failed on ports 8080/8081. Opening index.html directly.\n";
            closesocket(server);
            openBrowser("index.html");
#if defined(_WIN32)
            WSACleanup();
#endif
            return 1;
        }
    }

    if (listen(server, 10) != 0) {
        std::cerr << "Listen failed. Opening index.html directly.\n";
        closesocket(server);
        openBrowser("index.html");
#if defined(_WIN32)
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "========================================================\n"
              << "  RapidAid Server started at http://localhost:" << port << "\n"
              << "  Bridge & Pipeline API ready for Dispatcher UI\n"
              << "========================================================\n";

    // Launch UI in browser
    openBrowser("http://localhost:" + std::to_string(port));

    // Background thread calling tickDispatch() once per second
    std::atomic<bool> bgRunning{true};
    std::thread bgDispatchThread([&bgRunning]() {
        while (bgRunning) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            rapidaid::ui::tickDispatch();
        }
    });
    bgDispatchThread.detach();

    rapidaid::data::DataCache cache("data", false);

    while (true) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            break;
        }
        std::thread([client, &auth, &cache, port, lanMode]() {
            handleClient(client, auth, cache, port, lanMode);
        }).detach();
    }

    closesocket(server);
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}