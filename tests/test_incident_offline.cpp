#include "Incident.hpp"
#include "IncidentStore.hpp"
#include "FileStore.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

using rapidaid::dispatch::Assignment;
using rapidaid::dispatch::Incident;
using rapidaid::dispatch::IncidentStore;
using rapidaid::dispatch::LogEntry;
using rapidaid::dispatch::UnitKind;
using rapidaid::dispatch::parseUnitKind;
using rapidaid::dispatch::priorityScore;
using rapidaid::dispatch::unitKindName;
using rapidaid::io::FileStore;

static int failures = 0;

static void assertTest(bool condition, const std::string& testName) {
    if (condition) {
        std::cout << "[PASS] " << testName << "\n";
    } else {
        std::cout << "[FAIL] " << testName << "\n";
        failures++;
    }
}

int main() {
    std::cout << "Running test_incident_offline...\n";

    const std::string testDir = "build/test_tmp_incident";
    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);
    std::filesystem::create_directories(testDir, ec);

    // 1. priorityScore calculation
    {
        double s1 = priorityScore(3, 0, 5.0);
        assertTest(std::abs(s1 - 35.0) < 1e-6, "priorityScore(3, 0, 5.0) == 35.0");

        double s2 = priorityScore(5, 10, 12.5);
        assertTest(std::abs(s2 - 82.5) < 1e-6, "priorityScore(5, 10, 12.5) == 82.5");

        double s3 = priorityScore(1, 2, 0.0);
        assertTest(std::abs(s3 - 14.0) < 1e-6, "priorityScore(1, 2, 0.0) == 14.0");
    }

    // 2. unitKindName and parseUnitKind
    {
        assertTest(unitKindName(UnitKind::Ambulance) == "ambulance", "unitKindName(Ambulance) == ambulance");
        assertTest(unitKindName(UnitKind::FireTruck) == "fire_truck", "unitKindName(FireTruck) == fire_truck");
        assertTest(unitKindName(UnitKind::PoliceUnit) == "police_unit", "unitKindName(PoliceUnit) == police_unit");
        assertTest(unitKindName(UnitKind::Hospital) == "hospital", "unitKindName(Hospital) == hospital");
        assertTest(unitKindName(UnitKind::Shelter) == "shelter", "unitKindName(Shelter) == shelter");

        UnitKind kind = UnitKind::Hospital;
        assertTest(parseUnitKind("ambulance", kind) && kind == UnitKind::Ambulance, "parseUnitKind ambulance");
        assertTest(parseUnitKind("fire_truck", kind) && kind == UnitKind::FireTruck, "parseUnitKind fire_truck");
        assertTest(parseUnitKind("police_unit", kind) && kind == UnitKind::PoliceUnit, "parseUnitKind police_unit");
        assertTest(parseUnitKind("hospital", kind) && kind == UnitKind::Hospital, "parseUnitKind hospital");
        assertTest(parseUnitKind("shelter", kind) && kind == UnitKind::Shelter, "parseUnitKind shelter");

        // Failure case: invalid string does not change kind
        kind = UnitKind::Ambulance;
        assertTest(!parseUnitKind("unknown_kind", kind) && kind == UnitKind::Ambulance, "parseUnitKind invalid string fails safely");
        assertTest(!parseUnitKind("", kind) && kind == UnitKind::Ambulance, "parseUnitKind empty string fails safely");
    }

    // 3. IncidentStore save/load round-trip and no .tmp left behind
    {
        std::string storePath = testDir + "/incidents_roundtrip.json";
        IncidentStore store(storePath);

        Incident inc;
        inc.type = "Medical";
        inc.severity = 4;
        inc.peopleAffected = 2;
        inc.description = "Cardiac arrest";
        inc.callerName = "Alice";
        inc.callerPhone = "555-0100";
        inc.locationText = "123 Elm St";
        inc.locationSource = "geocoded";
        inc.lat = 12.9716;
        inc.lon = 77.5946;
        inc.snapMeters = 4.2;
        inc.nodeOsmId = 12345678LL;
        inc.createdAt = 1600000000LL;
        inc.status = "Assigned";

        Assignment a;
        a.username = "amb_unit1";
        a.name = "Ambulance 1";
        a.kind = "ambulance";
        a.role = "primary";
        a.alertId = 101;
        a.distanceMeters = 850.5;
        a.attempt = 1;
        a.state = "EnRoute";
        inc.assignments.push_back(a);

        inc.warnings.push_back("High priority");
        inc.log.push_back({1600000005LL, "Incident created"});
        inc.log.push_back({1600000010LL, "Unit assigned"});

        int assignedId = store.add(inc);
        assertTest(assignedId == 1, "store.add assigns ID 1 to first incident");
        assertTest(store.save(), "store.save succeeds");

        // Verify no .tmp file exists
        assertTest(!std::filesystem::exists(storePath + ".tmp"), "no .tmp file left behind after save");

        // Reload in a fresh store instance
        IncidentStore loadedStore(storePath);
        assertTest(loadedStore.load(), "loadedStore.load succeeds");
        assertTest(loadedStore.all().size() == 1, "loadedStore contains exactly 1 incident");

        Incident* found = loadedStore.find(1);
        assertTest(found != nullptr, "loadedStore.find(1) returns incident");
        if (found != nullptr) {
            assertTest(found->id == 1, "loaded incident id == 1");
            assertTest(found->type == "Medical", "loaded incident type == Medical");
            assertTest(found->severity == 4, "loaded incident severity == 4");
            assertTest(found->peopleAffected == 2, "loaded incident peopleAffected == 2");
            assertTest(found->description == "Cardiac arrest", "loaded incident description matches");
            assertTest(found->callerName == "Alice", "loaded incident callerName matches");
            assertTest(found->callerPhone == "555-0100", "loaded incident callerPhone matches");
            assertTest(found->locationText == "123 Elm St", "loaded incident locationText matches");
            assertTest(found->locationSource == "geocoded", "loaded incident locationSource matches");
            assertTest(std::abs(found->lat - 12.9716) < 1e-4, "loaded incident lat matches");
            assertTest(std::abs(found->lon - 77.5946) < 1e-4, "loaded incident lon matches");
            assertTest(std::abs(found->snapMeters - 4.2) < 1e-4, "loaded incident snapMeters matches");
            assertTest(found->nodeOsmId == 12345678LL, "loaded incident nodeOsmId matches");
            assertTest(found->createdAt == 1600000000LL, "loaded incident createdAt matches");
            assertTest(found->status == "Assigned", "loaded incident status matches");

            assertTest(found->assignments.size() == 1, "loaded assignments count == 1");
            if (!found->assignments.empty()) {
                assertTest(found->assignments[0].username == "amb_unit1", "assignment username matches");
                assertTest(found->assignments[0].name == "Ambulance 1", "assignment name matches");
                assertTest(found->assignments[0].kind == "ambulance", "assignment kind matches");
                assertTest(found->assignments[0].role == "primary", "assignment role matches");
                assertTest(found->assignments[0].alertId == 101, "assignment alertId matches");
                assertTest(std::abs(found->assignments[0].distanceMeters - 850.5) < 1e-4, "assignment distance matches");
                assertTest(found->assignments[0].attempt == 1, "assignment attempt matches");
                assertTest(found->assignments[0].state == "EnRoute", "assignment state matches");
            }

            assertTest(found->warnings.size() == 1 && found->warnings[0] == "High priority", "warnings match");
            assertTest(found->log.size() == 2, "log entries count == 2");
            if (found->log.size() == 2) {
                assertTest(found->log[0].time == 1600000005LL && found->log[0].text == "Incident created", "log 0 matches");
                assertTest(found->log[1].time == 1600000010LL && found->log[1].text == "Unit assigned", "log 1 matches");
            }
        }
    }

    // 4. Missing file handling
    {
        std::string missingPath = testDir + "/missing_incidents.json";
        IncidentStore store(missingPath);
        assertTest(store.load(), "load on missing file returns true");
        assertTest(store.all().empty(), "load on missing file results in empty store");
        assertTest(store.find(1) == nullptr, "find on empty store returns nullptr");
    }

    // 5. Corrupt file handling
    {
        std::string corruptPath = testDir + "/corrupt_incidents.json";
        FileStore::save(corruptPath, "{not valid json at all");
        IncidentStore store(corruptPath);
        assertTest(!store.load(), "load on corrupt syntax returns false");
        assertTest(store.all().empty(), "store remains empty after corrupt load");

        // Non-array JSON
        FileStore::save(corruptPath, "{\"id\": 1, \"type\": \"Medical\"}");
        assertTest(!store.load(), "load on non-array JSON returns false");
        assertTest(store.all().empty(), "store remains empty after non-array JSON");
    }

    // 6. ID auto-allocation, explicit IDs, and clear()
    {
        IncidentStore store(testDir + "/ids.json");
        Incident inc1;
        inc1.type = "Fire";
        int id1 = store.add(inc1);
        assertTest(id1 == 1, "first auto id == 1");

        Incident inc2;
        inc2.id = 50;
        int id2 = store.add(inc2);
        assertTest(id2 == 50, "explicit id 50 preserved");

        Incident inc3;
        int id3 = store.add(inc3);
        assertTest(id3 == 51, "next auto id jumps to 51");

        assertTest(store.find(50) != nullptr, "find(50) succeeds");
        assertTest(store.find(999) == nullptr, "find(999) returns nullptr");

        store.clear();
        assertTest(store.all().empty(), "clear() empties the store");

        Incident inc4;
        int id4 = store.add(inc4);
        assertTest(id4 == 1, "auto id resets to 1 after clear()");
    }

    // 7. Isolation check: working directory chdir
    {
        std::string cwdTestDir = testDir + "/cwd_isolated";
        std::filesystem::create_directories(cwdTestDir, ec);

        auto originalCwd = std::filesystem::current_path();
        std::filesystem::current_path(cwdTestDir, ec);

        IncidentStore defaultStore; // uses "data/incidents.json" in cwdTestDir
        Incident inc;
        inc.type = "Other";
        defaultStore.add(inc);
        assertTest(defaultStore.save(), "default path save in isolated cwd succeeds");
        assertTest(std::filesystem::exists("data/incidents.json"), "isolated data/incidents.json created");
        assertTest(!std::filesystem::exists("data/incidents.json.tmp"), "no .tmp left in isolated cwd");

        std::filesystem::current_path(originalCwd, ec);
    }

    // Cleanup temp directory
    std::filesystem::remove_all(testDir, ec);
    assertTest(!std::filesystem::exists(testDir), "temporary test directory cleaned up");

    return failures == 0 ? 0 : 1;
}
