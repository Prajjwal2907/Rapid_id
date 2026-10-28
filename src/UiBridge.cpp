#include "UiBridge.hpp"
#include "FileStore.hpp"
#include "Graph.hpp"
#include "GraphBuilder.hpp"
#include "GeoJsonWriter.hpp"
#include "UnitRegistry.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace rapidaid::ui {

namespace {

struct StoredIncident {
    std::string id;
    std::string title;
    std::string location;
    std::string type;
    std::string priority;
    std::string timestamp;
    std::string status;
    std::string notes;
};

struct SessionData {
    std::string username;
    std::string role;
};

// Global mutexes separating domain state from pipeline computation (B7)
std::mutex g_stateMutex;
std::mutex g_pipelineMutex;

// In-memory sessions: token -> SessionData
std::unordered_map<std::string, SessionData> g_sessions;

// Global UnitRegistry instance
UnitRegistry g_registry;

// In-memory mock storage for incidents
std::vector<StoredIncident>& getIncidentStore() {
    static std::vector<StoredIncident> store;
    return store;
}

std::string generateSessionToken() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t p1 = dist(gen);
    uint64_t p2 = dist(gen);
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::hex << std::setfill('0') << std::setw(16) << p1 << std::setw(16) << p2;
    return ss.str();
}

std::string currentTimestampIso() {
    std::time_t t = std::time(nullptr);
    std::tm buf{};
#if defined(_WIN32)
    localtime_s(&buf, &t);
#else
    localtime_r(&t, &buf);
#endif
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

rapidaid::osm::Category parseCategoryString(const std::string& cat) {
    if (cat == "clinic") return rapidaid::osm::Category::Clinic;
    if (cat == "pharmacy") return rapidaid::osm::Category::Pharmacy;
    if (cat == "fire_station" || cat == "firestation") return rapidaid::osm::Category::FireStation;
    if (cat == "police") return rapidaid::osm::Category::Police;
    if (cat == "shelter") return rapidaid::osm::Category::Shelter;
    if (cat == "water") return rapidaid::osm::Category::Water;
    return rapidaid::osm::Category::Hospital;
}

} // namespace

bool isTokenAuthorized(const std::string& token, const std::string& requiredRole) {
    if (token.empty()) return false;
    std::lock_guard<std::mutex> lock(g_stateMutex);
    auto it = g_sessions.find(token);
    if (it == g_sessions.end()) return false;
    if (requiredRole == "dispatcher") {
        return (it->second.role == "dispatcher" || it->second.role == "admin");
    }
    return it->second.role == requiredRole;
}

void resetBridgeState() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_sessions.clear();
    g_registry.clear();
    g_registry.setDataPath("data/units.json");
}

bool checkGeoJsonExists(const std::string& outDir) {
    std::error_code ec;
    fs::path dir(outDir);
    fs::path roads = dir / "roads.geojson";
    fs::path places = dir / "places.geojson";

    bool roadsOk = fs::exists(roads, ec) && !ec && fs::file_size(roads, ec) > 0;
    bool placesOk = fs::exists(places, ec) && !ec && fs::file_size(places, ec) > 0;
    return roadsOk || placesOk;
}

std::string handleAreaStatusRequest(const std::string& outDir) {
    std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
    try {
        std::error_code ec;
        fs::path dir(outDir);
        fs::path roads = dir / "roads.geojson";
        fs::path places = dir / "places.geojson";

        bool roadsExists = fs::exists(roads, ec) && !ec && fs::file_size(roads, ec) > 0;
        bool placesExists = fs::exists(places, ec) && !ec && fs::file_size(places, ec) > 0;
        bool exists = roadsExists || placesExists;

        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["exists"] = exists;
        resp["roadsExists"] = roadsExists;
        resp["placesExists"] = placesExists;
        resp["roadsFile"] = roads.lexically_normal().string();
        resp["placesFile"] = places.lexically_normal().string();
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["exists"] = false;
        resp["roadsExists"] = false;
        resp["placesExists"] = false;
        resp["roadsFile"] = outDir + "/roads.geojson";
        resp["placesFile"] = outDir + "/places.geojson";
        return resp.dump();
    }
}

std::string handleGeocodeRequest(rapidaid::data::DataCache& cache, const std::string& requestJson) {
    std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(requestJson);
        } catch (...) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        std::string query;
        if (j.is_object() && j.contains("query") && j["query"].is_string()) {
            query = j["query"].get<std::string>();
        } else if (j.is_object() && j.contains("name") && j["name"].is_string()) {
            query = j["name"].get<std::string>();
        } else {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (query.empty()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Query cannot be empty";
            return resp.dump();
        }

        auto loc = cache.geocode(query);
        if (!loc.has_value()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Location not found";
            return resp.dump();
        }

        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["query"] = query;
        resp["lat"] = loc->lat;
        resp["lon"] = loc->lon;
        resp["displayName"] = loc->displayName;
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Geocoding failure";
        return resp.dump();
    }
}

std::string handleCreateAreaRequest(rapidaid::data::DataCache& cache, const std::string& requestJson, const std::string& outDir) {
    std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(requestJson);
        } catch (...) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j.is_object()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        std::string name;
        if (j.contains("name") && j["name"].is_string()) {
            name = j["name"].get<std::string>();
        } else if (j.contains("query") && j["query"].is_string()) {
            name = j["query"].get<std::string>();
        } else {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Missing area name";
            return resp.dump();
        }

        int radius = 3000;
        if (j.contains("radius") && j["radius"].is_number()) {
            radius = j["radius"].get<int>();
        }
        if (radius <= 0) radius = 3000;

        double lat = 0.0;
        double lon = 0.0;
        std::string displayName = name;

        if (j.contains("lat") && j["lat"].is_number() && j.contains("lon") && j["lon"].is_number()) {
            lat = j["lat"].get<double>();
            lon = j["lon"].get<double>();
            if (j.contains("displayName") && j["displayName"].is_string()) {
                displayName = j["displayName"].get<std::string>();
            }
        } else {
            auto loc = cache.geocode(name);
            if (!loc.has_value()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Geocoding failed for: " + name;
                return resp.dump();
            }
            lat = loc->lat;
            lon = loc->lon;
            displayName = loc->displayName;
        }

        // 1. Fetch places & roads
        std::vector<rapidaid::osm::Place> places;
        std::string catStr = "all";
        if (!j.contains("category") || j["category"] == "all") {
            const rapidaid::osm::Category allCats[] = {
                rapidaid::osm::Category::Hospital,
                rapidaid::osm::Category::FireStation,
                rapidaid::osm::Category::Police,
                rapidaid::osm::Category::Clinic,
                rapidaid::osm::Category::Pharmacy,
                rapidaid::osm::Category::Shelter
            };
            for (auto c : allCats) {
                auto p = cache.places(c, lat, lon, radius);
                places.insert(places.end(), p.begin(), p.end());
            }
        } else {
            catStr = "hospital";
            if (j["category"].is_string()) {
                catStr = j["category"].get<std::string>();
            }
            rapidaid::osm::Category cat = parseCategoryString(catStr);
            places = cache.places(cat, lat, lon, radius);
        }
        auto roads = cache.roads(lat, lon, radius);

        // 2. Build graph & snap places
        rapidaid::graph::Graph graph = rapidaid::graph::GraphBuilder::build(roads);
        auto snapped = rapidaid::graph::GraphBuilder::snapPlaces(graph, places);

        int snappedCount = 0;
        for (const auto& sp : snapped) {
            if (sp.snapped) snappedCount++;
        }

        // 3. Serialize GeoJSON
        std::string roadsGeoJson = rapidaid::geojson::GeoJsonWriter::roadsCollection(graph);
        std::string placesGeoJson = rapidaid::geojson::GeoJsonWriter::snappedPlacesCollection(snapped);

        // 4. Save permanently to disk
        fs::path dir(outDir);
        std::error_code ec;
        fs::create_directories(dir, ec);

        std::string roadsPath = (dir / "roads.geojson").lexically_normal().string();
        std::string placesPath = (dir / "places.geojson").lexically_normal().string();

        bool roadsSaved = rapidaid::geojson::GeoJsonWriter::writeFile(roadsPath, roadsGeoJson);
        bool placesSaved = rapidaid::geojson::GeoJsonWriter::writeFile(placesPath, placesGeoJson);
        if (roadsSaved) {
            rapidaid::geojson::GeoJsonWriter::writeFile((dir / "roads.js").lexically_normal().string(), "window.RAPIDAID_ROADS = " + roadsGeoJson + ";");
        }
        if (placesSaved) {
            rapidaid::geojson::GeoJsonWriter::writeFile((dir / "places.js").lexically_normal().string(), "window.RAPIDAID_PLACES = " + placesGeoJson + ";");
        }

        if (!roadsSaved && !placesSaved) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Failed to write GeoJSON files to disk";
            return resp.dump();
        }

        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["displayName"] = displayName;
        resp["lat"] = lat;
        resp["lon"] = lon;
        resp["radius"] = radius;
        resp["category"] = catStr;
        resp["nodesCount"] = static_cast<long long>(graph.nodeCount());
        resp["edgesCount"] = static_cast<long long>(graph.edgeCount());
        resp["placesCount"] = static_cast<long long>(places.size());
        resp["snappedCount"] = snappedCount;
        resp["roadsFile"] = roadsPath;
        resp["placesFile"] = placesPath;
        resp["roadsGeoJson"] = roadsGeoJson;
        resp["placesGeoJson"] = placesGeoJson;
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Pipeline processing failure";
        return resp.dump();
    }
}

std::string handleGetGeoJsonRequest(const std::string& type, const std::string& outDir) {
    std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
    try {
        fs::path file = fs::path(outDir) / (type + ".geojson");
        std::string content = rapidaid::io::FileStore::load(file.lexically_normal().string());
        if (content.empty()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "File not found: " + type;
            return resp.dump();
        }
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["type"] = type;
        resp["geojson"] = content;
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Failed reading GeoJSON: " + type;
        return resp.dump();
    }
}

std::string handleGetIncidentsRequest() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    try {
        auto& store = getIncidentStore();
        nlohmann::ordered_json items = nlohmann::json::array();
        for (const auto& inc : store) {
            nlohmann::ordered_json item;
            item["id"] = inc.id;
            item["title"] = inc.title;
            item["location"] = inc.location;
            item["type"] = inc.type;
            item["priority"] = inc.priority;
            item["timestamp"] = inc.timestamp;
            item["status"] = inc.status;
            item["notes"] = inc.notes;
            items.push_back(item);
        }

        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["status"] = "under_development";
        resp["notice"] = "Incident report handling backend is currently under development.";
        resp["count"] = store.size();
        resp["incidents"] = items;
        return resp.dump();
    } catch (...) {
        return "{\"ok\":true,\"status\":\"under_development\",\"incidents\":[]}";
    }
}

std::string handleAddIncidentRequest(const std::string& requestJson) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(requestJson);
        } catch (...) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j.is_object() || !j.contains("title") || !j.contains("location")) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Missing required fields";
            return resp.dump();
        }

        auto& store = getIncidentStore();
        std::ostringstream idSs;
        idSs.imbue(std::locale::classic());
        idSs << "INC-" << (store.size() + 101);

        StoredIncident inc;
        inc.id = idSs.str();
        inc.title = j.value("title", "Untitled Incident");
        inc.location = j.value("location", "Unknown Location");
        inc.type = j.value("type", "Medical");
        inc.priority = j.value("priority", "Medium");
        inc.timestamp = currentTimestampIso();
        inc.status = "Pending Dispatch";
        inc.notes = j.value("notes", "");

        store.push_back(inc);

        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["status"] = "under_development";
        resp["message"] = "Incident report recorded (mock backend)";
        resp["incident"] = {
            {"id", inc.id},
            {"title", inc.title},
            {"location", inc.location},
            {"type", inc.type},
            {"priority", inc.priority},
            {"timestamp", inc.timestamp},
            {"status", inc.status},
            {"notes", inc.notes}
        };
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Bad request";
        return resp.dump();
    }
}

std::string handleGetStatsRequest() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    try {
        auto& store = getIncidentStore();
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["status"] = "under_development";
        resp["notice"] = "Past dispatch statistics module is currently under development.";
        resp["totalIncidents"] = store.size();
        resp["activeDispatches"] = 0;
        resp["averageResponseTimeMinutes"] = 8.5;
        resp["dispatchedUnitsCount"] = 0;
        return resp.dump();
    } catch (...) {
        return "{\"ok\":true,\"status\":\"under_development\"}";
    }
}

std::string handleBridgeMessage(Auth& auth, rapidaid::data::DataCache& cache, const std::string& messageJson, const std::string& outDir) {
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(messageJson);
        } catch (...) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j.is_object() || !j.contains("action") || !j["action"].is_string()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        std::string action = j["action"].get<std::string>();

        // Extract payload
        std::string payloadStr = "{}";
        nlohmann::json pj = nlohmann::json::object();
        if (j.contains("payload")) {
            if (j["payload"].is_string()) {
                payloadStr = j["payload"].get<std::string>();
                try { pj = nlohmann::json::parse(payloadStr); } catch (...) {}
            } else if (j["payload"].is_object()) {
                pj = j["payload"];
                payloadStr = pj.dump();
            }
        }

        // Extract token (check top-level or payload)
        std::string token;
        if (j.contains("token") && j["token"].is_string()) {
            token = j["token"].get<std::string>();
        } else if (pj.is_object() && pj.contains("token") && pj["token"].is_string()) {
            token = pj["token"].get<std::string>();
        }

        // ==========================================
        // Action: login (no token required)
        // ==========================================
        if (action == "login") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            std::string res = handleLoginRequest(auth, payloadStr);
            try {
                nlohmann::json lr = nlohmann::json::parse(res);
                if (lr.value("ok", false)) {
                    std::string u = lr.value("username", "");
                    std::string r = lr.value("role", "");
                    std::string sessToken = generateSessionToken();
                    g_sessions[sessToken] = {u, r};

                    nlohmann::ordered_json successResp;
                    successResp["ok"] = true;
                    successResp["username"] = u;
                    successResp["role"] = r;
                    successResp["token"] = sessToken;
                    return successResp.dump();
                }
            } catch (...) {}
            return res;
        }

        // ==========================================
        // Action: signup (no token required, except admin role)
        // ==========================================
        if (action == "signup") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (!pj.is_object()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Bad request";
                return resp.dump();
            }

            std::string username = pj.value("username", "");
            std::string role = pj.value("role", "dispatcher");

            if (username.empty() || std::any_of(username.begin(), username.end(), [](unsigned char c) { return std::isspace(c); })) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Username cannot be empty or contain whitespace";
                return resp.dump();
            }

            if (role != "dispatcher" && role != "unit" && role != "admin") {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Not allowed";
                return resp.dump();
            }

            if (role == "admin") {
                if (token.empty() || g_sessions.find(token) == g_sessions.end() || g_sessions[token].role != "admin") {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Not allowed";
                    return resp.dump();
                }
            }

            UnitProfile unitProf;
            if (role == "unit") {
                if (!pj.contains("unit") || !pj["unit"].is_object()) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Missing unit profile for role unit";
                    return resp.dump();
                }
                const auto& uj = pj["unit"];
                unitProf.username = username;
                unitProf.kind = uj.value("kind", "");
                unitProf.name = uj.value("name", "");
                unitProf.lat = uj.value("lat", 999.0);
                unitProf.lon = uj.value("lon", 999.0);
                unitProf.count = uj.value("count", 0);

                if (unitProf.name.empty()) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Unit name cannot be empty";
                    return resp.dump();
                }
                if (unitProf.lat < -90.0 || unitProf.lat > 90.0) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Unit latitude must be in [-90, 90]";
                    return resp.dump();
                }
                if (unitProf.lon < -180.0 || unitProf.lon > 180.0) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Unit longitude must be in [-180, 180]";
                    return resp.dump();
                }
                if (unitProf.count < 0) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Unit count cannot be negative";
                    return resp.dump();
                }
                const std::vector<std::string> validKinds = {
                    "ambulance", "fire_truck", "police_unit", "hospital", "fire_station", "police_station", "shelter"
                };
                if (std::find(validKinds.begin(), validKinds.end(), unitProf.kind) == validKinds.end()) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Unknown unit kind: " + unitProf.kind;
                    return resp.dump();
                }
            }

            // Create user account via Auth
            std::string res = handleSignUpRequest(auth, payloadStr);
            try {
                nlohmann::json sr = nlohmann::json::parse(res);
                if (sr.value("ok", false) && role == "unit") {
                    std::string err;
                    if (!g_registry.registerUnit(unitProf, err)) {
                        nlohmann::ordered_json resp;
                        resp["ok"] = false;
                        resp["error"] = err;
                        return resp.dump();
                    }
                }
            } catch (...) {}
            return res;
        }

        // ==========================================
        // Action: health (no token required)
        // ==========================================
        if (action == "health") {
            return handleHealthRequest();
        }

        // ==========================================
        // All subsequent actions REQUIRE a valid session token (B2)
        // ==========================================
        SessionData session;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (token.empty()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Not logged in";
                return resp.dump();
            }
            auto it = g_sessions.find(token);
            if (it == g_sessions.end()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Not logged in";
                return resp.dump();
            }
            session = it->second;
        }

        // ==========================================
        // Action: session (returns current profile / role)
        // ==========================================
        if (action == "session") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["username"] = session.username;
            resp["role"] = session.role;
            if (session.role == "unit") {
                auto prof = g_registry.getProfile(session.username);
                if (prof.has_value()) {
                    nlohmann::ordered_json uj;
                    uj["username"] = prof->username;
                    uj["kind"] = prof->kind;
                    uj["name"] = prof->name;
                    uj["lat"] = prof->lat;
                    uj["lon"] = prof->lon;
                    uj["count"] = prof->count;
                    resp["unit"] = uj;
                } else {
                    resp["unit"] = nullptr;
                }
            } else {
                resp["unit"] = nullptr;
            }
            return resp.dump();
        }

        // ==========================================
        // Action: logout
        // ==========================================
        if (action == "logout") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            g_sessions.erase(token);
            return handleLogoutRequest(auth, session.username);
        }

        // ==========================================
        // Authorization check: Dispatcher/Admin actions
        // ==========================================
        bool isDispatcherOrAdmin = (session.role == "dispatcher" || session.role == "admin");
        if (action == "area_status" || action == "geocode" || action == "create_area" ||
            action == "get_geojson" || action == "get_incidents" || action == "add_incident" ||
            action == "get_stats" || action == "list_units" || action == "send_test_alert" ||
            action == "list_alerts") {
            if (!isDispatcherOrAdmin) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Not allowed";
                return resp.dump();
            }
        }

        // Pipeline actions
        if (action == "area_status") {
            return handleAreaStatusRequest(outDir);
        }
        if (action == "geocode") {
            return handleGeocodeRequest(cache, payloadStr);
        }
        if (action == "create_area") {
            return handleCreateAreaRequest(cache, payloadStr, outDir);
        }
        if (action == "get_geojson") {
            std::string type = pj.value("type", "roads");
            return handleGetGeoJsonRequest(type, outDir);
        }
        if (action == "get_incidents") {
            return handleGetIncidentsRequest();
        }
        if (action == "add_incident") {
            return handleAddIncidentRequest(payloadStr);
        }
        if (action == "get_stats") {
            return handleGetStatsRequest();
        }

        // Dispatcher Unit & Alert queries
        if (action == "list_units") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            return g_registry.listUnitsJson().dump();
        }
        if (action == "send_test_alert") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (!pj.is_object()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Bad request";
                return resp.dump();
            }
            Alert a;
            a.toUser = pj.value("toUser", "");
            a.type = pj.value("type", "Medical");
            a.severity = pj.value("severity", 3);
            a.peopleAffected = pj.value("peopleAffected", 0);
            a.locationText = pj.value("locationText", "Unknown Location");
            a.note = pj.value("note", "");
            if (pj.contains("lat") && pj["lat"].is_number()) a.lat = pj["lat"].get<double>();
            if (pj.contains("lon") && pj["lon"].is_number()) a.lon = pj["lon"].get<double>();

            std::string err;
            int alertId = g_registry.sendAlert(a, err);
            if (alertId < 0) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = err;
                return resp.dump();
            }
            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["alertId"] = alertId;
            return resp.dump();
        }
        if (action == "list_alerts") {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            return g_registry.listAlertsJson().dump();
        }

        // ==========================================
        // Authorization check: Unit actions (B2)
        // ==========================================
        if (action.rfind("unit_", 0) == 0) {
            if (session.role != "unit") {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Not allowed";
                return resp.dump();
            }

            std::lock_guard<std::mutex> lock(g_stateMutex);
            if (action == "unit_get_state") {
                return g_registry.getUnitStateJson(session.username).dump();
            }
            if (action == "unit_set_availability") {
                bool avail = pj.value("available", true);
                std::string err;
                if (!g_registry.setAvailability(session.username, avail, err)) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = err;
                    return resp.dump();
                }
                return g_registry.getUnitStateJson(session.username).dump();
            }
            if (action == "unit_update_location") {
                if (!pj.contains("lat") || !pj.contains("lon") || !pj["lat"].is_number() || !pj["lon"].is_number()) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Invalid coordinates in payload";
                    return resp.dump();
                }
                double lat = pj["lat"].get<double>();
                double lon = pj["lon"].get<double>();
                std::string err;
                if (!g_registry.updateLocation(session.username, lat, lon, err)) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = err;
                    return resp.dump();
                }
                nlohmann::ordered_json resp;
                resp["ok"] = true;
                resp["lat"] = lat;
                resp["lon"] = lon;
                return resp.dump();
            }
            if (action == "unit_update_resources") {
                std::string resAction = pj.value("action", "");
                int val = 0;
                if (pj.contains("count") && pj["count"].is_number()) {
                    val = pj["count"].get<int>();
                } else if (pj.contains("value") && pj["value"].is_number()) {
                    val = pj["value"].get<int>();
                }
                std::string err;
                if (!g_registry.updateResources(session.username, resAction, val, err)) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = err;
                    return resp.dump();
                }
                return g_registry.getUnitStateJson(session.username).dump();
            }
            if (action == "unit_add_subunit") {
                std::string sname = pj.value("name", "");
                int scount = pj.value("count", 0);
                std::string err;
                int subId = g_registry.addSubunit(session.username, sname, scount, err);
                if (subId < 0) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = err;
                    return resp.dump();
                }
                nlohmann::ordered_json resp;
                resp["ok"] = true;
                resp["subunitId"] = subId;
                return resp.dump();
            }
            if (action == "unit_set_subunit_availability") {
                int subId = pj.value("subunitId", 0);
                bool avail = pj.value("available", true);
                std::string err;
                if (!g_registry.setSubunitAvailability(session.username, subId, avail, err)) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = err;
                    return resp.dump();
                }
                return g_registry.getUnitStateJson(session.username).dump();
            }
            if (action == "unit_alert_respond") {
                int alertId = pj.value("alertId", 0);
                std::string respAction = pj.value("action", "");
                std::string reason = pj.value("reason", "");
                std::string err;
                if (!g_registry.respondToAlert(alertId, session.username, respAction, reason, err)) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = err;
                    return resp.dump();
                }
                return g_registry.getUnitStateJson(session.username).dump();
            }

            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Unknown unit action: " + action;
            return resp.dump();
        }

        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Unknown action: " + action;
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Bridge message handling failure";
        return resp.dump();
    }
}

} // namespace rapidaid::ui
