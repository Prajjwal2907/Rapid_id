#include "OverpassClient.hpp"
#include <iostream>
#include <string>
#include <vector>

using rapidaid::osm::Category;
using rapidaid::osm::OverpassClient;
using rapidaid::osm::Place;

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
    std::cout << "Running test_overpass_offline...\n";

    // 1. categoryName check
    assertTest(OverpassClient::categoryName(Category::Hospital) == "hospital", "categoryName(Hospital)");
    assertTest(OverpassClient::categoryName(Category::Clinic) == "clinic", "categoryName(Clinic)");
    assertTest(OverpassClient::categoryName(Category::Pharmacy) == "pharmacy", "categoryName(Pharmacy)");
    assertTest(OverpassClient::categoryName(Category::FireStation) == "fire_station", "categoryName(FireStation)");
    assertTest(OverpassClient::categoryName(Category::Police) == "police", "categoryName(Police)");
    assertTest(OverpassClient::categoryName(Category::Shelter) == "shelter", "categoryName(Shelter)");
    assertTest(OverpassClient::categoryName(Category::Water) == "water", "categoryName(Water)");

    // 2. buildQuery output clauses check
    // Check Hospital
    std::string hospitalQuery = OverpassClient::buildQuery(Category::Hospital, 48.8584, 2.2945, 1000);
    assertTest(hospitalQuery.find("[out:json][timeout:25];") != std::string::npos, "Hospital query header");
    assertTest(hospitalQuery.find("node[\"amenity\"=\"hospital\"](around:1000,48.8584000,2.2945000);") != std::string::npos, "Hospital query node clause");
    assertTest(hospitalQuery.find("way[\"amenity\"=\"hospital\"](around:1000,48.8584000,2.2945000);") != std::string::npos, "Hospital query way clause");
    assertTest(hospitalQuery.find("relation[\"amenity\"=\"hospital\"](around:1000,48.8584000,2.2945000);") != std::string::npos, "Hospital query relation clause");
    assertTest(hospitalQuery.find("out center tags;") != std::string::npos, "Hospital query footer");

    // Check Shelter (amenity=shelter and emergency=assembly_point)
    std::string shelterQuery = OverpassClient::buildQuery(Category::Shelter, 12.3456, 78.9012, 500);
    assertTest(shelterQuery.find("node[\"amenity\"=\"shelter\"](around:500,12.3456000,78.9012000);") != std::string::npos, "Shelter query node amenity clause");
    assertTest(shelterQuery.find("node[\"emergency\"=\"assembly_point\"](around:500,12.3456000,78.9012000);") != std::string::npos, "Shelter query node emergency clause");
    assertTest(shelterQuery.find("way[\"amenity\"=\"shelter\"](around:500,12.3456000,78.9012000);") != std::string::npos, "Shelter query way amenity clause");
    assertTest(shelterQuery.find("way[\"emergency\"=\"assembly_point\"](around:500,12.3456000,78.9012000);") != std::string::npos, "Shelter query way emergency clause");
    assertTest(shelterQuery.find("relation[\"amenity\"=\"shelter\"](around:500,12.3456000,78.9012000);") != std::string::npos, "Shelter query relation amenity clause");
    assertTest(shelterQuery.find("relation[\"emergency\"=\"assembly_point\"](around:500,12.3456000,78.9012000);") != std::string::npos, "Shelter query relation emergency clause");

    // 3. buildRequestBody starts with "data=" and contains no raw spaces
    std::string reqBody = OverpassClient::buildRequestBody(hospitalQuery);
    assertTest(reqBody.rfind("data=", 0) == 0, "buildRequestBody starts with data=");
    assertTest(reqBody.find(' ') == std::string::npos, "buildRequestBody contains no raw spaces");
    assertTest(reqBody.length() > 5, "buildRequestBody contains encoded content");

    // 4. parseResponse on canned JSON
    const std::string cannedJson = R"({
      "version": 0.6,
      "generator": "Overpass API",
      "elements": [
        {
          "type": "node",
          "id": 1001,
          "lat": 12.9716,
          "lon": 77.5946,
          "tags": {
            "amenity": "hospital",
            "name": "City General Hospital",
            "emergency": "yes"
          }
        },
        {
          "type": "way",
          "id": 2002,
          "center": {
            "lat": 12.9750,
            "lon": 77.5990
          },
          "tags": {
            "amenity": "hospital",
            "name": "Apollo Clinic",
            "healthcare": "hospital"
          }
        },
        {
          "type": "relation",
          "id": 3003,
          "center": {
            "lat": 12.9800,
            "lon": 77.6000
          },
          "tags": {
            "amenity": "hospital"
          }
        },
        {
          "type": "way",
          "id": 4004,
          "tags": {
            "amenity": "hospital",
            "name": "Way Without Center"
          }
        },
        {
          "type": "node",
          "id": 5005,
          "tags": {
            "amenity": "hospital",
            "name": "Node Without Coords"
          }
        }
      ]
    })";

    std::vector<Place> places = OverpassClient::parseResponse(cannedJson, Category::Hospital);
    assertTest(places.size() == 3, "parseResponse filters elements without coordinates (expected 3)");

    if (places.size() >= 3) {
        // Check node
        assertTest(places[0].id == 1001, "Place 0 id == 1001");
        assertTest(places[0].osmType == "node", "Place 0 osmType == node");
        assertTest(places[0].lat == 12.9716, "Place 0 lat == 12.9716");
        assertTest(places[0].lon == 77.5946, "Place 0 lon == 77.5946");
        assertTest(places[0].name == "City General Hospital", "Place 0 name == City General Hospital");
        assertTest(places[0].tags["amenity"] == "hospital", "Place 0 tags[amenity]");
        assertTest(places[0].tags["emergency"] == "yes", "Place 0 tags[emergency]");
        assertTest(places[0].category == Category::Hospital, "Place 0 category == Hospital");

        // Check way
        assertTest(places[1].id == 2002, "Place 1 id == 2002");
        assertTest(places[1].osmType == "way", "Place 1 osmType == way");
        assertTest(places[1].lat == 12.9750, "Place 1 lat == 12.9750");
        assertTest(places[1].lon == 77.5990, "Place 1 lon == 77.5990");
        assertTest(places[1].name == "Apollo Clinic", "Place 1 name == Apollo Clinic");
        assertTest(places[1].tags["healthcare"] == "hospital", "Place 1 tags[healthcare]");
        assertTest(places[1].category == Category::Hospital, "Place 1 category == Hospital");

        // Check relation without name tag
        assertTest(places[2].id == 3003, "Place 2 id == 3003");
        assertTest(places[2].osmType == "relation", "Place 2 osmType == relation");
        assertTest(places[2].lat == 12.9800, "Place 2 lat == 12.9800");
        assertTest(places[2].lon == 77.6000, "Place 2 lon == 77.6000");
        assertTest(places[2].name == "", "Place 2 name empty");
        assertTest(places[2].tags["amenity"] == "hospital", "Place 2 tags[amenity]");
        assertTest(places[2].category == Category::Hospital, "Place 2 category == Hospital");
    }

    // 5. Test invalid JSON and empty strings
    std::vector<Place> emptyPlaces = OverpassClient::parseResponse("", Category::Hospital);
    assertTest(emptyPlaces.empty(), "parseResponse on empty string returns empty vector");

    std::vector<Place> invalidPlaces = OverpassClient::parseResponse("not valid json", Category::Hospital);
    assertTest(invalidPlaces.empty(), "parseResponse on invalid JSON returns empty vector");

    std::vector<Place> noElementsPlaces = OverpassClient::parseResponse("{\"version\": 0.6}", Category::Hospital);
    assertTest(noElementsPlaces.empty(), "parseResponse on missing elements returns empty vector");

    // 6. Test input validation in findNearby
    // Negative radius
    auto invalidRadius1 = OverpassClient::findNearby(Category::Hospital, 10.0, 10.0, 0);
    assertTest(invalidRadius1.empty(), "findNearby radius 0 returns empty vector");
    auto invalidRadius2 = OverpassClient::findNearby(Category::Hospital, 10.0, 10.0, 50001);
    assertTest(invalidRadius2.empty(), "findNearby radius 50001 returns empty vector");

    // Invalid lat / lon
    auto invalidLat1 = OverpassClient::findNearby(Category::Hospital, -90.1, 0.0, 100);
    assertTest(invalidLat1.empty(), "findNearby lat < -90 returns empty vector");
    auto invalidLat2 = OverpassClient::findNearby(Category::Hospital, 90.1, 0.0, 100);
    assertTest(invalidLat2.empty(), "findNearby lat > 90 returns empty vector");

    auto invalidLon1 = OverpassClient::findNearby(Category::Hospital, 0.0, -180.1, 100);
    assertTest(invalidLon1.empty(), "findNearby lon < -180 returns empty vector");
    auto invalidLon2 = OverpassClient::findNearby(Category::Hospital, 0.0, 180.1, 100);
    assertTest(invalidLon2.empty(), "findNearby lon > 180 returns empty vector");

    std::cout << "\nTest Summary: " << (failures == 0 ? "ALL PASSED" : "FAILED")
              << " (" << failures << " failures)\n";

    return (failures == 0) ? 0 : 1;
}
