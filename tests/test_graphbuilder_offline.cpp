#include "GraphBuilder.hpp"
#include "GeoMath.hpp"
#include "OverpassClient.hpp"
#include "RoadClient.hpp"

#include <cmath>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

using rapidaid::graph::Graph;
using rapidaid::graph::GraphBuilder;
using rapidaid::graph::SnappedPlace;
using rapidaid::geo::haversineMeters;
using rapidaid::osm::Place;
using rapidaid::osm::RoadData;
using rapidaid::osm::RoadNode;
using rapidaid::osm::RoadWay;

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
    std::cout << "Running test_graphbuilder_offline...\n";

    // 1. build from a hand-made RoadData (4 nodes, 2 ways)
    // check node/edge counts and that edge weights equal haversineMeters of the endpoints
    {
        RoadData rd;
        rd.nodes[10] = RoadNode{10, 12.9716, 77.5946};
        rd.nodes[20] = RoadNode{20, 12.9720, 77.5950};
        rd.nodes[30] = RoadNode{30, 12.9725, 77.5955};
        rd.nodes[40] = RoadNode{40, 12.9730, 77.5960};

        RoadWay w1;
        w1.id = 100;
        w1.nodeIds = {10, 20, 30};
        w1.highway = "residential";

        RoadWay w2;
        w2.id = 200;
        w2.nodeIds = {30, 40};
        w2.highway = "residential";

        rd.ways = {w1, w2};

        Graph g = GraphBuilder::build(rd, false);
        assertTest(g.nodeCount() == 4, "Case 1: hand-made RoadData nodeCount is 4");
        assertTest(g.edgeCount() == 3, "Case 1: hand-made RoadData edgeCount is 3");

        int idx10 = g.indexOf(10);
        int idx20 = g.indexOf(20);
        int idx30 = g.indexOf(30);
        int idx40 = g.indexOf(40);

        assertTest(idx10 != -1 && idx20 != -1 && idx30 != -1 && idx40 != -1,
                   "Case 1: all 4 nodes present in graph");

        // Check edge weight 10 <-> 20
        double expectedW1 = haversineMeters(12.9716, 77.5946, 12.9720, 77.5950);
        bool foundW1 = false;
        for (const auto& edge : g.neighbors(idx10)) {
            if (edge.to == idx20 && std::abs(edge.weightMeters - expectedW1) < 1e-6) {
                foundW1 = true;
                break;
            }
        }
        assertTest(foundW1, "Case 1: edge weight (10, 20) equals haversineMeters");

        // Check edge weight 20 <-> 30
        double expectedW2 = haversineMeters(12.9720, 77.5950, 12.9725, 77.5955);
        bool foundW2 = false;
        for (const auto& edge : g.neighbors(idx20)) {
            if (edge.to == idx30 && std::abs(edge.weightMeters - expectedW2) < 1e-6) {
                foundW2 = true;
                break;
            }
        }
        assertTest(foundW2, "Case 1: edge weight (20, 30) equals haversineMeters");

        // Check edge weight 30 <-> 40
        double expectedW3 = haversineMeters(12.9725, 77.5955, 12.9730, 77.5960);
        bool foundW3 = false;
        for (const auto& edge : g.neighbors(idx30)) {
            if (edge.to == idx40 && std::abs(edge.weightMeters - expectedW3) < 1e-6) {
                foundW3 = true;
                break;
            }
        }
        assertTest(foundW3, "Case 1: edge weight (30, 40) equals haversineMeters");
    }

    // 2. a way referencing a missing node skips only that segment
    {
        RoadData rd;
        rd.nodes[1] = RoadNode{1, 10.0, 10.0};
        rd.nodes[2] = RoadNode{2, 10.1, 10.0};
        // Node 999 is intentionally missing
        rd.nodes[4] = RoadNode{4, 10.3, 10.0};
        rd.nodes[5] = RoadNode{5, 10.4, 10.0};

        RoadWay w;
        w.id = 500;
        // Segments: (1, 2) valid, (2, 999) invalid, (999, 4) invalid, (4, 5) valid
        w.nodeIds = {1, 2, 999, 4, 5};
        rd.ways = {w};

        Graph g = GraphBuilder::build(rd, false);
        assertTest(g.nodeCount() == 4, "Case 2: 4 valid nodes added");
        assertTest(g.edgeCount() == 2, "Case 2: 2 valid edges added, missing node segments skipped");
        assertTest(g.indexOf(999) == -1, "Case 2: missing node 999 not in graph");

        int idx1 = g.indexOf(1);
        int idx2 = g.indexOf(2);
        int idx4 = g.indexOf(4);
        int idx5 = g.indexOf(5);
        assertTest(g.neighbors(idx1).size() == 1 && g.neighbors(idx1)[0].to == idx2,
                   "Case 2: segment (1, 2) connected");
        assertTest(g.neighbors(idx4).size() == 1 && g.neighbors(idx4)[0].to == idx5,
                   "Case 2: segment (4, 5) connected");
    }

    // 3. a RoadData with two disconnected roads keeps only the larger one when keepLargestComponent is true and both when false
    {
        RoadData rd;
        // Road A: 2 nodes
        rd.nodes[11] = RoadNode{11, 20.0, 30.0};
        rd.nodes[12] = RoadNode{12, 20.1, 30.1};
        RoadWay wa;
        wa.id = 1;
        wa.nodeIds = {11, 12};

        // Road B: 3 nodes
        rd.nodes[21] = RoadNode{21, 40.0, 50.0};
        rd.nodes[22] = RoadNode{22, 40.1, 50.1};
        rd.nodes[23] = RoadNode{23, 40.2, 50.2};
        RoadWay wb;
        wb.id = 2;
        wb.nodeIds = {21, 22, 23};

        rd.ways = {wa, wb};

        // keepLargestComponent == true
        Graph gLargest = GraphBuilder::build(rd, true);
        assertTest(gLargest.nodeCount() == 3, "Case 3: keepLargestComponent true has 3 nodes");
        assertTest(gLargest.edgeCount() == 2, "Case 3: keepLargestComponent true has 2 edges");
        assertTest(gLargest.indexOf(21) != -1 && gLargest.indexOf(22) != -1 && gLargest.indexOf(23) != -1,
                   "Case 3: larger road B nodes present");
        assertTest(gLargest.indexOf(11) == -1 && gLargest.indexOf(12) == -1,
                   "Case 3: smaller road A nodes dropped");

        // keepLargestComponent == false
        Graph gAll = GraphBuilder::build(rd, false);
        assertTest(gAll.nodeCount() == 5, "Case 3: keepLargestComponent false has 5 nodes");
        assertTest(gAll.edgeCount() == 3, "Case 3: keepLargestComponent false has 3 edges");
        assertTest(gAll.indexOf(11) != -1 && gAll.indexOf(12) != -1 &&
                   gAll.indexOf(21) != -1 && gAll.indexOf(22) != -1 && gAll.indexOf(23) != -1,
                   "Case 3: all nodes from both roads present");
    }

    // 4. empty RoadData gives an empty graph
    {
        RoadData emptyRd;
        Graph g = GraphBuilder::build(emptyRd, true);
        assertTest(g.nodeCount() == 0, "Case 4: empty RoadData nodeCount is 0");
        assertTest(g.edgeCount() == 0, "Case 4: empty RoadData edgeCount is 0");

        Graph gNoFilter = GraphBuilder::build(emptyRd, false);
        assertTest(gNoFilter.nodeCount() == 0, "Case 4: empty RoadData (keepLargestComponent=false) nodeCount is 0");
        assertTest(gNoFilter.edgeCount() == 0, "Case 4: empty RoadData (keepLargestComponent=false) edgeCount is 0");
    }

    // 5. snapPlaces: a place near a node snaps, a place 5 km away is unsnapped, input order is preserved
    {
        Graph g;
        int nIdx = g.addNode(100, 12.9716, 77.5946);

        // Place near node (approx 15 meters away)
        Place pNear;
        pNear.id = 1;
        pNear.lat = 12.9717;
        pNear.lon = 77.5947;
        pNear.name = "Near Clinic";

        // Place far away (~5.5 km away)
        // 0.05 degrees latitude is roughly 5.5 km
        Place pFar;
        pFar.id = 2;
        pFar.lat = 13.0216;
        pFar.lon = 77.5946;
        pFar.name = "Far Hospital";

        std::vector<Place> places = {pNear, pFar};
        std::vector<SnappedPlace> snapped = GraphBuilder::snapPlaces(g, places, 500.0);

        assertTest(snapped.size() == 2, "Case 5: snapPlaces output size matches input size");
        assertTest(snapped[0].place.id == pNear.id, "Case 5: input order preserved for first place");
        assertTest(snapped[1].place.id == pFar.id, "Case 5: input order preserved for second place");

        // Verify near place
        assertTest(snapped[0].snapped, "Case 5: near place is snapped");
        assertTest(snapped[0].nodeIndex == nIdx, "Case 5: near place snapped to correct nodeIndex");
        assertTest(snapped[0].snapDistanceMeters < 500.0 && snapped[0].snapDistanceMeters > 0.0,
                   "Case 5: near place snap distance within 500m");

        // Verify far place
        assertTest(!snapped[1].snapped, "Case 5: far place (5km) is unsnapped");
        assertTest(snapped[1].nodeIndex == -1, "Case 5: unsnapped place nodeIndex is -1");
        assertTest(snapped[1].snapDistanceMeters > 5000.0,
                   "Case 5: far place distance is > 5000m");
    }

    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "GraphBuilder Test Summary: " << (failures == 0 ? "ALL PASSED" : "FAILED")
        << " (" << failures << " failures)";
    std::cout << oss.str() << "\n";

    return (failures == 0) ? 0 : 1;
}
