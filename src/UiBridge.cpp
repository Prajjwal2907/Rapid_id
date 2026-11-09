#include "UiBridge.hpp"
#include "AreaContext.hpp"
#include "DispatchEngine.hpp"
#include "FileStore.hpp"
#include "Geocoder.hpp"
#include "GeoJsonWriter.hpp"
#include "Graph.hpp"
#include "GraphBuilder.hpp"
#include "Incident.hpp"
#include "IncidentStore.hpp"
#include "LocationResolver.hpp"
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
#include <unordered_set>
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

// Global mutexes separating domain state from pipeline computation
std::mutex g_stateMutex;
std::mutex g_pipelineMutex;

// In-memory sessions: token -> SessionData
std::unordered_map<std::string, SessionData> g_sessions;

// Global UnitRegistry instance
UnitRegistry g_registry;

// Global IncidentStore instance
rapidaid::dispatch::IncidentStore g_incidentStore("data/incidents.json");
bool g_incidentsLoaded = false;

// Global AreaContext instance
std::optional<rapidaid::area::AreaContext> g_areaContext;

// Stored dispatch proposals: incidentId -> Proposal
std::unordered_map<int, rapidaid::dispatch::Proposal> g_proposals;

// Slot tried candidates history: "incidentId:slotIdx" -> set of usernames
std::unordered_map<std::string, std::set<std::string>> g_slotTriedUsernames;

// Legacy in-memory mock storage for incidents
std::vector<StoredIncident>& getLegacyIncidentStore() {
    static std::vector<StoredIncident> store;
    return store;
}

void ensureIncidentsLoaded() {
    if (!g_incidentsLoaded) {
        g_incidentStore.load();
        g_registry.ensureLoaded();
        g_incidentsLoaded = true;
    }
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

void tryLoadAreaContext(rapidaid::data::DataCache& cache, const std::string& outDir) {
    std::string areaJsonPath = (fs::path(outDir) / "area.json").lexically_normal().string();
    auto optInfo = rapidaid::area::AreaContext::loadInfo(areaJsonPath);
    if (!optInfo.has_value()) {
        g_areaContext.reset();
        return;
    }
    rapidaid::area::AreaContext ctx;
    std::string err;
    if (rapidaid::area::AreaContext::build(cache, *optInfo, false, ctx, &err)) {
        g_areaContext = std::move(ctx);
    } else {
        g_areaContext.reset();
    }
}

nlohmann::ordered_json incidentToJson(const rapidaid::dispatch::Incident& inc, long long nowEpoch) {
    nlohmann::json baseJ;
    to_json(baseJ, inc);
    nlohmann::ordered_json j = baseJ;

    double waitingMinutes = 0.0;
    if (nowEpoch > inc.createdAt && inc.createdAt > 0) {
        waitingMinutes = static_cast<double>(nowEpoch - inc.createdAt) / 60.0;
    }
    double score = rapidaid::dispatch::priorityScore(inc.severity, inc.peopleAffected, waitingMinutes);
    j["priority"] = score;
    j["waitingMinutes"] = waitingMinutes;

    nlohmann::ordered_json assignmentsArr = nlohmann::json::array();
    for (const auto& a : inc.assignments) {
        nlohmann::json baseAj;
        to_json(baseAj, a);
        nlohmann::ordered_json aj = baseAj;
        if (a.alertId >= 0) {
            const Alert* al = g_registry.findAlert(a.alertId);
            if (al) {
                aj["state"] = al->state;
                aj["alertState"] = al->state;
            } else {
                aj["alertState"] = a.state;
            }
        } else {
            aj["alertState"] = a.state;
        }
        assignmentsArr.push_back(aj);
    }
    j["assignments"] = assignmentsArr;
    return j;
}

} // anonymous namespace

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
    std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
    std::lock_guard<std::mutex> stateLock(g_stateMutex);
    g_sessions.clear();
    g_registry.clear();
    g_registry.setDataPath("data/units.json");
    g_incidentStore.clear();
    g_incidentsLoaded = false;
    g_proposals.clear();
    g_slotTriedUsernames.clear();
    g_areaContext.reset();
    getLegacyIncidentStore().clear();

    rapidaid::data::DataCache defaultCache("data", false);
    tryLoadAreaContext(defaultCache, "data/out");
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
        fs::path areaFile = dir / "area.json";
        fs::path roads = dir / "roads.geojson";
        fs::path places = dir / "places.geojson";

        bool areaExists = fs::exists(areaFile, ec) && !ec && fs::file_size(areaFile, ec) > 0;
        bool roadsExists = fs::exists(roads, ec) && !ec && fs::file_size(roads, ec) > 0;
        bool placesExists = fs::exists(places, ec) && !ec && fs::file_size(places, ec) > 0;
        bool exists = areaExists && roadsExists && placesExists;

        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["exists"] = exists;

        if (exists) {
            auto optInfo = rapidaid::area::AreaContext::loadInfo(areaFile.string());
            if (optInfo.has_value()) {
                nlohmann::ordered_json aj;
                aj["name"] = optInfo->name;
                aj["displayName"] = optInfo->displayName;
                aj["lat"] = optInfo->lat;
                aj["lon"] = optInfo->lon;
                aj["radius"] = optInfo->radiusMeters;
                resp["area"] = aj;
            } else if (g_areaContext.has_value()) {
                const auto& info = g_areaContext->info();
                nlohmann::ordered_json aj;
                aj["name"] = info.name;
                aj["displayName"] = info.displayName;
                aj["lat"] = info.lat;
                aj["lon"] = info.lon;
                aj["radius"] = info.radiusMeters;
                resp["area"] = aj;
            } else {
                resp["area"] = nullptr;
                resp["exists"] = false;
            }
        } else {
            resp["area"] = nullptr;
        }

        resp["roadsFile"] = roads.lexically_normal().string();
        resp["placesFile"] = places.lexically_normal().string();
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["exists"] = false;
        resp["area"] = nullptr;
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

        rapidaid::area::AreaInfo info;
        info.name = name;
        info.displayName = displayName;
        info.lat = lat;
        info.lon = lon;
        info.radiusMeters = radius;

        rapidaid::area::AreaContext newCtx;
        std::string buildError;
        if (!rapidaid::area::AreaContext::build(cache, info, true, newCtx, &buildError)) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = buildError.empty() ? "Pipeline processing failure" : buildError;
            return resp.dump();
        }

        // Save area.json permanently
        fs::path dir(outDir);
        std::error_code ec;
        fs::create_directories(dir, ec);

        std::string areaJsonPath = (dir / "area.json").lexically_normal().string();
        rapidaid::area::AreaContext::saveInfo(areaJsonPath, info);

        // Serialize and save GeoJSON
        std::string roadsGeoJson = rapidaid::geojson::GeoJsonWriter::roadsCollection(newCtx.graph());
        std::string placesGeoJson = rapidaid::geojson::GeoJsonWriter::snappedPlacesCollection(newCtx.snappedPlaces());

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

        int snappedCount = 0;
        for (const auto& sp : newCtx.snappedPlaces()) {
            if (sp.snapped) snappedCount++;
        }

        {
            std::lock_guard<std::mutex> stateLock(g_stateMutex);
            g_areaContext = std::move(newCtx);
        }

        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["displayName"] = displayName;
        resp["lat"] = lat;
        resp["lon"] = lon;
        resp["radius"] = radius;
        resp["nodesCount"] = static_cast<long long>(g_areaContext->graph().nodeCount());
        resp["edgesCount"] = static_cast<long long>(g_areaContext->graph().edgeCount());
        resp["placesCount"] = static_cast<long long>(g_areaContext->places().size());
        resp["snappedCount"] = snappedCount;
        resp["roadsFile"] = roadsPath;
        resp["placesFile"] = placesPath;
        resp["roadsGeoJson"] = roadsGeoJson;
        resp["placesGeoJson"] = placesGeoJson;
        resp["area"] = {
            {"name", info.name},
            {"displayName", info.displayName},
            {"lat", info.lat},
            {"lon", info.lon},
            {"radius", info.radiusMeters}
        };
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
        ensureIncidentsLoaded();
        long long nowEpoch = std::time(nullptr);

        if (!g_incidentStore.all().empty()) {
            nlohmann::ordered_json incArray = nlohmann::json::array();
            for (const auto& inc : g_incidentStore.all()) {
                incArray.push_back(incidentToJson(inc, nowEpoch));
            }
            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["count"] = incArray.size();
            resp["incidents"] = incArray;
            return resp.dump();
        }

        auto& legacyStore = getLegacyIncidentStore();
        nlohmann::ordered_json items = nlohmann::json::array();
        for (const auto& inc : legacyStore) {
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
        resp["count"] = legacyStore.size();
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

        auto& store = getLegacyIncidentStore();
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
        ensureIncidentsLoaded();
        size_t total = g_incidentStore.all().size() + getLegacyIncidentStore().size();
        int activeDispatches = 0;
        for (const auto& inc : g_incidentStore.all()) {
            if (inc.status == "Dispatched") {
                activeDispatches++;
            }
        }
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["status"] = "under_development";
        resp["notice"] = "Past dispatch statistics module is currently under development.";
        resp["totalIncidents"] = total;
        resp["activeDispatches"] = activeDispatches;
        resp["averageResponseTimeMinutes"] = 8.5;
        resp["dispatchedUnitsCount"] = activeDispatches;
        return resp.dump();
    } catch (...) {
        return "{\"ok\":true,\"status\":\"under_development\"}";
    }
}

void tickDispatch() {
    tickDispatch(std::time(nullptr));
}

void tickDispatch(long long nowEpoch) {
    try {
        std::lock_guard<std::mutex> stateLock(g_stateMutex);
        ensureIncidentsLoaded();
        if (!g_areaContext.has_value()) {
            return;
        }
        const auto& graph = g_areaContext->graph();

        for (const auto& incSummary : g_incidentStore.all()) {
            if (incSummary.status != "Dispatched") {
                continue;
            }
            rapidaid::dispatch::Incident* inc = g_incidentStore.find(incSummary.id);
            if (!inc) continue;

            // 1. Check if all assignments are Cleared
            bool allCleared = !inc->assignments.empty();
            for (const auto& a : inc->assignments) {
                std::string st = a.state;
                if (a.alertId >= 0) {
                    const Alert* al = g_registry.findAlert(a.alertId);
                    if (al) st = al->state;
                }
                if (st != "Cleared") {
                    allCleared = false;
                    break;
                }
            }

            if (allCleared) {
                inc->status = "Completed";
                inc->log.push_back({nowEpoch, "Incident completed (all units cleared)"});
                g_incidentStore.save();
                continue;
            }

            // 2. Evaluate assignments for escalation (Declined or Overdue Pending)
            int incidentNode = graph.indexOf(inc->nodeOsmId);
            if (incidentNode < 0) {
                incidentNode = graph.nearestNode(inc->lat, inc->lon);
            }

            for (size_t slotIdx = 0; slotIdx < inc->assignments.size(); ++slotIdx) {
                auto& a = inc->assignments[slotIdx];
                if (a.alertId < 0) continue;

                Alert* al = g_registry.findAlert(a.alertId);
                if (!al) continue;

                a.state = al->state;

                bool isDeclined = (al->state == "Declined");
                bool isOverdue = (al->state == "Pending" && (al->createdAt + al->ackSeconds <= nowEpoch));

                if (isDeclined || isOverdue) {
                    if (isOverdue) {
                        g_registry.closeAlert(al->id, "Declined", "No acknowledgement");
                        a.state = "Declined";
                    }

                    std::string slotKey = std::to_string(inc->id) + ":" + std::to_string(slotIdx);
                    g_slotTriedUsernames[slotKey].insert(a.username);

                    if (a.attempt >= 5) {
                        std::string warn = "No available " + a.kind + " for " + a.role;
                        inc->warnings.push_back(warn);
                        inc->log.push_back({nowEpoch, warn + " (max attempts reached)"});
                        g_incidentStore.save();
                        continue;
                    }

                    // Build exclusion set
                    std::set<std::string> excluded = g_slotTriedUsernames[slotKey];
                    for (size_t otherIdx = 0; otherIdx < inc->assignments.size(); ++otherIdx) {
                        if (otherIdx != slotIdx && !inc->assignments[otherIdx].username.empty()) {
                            excluded.insert(inc->assignments[otherIdx].username);
                        }
                    }

                    rapidaid::dispatch::Slot slot;
                    rapidaid::dispatch::parseUnitKind(a.kind, slot.kind);
                    slot.role = a.role;
                    if (slot.kind == rapidaid::dispatch::UnitKind::Hospital ||
                        slot.kind == rapidaid::dispatch::UnitKind::Shelter) {
                        slot.minCapacity = inc->peopleAffected;
                    }

                    auto candidates = g_registry.listCandidates(graph);
                    auto pick = rapidaid::dispatch::pickReplacement(slot, incidentNode, *inc, graph, candidates, excluded);

                    if (pick.found) {
                        std::vector<std::vector<double>> routeCoords;
                        for (int nIdx : pick.pathNodes) {
                            if (nIdx >= 0 && nIdx < graph.nodeCount()) {
                                routeCoords.push_back({graph.lon(nIdx), graph.lat(nIdx)});
                            }
                        }
                        if (routeCoords.empty()) {
                            routeCoords.push_back({pick.candidate.lon, pick.candidate.lat});
                            routeCoords.push_back({inc->lon, inc->lat});
                        }

                        auto optAlert = g_registry.sendDispatchAlert(
                            pick.candidate.username,
                            inc->id,
                            a.role,
                            pick.distanceMeters,
                            routeCoords,
                            inc->type,
                            inc->severity,
                            inc->peopleAffected,
                            inc->locationText,
                            inc->lat,
                            inc->lon,
                            "",
                            60
                        );

                        if (optAlert.has_value()) {
                            std::string oldUser = a.username;
                            std::string newUser = pick.candidate.username;
                            a.username = newUser;
                            a.name = pick.candidate.name;
                            a.kind = rapidaid::dispatch::unitKindName(pick.candidate.kind);
                            a.alertId = optAlert->id;
                            a.distanceMeters = pick.distanceMeters;
                            a.attempt += 1;
                            a.state = "Pending";

                            g_slotTriedUsernames[slotKey].insert(newUser);

                            std::string repMsg = "Replaced " + oldUser + " with " + newUser + " for " + a.role;
                            inc->log.push_back({nowEpoch, repMsg});
                            inc->warnings.push_back(repMsg);
                            g_incidentStore.save();
                        } else {
                            std::string warn = "No available " + a.kind + " for " + a.role;
                            inc->warnings.push_back(warn);
                            inc->log.push_back({nowEpoch, warn});
                            g_incidentStore.save();
                        }
                    } else {
                        std::string warn = "No available " + a.kind + " for " + a.role;
                        inc->warnings.push_back(warn);
                        inc->log.push_back({nowEpoch, warn});
                        g_incidentStore.save();
                    }
                }
            }
        }
    } catch (...) {}
}

std::string handleBridgeMessage(Auth& auth, rapidaid::data::DataCache& cache, const std::string& messageJson, const std::string& outDir) {
    try {
        tickDispatch();

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

        // Extract token
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
        // All subsequent actions REQUIRE a valid session token
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
        // Action: session
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
            action == "list_alerts" || action == "search_location" || action == "snap_point" ||
            action == "create_incident" || action == "list_incidents" || action == "get_incident" ||
            action == "update_incident" || action == "propose_dispatch" || action == "approve_dispatch" ||
            action == "cancel_incident") {
            if (!isDispatcherOrAdmin) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Not allowed";
                return resp.dump();
            }
        }

        // Ensure area context loaded if area.json exists
        {
            std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
            if (!g_areaContext.has_value()) {
                tryLoadAreaContext(cache, outDir);
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

        // Location & Map actions
        if (action == "search_location") {
            std::string query = pj.value("query", "");
            bool localOnly = pj.value("localOnly", false);

            std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
            std::lock_guard<std::mutex> stateLock(g_stateMutex);

            if (!g_areaContext.has_value()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "No area configured";
                return resp.dump();
            }

            const auto& area = *g_areaContext;
            rapidaid::locate::LocationResolver resolver(area.graph(), area.places(), area.roads());

            std::vector<rapidaid::locate::LocationCandidate> allCandidates;
            if (!query.empty()) {
                auto placesCands = resolver.searchPlaces(query, 10);
                allCandidates.insert(allCandidates.end(), placesCands.begin(), placesCands.end());

                if (allCandidates.size() < 10) {
                    auto interCands = resolver.searchIntersections(query, 10 - allCandidates.size());
                    allCandidates.insert(allCandidates.end(), interCands.begin(), interCands.end());
                }

                if (!localOnly && allCandidates.size() < 10) {
                    auto box = rapidaid::geo::Geocoder::viewBoxAround(area.info().lat, area.info().lon, area.info().radiusMeters);
                    std::string raw = rapidaid::geo::Geocoder::fetchRawMany(query, box, 10 - static_cast<int>(allCandidates.size()));
                    auto locs = rapidaid::geo::Geocoder::parseMany(raw);
                    for (const auto& l : locs) {
                        rapidaid::locate::LocationCandidate c;
                        c.label = l.displayName;
                        c.lat = l.lat;
                        c.lon = l.lon;
                        c.source = "address";
                        allCandidates.push_back(c);
                        if (allCandidates.size() >= 10) break;
                    }
                }
            }

            nlohmann::ordered_json arr = nlohmann::json::array();
            for (const auto& c : allCandidates) {
                arr.push_back({
                    {"label", c.label},
                    {"lat", c.lat},
                    {"lon", c.lon},
                    {"source", c.source}
                });
            }

            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["candidates"] = arr;
            return resp.dump();
        }

        if (action == "snap_point") {
            if (!pj.contains("lat") || !pj.contains("lon") || !pj["lat"].is_number() || !pj["lon"].is_number()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Invalid or missing lat/lon in payload";
                return resp.dump();
            }
            double lat = pj["lat"].get<double>();
            double lon = pj["lon"].get<double>();

            std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
            std::lock_guard<std::mutex> stateLock(g_stateMutex);

            if (!g_areaContext.has_value()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "No area configured";
                return resp.dump();
            }

            const auto& graph = g_areaContext->graph();
            double dist = 0.0;
            int nodeIdx = graph.nearestNode(lat, lon, &dist);
            long long nodeOsmId = (nodeIdx >= 0) ? graph.osmId(nodeIdx) : 0LL;
            bool withinArea = (nodeIdx >= 0 && dist <= 500.0);

            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["lat"] = lat;
            resp["lon"] = lon;
            resp["nodeOsmId"] = nodeOsmId;
            resp["snapMeters"] = dist;
            resp["withinArea"] = withinArea;
            return resp.dump();
        }

        // ==========================================
        // Incident Management & Dispatch Actions
        // ==========================================
        if (action == "create_incident") {
            if (!pj.is_object()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Bad request";
                return resp.dump();
            }

            std::string type = pj.value("type", "Medical");
            int severity = pj.value("severity", 3);
            int peopleAffected = pj.value("peopleAffected", 0);
            std::string description = pj.value("description", "");
            std::string callerName = pj.value("callerName", "");
            std::string callerPhone = pj.value("callerPhone", "");

            if (severity < 1 || severity > 5) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Severity must be between 1 and 5";
                return resp.dump();
            }
            if (peopleAffected < 0) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "People affected cannot be negative";
                return resp.dump();
            }

            if (!pj.contains("location") || !pj["location"].is_object()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Missing location object";
                return resp.dump();
            }
            const auto& locJ = pj["location"];
            if (!locJ.contains("lat") || !locJ.contains("lon") || !locJ["lat"].is_number() || !locJ["lon"].is_number()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Location lat/lon must be valid numbers";
                return resp.dump();
            }

            double lat = locJ["lat"].get<double>();
            double lon = locJ["lon"].get<double>();
            std::string locText = locJ.value("text", "Unknown Location");
            std::string locSource = locJ.value("source", "coordinates");

            std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
            std::lock_guard<std::mutex> stateLock(g_stateMutex);
            ensureIncidentsLoaded();

            if (!g_areaContext.has_value()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "No area configured";
                return resp.dump();
            }

            const auto& graph = g_areaContext->graph();
            double snapMeters = 0.0;
            int nodeIdx = graph.nearestNode(lat, lon, &snapMeters);
            if (nodeIdx < 0 || snapMeters > 500.0) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Location is outside the mapped area";
                return resp.dump();
            }
            long long osmId = graph.osmId(nodeIdx);

            rapidaid::dispatch::Incident inc;
            inc.type = type;
            inc.severity = severity;
            inc.peopleAffected = peopleAffected;
            inc.description = description;
            inc.callerName = callerName;
            inc.callerPhone = callerPhone;
            inc.locationText = locText;
            inc.locationSource = locSource;
            inc.lat = lat;
            inc.lon = lon;
            inc.snapMeters = snapMeters;
            inc.nodeOsmId = osmId;
            inc.createdAt = std::time(nullptr);
            inc.status = "New";
            inc.log.push_back({inc.createdAt, "Incident created"});

            int id = g_incidentStore.add(inc);
            g_incidentStore.save();
            auto* stored = g_incidentStore.find(id);

            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["incident"] = incidentToJson(*stored, std::time(nullptr));
            return resp.dump();
        }

        if (action == "list_incidents") {
            std::lock_guard<std::mutex> stateLock(g_stateMutex);
            ensureIncidentsLoaded();
            long long nowEpoch = std::time(nullptr);

            struct ScoredInc {
                nlohmann::ordered_json json;
                double priority;
                long long createdAt;
                int id;
            };

            std::vector<ScoredInc> scored;
            for (const auto& inc : g_incidentStore.all()) {
                nlohmann::ordered_json ij = incidentToJson(inc, nowEpoch);
                double prio = ij["priority"].get<double>();
                scored.push_back({ij, prio, inc.createdAt, inc.id});
            }

            std::sort(scored.begin(), scored.end(), [](const ScoredInc& a, const ScoredInc& b) {
                if (a.priority != b.priority) {
                    return a.priority > b.priority;
                }
                if (a.createdAt != b.createdAt) {
                    return a.createdAt < b.createdAt;
                }
                return a.id < b.id;
            });

            nlohmann::ordered_json incArray = nlohmann::json::array();
            for (const auto& s : scored) {
                incArray.push_back(s.json);
            }

            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["incidents"] = incArray;
            return resp.dump();
        }

        if (action == "get_incident") {
            int id = pj.value("incidentId", 0);
            if (id <= 0 && pj.contains("id") && pj["id"].is_number()) {
                id = pj["id"].get<int>();
            }
            std::lock_guard<std::mutex> stateLock(g_stateMutex);
            ensureIncidentsLoaded();

            auto* inc = g_incidentStore.find(id);
            if (!inc) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Incident not found";
                return resp.dump();
            }

            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["incident"] = incidentToJson(*inc, std::time(nullptr));
            return resp.dump();
        }

        if (action == "update_incident") {
            int id = pj.value("incidentId", 0);
            if (id <= 0 && pj.contains("id") && pj["id"].is_number()) {
                id = pj["id"].get<int>();
            }

            std::lock_guard<std::mutex> stateLock(g_stateMutex);
            ensureIncidentsLoaded();

            auto* inc = g_incidentStore.find(id);
            if (!inc) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Incident not found";
                return resp.dump();
            }

            if (inc->status != "New" && inc->status != "Proposed") {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Incident can only be updated while New or Proposed";
                return resp.dump();
            }

            if (pj.contains("severity") && pj["severity"].is_number()) {
                int s = pj["severity"].get<int>();
                if (s < 1 || s > 5) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "Severity must be between 1 and 5";
                    return resp.dump();
                }
                inc->severity = s;
            }

            if (pj.contains("peopleAffected") && pj["peopleAffected"].is_number()) {
                int p = pj["peopleAffected"].get<int>();
                if (p < 0) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "People affected cannot be negative";
                    return resp.dump();
                }
                inc->peopleAffected = p;
            }

            if (pj.contains("description") && pj["description"].is_string()) {
                inc->description = pj["description"].get<std::string>();
            }

            g_incidentStore.save();

            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["incident"] = incidentToJson(*inc, std::time(nullptr));
            return resp.dump();
        }

        if (action == "propose_dispatch") {
            int id = pj.value("incidentId", 0);
            if (id <= 0 && pj.contains("id") && pj["id"].is_number()) {
                id = pj["id"].get<int>();
            }

            std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
            std::lock_guard<std::mutex> stateLock(g_stateMutex);
            ensureIncidentsLoaded();

            if (!g_areaContext.has_value()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "No area configured";
                return resp.dump();
            }

            auto* inc = g_incidentStore.find(id);
            if (!inc) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Incident not found";
                return resp.dump();
            }

            const auto& graph = g_areaContext->graph();
            int incidentNode = graph.indexOf(inc->nodeOsmId);
            if (incidentNode < 0) {
                incidentNode = graph.nearestNode(inc->lat, inc->lon);
            }
            if (incidentNode < 0) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Incident node not found on road network";
                return resp.dump();
            }

            auto candidates = g_registry.listCandidates(graph);
            auto proposal = rapidaid::dispatch::propose(*inc, graph, incidentNode, candidates);

            g_proposals[id] = proposal;
            inc->status = "Proposed";
            inc->warnings = proposal.warnings;
            inc->log.push_back({std::time(nullptr), "Dispatch proposal generated"});
            g_incidentStore.save();

            nlohmann::ordered_json slotsArr = nlohmann::json::array();
            for (const auto& pick : proposal.picks) {
                nlohmann::ordered_json sj;
                sj["role"] = pick.slot.role;
                sj["kind"] = rapidaid::dispatch::unitKindName(pick.slot.kind);
                sj["found"] = pick.found;

                if (pick.found) {
                    sj["username"] = pick.candidate.username;
                    sj["name"] = pick.candidate.name;
                    sj["distanceMeters"] = pick.distanceMeters;

                    nlohmann::ordered_json routeCoords = nlohmann::json::array();
                    for (int nIdx : pick.pathNodes) {
                        if (nIdx >= 0 && nIdx < graph.nodeCount()) {
                            routeCoords.push_back({graph.lon(nIdx), graph.lat(nIdx)});
                        }
                    }
                    if (routeCoords.empty()) {
                        routeCoords.push_back({pick.candidate.lon, pick.candidate.lat});
                        routeCoords.push_back({inc->lon, inc->lat});
                    }
                    sj["routeCoordinates"] = routeCoords;
                } else {
                    sj["username"] = "";
                    sj["name"] = "";
                    sj["distanceMeters"] = 0.0;
                    sj["routeCoordinates"] = nlohmann::json::array();
                }

                nlohmann::ordered_json altsArr = nlohmann::json::array();
                for (size_t aIdx = 0; aIdx < pick.alternatives.size(); ++aIdx) {
                    const auto& alt = pick.alternatives[aIdx];
                    double altDist = (aIdx < pick.alternativeDistances.size()) ? pick.alternativeDistances[aIdx] : 0.0;
                    altsArr.push_back({
                        {"username", alt.username},
                        {"name", alt.name},
                        {"distanceMeters", altDist}
                    });
                }
                sj["alternatives"] = altsArr;
                slotsArr.push_back(sj);
            }

            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["proposal"] = {
                {"incidentId", id},
                {"slots", slotsArr},
                {"warnings", proposal.warnings}
            };
            return resp.dump();
        }

        if (action == "approve_dispatch") {
            int id = pj.value("incidentId", 0);
            if (id <= 0 && pj.contains("id") && pj["id"].is_number()) {
                id = pj["id"].get<int>();
            }

            std::lock_guard<std::mutex> pipeLock(g_pipelineMutex);
            std::lock_guard<std::mutex> stateLock(g_stateMutex);
            ensureIncidentsLoaded();

            if (!g_areaContext.has_value()) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "No area configured";
                return resp.dump();
            }

            auto* inc = g_incidentStore.find(id);
            if (!inc) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Incident not found";
                return resp.dump();
            }

            const auto& graph = g_areaContext->graph();
            int incidentNode = graph.indexOf(inc->nodeOsmId);
            if (incidentNode < 0) {
                incidentNode = graph.nearestNode(inc->lat, inc->lon);
            }

            struct DispatchTarget {
                std::string username;
                std::string role;
                std::string kind;
                std::string name;
                double distanceMeters = 0.0;
                std::vector<std::vector<double>> routeCoordinates;
            };

            std::vector<DispatchTarget> targets;

            if (pj.contains("units") && pj["units"].is_array() && !pj["units"].empty()) {
                // Units specified in payload
                std::unordered_set<std::string> requestedUsers;
                for (const auto& uItem : pj["units"]) {
                    if (!uItem.is_object()) {
                        nlohmann::ordered_json resp;
                        resp["ok"] = false;
                        resp["error"] = "Invalid unit item in units array";
                        return resp.dump();
                    }
                    std::string uName = uItem.value("username", "");
                    std::string uRole = uItem.value("role", "primary");
                    if (uName.empty()) {
                        nlohmann::ordered_json resp;
                        resp["ok"] = false;
                        resp["error"] = "Unit username cannot be empty";
                        return resp.dump();
                    }
                    if (requestedUsers.count(uName)) {
                        nlohmann::ordered_json resp;
                        resp["ok"] = false;
                        resp["error"] = "Duplicate unit in dispatch approval: " + uName;
                        return resp.dump();
                    }
                    requestedUsers.insert(uName);

                    auto prof = g_registry.getProfile(uName);
                    if (!prof.has_value()) {
                        nlohmann::ordered_json resp;
                        resp["ok"] = false;
                        resp["error"] = "Unit not found: " + uName;
                        return resp.dump();
                    }

                    auto stJson = g_registry.getUnitStateJson(uName);
                    if (stJson.value("status", "") != "Available" || stJson["openAlert"] != nullptr) {
                        nlohmann::ordered_json resp;
                        resp["ok"] = false;
                        resp["error"] = "Unit is busy or unavailable: " + uName;
                        return resp.dump();
                    }

                    DispatchTarget dt;
                    dt.username = uName;
                    dt.role = uRole;
                    dt.kind = prof->kind;
                    dt.name = prof->name;

                    int uNode = graph.nearestNode(prof->lat, prof->lon);
                    if (uNode >= 0 && incidentNode >= 0) {
                        auto sp = graph.shortestPath(uNode, incidentNode);
                        dt.distanceMeters = sp.distanceMeters;
                        for (int nIdx : sp.nodes) {
                            if (nIdx >= 0 && nIdx < graph.nodeCount()) {
                                dt.routeCoordinates.push_back({graph.lon(nIdx), graph.lat(nIdx)});
                            }
                        }
                    }
                    if (dt.routeCoordinates.empty()) {
                        dt.routeCoordinates.push_back({prof->lon, prof->lat});
                        dt.routeCoordinates.push_back({inc->lon, inc->lat});
                    }
                    targets.push_back(dt);
                }
            } else {
                // Use stored proposal
                auto pIt = g_proposals.find(id);
                if (pIt == g_proposals.end()) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "No proposal found for incident";
                    return resp.dump();
                }

                const auto& prop = pIt->second;
                for (const auto& pick : prop.picks) {
                    if (!pick.found) continue;

                    std::string uName = pick.candidate.username;
                    auto prof = g_registry.getProfile(uName);
                    if (!prof.has_value()) {
                        nlohmann::ordered_json resp;
                        resp["ok"] = false;
                        resp["error"] = "Unit not found: " + uName;
                        return resp.dump();
                    }

                    auto stJson = g_registry.getUnitStateJson(uName);
                    if (stJson.value("status", "") != "Available" || stJson["openAlert"] != nullptr) {
                        nlohmann::ordered_json resp;
                        resp["ok"] = false;
                        resp["error"] = "Unit is busy or unavailable: " + uName;
                        return resp.dump();
                    }

                    DispatchTarget dt;
                    dt.username = uName;
                    dt.role = pick.slot.role;
                    dt.kind = rapidaid::dispatch::unitKindName(pick.slot.kind);
                    dt.name = pick.candidate.name;
                    dt.distanceMeters = pick.distanceMeters;

                    for (int nIdx : pick.pathNodes) {
                        if (nIdx >= 0 && nIdx < graph.nodeCount()) {
                            dt.routeCoordinates.push_back({graph.lon(nIdx), graph.lat(nIdx)});
                        }
                    }
                    if (dt.routeCoordinates.empty()) {
                        dt.routeCoordinates.push_back({pick.candidate.lon, pick.candidate.lat});
                        dt.routeCoordinates.push_back({inc->lon, inc->lat});
                    }
                    targets.push_back(dt);
                }

                if (targets.empty()) {
                    nlohmann::ordered_json resp;
                    resp["ok"] = false;
                    resp["error"] = "No units available to dispatch";
                    return resp.dump();
                }
            }

            // Create alerts for all units at once
            std::vector<rapidaid::dispatch::Assignment> newAssignments;
            for (size_t slotIdx = 0; slotIdx < targets.size(); ++slotIdx) {
                const auto& dt = targets[slotIdx];
                auto optAlert = g_registry.sendDispatchAlert(
                    dt.username,
                    inc->id,
                    dt.role,
                    dt.distanceMeters,
                    dt.routeCoordinates,
                    inc->type,
                    inc->severity,
                    inc->peopleAffected,
                    inc->locationText,
                    inc->lat,
                    inc->lon,
                    "",
                    60
                );

                rapidaid::dispatch::Assignment a;
                a.username = dt.username;
                a.name = dt.name;
                a.kind = dt.kind;
                a.role = dt.role;
                a.alertId = optAlert.has_value() ? optAlert->id : -1;
                a.distanceMeters = dt.distanceMeters;
                a.attempt = 1;
                a.state = "Pending";
                newAssignments.push_back(a);

                std::string slotKey = std::to_string(inc->id) + ":" + std::to_string(slotIdx);
                g_slotTriedUsernames[slotKey].insert(dt.username);
            }

            inc->assignments = newAssignments;
            inc->status = "Dispatched";
            inc->log.push_back({std::time(nullptr), "Dispatch approved"});
            g_incidentStore.save();

            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["incident"] = incidentToJson(*inc, std::time(nullptr));
            return resp.dump();
        }

        if (action == "cancel_incident") {
            int id = pj.value("incidentId", 0);
            if (id <= 0 && pj.contains("id") && pj["id"].is_number()) {
                id = pj["id"].get<int>();
            }
            std::string reason = pj.value("reason", "Cancelled by dispatcher");
            if (reason.empty()) reason = "Cancelled by dispatcher";

            std::lock_guard<std::mutex> stateLock(g_stateMutex);
            ensureIncidentsLoaded();

            auto* inc = g_incidentStore.find(id);
            if (!inc) {
                nlohmann::ordered_json resp;
                resp["ok"] = false;
                resp["error"] = "Incident not found";
                return resp.dump();
            }

            for (auto& a : inc->assignments) {
                if (a.alertId >= 0) {
                    g_registry.closeAlert(a.alertId, "Cleared", "Cancelled by dispatcher");
                }
                a.state = "Cancelled";
            }

            inc->status = "Cancelled";
            inc->log.push_back({std::time(nullptr), "Incident cancelled: " + reason});
            g_incidentStore.save();

            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["incident"] = incidentToJson(*inc, std::time(nullptr));
            return resp.dump();
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
        // Authorization check: Unit actions
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
