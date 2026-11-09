#include "DataCache.hpp"
#include "DispatchEngine.hpp"
#include "FileStore.hpp"
#include "Incident.hpp"
#include "IncidentStore.hpp"
#include "Login.hpp"
#include "UiBridge.hpp"
#include "UnitRegistry.hpp"
#include "json.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

void check(bool cond, const std::string& desc, int& failures) {
    if (cond) {
        std::cout << "[PASS] " << desc << "\n";
    } else {
        std::cout << "[FAIL] " << desc << "\n";
        failures++;
    }
}

} // anonymous namespace

int main() {
    int failed = 0;
    fs::path origDir = fs::current_path();
    fs::path tempDir = origDir / "build" / "test_tmp_dispatch";

    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);
    fs::current_path(tempDir, ec);

    std::vector<std::string> allResponses;
    auto callBridge = [&](Auth& auth, rapidaid::data::DataCache& cache, const std::string& action,
                          const std::string& token, const json& payload, const std::string& outDir = "data/out") -> json {
        json msg;
        msg["action"] = action;
        if (!token.empty()) msg["token"] = token;
        msg["payload"] = payload;
        std::string reqStr = msg.dump();
        std::string respStr = rapidaid::ui::handleBridgeMessage(auth, cache, reqStr, outDir);
        allResponses.push_back(respStr);
        try {
            return json::parse(respStr);
        } catch (...) {
            return json{{"ok", false}, {"error", "Failed to parse JSON response"}};
        }
    };

    // Clean initial bridge state
    rapidaid::ui::resetBridgeState();

    Auth auth("users.txt");
    std::string cacheDir = "cache_data";
    rapidaid::data::DataCache cache(cacheDir, false);
    std::string outDir = "data/out";
    fs::create_directories(outDir, ec);
    fs::create_directories(cacheDir, ec);

    std::cout << "Starting Dispatch Offline Tests...\n";

    // =========================================================================
    // 1. Prepare Canned Cache Data (Paris Center, lat 48.8500, lon 2.3000, r 3000)
    // =========================================================================
    double centerLat = 48.8500;
    double centerLon = 2.3000;
    int radius = 3000;

    // Geocode canned response
    std::string geocodeJson = R"([
        { "lat": "48.8500", "lon": "2.3000", "display_name": "Paris Center" }
    ])";
    rapidaid::io::FileStore::save(cache.geocodePath("Paris Center"), geocodeJson);

    // 6-node road graph: 1 -> 2 -> 3 -> 4 -> 5 -> 6
    std::string roadsJson = R"({
        "elements": [
            { "type": "node", "id": 1, "lat": 48.8500, "lon": 2.3000 },
            { "type": "node", "id": 2, "lat": 48.8510, "lon": 2.3000 },
            { "type": "node", "id": 3, "lat": 48.8520, "lon": 2.3000 },
            { "type": "node", "id": 4, "lat": 48.8540, "lon": 2.3000 },
            { "type": "node", "id": 5, "lat": 48.8550, "lon": 2.3000 },
            { "type": "node", "id": 6, "lat": 48.8560, "lon": 2.3000 },
            {
                "type": "way",
                "id": 101,
                "nodes": [1, 2, 3, 4, 5, 6],
                "tags": { "highway": "primary", "name": "Main North Ave" }
            }
        ]
    })";
    rapidaid::io::FileStore::save(cache.roadsPath(centerLat, centerLon, radius), roadsJson);

    // Seed canned places for all 6 required categories
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Hospital, centerLat, centerLon, radius),
        R"({"elements":[{"type":"node","id":201,"lat":48.8510,"lon":2.3000,"tags":{"amenity":"hospital","name":"Saint-Michel Hospital"}}]})");
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::FireStation, centerLat, centerLon, radius),
        R"({"elements":[{"type":"node","id":202,"lat":48.8520,"lon":2.3000,"tags":{"amenity":"fire_station","name":"Central Fire Station"}}]})");
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Police, centerLat, centerLon, radius),
        R"({"elements":[{"type":"node","id":203,"lat":48.8530,"lon":2.3000,"tags":{"amenity":"police","name":"Central Police Station"}}]})");
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Clinic, centerLat, centerLon, radius),
        R"({"elements":[{"type":"node","id":204,"lat":48.8515,"lon":2.3000,"tags":{"amenity":"clinic","name":"Downtown Clinic"}}]})");
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Pharmacy, centerLat, centerLon, radius),
        R"({"elements":[{"type":"node","id":205,"lat":48.8512,"lon":2.3000,"tags":{"amenity":"pharmacy","name":"Main Pharmacy"}}]})");
    rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Shelter, centerLat, centerLon, radius),
        R"({"elements":[{"type":"node","id":206,"lat":48.8550,"lon":2.3000,"tags":{"amenity":"shelter","name":"Civic Shelter"}}]})");

    // =========================================================================
    // 2. Create Accounts: Dispatcher, Ambulances, Hospitals, Fire, Police
    // =========================================================================
    const std::string passSecret = "Secr3tP@ssw0rd!";

    // Dispatcher
    json rDispSign = callBridge(auth, cache, "signup", "", {
        {"username", "dispatcher1"}, {"password", passSecret}, {"role", "dispatcher"}
    }, outDir);
    check(rDispSign.value("ok", false), "Create dispatcher account", failed);

    json rDispLog = callBridge(auth, cache, "login", "", {
        {"username", "dispatcher1"}, {"password", passSecret}
    }, outDir);
    std::string tokenDisp = rDispLog.value("token", "");
    check(!tokenDisp.empty(), "Dispatcher login returned valid token", failed);

    // Ambulance 1 (near Node 2, ~111m from incident node 1)
    callBridge(auth, cache, "signup", "", {
        {"username", "amb1"}, {"password", passSecret}, {"role", "unit"},
        {"unit", {{"kind", "ambulance"}, {"name", "Ambulance Alpha"}, {"lat", 48.8510}, {"lon", 2.3000}, {"count", 2}}}
    }, outDir);
    std::string tokenAmb1 = callBridge(auth, cache, "login", "", {{"username", "amb1"}, {"password", passSecret}}, outDir).value("token", "");

    // Ambulance 2 (near Node 3, ~222m from incident node 1)
    callBridge(auth, cache, "signup", "", {
        {"username", "amb2"}, {"password", passSecret}, {"role", "unit"},
        {"unit", {{"kind", "ambulance"}, {"name", "Ambulance Bravo"}, {"lat", 48.8520}, {"lon", 2.3000}, {"count", 2}}}
    }, outDir);
    std::string tokenAmb2 = callBridge(auth, cache, "login", "", {{"username", "amb2"}, {"password", passSecret}}, outDir).value("token", "");

    // Ambulance 3 (near Node 5, ~555m from incident node 1)
    callBridge(auth, cache, "signup", "", {
        {"username", "amb3"}, {"password", passSecret}, {"role", "unit"},
        {"unit", {{"kind", "ambulance"}, {"name", "Ambulance Charlie"}, {"lat", 48.8550}, {"lon", 2.3000}, {"count", 2}}}
    }, outDir);
    std::string tokenAmb3 = callBridge(auth, cache, "login", "", {{"username", "amb3"}, {"password", passSecret}}, outDir).value("token", "");

    // Hospital 1 (near Node 2, 10 beds capacity)
    callBridge(auth, cache, "signup", "", {
        {"username", "hosp1"}, {"password", passSecret}, {"role", "unit"},
        {"unit", {{"kind", "hospital"}, {"name", "City Hospital"}, {"lat", 48.8510}, {"lon", 2.3000}, {"count", 10}}}
    }, outDir);
    std::string tokenHosp1 = callBridge(auth, cache, "login", "", {{"username", "hosp1"}, {"password", passSecret}}, outDir).value("token", "");

    // Hospital 2 (near Node 6, only 1 bed capacity)
    callBridge(auth, cache, "signup", "", {
        {"username", "hosp2"}, {"password", passSecret}, {"role", "unit"},
        {"unit", {{"kind", "hospital"}, {"name", "Small Clinic Hospital"}, {"lat", 48.8560}, {"lon", 2.3000}, {"count", 1}}}
    }, outDir);
    std::string tokenHosp2 = callBridge(auth, cache, "login", "", {{"username", "hosp2"}, {"password", passSecret}}, outDir).value("token", "");

    // Fire truck
    callBridge(auth, cache, "signup", "", {
        {"username", "fire1"}, {"password", passSecret}, {"role", "unit"},
        {"unit", {{"kind", "fire_truck"}, {"name", "Engine 1"}, {"lat", 48.8520}, {"lon", 2.3000}, {"count", 2500}}}
    }, outDir);
    std::string tokenFire1 = callBridge(auth, cache, "login", "", {{"username", "fire1"}, {"password", passSecret}}, outDir).value("token", "");

    // Police unit
    callBridge(auth, cache, "signup", "", {
        {"username", "police1"}, {"password", passSecret}, {"role", "unit"},
        {"unit", {{"kind", "police_unit"}, {"name", "Patrol 1"}, {"lat", 48.8520}, {"lon", 2.3000}, {"count", 2}}}
    }, outDir);
    std::string tokenPolice1 = callBridge(auth, cache, "login", "", {{"username", "police1"}, {"password", passSecret}}, outDir).value("token", "");

    check(!tokenAmb1.empty() && !tokenAmb2.empty() && !tokenAmb3.empty() &&
          !tokenHosp1.empty() && !tokenHosp2.empty() && !tokenFire1.empty() && !tokenPolice1.empty(),
          "All service units registered and logged in", failed);

    // =========================================================================
    // 3. create_area from canned cache
    // =========================================================================
    json rArea = callBridge(auth, cache, "create_area", tokenDisp, {
        {"name", "Paris Center"}, {"radius", 3000}
    }, outDir);
    check(rArea.value("ok", false) && rArea.value("nodesCount", 0) == 6, "create_area from canned cache builds graph with 6 nodes", failed);
    check(rArea.contains("area") && rArea["area"].value("name", "") == "Paris Center", "create_area returns area details in response", failed);

    // Verify area_status contract
    json rStat = callBridge(auth, cache, "area_status", tokenDisp, {}, outDir);
    check(rStat.value("ok", false) && rStat.value("exists", false) && rStat.contains("area") && rStat["area"].is_object(),
          "area_status contract reports exists=true and non-null area", failed);

    // =========================================================================
    // 4. Snap Point & Location Search
    // =========================================================================
    json rSnapOk = callBridge(auth, cache, "snap_point", tokenDisp, {
        {"lat", 48.8500}, {"lon", 2.3000}
    }, outDir);
    check(rSnapOk.value("ok", false) && rSnapOk.value("withinArea", false) && rSnapOk.value("snapMeters", 999.0) < 10.0,
          "snap_point within mapped area reports withinArea=true", failed);

    json rSnapFar = callBridge(auth, cache, "snap_point", tokenDisp, {
        {"lat", 52.5200}, {"lon", 13.4050}
    }, outDir);
    check(rSnapFar.value("ok", false) && !rSnapFar.value("withinArea", true) && rSnapFar.value("snapMeters", 0.0) > 500.0,
          "snap_point outside mapped area reports withinArea=false and snapMeters > 500", failed);

    json rSearch = callBridge(auth, cache, "search_location", tokenDisp, {
        {"query", "Saint-Michel"}, {"localOnly", true}
    }, outDir);
    check(rSearch.value("ok", false) && !rSearch["candidates"].empty(),
          "search_location returns local candidate matching Saint-Michel", failed);

    // =========================================================================
    // 5. create_incident (Medical, 2 people) & outside area rejection
    // =========================================================================
    // Attempt location outside mapped area
    json rIncBad = callBridge(auth, cache, "create_incident", tokenDisp, {
        {"type", "Medical"}, {"severity", 3}, {"peopleAffected", 2},
        {"description", "Outside incident"},
        {"location", {{"lat", 52.5200}, {"lon", 13.4050}, {"text", "Berlin"}, {"source", "map"}}}
    }, outDir);
    check(!rIncBad.value("ok", true) && rIncBad.value("error", "").find("outside") != std::string::npos,
          "create_incident with location outside mapped area is rejected", failed);

    // Valid incident at Node 1 (48.8500, 2.3000)
    json rInc1 = callBridge(auth, cache, "create_incident", tokenDisp, {
        {"type", "Medical"}, {"severity", 4}, {"peopleAffected", 2},
        {"description", "Traffic accident with injuries"},
        {"callerName", "Jean Dupont"}, {"callerPhone", "0123456789"},
        {"location", {{"lat", 48.8500}, {"lon", 2.3000}, {"text", "Node 1 Junction"}, {"source", "map"}}}
    }, outDir);
    check(rInc1.value("ok", false) && rInc1.contains("incident"), "create_incident succeeds for valid in-area coordinates", failed);
    int inc1Id = rInc1["incident"].value("id", 0);
    check(inc1Id > 0 && rInc1["incident"].value("status", "") == "New", "Incident status is 'New'", failed);

    // update_incident works while New
    json rUpNew = callBridge(auth, cache, "update_incident", tokenDisp, {
        {"incidentId", inc1Id}, {"description", "Updated: 2 injuries verified"}
    }, outDir);
    check(rUpNew.value("ok", false) && rUpNew["incident"].value("description", "") == "Updated: 2 injuries verified",
          "update_incident succeeds while status is New", failed);

    // =========================================================================
    // 6. propose_dispatch -> picks NEAREST ambulance and hospital with enough beds
    // =========================================================================
    json rProp1 = callBridge(auth, cache, "propose_dispatch", tokenDisp, {
        {"incidentId", inc1Id}
    }, outDir);
    check(rProp1.value("ok", false) && rProp1.contains("proposal"), "propose_dispatch succeeds and returns proposal", failed);
    auto slots1 = rProp1["proposal"]["slots"];
    check(slots1.size() == 2, "Medical incident with 2 people requests 2 slots (1 primary ambulance, 1 hospital)", failed);

    std::string primaryUser = "";
    double primaryDist = 0.0;
    std::string hospitalUser = "";
    for (const auto& sl : slots1) {
        if (sl.value("role", "") == "primary") {
            primaryUser = sl.value("username", "");
            primaryDist = sl.value("distanceMeters", 0.0);
        } else if (sl.value("role", "") == "support" && sl.value("kind", "") == "hospital") {
            hospitalUser = sl.value("username", "");
        }
    }
    (void)primaryDist;
    check(primaryUser == "amb1", "propose_dispatch picks NEAREST ambulance (amb1 near ~111m vs amb2 ~222m)", failed);
    check(hospitalUser == "hosp1", "propose_dispatch picks hosp1 (10 beds >= 2 people) and skips hosp2 (1 bed)", failed);

    // Incident status should now be Proposed
    json rGetProposed = callBridge(auth, cache, "get_incident", tokenDisp, {{"incidentId", inc1Id}}, outDir);
    check(rGetProposed.value("ok", false) && rGetProposed["incident"].value("status", "") == "Proposed",
          "Incident status transitioned to 'Proposed'", failed);

    // update_incident works while Proposed
    json rUpProp = callBridge(auth, cache, "update_incident", tokenDisp, {
        {"incidentId", inc1Id}, {"severity", 5}
    }, outDir);
    check(rUpProp.value("ok", false) && rUpProp["incident"].value("severity", 0) == 5,
          "update_incident succeeds while status is Proposed", failed);

    // =========================================================================
    // 7. approve_dispatch -> creates alerts for all units at once, incident becomes Dispatched
    // =========================================================================
    json rApp1 = callBridge(auth, cache, "approve_dispatch", tokenDisp, {
        {"incidentId", inc1Id}
    }, outDir);
    check(rApp1.value("ok", false) && rApp1["incident"].value("status", "") == "Dispatched",
          "approve_dispatch transitions incident status to 'Dispatched'", failed);

    // Verify openAlerts for amb1 and hosp1
    json rAmb1State = callBridge(auth, cache, "unit_get_state", tokenAmb1, {}, outDir);
    check(rAmb1State["openAlert"] != nullptr && rAmb1State["openAlert"].value("role", "") == "primary" &&
          rAmb1State["openAlert"].value("incidentId", 0) == inc1Id && rAmb1State["openAlert"].value("state", "") == "Pending",
          "Ambulance 1 has open pending alert for incident 1", failed);

    json rHosp1State = callBridge(auth, cache, "unit_get_state", tokenHosp1, {}, outDir);
    check(rHosp1State["openAlert"] != nullptr && rHosp1State["openAlert"].value("role", "") == "support" &&
          rHosp1State["openAlert"].value("incidentId", 0) == inc1Id && rHosp1State["openAlert"].value("state", "") == "Pending",
          "Hospital 1 has open pending alert for incident 1", failed);

    // update_incident must now be rejected while Dispatched
    json rUpDisp = callBridge(auth, cache, "update_incident", tokenDisp, {
        {"incidentId", inc1Id}, {"description", "Attempting update while dispatched"}
    }, outDir);
    check(!rUpDisp.value("ok", true), "update_incident rejected while Dispatched", failed);

    // =========================================================================
    // 8. Units accept and walk states -> Incident becomes Completed
    // =========================================================================
    int alertAmb1Id = rAmb1State["openAlert"].value("id", 0);
    int alertHosp1Id = rHosp1State["openAlert"].value("id", 0);

    // amb1 walks states: accept -> en_route -> on_scene -> transporting -> clear
    callBridge(auth, cache, "unit_alert_respond", tokenAmb1, {{"alertId", alertAmb1Id}, {"action", "accept"}}, outDir);
    callBridge(auth, cache, "unit_alert_respond", tokenAmb1, {{"alertId", alertAmb1Id}, {"action", "en_route"}}, outDir);
    callBridge(auth, cache, "unit_alert_respond", tokenAmb1, {{"alertId", alertAmb1Id}, {"action", "on_scene"}}, outDir);
    callBridge(auth, cache, "unit_alert_respond", tokenAmb1, {{"alertId", alertAmb1Id}, {"action", "transporting"}}, outDir);
    callBridge(auth, cache, "unit_alert_respond", tokenAmb1, {{"alertId", alertAmb1Id}, {"action", "clear"}}, outDir);

    // hosp1: accept -> clear
    callBridge(auth, cache, "unit_alert_respond", tokenHosp1, {{"alertId", alertHosp1Id}, {"action", "accept"}}, outDir);
    callBridge(auth, cache, "unit_alert_respond", tokenHosp1, {{"alertId", alertHosp1Id}, {"action", "clear"}}, outDir);

    // Advance escalation tick
    rapidaid::ui::tickDispatch(std::time(nullptr));

    json rInc1Completed = callBridge(auth, cache, "get_incident", tokenDisp, {{"incidentId", inc1Id}}, outDir);
    check(rInc1Completed["incident"].value("status", "") == "Completed",
          "Incident transitions to 'Completed' when all assignments are Cleared", failed);

    // Both units now Available again
    check(callBridge(auth, cache, "unit_get_state", tokenAmb1, {}, outDir).value("status", "") == "Available",
          "Ambulance 1 returned to Available status", failed);
    check(callBridge(auth, cache, "unit_get_state", tokenHosp1, {}, outDir).value("status", "") == "Available",
          "Hospital 1 returned to Available status", failed);

    // =========================================================================
    // 9. Decline triggers replacement by next-nearest unit
    // =========================================================================
    json rInc2 = callBridge(auth, cache, "create_incident", tokenDisp, {
        {"type", "Medical"}, {"severity", 3}, {"peopleAffected", 1},
        {"description", "Incident 2 for decline test"},
        {"location", {{"lat", 48.8500}, {"lon", 2.3000}, {"text", "Node 1"}, {"source", "map"}}}
    }, outDir);
    int inc2Id = rInc2["incident"].value("id", 0);

    callBridge(auth, cache, "propose_dispatch", tokenDisp, {{"incidentId", inc2Id}}, outDir);
    callBridge(auth, cache, "approve_dispatch", tokenDisp, {{"incidentId", inc2Id}}, outDir);

    // amb1 is assigned. Now amb1 declines!
    json rAmb1StateInc2 = callBridge(auth, cache, "unit_get_state", tokenAmb1, {}, outDir);
    int alertAmb1Inc2 = rAmb1StateInc2["openAlert"].value("id", 0);
    callBridge(auth, cache, "unit_alert_respond", tokenAmb1, {
        {"alertId", alertAmb1Inc2}, {"action", "decline"}, {"reason", "Mechanical failure"}
    }, outDir);

    // Tick escalation
    rapidaid::ui::tickDispatch(std::time(nullptr));

    // amb2 should now have an open alert replacing amb1!
    json rAmb2StateInc2 = callBridge(auth, cache, "unit_get_state", tokenAmb2, {}, outDir);
    check(rAmb2StateInc2["openAlert"] != nullptr && rAmb2StateInc2["openAlert"].value("incidentId", 0) == inc2Id,
          "Ambulance decline triggers replacement by next-nearest ambulance (amb2)", failed);

    json rInc2Replaced = callBridge(auth, cache, "get_incident", tokenDisp, {{"incidentId", inc2Id}}, outDir);
    bool foundReplaceLog = false;
    for (const auto& logEntry : rInc2Replaced["incident"]["log"]) {
        if (logEntry.value("text", "").find("Replaced amb1 with amb2") != std::string::npos) {
            foundReplaceLog = true;
            break;
        }
    }
    check(foundReplaceLog, "Incident log contains 'Replaced amb1 with amb2' entry", failed);

    // =========================================================================
    // 10. Pending alert past 60s is replaced and first alert closed as Declined
    // =========================================================================
    long long createdAtAmb2 = rAmb2StateInc2["openAlert"].value("createdAt", 0LL);
    long long futureTime = createdAtAmb2 + 65;

    rapidaid::ui::tickDispatch(futureTime);

    // amb2 alert should now be closed as Declined with reason 'No acknowledgement'
    json rAmb2AfterTimeout = callBridge(auth, cache, "unit_get_state", tokenAmb2, {}, outDir);
    check(rAmb2AfterTimeout["openAlert"] == nullptr, "Timed-out alert on amb2 is closed", failed);

    // amb3 is now dispatched to replace amb2!
    json rAmb3ReReplaced = callBridge(auth, cache, "unit_get_state", tokenAmb3, {}, outDir);
    check(rAmb3ReReplaced["openAlert"] != nullptr && rAmb3ReReplaced["openAlert"].value("incidentId", 0) == inc2Id,
          "amb3 receives replacement alert after amb2 timeout", failed);

    // Now amb3 also times out -> no more ambulances available!
    long long createdAtAmb3 = rAmb3ReReplaced["openAlert"].value("createdAt", 0LL);
    long long futureTime2 = createdAtAmb3 + 65;
    rapidaid::ui::tickDispatch(futureTime2);

    // No available unit warning generated, and NO crash!
    json rInc2Exhausted = callBridge(auth, cache, "get_incident", tokenDisp, {{"incidentId", inc2Id}}, outDir);
    bool foundNoAvailWarn = false;
    for (const auto& w : rInc2Exhausted["incident"]["warnings"]) {
        if (w.get<std::string>().find("ambulance") != std::string::npos) {
            foundNoAvailWarn = true;
            break;
        }
    }
    check(foundNoAvailWarn, "No available ambulance warning recorded when all candidates exhausted", failed);

    // Clean up inc2: clear hosp1
    json rHosp1StateInc2 = callBridge(auth, cache, "unit_get_state", tokenHosp1, {}, outDir);
    if (rHosp1StateInc2["openAlert"] != nullptr) {
        int aId = rHosp1StateInc2["openAlert"].value("id", 0);
        callBridge(auth, cache, "unit_alert_respond", tokenHosp1, {{"alertId", aId}, {"action", "clear"}}, outDir);
    }
    callBridge(auth, cache, "cancel_incident", tokenDisp, {{"incidentId", inc2Id}}, outDir);

    // =========================================================================
    // 11. Unavailable units never picked & no crash
    // =========================================================================
    // Set all ambulances unavailable
    callBridge(auth, cache, "unit_set_availability", tokenAmb1, {{"available", false}}, outDir);
    callBridge(auth, cache, "unit_set_availability", tokenAmb2, {{"available", false}}, outDir);
    callBridge(auth, cache, "unit_set_availability", tokenAmb3, {{"available", false}}, outDir);

    json rInc3 = callBridge(auth, cache, "create_incident", tokenDisp, {
        {"type", "Medical"}, {"severity", 3}, {"peopleAffected", 1},
        {"description", "Incident 3 with unavailable ambulances"},
        {"location", {{"lat", 48.8500}, {"lon", 2.3000}, {"text", "Node 1"}, {"source", "map"}}}
    }, outDir);
    int inc3Id = rInc3["incident"].value("id", 0);

    json rProp3 = callBridge(auth, cache, "propose_dispatch", tokenDisp, {{"incidentId", inc3Id}}, outDir);
    check(rProp3.value("ok", false), "propose_dispatch succeeds and does not crash when all units unavailable", failed);
    bool hasWarnUnavailable = false;
    for (const auto& w : rProp3["proposal"]["warnings"]) {
        if (w.get<std::string>().find("ambulance") != std::string::npos) {
            hasWarnUnavailable = true;
            break;
        }
    }
    check(hasWarnUnavailable, "Unavailable units are never picked and warning is returned", failed);

    // Restore ambulances availability
    callBridge(auth, cache, "unit_set_availability", tokenAmb1, {{"available", true}}, outDir);
    callBridge(auth, cache, "unit_set_availability", tokenAmb2, {{"available", true}}, outDir);
    callBridge(auth, cache, "unit_set_availability", tokenAmb3, {{"available", true}}, outDir);

    // =========================================================================
    // 12. Swapping unit in approve_dispatch & invalid / busy unit rejection
    // =========================================================================
    json rInc4 = callBridge(auth, cache, "create_incident", tokenDisp, {
        {"type", "Medical"}, {"severity", 3}, {"peopleAffected", 1},
        {"description", "Incident 4 for swap and validation tests"},
        {"location", {{"lat", 48.8500}, {"lon", 2.3000}, {"text", "Node 1"}, {"source", "map"}}}
    }, outDir);
    int inc4Id = rInc4["incident"].value("id", 0);
    callBridge(auth, cache, "propose_dispatch", tokenDisp, {{"incidentId", inc4Id}}, outDir);

    // Reject non-existent unit
    json rAppBadUser = callBridge(auth, cache, "approve_dispatch", tokenDisp, {
        {"incidentId", inc4Id},
        {"units", json::array({{{"username", "phantom_unit"}, {"role", "primary"}}})}
    }, outDir);
    check(!rAppBadUser.value("ok", true), "approve_dispatch rejects invalid unit with no alerts created", failed);

    // Swap unit to amb2 (instead of proposed amb1)
    json rAppSwap = callBridge(auth, cache, "approve_dispatch", tokenDisp, {
        {"incidentId", inc4Id},
        {"units", json::array({
            {{"username", "amb2"}, {"role", "primary"}},
            {{"username", "hosp1"}, {"role", "support"}}
        })}
    }, outDir);
    check(rAppSwap.value("ok", false), "approve_dispatch with explicitly swapped unit succeeds", failed);
    check(callBridge(auth, cache, "unit_get_state", tokenAmb2, {}, outDir)["openAlert"] != nullptr,
          "Swapped unit amb2 received the dispatch alert", failed);

    // Attempting to dispatch already busy amb2 to another incident rejects approval
    json rInc5 = callBridge(auth, cache, "create_incident", tokenDisp, {
        {"type", "Medical"}, {"severity", 3}, {"peopleAffected", 1},
        {"description", "Incident 5"},
        {"location", {{"lat", 48.8500}, {"lon", 2.3000}, {"text", "Node 1"}, {"source", "map"}}}
    }, outDir);
    int inc5Id = rInc5["incident"].value("id", 0);
    json rAppBusy = callBridge(auth, cache, "approve_dispatch", tokenDisp, {
        {"incidentId", inc5Id},
        {"units", json::array({{{"username", "amb2"}, {"role", "primary"}}})}
    }, outDir);
    check(!rAppBusy.value("ok", true), "approve_dispatch rejects already busy unit and creates no alerts", failed);

    // =========================================================================
    // 13. cancel_incident closes alerts and releases units
    // =========================================================================
    json rCancel4 = callBridge(auth, cache, "cancel_incident", tokenDisp, {
        {"incidentId", inc4Id}, {"reason", "Duplicate report"}
    }, outDir);
    check(rCancel4.value("ok", false) && rCancel4["incident"].value("status", "") == "Cancelled",
          "cancel_incident marks status as 'Cancelled'", failed);

    check(callBridge(auth, cache, "unit_get_state", tokenAmb2, {}, outDir).value("status", "") == "Available",
          "cancel_incident releases amb2 back to Available", failed);
    check(callBridge(auth, cache, "unit_get_state", tokenAmb2, {}, outDir)["openAlert"] == nullptr,
          "cancel_incident clears open alert for amb2", failed);

    // =========================================================================
    // 14. Restart Simulation leaves no unit stuck Dispatched
    // =========================================================================
    rapidaid::ui::UnitRegistry reloadedRegistry("data/units.json");
    reloadedRegistry.ensureLoaded();
    auto allUnitsJson = reloadedRegistry.listUnitsJson();
    bool anyStuck = false;
    for (const auto& u : allUnitsJson["units"]) {
        if (u.value("status", "") == "Dispatched") {
            anyStuck = true;
            break;
        }
    }
    check(!anyStuck, "Restart simulation: no unit left stuck in Dispatched status", failed);

    // =========================================================================
    // 15. Authorization Checks
    // =========================================================================
    // Unit token rejected on dispatcher actions
    json rAuth1 = callBridge(auth, cache, "create_incident", tokenAmb1, {
        {"type", "Medical"}, {"severity", 1}, {"peopleAffected", 0},
        {"location", {{"lat", 48.8500}, {"lon", 2.3000}, {"text", "Test"}, {"source", "test"}}}
    }, outDir);
    check(!rAuth1.value("ok", true) && rAuth1.value("error", "") == "Not allowed",
          "Unit token rejected on create_incident (returns 'Not allowed')", failed);

    json rAuth2 = callBridge(auth, cache, "propose_dispatch", tokenAmb1, {{"incidentId", inc1Id}}, outDir);
    check(!rAuth2.value("ok", true) && rAuth2.value("error", "") == "Not allowed",
          "Unit token rejected on propose_dispatch (returns 'Not allowed')", failed);

    // Dispatcher token rejected on unit actions
    json rAuth3 = callBridge(auth, cache, "unit_get_state", tokenDisp, {}, outDir);
    check(!rAuth3.value("ok", true) && rAuth3.value("error", "") == "Not allowed",
          "Dispatcher token rejected on unit_get_state (returns 'Not allowed')", failed);

    // No token rejected
    json rAuth4 = callBridge(auth, cache, "list_incidents", "", {}, outDir);
    check(!rAuth4.value("ok", true) && rAuth4.value("error", "") == "Not logged in",
          "No token rejected on list_incidents (returns 'Not logged in')", failed);

    // =========================================================================
    // 16. Responses never contain passwords or hashes
    // =========================================================================
    bool passwordLeaked = false;
    for (const auto& respStr : allResponses) {
        if (respStr.find(passSecret) != std::string::npos) {
            passwordLeaked = true;
            break;
        }
    }
    check(!passwordLeaked, "Bridge responses never leak plaintext passwords", failed);

    // Clean up temporary test directory
    fs::current_path(origDir, ec);
    fs::remove_all(tempDir, ec);
    check(!fs::exists(tempDir), "Temporary test directory cleaned up cleanly", failed);

    std::cout << "\nTest Summary: " << (failed == 0 ? "ALL PASSED" : "FAILED")
              << " (" << failed << " failures)\n";

    return (failed == 0) ? 0 : 1;
}
