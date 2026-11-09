#include "GeoJsonWriter.hpp"
#include "FileStore.hpp"
#include "Graph.hpp"
#include "GraphBuilder.hpp"
#include "OverpassClient.hpp"
#include "json.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using rapidaid::geojson::GeoJsonWriter;
using rapidaid::graph::Graph;
using rapidaid::graph::PathResult;
using rapidaid::graph::SnappedPlace;
using rapidaid::osm::Category;
using rapidaid::osm::Place;
using json = nlohmann::json;

namespace {

bool nearlyEqual(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) < eps;
}

} // anonymous namespace

int main() {
    int failed = 0;
    const std::string testDir = "build/test_tmp_geojson";

    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);

    // Case 1: Top-level type is FeatureCollection and feature count matches input
    {
        std::vector<Place> places;
        Place p1{1, "node", 48.8584, 2.2945, "Eiffel Tower", {{"tourism", "attraction"}}, Category::Hospital};
        Place p2{2, "node", 48.8606, 2.3376, "Louvre", {{"tourism", "museum"}}, Category::Hospital};
        places.push_back(p1);
        places.push_back(p2);

        std::string jsonStr = GeoJsonWriter::placesCollection(places);
        json j = json::parse(jsonStr);

        bool okType = (j.value("type", "") == "FeatureCollection");
        bool okCount = (j.contains("features") && j["features"].is_array() && j["features"].size() == 2);

        std::string emptyStr = GeoJsonWriter::placesCollection({});
        json jEmpty = json::parse(emptyStr);
        bool okEmptyType = (jEmpty.value("type", "") == "FeatureCollection");
        bool okEmptyCount = (jEmpty.contains("features") && jEmpty["features"].is_array() && jEmpty["features"].empty());

        if (okType && okCount && okEmptyType && okEmptyCount) {
            std::cout << "[PASS] Case 1: Top-level type is FeatureCollection and feature count matches input\n";
        } else {
            std::cout << "[FAIL] Case 1: FeatureCollection structure or count mismatch\n";
            failed++;
        }
    }

    // Case 2: Coordinate order: lon then lat rounded to 7 decimals
    {
        Place p{10, "node", 48.8584, 2.2945, "Eiffel Tower", {}, Category::Hospital};
        std::string jsonStr = GeoJsonWriter::placesCollection({p});
        json j = json::parse(jsonStr);

        bool ok = false;
        if (j.contains("features") && j["features"].size() == 1) {
            const auto& feat = j["features"][0];
            if (feat.value("type", "") == "Feature" &&
                feat.contains("geometry") &&
                feat["geometry"].value("type", "") == "Point" &&
                feat["geometry"].contains("coordinates") &&
                feat["geometry"]["coordinates"].size() == 2) {
                double lon = feat["geometry"]["coordinates"][0].get<double>();
                double lat = feat["geometry"]["coordinates"][1].get<double>();
                if (nearlyEqual(lon, 2.2945) && nearlyEqual(lat, 48.8584)) {
                    ok = true;
                }
            }
        }

        if (ok) {
            std::cout << "[PASS] Case 2: Coordinate order: coordinates[0] is lon (2.2945), coordinates[1] is lat (48.8584)\n";
        } else {
            std::cout << "[FAIL] Case 2: Coordinate order or values incorrect\n";
            failed++;
        }
    }

    // Case 3: Place name with quotes, backslashes, and accented characters round-trips unchanged
    {
        std::string complexName = "H\xC3\xB4tel \"L'Arc\\de Triomphe\" \xC3\xA9t\xC3\xA9";
        Place p{20, "way", 10.0, 20.0, complexName, {{"amenity", "hotel"}}, Category::Hospital};

        std::string jsonStr = GeoJsonWriter::placesCollection({p});
        json j = json::parse(jsonStr);

        std::string parsedName = j["features"][0]["properties"]["name"].get<std::string>();
        std::string parsedCategory = j["features"][0]["properties"]["category"].get<std::string>();
        std::string parsedAmenity = j["features"][0]["properties"]["tags"]["amenity"].get<std::string>();

        if (parsedName == complexName && parsedCategory == "hospital" && parsedAmenity == "hotel") {
            std::cout << "[PASS] Case 3: Complex name with quotes, backslash, and accents round-trips unchanged\n";
        } else {
            std::cout << "[FAIL] Case 3: Complex name round-trip failed\n";
            failed++;
        }
    }

    // Case 4: snappedPlacesCollection carries snapped flag and distance for snapped and unsnapped places
    {
        Place p1{31, "node", 12.0, 77.0, "Near Clinic", {}, Category::Clinic};
        Place p2{32, "node", 13.0, 78.0, "Far Clinic", {}, Category::Clinic};

        SnappedPlace sp1{p1, 0, 15.25, true};
        SnappedPlace sp2{p2, -1, 550.0, false};

        std::string jsonStr = GeoJsonWriter::snappedPlacesCollection({sp1, sp2});
        json j = json::parse(jsonStr);

        bool ok = false;
        if (j["features"].size() == 2) {
            const auto& f1 = j["features"][0]["properties"];
            const auto& f2 = j["features"][1]["properties"];

            bool snap1 = f1["snapped"].get<bool>();
            double dist1 = f1["snapDistanceMeters"].get<double>();
            bool snap2 = f2["snapped"].get<bool>();
            double dist2 = f2["snapDistanceMeters"].get<double>();

            if (snap1 == true && nearlyEqual(dist1, 15.25) &&
                snap2 == false && nearlyEqual(dist2, 550.0) &&
                f1["id"].get<long long>() == 31 &&
                f2["id"].get<long long>() == 32 &&
                f1["category"].get<std::string>() == "clinic") {
                ok = true;
            }
        }

        if (ok) {
            std::cout << "[PASS] Case 4: snappedPlacesCollection carries snapped flag and distance for snapped/unsnapped\n";
        } else {
            std::cout << "[FAIL] Case 4: snappedPlacesCollection properties mismatch\n";
            failed++;
        }
    }

    // Case 5: pathCollection on 3-node path, found=false, 1-node path, out-of-range index
    {
        Graph g;
        g.addNode(101, 51.5000, -0.1000);
        g.addNode(102, 51.5100, -0.1100);
        g.addNode(103, 51.5200, -0.1200);
        g.addEdge(0, 1, 100.0);
        g.addEdge(1, 2, 150.0);

        // 3-node valid path
        PathResult pValid;
        pValid.found = true;
        pValid.distanceMeters = 250.0;
        pValid.nodes = {0, 1, 2};

        std::string validJsonStr = GeoJsonWriter::pathCollection(g, pValid);
        json jValid = json::parse(validJsonStr);

        bool okValid = false;
        if (jValid["features"].size() == 1) {
            const auto& f = jValid["features"][0];
            if (f["geometry"]["type"] == "LineString" &&
                f["geometry"]["coordinates"].size() == 3) {
                double lon0 = f["geometry"]["coordinates"][0][0].get<double>();
                double lat0 = f["geometry"]["coordinates"][0][1].get<double>();
                double lon1 = f["geometry"]["coordinates"][1][0].get<double>();
                double lat1 = f["geometry"]["coordinates"][1][1].get<double>();
                double lon2 = f["geometry"]["coordinates"][2][0].get<double>();
                double lat2 = f["geometry"]["coordinates"][2][1].get<double>();

                double dist = f["properties"]["distanceMeters"].get<double>();
                int count = f["properties"]["nodeCount"].get<int>();

                if (nearlyEqual(lon0, -0.1000) && nearlyEqual(lat0, 51.5000) &&
                    nearlyEqual(lon1, -0.1100) && nearlyEqual(lat1, 51.5100) &&
                    nearlyEqual(lon2, -0.1200) && nearlyEqual(lat2, 51.5200) &&
                    nearlyEqual(dist, 250.0) && count == 3) {
                    okValid = true;
                }
            }
        }

        // found = false
        PathResult pNotFound;
        pNotFound.found = false;
        pNotFound.distanceMeters = 0.0;
        pNotFound.nodes = {0, 1, 2};
        json jNotFound = json::parse(GeoJsonWriter::pathCollection(g, pNotFound));
        bool okNotFound = (jNotFound["features"].empty());

        // 1-node path
        PathResult pOneNode;
        pOneNode.found = true;
        pOneNode.distanceMeters = 0.0;
        pOneNode.nodes = {0};
        json jOneNode = json::parse(GeoJsonWriter::pathCollection(g, pOneNode));
        bool okOneNode = (jOneNode["features"].empty());

        // Out-of-range index (high)
        PathResult pOutOfRangeHigh;
        pOutOfRangeHigh.found = true;
        pOutOfRangeHigh.distanceMeters = 100.0;
        pOutOfRangeHigh.nodes = {0, 99};
        json jOutOfRangeHigh = json::parse(GeoJsonWriter::pathCollection(g, pOutOfRangeHigh));
        bool okOutOfRangeHigh = (jOutOfRangeHigh["features"].empty());

        // Out-of-range index (negative)
        PathResult pOutOfRangeNeg;
        pOutOfRangeNeg.found = true;
        pOutOfRangeNeg.distanceMeters = 100.0;
        pOutOfRangeNeg.nodes = {-1, 1};
        json jOutOfRangeNeg = json::parse(GeoJsonWriter::pathCollection(g, pOutOfRangeNeg));
        bool okOutOfRangeNeg = (jOutOfRangeNeg["features"].empty());

        if (okValid && okNotFound && okOneNode && okOutOfRangeHigh && okOutOfRangeNeg) {
            std::cout << "[PASS] Case 5: pathCollection handles 3-node path, found=false, 1-node path, and out-of-range indices\n";
        } else {
            std::cout << "[FAIL] Case 5: pathCollection behavior incorrect (valid=" << okValid
                      << ", notFound=" << okNotFound
                      << ", oneNode=" << okOneNode
                      << ", outHigh=" << okOutOfRangeHigh
                      << ", outNeg=" << okOutOfRangeNeg << ")\n";
            failed++;
        }
    }

    // Case 6: roadsCollection feature count equals graph.edgeCount() on small graph, empty gives zero
    {
        Graph emptyGraph;
        json jEmpty = json::parse(GeoJsonWriter::roadsCollection(emptyGraph));
        bool okEmpty = (jEmpty["type"] == "FeatureCollection" && jEmpty["features"].empty());

        Graph g;
        g.addNode(1, 48.0, 2.0);
        g.addNode(2, 48.1, 2.1);
        g.addNode(3, 48.2, 2.2);
        g.addNode(4, 48.3, 2.3);

        g.addEdge(0, 1, 100.0);
        g.addEdge(1, 2, 200.0);
        g.addEdge(2, 3, 300.0);
        g.addEdge(0, 2, 400.0);

        bool countMatches = (g.edgeCount() == 4);
        json jRoads = json::parse(GeoJsonWriter::roadsCollection(g));
        bool okCount = (jRoads["type"] == "FeatureCollection" && jRoads["features"].size() == 4);

        bool okGeom = true;
        for (const auto& feat : jRoads["features"]) {
            if (feat["geometry"]["type"] != "LineString" ||
                feat["geometry"]["coordinates"].size() != 2 ||
                !feat["properties"].contains("weightMeters")) {
                okGeom = false;
                break;
            }
        }

        if (okEmpty && countMatches && okCount && okGeom) {
            std::cout << "[PASS] Case 6: roadsCollection feature count matches edgeCount and empty gives 0\n";
        } else {
            std::cout << "[FAIL] Case 6: roadsCollection check failed\n";
            failed++;
        }
    }

    // Case 7: writeFile round-trips via FileStore::load, creates nested dirs, returns false for impossible path
    {
        std::string filePath = testDir + "/sub/nested/map.geojson";
        std::string content = "{\"type\":\"FeatureCollection\",\"features\":[]}";

        bool saveOk = GeoJsonWriter::writeFile(filePath, content);
        std::string loaded = rapidaid::io::FileStore::load(filePath);
        bool match = (saveOk && loaded == content);

        bool impossibleOk = !GeoJsonWriter::writeFile("", content);

        if (match && impossibleOk) {
            std::cout << "[PASS] Case 7: writeFile creates nested dirs, round-trips identically, and rejects invalid path\n";
        } else {
            std::cout << "[FAIL] Case 7: writeFile behavior incorrect (match=" << match
                      << ", impossibleOk=" << impossibleOk << ")\n";
            failed++;
        }
    }

    // Clean up temporary directory
    std::filesystem::remove_all(testDir, ec);
    if (!std::filesystem::exists(testDir)) {
        std::cout << "[PASS] Cleaned up temporary test directory\n";
    } else {
        std::cout << "[FAIL] Failed to clean up temporary test directory\n";
        failed++;
    }

    std::cout << "\nGeoJsonWriter Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed > 0 ? 1 : 0;
}
