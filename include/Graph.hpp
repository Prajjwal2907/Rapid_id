#ifndef RAPIDAID_GRAPH_GRAPH_HPP
#define RAPIDAID_GRAPH_GRAPH_HPP

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace rapidaid::graph {

struct Edge {
    int to;
    double weightMeters;
};

struct PathResult {
    bool found = false;
    double distanceMeters = 0.0;
    std::vector<int> nodes;
};

struct DistanceMap {
    std::vector<double> dist;
    std::vector<int> pred;
};

class Graph {
public:
    Graph() = default;

    // Adds a node with the given osmId and coordinates.
    // Failure behavior: If osmId already exists in the graph, returns the existing
    // node index without updating coordinates or adding a duplicate.
    int addNode(long long osmId, double lat, double lon);

    // Adds an undirected edge between node indices a and b with the given weight in meters.
    // Failure behavior: Ignores negative weights, self-loops (a == b), and out-of-range
    // node indices (does nothing). If an edge between a and b already exists, retains
    // the smaller weight.
    void addEdge(int a, int b, double weightMeters);

    // Returns the total number of nodes in the graph. Never fails (returns 0 if empty).
    int nodeCount() const;

    // Returns the total number of undirected edges in the graph, counting each edge once.
    // Never fails (returns 0 if empty).
    size_t edgeCount() const;

    // Returns the outgoing edges from the node at index.
    // Failure behavior: Returns a reference to an empty vector if index is out of range.
    const std::vector<Edge>& neighbors(int index) const;

    // Returns the OSM ID of the node at index.
    // Failure behavior: Returns 0 if index is out of range.
    long long osmId(int index) const;

    // Returns the latitude of the node at index.
    // Failure behavior: Returns 0.0 if index is out of range.
    double lat(int index) const;

    // Returns the longitude of the node at index.
    // Failure behavior: Returns 0.0 if index is out of range.
    double lon(int index) const;

    // Returns the dense node index for the given OSM ID.
    // Failure behavior: Returns -1 if osmId does not exist in the graph.
    int indexOf(long long osmId) const;

    // Finds the nearest node to (lat, lon) using haversine distance.
    // Failure behavior: Returns -1 if the graph is empty (distanceMetersOut is unchanged).
    // If distanceMetersOut is non-null and graph is non-empty, stores distance in meters.
    int nearestNode(double lat, double lon, double* distanceMetersOut = nullptr) const;

    // Computes the shortest path from node 'from' to node 'to' using Dijkstra's algorithm.
    // Failure behavior: Returns found=false, distanceMeters=0.0, and empty nodes vector
    // if from or to is out of range or if no path exists between them.
    // If from == to and index is valid, returns found=true, distanceMeters=0.0, nodes={from}.
    PathResult shortestPath(int from, int to) const;

    // Computes shortest paths from source node to all other nodes using Dijkstra's algorithm.
    // Failure behavior: Returns empty dist and pred vectors if source < 0 or source >= nodeCount().
    // For reachable nodes, dist contains the shortest distance in meters, and pred contains the predecessor node index.
    // For unreachable nodes, dist is std::numeric_limits<double>::infinity(), and pred is -1.
    // For source itself, dist[source] is 0.0, and pred[source] is -1.
    DistanceMap shortestPathsFrom(int source) const;

    // Reconstructs the node index sequence from source to target using a previously computed DistanceMap.
    // Failure behavior: Returns an empty vector if source or target is out of range, if m is invalid/empty,
    // or if target is unreachable from source (dist is infinity or target != source and pred is -1).
    // If source == target and valid, returns {source}.
    std::vector<int> pathFromMap(const DistanceMap& m, int source, int target) const;

    // Returns a new Graph containing only the largest connected component, preserving
    // OSM IDs and coordinates. Uses an iterative traversal.
    // Failure behavior: Returns an empty Graph if this graph is empty.
    // In case of a size tie, selects the component containing the lowest node index.
    Graph largestComponent() const;

private:
    struct NodeData {
        long long osmId = 0;
        double lat = 0.0;
        double lon = 0.0;
        std::vector<Edge> edges;
    };

    std::vector<NodeData> m_nodes;
    std::unordered_map<long long, int> m_osmIdToIndex;
    size_t m_edgeCount = 0;
};

} // namespace rapidaid::graph

#endif // RAPIDAID_GRAPH_GRAPH_HPP
