#ifndef RAPIDAID_GRAPH_GRAPH_BUILDER_HPP
#define RAPIDAID_GRAPH_GRAPH_BUILDER_HPP

#include "Graph.hpp"
#include "RoadClient.hpp"
#include "OverpassClient.hpp"

#include <vector>

namespace rapidaid::graph {

struct SnappedPlace {
    rapidaid::osm::Place place;
    int nodeIndex;
    double snapDistanceMeters;
    bool snapped;
};

class GraphBuilder {
public:
    // Builds a Graph from RoadData.
    // For every way, for each consecutive node id pair (a, b): if both exist in roads.nodes,
    // adds both nodes and an undirected edge weighted by haversineMeters.
    // Failure behavior: If either id is missing, skips only that segment and continues with the rest of the way.
    // Returns an empty Graph if roads is empty or contains no valid drivable segments.
    // If keepLargestComponent is true, retains only the largest connected component;
    // if false, retains all components.
    static Graph build(const rapidaid::osm::RoadData& roads, bool keepLargestComponent = true);

    // Snaps each place to its nearest node in the graph within maxSnapMeters.
    // Failure behavior: If the graph is empty or no node is within maxSnapMeters,
    // sets snapped = false and nodeIndex = -1. Otherwise sets snapped = true,
    // nodeIndex to the nearest node index, and snapDistanceMeters to the distance in meters.
    // Preserves the input order of places.
    static std::vector<SnappedPlace> snapPlaces(const Graph& graph,
                                                const std::vector<rapidaid::osm::Place>& places,
                                                double maxSnapMeters = 500.0);
};

} // namespace rapidaid::graph

#endif // RAPIDAID_GRAPH_GRAPH_BUILDER_HPP
