#include "AreaContext.hpp"
#include "DataCache.hpp"
#include "FileStore.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

static bool nearlyEqual(double a, double b, double eps = 1e-4) {
    return std::abs(a - b) < eps;
}

int main() {
    int failed = 0;
    const std::string tempDir = "build/test_tmp_areacontext";

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);

    // Case 1: saveInfo and loadInfo round-trip, verifying no .tmp left behind
    {
        rapidaid::area::AreaInfo original;
        original.name = "paris_center";
        original.displayName = "Paris Center, France";
        original.lat = 48.8500;
        original.lon = 2.3000;
        original.radiusMeters = 3000;

        const std::string areaFile = tempDir + "/area.json";
        const std::string tmpFile = areaFile + ".tmp";

        bool saved = rapidaid::area::AreaContext::saveInfo(areaFile, original);
        bool tmpExists = rapidaid::io::FileStore::exists(tmpFile);
        auto loaded = rapidaid::area::AreaContext::loadInfo(areaFile);

        if (saved && !tmpExists && loaded.has_value() &&
            loaded->name == original.name &&
            loaded->displayName == original.displayName &&
            nearlyEqual(loaded->lat, original.lat) &&
            nearlyEqual(loaded->lon, original.lon) &&
            loaded->radiusMeters == original.radiusMeters) {
            std::cout << "[PASS] Case 1: saveInfo and loadInfo round-trip successfully, no .tmp left behind\n";
        } else {
            std::cout << "[FAIL] Case 1: saveInfo/loadInfo round-trip failed\n";
            failed++;
        }
    }

    // Case 2: loadInfo handles missing file and corrupt JSON safely
    {
        const std::string missingFile = tempDir + "/nonexistent_area.json";
        auto missingResult = rapidaid::area::AreaContext::loadInfo(missingFile);

        const std::string corruptFile = tempDir + "/corrupt_area.json";
        rapidaid::io::FileStore::save(corruptFile, "{not_valid_json: 123");
        auto corruptResult = rapidaid::area::AreaContext::loadInfo(corruptFile);

        const std::string arrayFile = tempDir + "/array_area.json";
        rapidaid::io::FileStore::save(arrayFile, "[1, 2, 3]");
        auto arrayResult = rapidaid::area::AreaContext::loadInfo(arrayFile);

        bool tmpMissing = !rapidaid::io::FileStore::exists(corruptFile + ".tmp");

        if (!missingResult.has_value() && !corruptResult.has_value() && !arrayResult.has_value() && tmpMissing) {
            std::cout << "[PASS] Case 2: loadInfo returns nullopt for missing and corrupt files safely\n";
        } else {
            std::cout << "[FAIL] Case 2: loadInfo failed on missing or corrupt files\n";
            failed++;
        }
    }

    // Prepare area info and DataCache for build testing
    rapidaid::area::AreaInfo areaInfo;
    areaInfo.name = "paris_center";
    areaInfo.displayName = "Paris Center";
    areaInfo.lat = 48.8500;
    areaInfo.lon = 2.3000;
    areaInfo.radiusMeters = 3000;

    rapidaid::data::DataCache cache(tempDir, false);

    // Canned JSON strings
    const std::string cannedRoadsJson = R"({
        "elements": [
            { "type": "node", "id": 1, "lat": 48.8500, "lon": 2.3000 },
            { "type": "node", "id": 2, "lat": 48.8500, "lon": 2.3010 },
            { "type": "node", "id": 3, "lat": 48.8500, "lon": 2.3020 },
            { "type": "node", "id": 4, "lat": 48.8510, "lon": 2.3000 },
            { "type": "node", "id": 5, "lat": 48.8510, "lon": 2.3010 },
            { "type": "node", "id": 6, "lat": 48.8510, "lon": 2.3020 },
            {
                "type": "way",
                "id": 1001,
                "nodes": [1, 2, 3],
                "tags": { "highway": "residential", "name": "South Street" }
            },
            {
                "type": "way",
                "id": 1002,
                "nodes": [4, 5, 6],
                "tags": { "highway": "residential", "name": "North Street" }
            },
            {
                "type": "way",
                "id": 1003,
                "nodes": [1, 4],
                "tags": { "highway": "residential", "name": "West Street" }
            },
            {
                "type": "way",
                "id": 1004,
                "nodes": [2, 5],
                "tags": { "highway": "residential", "name": "Central Street" }
            },
            {
                "type": "way",
                "id": 1005,
                "nodes": [3, 6],
                "tags": { "highway": "residential", "name": "East Street" }
            }
        ]
    })";

    const std::string cannedHospitalJson = R"({
        "elements": [
            {
                "type": "node",
                "id": 101,
                "lat": 48.8500,
                "lon": 2.3000,
                "tags": { "amenity": "hospital", "name": "Hospital Saint-Michel" }
            },
            {
                "type": "node",
                "id": 102,
                "lat": 48.8510,
                "lon": 2.3020,
                "tags": { "amenity": "hospital", "name": "Hospital Saint-Louis" }
            }
        ]
    })";

    const std::string cannedPoliceJson = R"({
        "elements": [
            {
                "type": "node",
                "id": 201,
                "lat": 48.8500,
                "lon": 2.3010,
                "tags": { "amenity": "police", "name": "Central Police Station" }
            }
        ]
    })";

    const std::string cannedEmptyPlacesJson = R"({ "elements": [] })";

    // Case 3: build with allowNetwork=false FAILS when roads file is missing without network call
    {
        rapidaid::area::AreaContext ctx;
        std::string errMsg;
        bool ok = rapidaid::area::AreaContext::build(cache, areaInfo, false, ctx, &errMsg);

        if (!ok && !errMsg.empty()) {
            std::cout << "[PASS] Case 3: build with allowNetwork=false fails on missing roads file without network call\n";
        } else {
            std::cout << "[FAIL] Case 3: build should fail when roads file is missing\n";
            failed++;
        }
    }

    // Save roads file
    rapidaid::io::FileStore::save(cache.roadsPath(areaInfo.lat, areaInfo.lon, areaInfo.radiusMeters), cannedRoadsJson);

    // Case 4: build with allowNetwork=false FAILS when places file is missing without network call
    {
        rapidaid::area::AreaContext ctx;
        std::string errMsg;
        bool ok = rapidaid::area::AreaContext::build(cache, areaInfo, false, ctx, &errMsg);

        if (!ok && !errMsg.empty()) {
            std::cout << "[PASS] Case 4: build with allowNetwork=false fails on missing places file without network call\n";
        } else {
            std::cout << "[FAIL] Case 4: build should fail when places files are missing\n";
            failed++;
        }
    }

    // Save all places files: 2 hospitals, 1 police station, empty for others
    rapidaid::io::FileStore::save(
        cache.placesPath(rapidaid::osm::Category::Hospital, areaInfo.lat, areaInfo.lon, areaInfo.radiusMeters),
        cannedHospitalJson);
    rapidaid::io::FileStore::save(
        cache.placesPath(rapidaid::osm::Category::Police, areaInfo.lat, areaInfo.lon, areaInfo.radiusMeters),
        cannedPoliceJson);
    rapidaid::io::FileStore::save(
        cache.placesPath(rapidaid::osm::Category::FireStation, areaInfo.lat, areaInfo.lon, areaInfo.radiusMeters),
        cannedEmptyPlacesJson);
    rapidaid::io::FileStore::save(
        cache.placesPath(rapidaid::osm::Category::Clinic, areaInfo.lat, areaInfo.lon, areaInfo.radiusMeters),
        cannedEmptyPlacesJson);
    rapidaid::io::FileStore::save(
        cache.placesPath(rapidaid::osm::Category::Pharmacy, areaInfo.lat, areaInfo.lon, areaInfo.radiusMeters),
        cannedEmptyPlacesJson);
    rapidaid::io::FileStore::save(
        cache.placesPath(rapidaid::osm::Category::Shelter, areaInfo.lat, areaInfo.lon, areaInfo.radiusMeters),
        cannedEmptyPlacesJson);

    // Case 5: build with allowNetwork=false SUCCEEDS with canned files
    {
        rapidaid::area::AreaContext ctx;
        std::string errMsg;
        bool ok = rapidaid::area::AreaContext::build(cache, areaInfo, false, ctx, &errMsg);

        if (!ok) {
            std::cout << "[FAIL] Case 5: build failed unexpectedly with error: " << errMsg << "\n";
            failed++;
        } else {
            bool nodesOk = (ctx.graph().nodeCount() == 6);
            bool edgesOk = (ctx.graph().edgeCount() == 7);
            bool roadsNodesOk = (ctx.roads().nodes.size() == 6);
            bool roadsWaysOk = (ctx.roads().ways.size() == 5);
            bool placesOk = (ctx.places().size() == 3);
            bool snappedCountOk = (ctx.snappedPlaces().size() == 3);

            bool allSnapped = true;
            for (const auto& sp : ctx.snappedPlaces()) {
                if (!sp.snapped || sp.nodeIndex < 0) {
                    allSnapped = false;
                }
            }

            if (nodesOk && edgesOk && roadsNodesOk && roadsWaysOk && placesOk && snappedCountOk && allSnapped) {
                std::cout << "[PASS] Case 5: build succeeds with exact counts (nodes=6, edges=7, places=3, snapped=3)\n";
            } else {
                std::cout << "[FAIL] Case 5: count verification failed:"
                          << " graph.nodes=" << ctx.graph().nodeCount() << " (expected 6),"
                          << " graph.edges=" << ctx.graph().edgeCount() << " (expected 7),"
                          << " roads.nodes=" << ctx.roads().nodes.size() << " (expected 6),"
                          << " roads.ways=" << ctx.roads().ways.size() << " (expected 5),"
                          << " places=" << ctx.places().size() << " (expected 3),"
                          << " snappedPlaces=" << ctx.snappedPlaces().size() << " (expected 3),"
                          << " allSnapped=" << allSnapped << "\n";
                failed++;
            }
        }
    }

    // Case 6: Clean up temporary test directory
    std::filesystem::remove_all(tempDir, ec);
    if (!std::filesystem::exists(tempDir)) {
        std::cout << "[PASS] Case 6: Cleaned up temporary test directory\n";
    } else {
        std::cout << "[FAIL] Case 6: Failed to clean up temporary test directory\n";
        failed++;
    }

    std::cout << "\nAreaContext Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed > 0 ? 1 : 0;
}
