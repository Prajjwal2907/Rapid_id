#include "Graph.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using rapidaid::graph::Graph;
using rapidaid::graph::DistanceMap;
using rapidaid::graph::PathResult;

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
    std::cout << "Running test_graph_multi_offline...\n";

    // 1. Small connected graph: shortestPathsFrom matches shortestPath for every pair
    {
        Graph g;
        // 5 nodes: 0, 1, 2, 3, 4
        g.addNode(101, 10.0, 20.0);
        g.addNode(102, 10.1, 20.1);
        g.addNode(103, 10.2, 20.2);
        g.addNode(104, 10.3, 20.3);
        g.addNode(105, 10.4, 20.4);

        g.addEdge(0, 1, 100.0);
        g.addEdge(1, 2, 150.0);
        g.addEdge(0, 2, 300.0);
        g.addEdge(2, 3, 80.0);
        g.addEdge(3, 4, 120.0);
        g.addEdge(1, 4, 500.0);

        bool allDistancesMatch = true;
        bool allPathsMatch = true;

        for (int u = 0; u < g.nodeCount(); ++u) {
            DistanceMap dmap = g.shortestPathsFrom(u);
            if (static_cast<int>(dmap.dist.size()) != g.nodeCount() ||
                static_cast<int>(dmap.pred.size()) != g.nodeCount()) {
                allDistancesMatch = false;
                break;
            }

            for (int v = 0; v < g.nodeCount(); ++v) {
                PathResult sp = g.shortestPath(u, v);
                if (std::abs(dmap.dist[v] - sp.distanceMeters) > 1e-6) {
                    allDistancesMatch = false;
                }
                std::vector<int> path = g.pathFromMap(dmap, u, v);
                if (path != sp.nodes) {
                    allPathsMatch = false;
                }
            }
        }

        assertTest(allDistancesMatch, "shortestPathsFrom distances match shortestPath for all pairs");
        assertTest(allPathsMatch, "pathFromMap matches shortestPath nodes for all pairs");
    }

    // 2. Bad source indices return empty DistanceMap
    {
        Graph g;
        g.addNode(1, 0.0, 0.0);
        g.addNode(2, 1.0, 1.0);
        g.addEdge(0, 1, 50.0);

        DistanceMap negSource = g.shortestPathsFrom(-1);
        assertTest(negSource.dist.empty() && negSource.pred.empty(), "shortestPathsFrom(-1) returns empty vectors");

        DistanceMap outOfBounds = g.shortestPathsFrom(2);
        assertTest(outOfBounds.dist.empty() && outOfBounds.pred.empty(), "shortestPathsFrom(nodeCount) returns empty vectors");

        DistanceMap largeOutOfBounds = g.shortestPathsFrom(999);
        assertTest(largeOutOfBounds.dist.empty() && largeOutOfBounds.pred.empty(), "shortestPathsFrom(999) returns empty vectors");

        DistanceMap validMap = g.shortestPathsFrom(0);
        assertTest(g.pathFromMap(validMap, -1, 1).empty(), "pathFromMap with negative source returns empty");
        assertTest(g.pathFromMap(validMap, 0, -1).empty(), "pathFromMap with negative target returns empty");
        assertTest(g.pathFromMap(validMap, 2, 1).empty(), "pathFromMap with out-of-bounds source returns empty");
        assertTest(g.pathFromMap(validMap, 0, 2).empty(), "pathFromMap with out-of-bounds target returns empty");
        assertTest(g.pathFromMap(negSource, 0, 1).empty(), "pathFromMap with empty DistanceMap returns empty");
    }

    // 3. Unreachable nodes in disconnected graph
    {
        Graph g;
        g.addNode(10, 0.0, 0.0); // 0
        g.addNode(20, 0.1, 0.1); // 1
        g.addNode(30, 1.0, 1.0); // 2
        g.addNode(40, 1.1, 1.1); // 3
        g.addNode(50, 2.0, 2.0); // 4 (isolated)

        g.addEdge(0, 1, 42.0);
        g.addEdge(2, 3, 58.0);

        DistanceMap map0 = g.shortestPathsFrom(0);
        assertTest(map0.dist[0] == 0.0, "source distance is 0.0");
        assertTest(std::abs(map0.dist[1] - 42.0) < 1e-6, "reachable neighbor distance matches edge weight");
        assertTest(std::isinf(map0.dist[2]), "unreachable component node 2 distance is infinity");
        assertTest(std::isinf(map0.dist[3]), "unreachable component node 3 distance is infinity");
        assertTest(std::isinf(map0.dist[4]), "isolated node 4 distance is infinity");

        assertTest(map0.pred[0] == -1, "source pred is -1");
        assertTest(map0.pred[1] == 0, "node 1 pred is 0");
        assertTest(map0.pred[2] == -1, "unreachable node 2 pred is -1");
        assertTest(map0.pred[4] == -1, "unreachable isolated node 4 pred is -1");

        assertTest(g.pathFromMap(map0, 0, 1) == std::vector<int>({0, 1}), "pathFromMap to reachable node is {0, 1}");
        assertTest(g.pathFromMap(map0, 0, 2).empty(), "pathFromMap to unreachable node 2 is empty");
        assertTest(g.pathFromMap(map0, 0, 4).empty(), "pathFromMap to isolated node 4 is empty");
        assertTest(g.pathFromMap(map0, 0, 0) == std::vector<int>({0}), "pathFromMap source to source is {0}");
    }

    return failures == 0 ? 0 : 1;
}
