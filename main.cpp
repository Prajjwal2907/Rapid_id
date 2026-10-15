#include "DataCache.hpp"
#include "FileStore.hpp"
#include "Login.hpp"
#include "UiBridge.hpp"
#include "json.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <sstream>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#define closesocket close
#define SOCKET int
#define INVALID_SOCKET -1
#endif

namespace {

void sendHttpResponse(SOCKET client, const std::string& status, const std::string& contentType, const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        << "Access-Control-Allow-Headers: Content-Type\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    std::string resp = oss.str();
    send(client, resp.c_str(), static_cast<int>(resp.size()), 0);
}

void handleClient(SOCKET client, Auth& auth, rapidaid::data::DataCache& cache) {
    char buf[8192];
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        closesocket(client);
        return;
    }
    buf[n] = '\0';
    std::string req(buf, static_cast<std::size_t>(n));

    std::istringstream iss(req);
    std::string method, path;
    iss >> method >> path;

    if (method == "OPTIONS") {
        sendHttpResponse(client, "200 OK", "text/plain", "");
        closesocket(client);
        return;
    }

    if (path == "/" || path == "/index.html") {
        std::string html = rapidaid::io::FileStore::load("index.html");
        sendHttpResponse(client, "200 OK", "text/html; charset=utf-8", html);
    } else if (path == "/app.js") {
        std::string js = rapidaid::io::FileStore::load("app.js");
        sendHttpResponse(client, "200 OK", "application/javascript; charset=utf-8", js);
    } else if (path.rfind("/data/out/", 0) == 0) {
        std::string filePath = path.substr(1);
        std::string content = rapidaid::io::FileStore::load(filePath);
        sendHttpResponse(client, "200 OK", "application/json; charset=utf-8", content);
    } else if (path == "/api/bridge" || path == "/api/create_area" || path == "/api/pipeline") {
        std::string body;
        auto pos = req.find("\r\n\r\n");
        if (pos != std::string::npos) {
            body = req.substr(pos + 4);
        }
        std::string result;
        if (path == "/api/bridge") {
            result = rapidaid::ui::handleBridgeMessage(auth, cache, body, "data/out");
        } else {
            result = rapidaid::ui::handleCreateAreaRequest(cache, body, "data/out");
        }
        sendHttpResponse(client, "200 OK", "application/json; charset=utf-8", result);
    } else {
        sendHttpResponse(client, "404 Not Found", "text/plain", "Not Found");
    }

    closesocket(client);
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    // Mode 1: Command-line single JSON argument
    // Example: ./rapidaid.exe '{"action":"login","payload":{"username":"admin","password":"admin123"}}'
    if (argc >= 2) {
        Auth auth("users.txt");
        rapidaid::data::DataCache cache("data", false);
        std::string input = argv[1];
        std::cout << rapidaid::ui::handleBridgeMessage(auth, cache, input, "data/out") << "\n";
        return 0;
    }

    // Sync any pre-existing GeoJSON files to JavaScript wrappers for instantaneous browser loading
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
        std::system("start index.html");
        return 1;
    }
#endif

    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "Socket creation failed. Opening index.html directly.\n";
#if defined(_WIN32)
        std::system("start index.html");
        WSACleanup();
#else
        std::system("xdg-open index.html");
#endif
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    int port = 8080;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        // Port 8080 in use; try port 8081
        port = 8081;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            std::cerr << "Bind failed on ports 8080/8081. Opening index.html directly.\n";
            closesocket(server);
#if defined(_WIN32)
            std::system("start index.html");
            WSACleanup();
#endif
            return 1;
        }
    }

    if (listen(server, 10) != 0) {
        std::cerr << "Listen failed. Opening index.html directly.\n";
        closesocket(server);
#if defined(_WIN32)
        std::system("start index.html");
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "========================================================\n"
              << "  RapidAid Server started at http://localhost:" << port << "\n"
              << "  Bridge & Pipeline API ready for Dispatcher UI\n"
              << "========================================================\n";

    // Launch UI in browser pointing to local server
    std::string launchCmd = "start http://localhost:" + std::to_string(port);
#if defined(_WIN32)
    std::system(launchCmd.c_str());
#elif defined(__APPLE__)
    std::system(("open http://localhost:" + std::to_string(port)).c_str());
#else
    std::system(("xdg-open http://localhost:" + std::to_string(port)).c_str());
#endif

    // Lazy load auth and cache for request processing
    Auth auth("users.txt");
    rapidaid::data::DataCache cache("data", false);

    while (true) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            break;
        }
        handleClient(client, auth, cache);
    }

    closesocket(server);
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}