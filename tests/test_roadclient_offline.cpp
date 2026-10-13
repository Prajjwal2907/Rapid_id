#include "RoadClient.hpp"
#include <iostream>
#include <string>

using rapidaid::osm::RoadClient;
using rapidaid::osm::RoadData;

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
    std::cout << "Running test_roadclient_offline...\n";

    // Case 1: buildRoadQuery exact string for a sample input, and "" for radius 0, radius 5001, lat 91, lon -181.
    std::string sampleQuery = RoadClient::buildRoadQuery(48.8584, 2.2945, 1000);
    std::string expectedSample = "[out:json][timeout:60];way[\"highway\"](around:1000,48.8584000,2.2945000);(._;>;);out body;";
    assertTest(sampleQuery == expectedSample, "Case 1: buildRoadQuery exact sample query");
    assertTest(RoadClient::buildRoadQuery(48.8584, 2.2945, 0) == "", "Case 1: buildRoadQuery radius 0 returns empty");
    assertTest(RoadClient::buildRoadQuery(48.8584, 2.2945, 5001) == "", "Case 1: buildRoadQuery radius 5001 returns empty");
    assertTest(RoadClient::buildRoadQuery(91.0, 2.2945, 1000) == "", "Case 1: buildRoadQuery lat 91 returns empty");
    assertTest(RoadClient::buildRoadQuery(-91.0, 2.2945, 1000) == "", "Case 1: buildRoadQuery lat -91 returns empty");
    assertTest(RoadClient::buildRoadQuery(48.8584, -181.0, 1000) == "", "Case 1: buildRoadQuery lon -181 returns empty");
    assertTest(RoadClient::buildRoadQuery(48.8584, 181.0, 1000) == "", "Case 1: buildRoadQuery lon 181 returns empty");

    // Case 2: buildRequestBody starts with "data=" and has no raw space.
    std::string reqBody = RoadClient::buildRequestBody(expectedSample);
    assertTest(reqBody.rfind("data=", 0) == 0, "Case 2: buildRequestBody starts with data=");
    assertTest(reqBody.find(' ') == std::string::npos, "Case 2: buildRequestBody has no raw space");
    assertTest(reqBody.length() > 5, "Case 2: buildRequestBody has content");

    // Case 3: isDrivable checks
    // True cases: residential, service, primary_link
    assertTest(RoadClient::isDrivable({{"highway", "residential"}}), "Case 3: isDrivable residential is true");
    assertTest(RoadClient::isDrivable({{"highway", "service"}}), "Case 3: isDrivable service is true");
    assertTest(RoadClient::isDrivable({{"highway", "primary_link"}}), "Case 3: isDrivable primary_link is true");

    // False cases: footway, cycleway, steps, track, no highway tag, service with access=private, residential with motor_vehicle=no, residential with area=yes
    assertTest(!RoadClient::isDrivable({{"highway", "footway"}}), "Case 3: isDrivable footway is false");
    assertTest(!RoadClient::isDrivable({{"highway", "cycleway"}}), "Case 3: isDrivable cycleway is false");
    assertTest(!RoadClient::isDrivable({{"highway", "steps"}}), "Case 3: isDrivable steps is false");
    assertTest(!RoadClient::isDrivable({{"highway", "track"}}), "Case 3: isDrivable track is false");
    assertTest(!RoadClient::isDrivable({}), "Case 3: isDrivable no highway tag is false");
    assertTest(!RoadClient::isDrivable({{"name", "Some Way"}}), "Case 3: isDrivable tags without highway is false");
    assertTest(!RoadClient::isDrivable({{"highway", "service"}, {"access", "private"}}), "Case 3: isDrivable service with access=private is false");
    assertTest(!RoadClient::isDrivable({{"highway", "residential"}, {"motor_vehicle", "no"}}), "Case 3: isDrivable residential with motor_vehicle=no is false");
    assertTest(!RoadClient::isDrivable({{"highway", "residential"}, {"area", "yes"}}), "Case 3: isDrivable residential with area=yes is false");
    assertTest(!RoadClient::isDrivable({{"highway", "primary"}, {"vehicle", "no"}}), "Case 3: isDrivable primary with vehicle=no is false");
    assertTest(!RoadClient::isDrivable({{"highway", "trunk"}, {"access", "no"}}), "Case 3: isDrivable trunk with access=no is false");

    // Case 4: parseRoads on a canned response with 4 nodes and 3 ways (one residential, one footway, one with a single node) keeps only the residential way and all 4 nodes.
    const std::string cannedJson = R"({
      "version": 0.6,
      "generator": "Overpass API",
      "elements": [
        { "type": "node", "id": 1, "lat": 10.0, "lon": 20.0 },
        { "type": "node", "id": 2, "lat": 10.1, "lon": 20.1 },
        { "type": "node", "id": 3, "lat": 10.2, "lon": 20.2 },
        { "type": "node", "id": 4, "lat": 10.3, "lon": 20.3 },
        {
          "type": "way",
          "id": 101,
          "nodes": [1, 2],
          "tags": {
            "highway": "residential",
            "name": "Main Street"
          }
        },
        {
          "type": "way",
          "id": 102,
          "nodes": [2, 3],
          "tags": {
            "highway": "footway",
            "name": "Walking Path"
          }
        },
        {
          "type": "way",
          "id": 103,
          "nodes": [3],
          "tags": {
            "highway": "residential",
            "name": "Stub Way"
          }
        }
      ]
    })";

    RoadData data = RoadClient::parseRoads(cannedJson);
    assertTest(data.nodes.size() == 4, "Case 4: parseRoads keeps all 4 nodes");
    assertTest(data.nodes.count(1) == 1 && data.nodes[1].lat == 10.0 && data.nodes[1].lon == 20.0, "Case 4: node 1 verified");
    assertTest(data.nodes.count(2) == 1 && data.nodes[2].lat == 10.1 && data.nodes[2].lon == 20.1, "Case 4: node 2 verified");
    assertTest(data.nodes.count(3) == 1 && data.nodes[3].lat == 10.2 && data.nodes[3].lon == 20.2, "Case 4: node 3 verified");
    assertTest(data.nodes.count(4) == 1 && data.nodes[4].lat == 10.3 && data.nodes[4].lon == 20.3, "Case 4: node 4 verified");

    assertTest(data.ways.size() == 1, "Case 4: parseRoads keeps only 1 drivable way with >= 2 nodes");
    if (!data.ways.empty()) {
        assertTest(data.ways[0].id == 101, "Case 4: kept way id is 101");
        assertTest(data.ways[0].highway == "residential", "Case 4: kept way highway is residential");
        assertTest(data.ways[0].name == "Main Street", "Case 4: kept way name is Main Street");
        assertTest(data.ways[0].nodeIds.size() == 2 && data.ways[0].nodeIds[0] == 1 && data.ways[0].nodeIds[1] == 2, "Case 4: kept way nodeIds are [1, 2]");
        assertTest(data.ways[0].tags["highway"] == "residential", "Case 4: tags map contains highway");
    }
    assertTest(!data.empty(), "Case 4: data.empty() is false when ways present");

    // Case 5: A way whose tags contain a numeric value does not crash (e.g. "maxspeed": 50).
    const std::string numericTagJson = R"({
      "elements": [
        { "type": "node", "id": 10, "lat": 1.0, "lon": 2.0 },
        { "type": "node", "id": 11, "lat": 1.1, "lon": 2.1 },
        {
          "type": "way",
          "id": 201,
          "nodes": [10, 11],
          "tags": {
            "highway": "primary",
            "name": "Highway 50",
            "maxspeed": 50
          }
        }
      ]
    })";

    RoadData numData = RoadClient::parseRoads(numericTagJson);
    assertTest(numData.ways.size() == 1, "Case 5: numeric tag way parsed successfully");
    if (!numData.ways.empty()) {
        assertTest(numData.ways[0].tags.count("maxspeed") == 1, "Case 5: maxspeed tag present");
        assertTest(numData.ways[0].tags["maxspeed"] == "50", "Case 5: maxspeed tag converted to string 50");
    }

    // Case 6: Empty string, invalid JSON, and JSON without "elements" all return empty data.
    RoadData emptyStrData = RoadClient::parseRoads("");
    assertTest(emptyStrData.empty(), "Case 6: empty string returns empty RoadData");
    assertTest(emptyStrData.nodes.empty() && emptyStrData.ways.empty(), "Case 6: empty string has no nodes or ways");

    RoadData invalidJsonData = RoadClient::parseRoads("this is not json { [");
    assertTest(invalidJsonData.empty(), "Case 6: invalid JSON returns empty RoadData");
    assertTest(invalidJsonData.nodes.empty() && invalidJsonData.ways.empty(), "Case 6: invalid JSON has no nodes or ways");

    RoadData noElementsData = RoadClient::parseRoads(R"({"version": 0.6, "generator": "Overpass"})");
    assertTest(noElementsData.empty(), "Case 6: missing elements returns empty RoadData");
    assertTest(noElementsData.nodes.empty() && noElementsData.ways.empty(), "Case 6: missing elements has no nodes or ways");

    std::cout << "\nTest Summary: " << (failures == 0 ? "ALL PASSED" : "FAILED")
              << " (" << failures << " failures)\n";

    return (failures == 0) ? 0 : 1;
}
