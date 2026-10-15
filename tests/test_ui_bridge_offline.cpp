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

    // 1. Test Auth & SignUp Bridge
    Auth auth(usersPath);
    
    // Login with seeded admin
    std::string loginAdminReq = R"({"username":"admin","password":"admin123"})";
    std::string loginAdminResp = handleLoginRequest(auth, loginAdminReq);
    json jAdmin = json::parse(loginAdminResp);
    check(jAdmin["ok"] == true && jAdmin["role"] == "admin", "Admin login succeeds via bridge", failed);

    // Signup new dispatcher
    std::string signupReq = R"({"username":"sarah","password":"secret456","role":"dispatcher"})";
    std::string signupResp = handleSignUpRequest(auth, signupReq);
    json jSignup = json::parse(signupResp);
    check(jSignup["ok"] == true && jSignup["username"] == "sarah", "SignUp new dispatcher succeeds", failed);

    // Duplicate signup fails
    std::string signupDupResp = handleSignUpRequest(auth, signupReq);
    json jSignupDup = json::parse(signupDupResp);
    check(jSignupDup["ok"] == false && jSignupDup["error"] == "Username already exists", "Duplicate signup fails gracefully", failed);

    // Login with new dispatcher
    std::string loginSarahReq = R"({"username":"sarah","password":"secret456"})";
    std::string loginSarahResp = handleLoginRequest(auth, loginSarahReq);
    json jSarah = json::parse(loginSarahResp);
    check(jSarah["ok"] == true && jSarah["role"] == "dispatcher", "New dispatcher login succeeds", failed);

    // 2. Test Area Status Bridge (Before file creation)
    std::string statusResp1 = rapidaid::ui::handleAreaStatusRequest(outDir);
    json jStat1 = json::parse(statusResp1);
    check(jStat1["ok"] == true && jStat1["exists"] == false, "Area status reports false when no GeoJSON exists", failed);

    // 3. Prepare Canned Cache Data for Offline Pipeline Testing
    rapidaid::data::DataCache cache(cacheDir, false);
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
    check(jCreate["placesCount"] == 1, "Places count matches hospital data", failed);
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

    // 9. Test Stats Bridge
    std::string statsResp = rapidaid::ui::handleGetStatsRequest();
    json jStats = json::parse(statsResp);
    check(jStats["ok"] == true && jStats["status"] == "under_development", "handleGetStatsRequest returns stats payload", failed);

    // 10. Test Unified Bridge Router
    std::string bridgeMsg = R"({"action":"health","payload":{}})";
    std::string bridgeResp = rapidaid::ui::handleBridgeMessage(auth, cache, bridgeMsg, outDir);
    json jBridge = json::parse(bridgeResp);
    check(jBridge["ok"] == true && jBridge["service"] == "login", "Unified bridge dispatches health action", failed);

    // Clean up temporary test directory
    fs::remove_all(tempDir, ec);
    check(!fs::exists(tempDir), "Temporary test directory cleaned up", failed);

    std::cout << "\nTest Summary: " << (failed == 0 ? "ALL PASSED" : "FAILED")
              << " (" << failed << " failures)\n";
    return (failed == 0) ? 0 : 1;
}
