#include "Graph.hpp"
#include "GeoMath.hpp"

#include <cmath>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

using rapidaid::graph::Graph;
using rapidaid::graph::PathResult;
using rapidaid::geo::haversineMeters;

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
    std::cout << "Running test_graph_offline...\n";

    // 1. addNode dedupe
    {
        Graph g;
        int idx1 = g.addNode(1001, 12.0, 77.0);
        int idx2 = g.addNode(1001, 15.0, 80.0); // same osmId, different coords ignored
        assertTest(idx1 == 0 && idx2 == 0, "addNode dedupe returns same index");
        assertTest(g.nodeCount() == 1, "addNode dedupe keeps node count at 1");
        assertTest(g.indexOf(1001) == 0, "indexOf finds deduplicated node");
        assertTest(std::abs(g.lat(0) - 12.0) < 1e-6 && std::abs(g.lon(0) - 77.0) < 1e-6, "addNode dedupe keeps original coordinates");

        int idx3 = g.addNode(1002, 13.0, 78.0);
        assertTest(idx3 == 1 && g.nodeCount() == 2, "addNode with new osmId increments count and returns index 1");
    }

    // 2. addEdge undirected (neighbors of both ends)
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        int b = g.addNode(2, 10.1, 20.1);
        g.addEdge(a, b, 150.0);

        const auto& nA = g.neighbors(a);
        const auto& nB = g.neighbors(b);

        bool aToB = (nA.size() == 1 && nA[0].to == b && std::abs(nA[0].weightMeters - 150.0) < 1e-6);
        bool bToA = (nB.size() == 1 && nB[0].to == a && std::abs(nB[0].weightMeters - 150.0) < 1e-6);
        assertTest(aToB && bToA, "addEdge creates undirected edges in both neighbor lists");
    }

    // 3. self-loop ignored
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        g.addEdge(a, a, 50.0);
        assertTest(g.edgeCount() == 0, "self-loop edge is ignored (edgeCount is 0)");
        assertTest(g.neighbors(a).empty(), "self-loop does not appear in neighbors list");
    }

    // 4. parallel edge keeps the smaller weight
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        int b = g.addNode(2, 10.1, 20.1);
        g.addEdge(a, b, 100.0);
        g.addEdge(a, b, 150.0); // larger weight, should be ignored
        assertTest(g.edgeCount() == 1, "parallel edge does not increase edge count");
        assertTest(std::abs(g.neighbors(a)[0].weightMeters - 100.0) < 1e-6, "parallel edge with larger weight ignored");
        assertTest(std::abs(g.neighbors(b)[0].weightMeters - 100.0) < 1e-6, "parallel edge larger weight ignored in reverse list");

        g.addEdge(a, b, 60.0); // smaller weight, should replace
        assertTest(g.edgeCount() == 1, "parallel edge smaller weight does not increase edge count");
        assertTest(std::abs(g.neighbors(a)[0].weightMeters - 60.0) < 1e-6, "parallel edge updates to smaller weight in a");
        assertTest(std::abs(g.neighbors(b)[0].weightMeters - 60.0) < 1e-6, "parallel edge updates to smaller weight in b");
    }

    // 5. edgeCount counts once
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        int b = g.addNode(2, 10.1, 20.1);
        int c = g.addNode(3, 10.2, 20.2);
        g.addEdge(a, b, 10.0);
        assertTest(g.edgeCount() == 1, "edgeCount is 1 after first undirected edge");
        g.addEdge(b, c, 20.0);
        assertTest(g.edgeCount() == 2, "edgeCount is 2 after second undirected edge");
        g.addEdge(a, c, 30.0);
        assertTest(g.edgeCount() == 3, "edgeCount is 3 after third undirected edge");
    }

    // 6. shortestPath picks a cheaper two-hop route over a longer direct edge
    {
        Graph g;
        int n0 = g.addNode(10, 0.0, 0.0);
        int n1 = g.addNode(20, 0.0, 1.0);
        int n2 = g.addNode(30, 0.0, 2.0);

        g.addEdge(n0, n2, 100.0); // Direct edge with weight 100
        g.addEdge(n0, n1, 20.0);  // Two-hop route: 20 + 30 = 50
        g.addEdge(n1, n2, 30.0);

        PathResult res = g.shortestPath(n0, n2);
        assertTest(res.found, "shortestPath found two-hop route");
        assertTest(std::abs(res.distanceMeters - 50.0) < 1e-6, "shortestPath distance is 50.0");
        std::vector<int> expectedNodes = {n0, n1, n2};
        assertTest(res.nodes == expectedNodes, "shortestPath nodes sequence is [0, 1, 2]");
    }

    // 7. from == to
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        PathResult res = g.shortestPath(a, a);
        assertTest(res.found, "shortestPath from==to found is true");
        assertTest(std::abs(res.distanceMeters) < 1e-6, "shortestPath from==to distance is 0");
        std::vector<int> expected = {a};
        assertTest(res.nodes == expected, "shortestPath from==to nodes is {from}");
    }

    // 8. no path between two disconnected nodes
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        int b = g.addNode(2, 10.1, 20.1);
        int c = g.addNode(3, 10.2, 20.2);
        g.addEdge(a, b, 10.0);
        // c is disconnected
        PathResult res = g.shortestPath(a, c);
        assertTest(!res.found, "shortestPath between disconnected nodes has found=false");
        assertTest(std::abs(res.distanceMeters) < 1e-6, "shortestPath disconnected distance is 0.0");
        assertTest(res.nodes.empty(), "shortestPath disconnected nodes list is empty");
    }

    // 9. invalid indices
    {
        Graph g;
        int a = g.addNode(1, 10.0, 20.0);
        int b = g.addNode(2, 10.1, 20.1);
        g.addEdge(a, b, 15.0);

        PathResult badFrom = g.shortestPath(-1, b);
        assertTest(!badFrom.found && badFrom.nodes.empty(), "shortestPath with negative from returns not found");

        PathResult badTo = g.shortestPath(a, 99);
        assertTest(!badTo.found && badTo.nodes.empty(), "shortestPath with out-of-range to returns not found");

        assertTest(g.osmId(-1) == 0 && g.osmId(99) == 0, "osmId out-of-range returns 0");
        assertTest(std::abs(g.lat(-1)) < 1e-6 && std::abs(g.lat(99)) < 1e-6, "lat out-of-range returns 0.0");
        assertTest(std::abs(g.lon(-1)) < 1e-6 && std::abs(g.lon(99)) < 1e-6, "lon out-of-range returns 0.0");
        assertTest(g.indexOf(999999) == -1, "indexOf nonexistent osmId returns -1");

        // addEdge invalid indices and negative weight
        size_t initialEdges = g.edgeCount();
        g.addEdge(-1, b, 10.0);
        g.addEdge(a, 99, 10.0);
        g.addEdge(a, b, -10.0);
        assertTest(g.edgeCount() == initialEdges, "addEdge ignores bad indices and negative weights");
    }

    // 10. nearestNode on a small grid and on an empty graph
    {
        Graph emptyGraph;
        double dummyDist = 999.0;
        int emptyNearest = emptyGraph.nearestNode(10.0, 20.0, &dummyDist);
        assertTest(emptyNearest == -1, "nearestNode on empty graph returns -1");
        assertTest(std::abs(dummyDist - 999.0) < 1e-6, "nearestNode on empty graph does not modify distance out");

        Graph grid;
        int n00 = grid.addNode(1, 0.0, 0.0);
        grid.addNode(2, 0.0, 1.0);
        grid.addNode(3, 1.0, 0.0);
        grid.addNode(4, 1.0, 1.0);

        double outDist = 0.0;
        int nearest = grid.nearestNode(0.001, 0.001, &outDist);
        assertTest(nearest == n00, "nearestNode on small grid finds (0,0)");
        double expectedDist = haversineMeters(0.001, 0.001, 0.0, 0.0);
        assertTest(std::abs(outDist - expectedDist) < 1e-3, "nearestNode returns correct haversine distance");
    }

    // 11. largestComponent keeps the bigger of two components and preserves ids
    {
        Graph g;
        // Component A: 2 nodes
        int a1 = g.addNode(101, 1.0, 1.0);
        int a2 = g.addNode(102, 1.1, 1.1);
        g.addEdge(a1, a2, 10.0);

        // Component B: 3 nodes
        int b1 = g.addNode(201, 2.0, 2.0);
        int b2 = g.addNode(202, 2.1, 2.1);
        int b3 = g.addNode(203, 2.2, 2.2);
        g.addEdge(b1, b2, 20.0);
        g.addEdge(b2, b3, 30.0);

        Graph lc = g.largestComponent();
        assertTest(lc.nodeCount() == 3, "largestComponent nodeCount is 3");
        assertTest(lc.edgeCount() == 2, "largestComponent edgeCount is 2");

        assertTest(lc.indexOf(201) != -1 && lc.indexOf(202) != -1 && lc.indexOf(203) != -1,
                   "largestComponent preserves Component B OSM IDs");
        assertTest(lc.indexOf(101) == -1 && lc.indexOf(102) == -1,
                   "largestComponent drops Component A OSM IDs");

        int newB1 = lc.indexOf(201);
        assertTest(std::abs(lc.lat(newB1) - 2.0) < 1e-6 && std::abs(lc.lon(newB1) - 2.0) < 1e-6,
                   "largestComponent preserves coordinates");

        // Tie-breaker test: two components of equal size
        Graph tieGraph;
        int t0 = tieGraph.addNode(301, 3.0, 3.0);
        int t1 = tieGraph.addNode(302, 3.1, 3.1);
        tieGraph.addEdge(t0, t1, 5.0);

        int t2 = tieGraph.addNode(401, 4.0, 4.0);
        int t3 = tieGraph.addNode(402, 4.1, 4.1);
        tieGraph.addEdge(t2, t3, 5.0);

        Graph lcTie = tieGraph.largestComponent();
        assertTest(lcTie.nodeCount() == 2, "largestComponent tie-breaker keeps size 2");
        assertTest(lcTie.indexOf(301) != -1 && lcTie.indexOf(302) != -1,
                   "largestComponent tie-breaker selects component containing lowest node index");
        assertTest(lcTie.indexOf(401) == -1 && lcTie.indexOf(402) == -1,
                   "largestComponent tie-breaker excludes component with higher indices");
    }

    // 12. neighbors of a bad index
    {
        Graph g;
        g.addNode(1, 10.0, 20.0);
        assertTest(g.neighbors(-1).empty(), "neighbors of -1 is empty");
        assertTest(g.neighbors(99).empty(), "neighbors of 99 is empty");
    }

    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "Graph Test Summary: " << (failures == 0 ? "ALL PASSED" : "FAILED")
        << " (" << failures << " failures)";
    std::cout << oss.str() << "\n";

    return (failures == 0) ? 0 : 1;
}
