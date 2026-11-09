#include "LocationResolver.hpp"
#include "GeoMath.hpp"
#include <iostream>
#include <cmath>
#include <string>
#include <vector>

static bool nearlyEqual(double a, double b, double epsilon = 0.0001) {
    return std::abs(a - b) < epsilon;
}

int main() {
    int failed = 0;

    // --- Section 1: placeLabel ---
    // placeLabel with a name, without a name (hospital with operator, shelter with addr:street, shelter with neither)
    {
        // 1. With a name
        rapidaid::osm::Place p1;
        p1.id = 1;
        p1.name = "St. Mary Hospital";
        p1.category = rapidaid::osm::Category::Hospital;
        std::string label1 = rapidaid::locate::LocationResolver::placeLabel(p1);
        if (label1 == "St. Mary Hospital") {
            std::cout << "[PASS] Case 1: placeLabel with a name\n";
        } else {
            std::cout << "[FAIL] Case 1: placeLabel with a name (got: " << label1 << ")\n";
            failed++;
        }

        // 2. Hospital without a name, with operator
        rapidaid::osm::Place p2;
        p2.id = 2;
        p2.name = "";
        p2.category = rapidaid::osm::Category::Hospital;
        p2.tags["operator"] = "Red Cross";
        std::string label2 = rapidaid::locate::LocationResolver::placeLabel(p2);
        if (label2 == "Unnamed hospital (Red Cross)") {
            std::cout << "[PASS] Case 2: placeLabel hospital with operator\n";
        } else {
            std::cout << "[FAIL] Case 2: placeLabel hospital with operator (got: " << label2 << ")\n";
            failed++;
        }

        // 3. Shelter without a name, with addr:street
        rapidaid::osm::Place p3;
        p3.id = 3;
        p3.name = "";
        p3.category = rapidaid::osm::Category::Shelter;
        p3.tags["addr:street"] = "Oak Street";
        std::string label3 = rapidaid::locate::LocationResolver::placeLabel(p3);
        if (label3 == "Unnamed shelter (Oak Street)") {
            std::cout << "[PASS] Case 3: placeLabel shelter with addr:street\n";
        } else {
            std::cout << "[FAIL] Case 3: placeLabel shelter with addr:street (got: " << label3 << ")\n";
            failed++;
        }

        // 4. Shelter without a name, with neither
        rapidaid::osm::Place p4;
        p4.id = 4;
        p4.name = "";
        p4.category = rapidaid::osm::Category::Shelter;
        std::string label4 = rapidaid::locate::LocationResolver::placeLabel(p4);
        if (label4 == "Unnamed shelter") {
            std::cout << "[PASS] Case 4: placeLabel shelter with neither\n";
        } else {
            std::cout << "[FAIL] Case 4: placeLabel shelter with neither (got: " << label4 << ")\n";
            failed++;
        }
    }

    // Setup mock dataset for searchPlaces and searchIntersections
    rapidaid::graph::Graph graph;
    std::vector<rapidaid::osm::Place> places;
    rapidaid::osm::RoadData roadData;

    {
        // Places
        rapidaid::osm::Place pl1;
        pl1.id = 10;
        pl1.name = "Metro General Hospital";
        pl1.category = rapidaid::osm::Category::Hospital;
        pl1.lat = 40.7128;
        pl1.lon = -74.0060;
        places.push_back(pl1);

        rapidaid::osm::Place pl2;
        pl2.id = 11;
        pl2.name = "General Clinic";
        pl2.category = rapidaid::osm::Category::Clinic;
        pl2.lat = 40.7130;
        pl2.lon = -74.0070;
        places.push_back(pl2);

        rapidaid::osm::Place pl3;
        pl3.id = 12;
        pl3.name = "";
        pl3.category = rapidaid::osm::Category::Hospital;
        pl3.tags["operator"] = "St. Jude";
        pl3.lat = 40.7140;
        pl3.lon = -74.0080;
        places.push_back(pl3);

        rapidaid::osm::Place pl4;
        pl4.id = 13;
        pl4.name = "";
        pl4.category = rapidaid::osm::Category::Shelter;
        pl4.tags["addr:street"] = "Pine Road";
        pl4.lat = 40.7150;
        pl4.lon = -74.0090;
        places.push_back(pl4);

        // Roads
        // Node 1: intersection of "Elm Street" and "5th Avenue"
        roadData.nodes[1] = rapidaid::osm::RoadNode{1, 40.7100, -74.0000};
        // Node 2: duplicate intersection ~4 meters away (< 15 m)
        roadData.nodes[2] = rapidaid::osm::RoadNode{2, 40.71003, -74.00003};
        // Other road nodes
        roadData.nodes[10] = rapidaid::osm::RoadNode{10, 40.7090, -74.0000};
        roadData.nodes[11] = rapidaid::osm::RoadNode{11, 40.7110, -74.0000};
        roadData.nodes[12] = rapidaid::osm::RoadNode{12, 40.7120, -74.0000};
        roadData.nodes[20] = rapidaid::osm::RoadNode{20, 40.7100, -74.0010};
        roadData.nodes[21] = rapidaid::osm::RoadNode{21, 40.7100, -73.9990};
        roadData.nodes[22] = rapidaid::osm::RoadNode{22, 40.7100, -73.9980};

        // Node 3: intersection of "Oak Street" and "Main Street"
        roadData.nodes[3] = rapidaid::osm::RoadNode{3, 40.7200, -74.0100};
        roadData.nodes[30] = rapidaid::osm::RoadNode{30, 40.7190, -74.0100};
        roadData.nodes[31] = rapidaid::osm::RoadNode{31, 40.7210, -74.0100};
        roadData.nodes[40] = rapidaid::osm::RoadNode{40, 40.7200, -74.0110};
        roadData.nodes[41] = rapidaid::osm::RoadNode{41, 40.7200, -74.0090};

        // Ways
        rapidaid::osm::RoadWay w1;
        w1.id = 101;
        w1.name = "Elm Street";
        w1.nodeIds = {10, 1, 11};
        roadData.ways.push_back(w1);

        rapidaid::osm::RoadWay w2;
        w2.id = 102;
        w2.name = "5th Avenue";
        w2.nodeIds = {20, 1, 21};
        roadData.ways.push_back(w2);

        // Another segment forming duplicate intersection at node 2
        rapidaid::osm::RoadWay w3;
        w3.id = 103;
        w3.name = "Elm Street";
        w3.nodeIds = {11, 2, 12};
        roadData.ways.push_back(w3);

        rapidaid::osm::RoadWay w4;
        w4.id = 104;
        w4.name = "5th Avenue";
        w4.nodeIds = {21, 2, 22};
        roadData.ways.push_back(w4);

        rapidaid::osm::RoadWay w5;
        w5.id = 105;
        w5.name = "Oak Street";
        w5.nodeIds = {30, 3, 31};
        roadData.ways.push_back(w5);

        rapidaid::osm::RoadWay w6;
        w6.id = 106;
        w6.name = "Main Street";
        w6.nodeIds = {40, 3, 41};
        roadData.ways.push_back(w6);
    }

    rapidaid::locate::LocationResolver resolver(graph, places, roadData);

    // --- Section 2: searchPlaces ---
    // searchPlaces substring, case-insensitivity, unnamed fallback label
    {
        // Substring match
        auto res1 = resolver.searchPlaces("Metro");
        if (res1.size() == 1 && res1[0].label == "Metro General Hospital" &&
            res1[0].source == "place" && nearlyEqual(res1[0].lat, 40.7128) && nearlyEqual(res1[0].lon, -74.0060)) {
            std::cout << "[PASS] Case 5: searchPlaces substring match\n";
        } else {
            std::cout << "[FAIL] Case 5: searchPlaces substring match\n";
            failed++;
        }

        // Case-insensitivity
        auto res2 = resolver.searchPlaces("mEtRo GeNeRaL hOsPiTaL");
        if (res2.size() == 1 && res2[0].label == "Metro General Hospital") {
            std::cout << "[PASS] Case 6: searchPlaces case-insensitivity\n";
        } else {
            std::cout << "[FAIL] Case 6: searchPlaces case-insensitivity\n";
            failed++;
        }

        // Unnamed fallback label via operator tag
        auto res3 = resolver.searchPlaces("St. Jude");
        if (res3.size() == 1 && res3[0].label == "Unnamed hospital (St. Jude)" && res3[0].source == "place") {
            std::cout << "[PASS] Case 7: searchPlaces unnamed fallback label (operator)\n";
        } else {
            std::cout << "[FAIL] Case 7: searchPlaces unnamed fallback label (operator)\n";
            failed++;
        }

        // Unnamed fallback label via addr:street tag
        auto res4 = resolver.searchPlaces("Pine Road");
        if (res4.size() == 1 && res4[0].label == "Unnamed shelter (Pine Road)" && res4[0].source == "place") {
            std::cout << "[PASS] Case 8: searchPlaces unnamed fallback label (addr:street)\n";
        } else {
            std::cout << "[FAIL] Case 8: searchPlaces unnamed fallback label (addr:street)\n";
            failed++;
        }

        // Exact matches first, then partial matches
        auto res5 = resolver.searchPlaces("General Clinic");
        if (res5.size() == 1 && res5[0].label == "General Clinic") {
            std::cout << "[PASS] Case 9: searchPlaces exact match prioritization\n";
        } else {
            std::cout << "[FAIL] Case 9: searchPlaces exact match prioritization\n";
            failed++;
        }
    }

    // --- Section 3: searchIntersections ---
    // searchIntersections for each query syntax ("and", "&", "/", "corner of"), no match, duplicate suppression, empty query
    {
        // Syntax 1: "and"
        auto resAnd = resolver.searchIntersections("Elm and 5th");
        bool okAnd = (resAnd.size() == 1 && resAnd[0].source == "intersection" &&
                      resAnd[0].label.find("Elm") != std::string::npos &&
                      resAnd[0].label.find("5th") != std::string::npos &&
                      nearlyEqual(resAnd[0].lat, 40.7100) &&
                      nearlyEqual(resAnd[0].lon, -74.0000));
        if (okAnd) {
            std::cout << "[PASS] Case 10: searchIntersections query syntax 'and'\n";
        } else {
            std::cout << "[FAIL] Case 10: searchIntersections query syntax 'and' (count=" << resAnd.size() << ")\n";
            failed++;
        }

        // Syntax 2: "&"
        auto resAmp = resolver.searchIntersections("Elm & 5th");
        bool okAmp = (resAmp.size() == 1 && resAmp[0].source == "intersection" &&
                      nearlyEqual(resAmp[0].lat, 40.7100) && nearlyEqual(resAmp[0].lon, -74.0000));
        if (okAmp) {
            std::cout << "[PASS] Case 11: searchIntersections query syntax '&'\n";
        } else {
            std::cout << "[FAIL] Case 11: searchIntersections query syntax '&'\n";
            failed++;
        }

        // Syntax 3: "/"
        auto resSlash = resolver.searchIntersections("Elm / 5th");
        bool okSlash = (resSlash.size() == 1 && resSlash[0].source == "intersection" &&
                        nearlyEqual(resSlash[0].lat, 40.7100) && nearlyEqual(resSlash[0].lon, -74.0000));
        if (okSlash) {
            std::cout << "[PASS] Case 12: searchIntersections query syntax '/'\n";
        } else {
            std::cout << "[FAIL] Case 12: searchIntersections query syntax '/'\n";
            failed++;
        }

        // Syntax 4: "corner of"
        auto resCorner = resolver.searchIntersections("corner of Elm and 5th");
        bool okCorner = (resCorner.size() == 1 && resCorner[0].source == "intersection" &&
                         nearlyEqual(resCorner[0].lat, 40.7100) && nearlyEqual(resCorner[0].lon, -74.0000));
        if (okCorner) {
            std::cout << "[PASS] Case 13: searchIntersections query syntax 'corner of'\n";
        } else {
            std::cout << "[FAIL] Case 13: searchIntersections query syntax 'corner of'\n";
            failed++;
        }

        // Additional syntax: "corner of Oak & Main"
        auto resCorner2 = resolver.searchIntersections("corner of Oak & Main");
        bool okCorner2 = (resCorner2.size() == 1 && resCorner2[0].source == "intersection" &&
                          resCorner2[0].label.find("Oak") != std::string::npos &&
                          resCorner2[0].label.find("Main") != std::string::npos &&
                          nearlyEqual(resCorner2[0].lat, 40.7200) && nearlyEqual(resCorner2[0].lon, -74.0100));
        if (okCorner2) {
            std::cout << "[PASS] Case 14: searchIntersections query syntax 'corner of Oak & Main'\n";
        } else {
            std::cout << "[FAIL] Case 14: searchIntersections query syntax 'corner of Oak & Main'\n";
            failed++;
        }

        // Duplicate suppression: Node 1 and Node 2 are ~4m apart, resolver must return only 1 result
        // Distance check:
        double distBetweenNodes = rapidaid::geo::haversineMeters(40.7100, -74.0000, 40.71003, -74.00003);
        auto resDup = resolver.searchIntersections("Elm and 5th", 10);
        if (distBetweenNodes < 15.0 && resDup.size() == 1) {
            std::cout << "[PASS] Case 15: searchIntersections duplicate suppression (< 15 m)\n";
        } else {
            std::cout << "[FAIL] Case 15: searchIntersections duplicate suppression (dist="
                      << distBetweenNodes << ", count=" << resDup.size() << ")\n";
            failed++;
        }

        // No match
        auto resNone = resolver.searchIntersections("Broadway and 42nd");
        if (resNone.empty()) {
            std::cout << "[PASS] Case 16: searchIntersections no match\n";
        } else {
            std::cout << "[FAIL] Case 16: searchIntersections no match (count=" << resNone.size() << ")\n";
            failed++;
        }

        // Empty query
        auto resEmpty = resolver.searchIntersections("");
        auto resSpaces = resolver.searchIntersections("     ");
        if (resEmpty.empty() && resSpaces.empty()) {
            std::cout << "[PASS] Case 17: searchIntersections empty query\n";
        } else {
            std::cout << "[FAIL] Case 17: searchIntersections empty query\n";
            failed++;
        }
    }

    std::cout << "\nLocationResolver Test Summary: " << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed == 0 ? 0 : 1;
}
