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
    const std::string tempDir = "build/test_tmp_cache";

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);

    // Case 1: Path functions with default directory ("data")
    {
        rapidaid::data::DataCache cache;

        std::string query = "Eiffel Tower, Paris";
        std::string gPath = cache.geocodePath(query);
        std::string expectedG = "data/geocode_eiffel_tower_paris.json";

        std::string pPath = cache.placesPath(rapidaid::osm::Category::Hospital, 40.7128, -74.0060, 2500);
        std::string expectedP = "data/places_hospital_40.7128_-74.0060_2500.json";

        std::string rPath = cache.roadsPath(40.7128, -74.0060, 2500);
        std::string expectedR = "data/roads_40.7128_-74.0060_2500.json";

        if (gPath == expectedG && pPath == expectedP && rPath == expectedR) {
            std::cout << "[PASS] Case 1: Path functions produce exact expected strings (default dir, negative lon)\n";
        } else {
            std::cout << "[FAIL] Case 1: Path mismatch:\n"
                      << "  geocode: got " << gPath << ", expected " << expectedG << "\n"
                      << "  places:  got " << pPath << ", expected " << expectedP << "\n"
                      << "  roads:   got " << rPath << ", expected " << expectedR << "\n";
            failed++;
        }
    }

    // Case 2: Path functions with custom directory and query with commas and spaces
    {
        rapidaid::data::DataCache cache(tempDir, false);

        std::string query = "Statue of Liberty, New York, NY";
        std::string gPath = cache.geocodePath(query);
        std::string expectedG = tempDir + "/geocode_statue_of_liberty_new_york_ny.json";

        std::string pPath = cache.placesPath(rapidaid::osm::Category::Clinic, -33.8688, 151.2093, 1500);
        std::string expectedP = tempDir + "/places_clinic_-33.8688_151.2093_1500.json";

        std::string rPath = cache.roadsPath(-33.8688, -70.6693, 1000);
        std::string expectedR = tempDir + "/roads_-33.8688_-70.6693_1000.json";

        if (gPath == expectedG && pPath == expectedP && rPath == expectedR) {
            std::cout << "[PASS] Case 2: Path functions produce exact expected strings (custom dir, spaces and commas)\n";
        } else {
            std::cout << "[FAIL] Case 2: Path mismatch (custom dir):\n"
                      << "  geocode: got " << gPath << ", expected " << expectedG << "\n"
                      << "  places:  got " << pPath << ", expected " << expectedP << "\n"
                      << "  roads:   got " << rPath << ", expected " << expectedR << "\n";
            failed++;
        }
    }

    // Case 3: Cache hit for geocode from canned file
    {
        rapidaid::data::DataCache cache(tempDir, false);
        std::string query = "Eiffel Tower, Paris";
        std::string path = cache.geocodePath(query);

        std::string cannedJson = R"([{"lat":"48.8584","lon":"2.2945","display_name":"Eiffel Tower, Paris"}])";
        if (rapidaid::io::FileStore::save(path, cannedJson)) {
            auto loc = cache.geocode(query);
            if (loc.has_value() &&
                nearlyEqual(loc->lat, 48.8584) &&
                nearlyEqual(loc->lon, 2.2945) &&
                loc->displayName == "Eiffel Tower, Paris") {
                std::cout << "[PASS] Case 3: geocode returns parsed data from canned file (cache hit)\n";
            } else {
                std::cout << "[FAIL] Case 3: geocode did not return expected canned data\n";
                failed++;
            }
        } else {
            std::cout << "[FAIL] Case 3: failed to save canned geocode file\n";
            failed++;
        }
    }

    // Case 4: Cache hit for places from canned file
    {
        rapidaid::data::DataCache cache(tempDir, false);
        double lat = 48.8584;
        double lon = 2.2945;
        int radius = 1000;
        std::string path = cache.placesPath(rapidaid::osm::Category::Hospital, lat, lon, radius);

        std::string cannedJson = R"({
            "elements": [
                {
                    "type": "node",
                    "id": 1001,
                    "lat": 48.8585,
                    "lon": 2.2946,
                    "tags": {
                        "amenity": "hospital",
                        "name": "Eiffel General Hospital"
                    }
                }
            ]
        })";

        if (rapidaid::io::FileStore::save(path, cannedJson)) {
            auto places = cache.places(rapidaid::osm::Category::Hospital, lat, lon, radius);
            if (places.size() == 1 &&
                places[0].id == 1001 &&
                places[0].name == "Eiffel General Hospital" &&
                nearlyEqual(places[0].lat, 48.8585) &&
                nearlyEqual(places[0].lon, 2.2946)) {
                std::cout << "[PASS] Case 4: places returns parsed data from canned file (cache hit)\n";
            } else {
                std::cout << "[FAIL] Case 4: places did not return expected canned data\n";
                failed++;
            }
        } else {
            std::cout << "[FAIL] Case 4: failed to save canned places file\n";
            failed++;
        }
    }

    // Case 5: Cache hit for roads from canned file
    {
        rapidaid::data::DataCache cache(tempDir, false);
        double lat = 48.8584;
        double lon = 2.2945;
        int radius = 1000;
        std::string path = cache.roadsPath(lat, lon, radius);

        std::string cannedJson = R"({
            "elements": [
                { "type": "node", "id": 10, "lat": 48.8580, "lon": 2.2940 },
                { "type": "node", "id": 20, "lat": 48.8590, "lon": 2.2950 },
                {
                    "type": "way",
                    "id": 201,
                    "nodes": [10, 20],
                    "tags": { "highway": "residential", "name": "Rue de Champ de Mars" }
                }
            ]
        })";

        if (rapidaid::io::FileStore::save(path, cannedJson)) {
            auto roads = cache.roads(lat, lon, radius);
            if (!roads.empty() &&
                roads.nodes.size() == 2 &&
                roads.ways.size() == 1 &&
                roads.ways[0].id == 201 &&
                roads.ways[0].name == "Rue de Champ de Mars") {
                std::cout << "[PASS] Case 5: roads returns parsed data from canned file (cache hit)\n";
            } else {
                std::cout << "[FAIL] Case 5: roads did not return expected canned data\n";
                failed++;
            }
        } else {
            std::cout << "[FAIL] Case 5: failed to save canned roads file\n";
            failed++;
        }
    }

    // Case 6: Canned file with invalid JSON is not returned as result and is not deleted
    {
        rapidaid::data::DataCache cache(tempDir, false);

        // Geocode invalid JSON with empty query so fetchRaw returns "" without network
        std::string badGeoPath = cache.geocodePath("");
        rapidaid::io::FileStore::save(badGeoPath, "{not valid json at all");
        auto badGeo = cache.geocode("");
        bool geoPass = (!badGeo.has_value() && rapidaid::io::FileStore::exists(badGeoPath));

        // Places invalid JSON with out-of-range lat so fetchRaw returns "" without network
        std::string badPlacesPath = cache.placesPath(rapidaid::osm::Category::Hospital, 999.0, 999.0, 1000);
        rapidaid::io::FileStore::save(badPlacesPath, "{bad json elements [");
        auto badPlaces = cache.places(rapidaid::osm::Category::Hospital, 999.0, 999.0, 1000);
        bool placesPass = (badPlaces.empty() && rapidaid::io::FileStore::exists(badPlacesPath));

        // Roads invalid JSON with out-of-range lat so fetchRaw returns "" without network
        std::string badRoadsPath = cache.roadsPath(999.0, 999.0, 1000);
        rapidaid::io::FileStore::save(badRoadsPath, "invalid json string");
        auto badRoads = cache.roads(999.0, 999.0, 1000);
        bool roadsPass = (badRoads.empty() && rapidaid::io::FileStore::exists(badRoadsPath));

        if (geoPass && placesPass && roadsPass) {
            std::cout << "[PASS] Case 6: Invalid JSON files are treated as misses, not returned, and not deleted\n";
        } else {
            std::cout << "[FAIL] Case 6: Invalid JSON handling failed (geo=" << geoPass
                      << ", places=" << placesPass << ", roads=" << roadsPass << ")\n";
            failed++;
        }
    }

    // Case 7: Clean up temporary test directory
    std::filesystem::remove_all(tempDir, ec);
    if (!std::filesystem::exists(tempDir)) {
        std::cout << "[PASS] Case 7: Cleaned up temporary test directory\n";
    } else {
        std::cout << "[FAIL] Case 7: Failed to clean up temporary test directory\n";
        failed++;
    }

    std::cout << "\nDataCache Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed > 0 ? 1 : 0;
}
