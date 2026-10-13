#include "GraphBuilder.hpp"
#include "GeoMath.hpp"

namespace rapidaid::graph {

Graph GraphBuilder::build(const rapidaid::osm::RoadData& roads, bool keepLargestComponent) {
    Graph graph;

    for (const auto& way : roads.ways) {
        if (way.nodeIds.size() < 2) {
            continue;
        }

        for (size_t i = 0; i + 1 < way.nodeIds.size(); ++i) {
            long long idA = way.nodeIds[i];
            long long idB = way.nodeIds[i + 1];

            auto itA = roads.nodes.find(idA);
            auto itB = roads.nodes.find(idB);

            if (itA == roads.nodes.end() || itB == roads.nodes.end()) {
                continue;
            }

            const auto& nodeA = itA->second;
            const auto& nodeB = itB->second;

            int idxA = graph.addNode(nodeA.id, nodeA.lat, nodeA.lon);
            int idxB = graph.addNode(nodeB.id, nodeB.lat, nodeB.lon);

            double dist = rapidaid::geo::haversineMeters(nodeA.lat, nodeA.lon, nodeB.lat, nodeB.lon);
            graph.addEdge(idxA, idxB, dist);
        }
    }

    if (keepLargestComponent) {
        return graph.largestComponent();
    }
    return graph;
}

std::vector<SnappedPlace> GraphBuilder::snapPlaces(const Graph& graph,
                                                  const std::vector<rapidaid::osm::Place>& places,
                                                  double maxSnapMeters) {
    std::vector<SnappedPlace> result;
    result.reserve(places.size());

    for (const auto& place : places) {
        SnappedPlace sp;
        sp.place = place;
        sp.nodeIndex = -1;
        sp.snapDistanceMeters = 0.0;
        sp.snapped = false;

        if (graph.nodeCount() > 0) {
            double dist = 0.0;
            int nearest = graph.nearestNode(place.lat, place.lon, &dist);
            if (nearest != -1 && dist <= maxSnapMeters) {
                sp.nodeIndex = nearest;
                sp.snapDistanceMeters = dist;
                sp.snapped = true;
            } else {
                sp.nodeIndex = -1;
                sp.snapDistanceMeters = dist;
                sp.snapped = false;
            }
        }

        result.push_back(sp);
    }

    return result;
}

} // namespace rapidaid::graph
