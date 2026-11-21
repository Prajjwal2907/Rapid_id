#include "DataCache.hpp"
#include "FileStore.hpp"
#include "Login.hpp"
#include "UiBridge.hpp"
#include "json.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

static void check(bool condition, const std::string& testName, int& failed) {
    if (condition) {
        std::cout << "[PASS] " << testName << "\n";
    } else {
        std::cout << "[FAIL] " << testName << "\n";
        failed++;
    }
}

int main() {
    int failed = 0;
    const std::string tempDir = "build/test_tmp_ui";

    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    std::string usersPath = tempDir + "/users.txt";
    std::string cacheDir = tempDir + "/cache";
    std::string outDir = tempDir + "/out";
    fs::create_directories(cacheDir, ec);
    fs::create_directories(outDir, ec);

    rapidaid::data::DataCache cache(cacheDir, false);

    // 1. Test Auth & SignUp Bridge using AuthOptions
    AuthOptions opts;
    opts.usersPath = usersPath;
    opts.keyPath = tempDir + "/auth.key";
    opts.bootstrapAdminPassword = "Admin-Test-Pass-9";
    opts.pbkdf2Iterations = 1000;
    opts.announceBootstrap = false;
    Auth auth(opts);

    // Login with seeded admin
    std::string loginAdminReq = R"({"username":"admin","password":"Admin-Test-Pass-9"})";
    std::string loginAdminResp = handleLoginRequest(auth, loginAdminReq);
    json jAdmin = json::parse(loginAdminResp);
    check(jAdmin["ok"] == true && jAdmin["role"] == "admin", "Admin login succeeds via bridge", failed);

    // Signup new dispatcher (self-signup -> pending)
    std::string signupReq = R"({"action":"signup","payload":{"username":"sarah","password":"Secure-Dispat-Pass-1!","role":"dispatcher"}})";
    std::string signupResp = rapidaid::ui::handleBridgeMessage(auth, cache, signupReq, outDir);
    json jSignup = json::parse(signupResp);
    check(jSignup["ok"] == true && jSignup["username"] == "sarah", "SignUp new dispatcher succeeds", failed);

    // Duplicate signup fails
    std::string signupDupResp = rapidaid::ui::handleBridgeMessage(auth, cache, signupReq, outDir);
    json jSignupDup = json::parse(signupDupResp);
    check(jSignupDup["ok"] == false && jSignupDup["error"] == "Username already exists", "Duplicate signup fails gracefully", failed);

    // Login with pending dispatcher fails
    std::string loginSarahReq = R"({"username":"sarah","password":"Secure-Dispat-Pass-1!"})";
    std::string loginSarahPendingResp = handleLoginRequest(auth, loginSarahReq);
    json jSarahPending = json::parse(loginSarahPendingResp);
    check(jSarahPending["ok"] == false && jSarahPending["code"] == "pending", "Pending dispatcher cannot log in", failed);

    // Admin approves dispatcher
    std::string appErr;
    bool approved = auth.approve("sarah", appErr);
    check(approved, "Admin approval of sarah succeeds", failed);

    // Login with approved dispatcher succeeds
    std::string loginSarahResp = handleLoginRequest(auth, loginSarahReq);
    json jSarah = json::parse(loginSarahResp);
    check(jSarah["ok"] == true && jSarah["role"] == "dispatcher", "New dispatcher login succeeds", failed);

    // 2. Test Area Status Bridge (Before file creation)
    std::string statusResp1 = rapidaid::ui::handleAreaStatusRequest(outDir);
    json jStat1 = json::parse(statusResp1);
    check(jStat1["ok"] == true && jStat1["exists"] == false, "Area status reports false when no GeoJSON exists", failed);

    // 3. Prepare Canned Cache Data for Offline Pipeline Testing
    std::string query = "Paris Center";
    std::string geocodeJson = R"([
        {
            "lat": "48.8500",
            "lon": "2.3000",
            "display_name": "Paris Center"
        }
    ])";
    rapidaid::io::FileStore::save(cache.geocodePath(query), geocodeJson);

    double lat = 48.8500;
    double lon = 2.3000;
    int radius = 3000;
    std::string placesJson = R"({
        "elements": [
            {
                "type": "node",
                "id": 201,
                "lat": 48.8500,
                "lon": 2.3000,
                "tags": { "amenity": "hospital", "name": "Center Hospital" }
            }
        ]
    })";
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Hospital, lat, lon, radius), placesJson);
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::FireStation, lat, lon, radius),
        R"({"elements":[{"type":"node","id":202,"lat":48.8500,"lon":2.3000,"tags":{"amenity":"fire_station","name":"Fire Station"}}]})");
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Police, lat, lon, radius),
        R"({"elements":[{"type":"node","id":203,"lat":48.8500,"lon":2.3000,"tags":{"amenity":"police","name":"Police Station"}}]})");
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Clinic, lat, lon, radius),
        R"({"elements":[{"type":"node","id":204,"lat":48.8500,"lon":2.3000,"tags":{"amenity":"clinic","name":"Clinic"}}]})");
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Pharmacy, lat, lon, radius),
        R"({"elements":[{"type":"node","id":205,"lat":48.8500,"lon":2.3000,"tags":{"amenity":"pharmacy","name":"Pharmacy"}}]})");
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Shelter, lat, lon, radius),
        R"({"elements":[{"type":"node","id":206,"lat":48.8500,"lon":2.3000,"tags":{"amenity":"shelter","name":"Shelter"}}]})");

    std::string roadsJson = R"({
        "elements": [
            { "type": "node", "id": 1, "lat": 48.8500, "lon": 2.3000 },
            { "type": "node", "id": 2, "lat": 48.8510, "lon": 2.3010 },
            {
                "type": "way",
                "id": 101,
                "nodes": [1, 2],
                "tags": { "highway": "primary", "name": "Main Blvd" }
            }
        ]
    })";
    rapidaid::io::FileStore::save(cache.roadsPath(lat, lon, radius), roadsJson);

    // 4. Test Geocode Bridge
    std::string geoReq = R"({"query":"Paris Center"})";
    std::string geoResp = rapidaid::ui::handleGeocodeRequest(cache, geoReq);
    json jGeo = json::parse(geoResp);
    check(jGeo["ok"] == true && jGeo["lat"] == 48.8500 && jGeo["lon"] == 2.3000, "handleGeocodeRequest resolves coordinates from cache", failed);

    // 5. Test Create Area Pipeline Bridge
    std::string createReq = R"({
        "name": "Paris Center",
        "radius": 3000,
        "category": "hospital"
    })";
    std::string createResp = rapidaid::ui::handleCreateAreaRequest(cache, createReq, outDir);
    json jCreate = json::parse(createResp);
    check(jCreate["ok"] == true, "handleCreateAreaRequest completes pipeline", failed);
    check(jCreate["nodesCount"] == 2, "Nodes count matches road network", failed);
    check(jCreate["edgesCount"] == 1, "Edges count matches road network", failed);
    check(jCreate["placesCount"] >= 1, "Places count matches hospital data", failed);
    check(fs::exists(outDir + "/roads.geojson"), "Permanent roads.geojson created on disk", failed);
    check(fs::exists(outDir + "/places.geojson"), "Permanent places.geojson created on disk", failed);

    // 6. Test Area Status Bridge (After file creation)
    std::string statusResp2 = rapidaid::ui::handleAreaStatusRequest(outDir);
    json jStat2 = json::parse(statusResp2);
    check(jStat2["ok"] == true && jStat2["exists"] == true, "Area status reports true when GeoJSON files exist", failed);

    // 7. Test Get GeoJSON Bridge
    std::string roadsGeoResp = rapidaid::ui::handleGetGeoJsonRequest("roads", outDir);
    json jRoads = json::parse(roadsGeoResp);
    check(jRoads["ok"] == true && jRoads.contains("geojson"), "handleGetGeoJsonRequest returns roads geojson", failed);

    // 8. Test Incident Handling
    std::string incReq = R"({
        "title": "Medical emergency at boulevard",
        "location": "Main Blvd & 1st Ave",
        "type": "Medical",
        "priority": "Critical",
        "notes": "Patient requires immediate transport"
    })";
    std::string addIncResp = rapidaid::ui::handleAddIncidentRequest(incReq);
    json jAddInc = json::parse(addIncResp);
    check(jAddInc["ok"] == true && jAddInc["status"] == "under_development", "handleAddIncidentRequest records incident", failed);

    std::string getIncResp = rapidaid::ui::handleGetIncidentsRequest();
    json jGetInc = json::parse(getIncResp);
    check(jGetInc["ok"] == true && jGetInc["count"] >= 1, "handleGetIncidentsRequest returns active queue", failed);

    // 9. Test Stats Bridge per Contract C
    std::string statsResp = rapidaid::ui::handleGetStatsRequest();
    json jStats = json::parse(statsResp);
    check(jStats["ok"] == true && jStats.contains("incidents") && jStats.contains("alerts") && jStats.contains("recent"),
          "handleGetStatsRequest returns stats payload per Contract C", failed);

    // 10. Test Unified Bridge Router
    std::string bridgeMsg = R"({"action":"health","payload":{}})";
    std::string bridgeResp = rapidaid::ui::handleBridgeMessage(auth, cache, bridgeMsg, outDir);
    json jBridge = json::parse(bridgeResp);
    check(jBridge["ok"] == true && jBridge["service"] == "login", "Unified bridge dispatches health action", failed);

    // Clean up temporary test directory and logs
    fs::remove("log.txt", ec);
    fs::remove_all(tempDir, ec);
    check(!fs::exists(tempDir), "Temporary test directory cleaned up", failed);

    std::cout << "\nTest Summary: " << (failed == 0 ? "ALL PASSED" : "FAILED")
              << " (" << failed << " failures)\n";
    return (failed == 0) ? 0 : 1;
}
