#include "DataCache.hpp"
#include "FileStore.hpp"
#include "Graph.hpp"
#include "GraphBuilder.hpp"
#include "GeoJsonWriter.hpp"

#include <iostream>
#include <string>
#include <vector>

int main() {
    rapidaid::data::DataCache cache("data", false);

    // 1. Geocode
    std::string query = "Eiffel Tower, Paris";
    std::string geoPath = cache.geocodePath(query);
    bool geoFromCache = rapidaid::io::FileStore::exists(geoPath);
    std::cout << "Geocode input source: " << (geoFromCache ? "cache" : "network") << "\n";

    auto loc = cache.geocode(query);
    if (!loc.has_value()) {
        std::cerr << "Geocode failed for: " << query << "\n";
        return 1;
    }
    std::cout << "Geocoded location: (" << loc->lat << ", " << loc->lon << ") "
              << loc->displayName << "\n";

    // 2. Places (Hospital, radius 3000 around result)
    int radius = 3000;
    std::string placesPath = cache.placesPath(rapidaid::osm::Category::Hospital, loc->lat, loc->lon, radius);
    bool placesFromCache = rapidaid::io::FileStore::exists(placesPath);
    std::cout << "Places input source: " << (placesFromCache ? "cache" : "network") << "\n";

    auto places = cache.places(rapidaid::osm::Category::Hospital, loc->lat, loc->lon, radius);
    std::cout << "Found " << places.size() << " hospitals.\n";

    // 3. Roads (radius 3000 around result)
    std::string roadsPath = cache.roadsPath(loc->lat, loc->lon, radius);
    bool roadsFromCache = rapidaid::io::FileStore::exists(roadsPath);
    std::cout << "Roads input source: " << (roadsFromCache ? "cache" : "network") << "\n";

    auto roads = cache.roads(loc->lat, loc->lon, radius);
    std::cout << "Road data contains " << roads.nodes.size() << " nodes, "
              << roads.ways.size() << " ways.\n";

    // 4. Build graph
    rapidaid::graph::Graph graph = rapidaid::graph::GraphBuilder::build(roads);
    std::cout << "Node count: " << graph.nodeCount() << "\n";
    std::cout << "Edge count: " << graph.edgeCount() << "\n";

    // 5. Snap places
    auto snapped = rapidaid::graph::GraphBuilder::snapPlaces(graph, places);
    std::vector<rapidaid::graph::SnappedPlace> successfullySnapped;
    for (const auto& sp : snapped) {
        if (sp.snapped) {
            successfullySnapped.push_back(sp);
        }
    }
    std::cout << "Number of snapped places: " << successfullySnapped.size() << "\n";

    // 6. Shortest path distance between the first two snapped places
    if (successfullySnapped.size() >= 2) {
        auto path = graph.shortestPath(successfullySnapped[0].nodeIndex, successfullySnapped[1].nodeIndex);
        if (path.found) {
            std::cout << "Shortest path distance between first two snapped places: "
                      << path.distanceMeters << " meters\n";
        } else {
            std::cout << "No path found between first two snapped places.\n";
        }
    } else {
        std::cout << "Fewer than two snapped places found (" << successfullySnapped.size()
                  << "); cannot compute shortest path between two places.\n";
    }

    // 7. Export GeoJSON
    std::string placesOut = "data/out/places.geojson";
    std::string roadsOut = "data/out/roads.geojson";
    std::string pathOut = "data/out/path.geojson";

    rapidaid::geojson::GeoJsonWriter::writeFile(placesOut,
        rapidaid::geojson::GeoJsonWriter::snappedPlacesCollection(snapped));
    rapidaid::geojson::GeoJsonWriter::writeFile(roadsOut,
        rapidaid::geojson::GeoJsonWriter::roadsCollection(graph));

    rapidaid::graph::PathResult pathExport;
    if (successfullySnapped.size() >= 2) {
        pathExport = graph.shortestPath(successfullySnapped[0].nodeIndex, successfullySnapped[1].nodeIndex);
    }
    rapidaid::geojson::GeoJsonWriter::writeFile(pathOut,
        rapidaid::geojson::GeoJsonWriter::pathCollection(graph, pathExport));

    std::cout << placesOut << "\n";
    std::cout << roadsOut << "\n";
    std::cout << pathOut << "\n";

    return 0;
}
