#include "DataCache.hpp"
#include "FileStore.hpp"
#include "Graph.hpp"
#include "GraphBuilder.hpp"
#include "GeoMath.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

static bool nearlyEqual(double a, double b, double eps = 1e-4) {
    return std::abs(a - b) < eps;
}

int main() {
    int failed = 0;
    const std::string tempDir = "build/test_tmp_pipeline";

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);

    rapidaid::data::DataCache cache(tempDir, false);

    // 1. Prepare canned Geocode JSON
    std::string query = "Paris Center";
    std::string geocodeJson = R"([
        {
            "lat": "48.8500",
            "lon": "2.3000",
            "display_name": "Paris Center"
        }
    ])";
    if (!rapidaid::io::FileStore::save(cache.geocodePath(query), geocodeJson)) {
        std::cerr << "Failed to save canned geocode file.\n";
        return 1;
    }

    // 2. Prepare canned Places JSON (2 hospitals)
    double lat = 48.8500;
    double lon = 2.3000;
    int radius = 3000;
    std::string placesJson = R"({
        "elements": [
            {
                "type": "node",
                "id": 101,
                "lat": 48.8500,
                "lon": 2.3000,
                "tags": {
                    "amenity": "hospital",
                    "name": "Hospital Saint-Michel"
                }
            },
            {
                "type": "node",
                "id": 102,
                "lat": 48.8510,
                "lon": 2.3020,
                "tags": {
                    "amenity": "hospital",
                    "name": "Hospital Saint-Louis"
                }
            }
        ]
    })";
    if (!rapidaid::io::FileStore::save(cache.placesPath(rapidaid::osm::Category::Hospital, lat, lon, radius), placesJson)) {
        std::cerr << "Failed to save canned places file.\n";
        return 1;
    }

    // 3. Prepare canned Roads JSON (6 nodes street grid, 5 ways, 7 edges)
    std::string roadsJson = R"({
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
    if (!rapidaid::io::FileStore::save(cache.roadsPath(lat, lon, radius), roadsJson)) {
        std::cerr << "Failed to save canned roads file.\n";
        return 1;
    }

    // Pipeline Step 1: DataCache -> geocode
    auto loc = cache.geocode(query);
    if (loc.has_value() && nearlyEqual(loc->lat, 48.8500) && nearlyEqual(loc->lon, 2.3000)) {
        std::cout << "[PASS] Pipeline Step 1: geocode from cache\n";
    } else {
        std::cout << "[FAIL] Pipeline Step 1: geocode failed\n";
        failed++;
    }

    // Pipeline Step 2: DataCache -> places
    auto places = cache.places(rapidaid::osm::Category::Hospital, loc ? loc->lat : 48.8500, loc ? loc->lon : 2.3000, radius);
    if (places.size() == 2) {
        std::cout << "[PASS] Pipeline Step 2: places from cache (2 hospitals)\n";
    } else {
        std::cout << "[FAIL] Pipeline Step 2: places returned " << places.size() << " items (expected 2)\n";
        failed++;
    }

    // Pipeline Step 3: DataCache -> roads
    auto roads = cache.roads(loc ? loc->lat : 48.8500, loc ? loc->lon : 2.3000, radius);
    if (!roads.empty() && roads.nodes.size() == 6 && roads.ways.size() == 5) {
        std::cout << "[PASS] Pipeline Step 3: roads from cache\n";
    } else {
        std::cout << "[FAIL] Pipeline Step 3: roads returned unexpected structure\n";
        failed++;
    }

    // Pipeline Step 4: GraphBuilder::build
    rapidaid::graph::Graph graph = rapidaid::graph::GraphBuilder::build(roads, true);
    if (graph.nodeCount() == 6 && graph.edgeCount() == 7) {
        std::cout << "[PASS] Pipeline Step 4: GraphBuilder::build (6 nodes, 7 edges)\n";
    } else {
        std::cout << "[FAIL] Pipeline Step 4: unexpected graph size (nodes=" << graph.nodeCount()
                  << ", edges=" << graph.edgeCount() << ")\n";
        failed++;
    }

    // Pipeline Step 5: snapPlaces
    auto snapped = rapidaid::graph::GraphBuilder::snapPlaces(graph, places, 500.0);
    if (snapped.size() == 2 && snapped[0].snapped && snapped[1].snapped) {
        std::cout << "[PASS] Pipeline Step 5: snapPlaces successfully snapped both hospitals\n";
    } else {
        std::cout << "[FAIL] Pipeline Step 5: snapPlaces failed to snap both hospitals\n";
        failed++;
    }

    // Pipeline Step 6: shortestPath between the two snapped places
    if (snapped.size() >= 2 && snapped[0].snapped && snapped[1].snapped) {
        auto path = graph.shortestPath(snapped[0].nodeIndex, snapped[1].nodeIndex);
        if (path.found && path.nodes.size() >= 2) {
            double sumWeights = 0.0;
            for (size_t i = 0; i + 1 < path.nodes.size(); ++i) {
                int u = path.nodes[i];
                int v = path.nodes[i + 1];
                sumWeights += rapidaid::geo::haversineMeters(graph.lat(u), graph.lon(u),
                                                             graph.lat(v), graph.lon(v));
            }

            if (nearlyEqual(path.distanceMeters, sumWeights, 1e-3)) {
                std::cout << "[PASS] Pipeline Step 6: shortestPath found and distance ("
                          << path.distanceMeters << " m) equals sum of haversine edge weights\n";
            } else {
                std::cout << "[FAIL] Pipeline Step 6: path distance mismatch (path="
                          << path.distanceMeters << ", sum=" << sumWeights << ")\n";
                failed++;
            }
        } else {
            std::cout << "[FAIL] Pipeline Step 6: shortestPath not found\n";
            failed++;
        }
    } else {
        std::cout << "[FAIL] Pipeline Step 6: places not snapped, skipping path test\n";
        failed++;
    }

    // Step 7: Clean up temp directory
    std::filesystem::remove_all(tempDir, ec);
    if (!std::filesystem::exists(tempDir)) {
        std::cout << "[PASS] Cleaned up temporary test directory\n";
    } else {
        std::cout << "[FAIL] Failed to clean up temporary test directory\n";
        failed++;
    }

    std::cout << "\nPipeline Offline Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed > 0 ? 1 : 0;
}
