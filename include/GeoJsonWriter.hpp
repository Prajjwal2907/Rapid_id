#ifndef RAPIDAID_GEOJSON_GEOJSONWRITER_HPP
#define RAPIDAID_GEOJSON_GEOJSONWRITER_HPP

#include "Graph.hpp"
#include "GraphBuilder.hpp"
#include "OverpassClient.hpp"

#include <string>
#include <vector>

namespace rapidaid::geojson {

class GeoJsonWriter {
public:
    // Serializes a vector of places into an RFC 7946 GeoJSON FeatureCollection string.
    // Each place is converted to a Point feature with coordinates [lon, lat] rounded to 7 decimals,
    // and properties: id, osmType, name, category, and tags.
    // Failure behavior: Returns a FeatureCollection with an empty features array if places is empty. Never throws.
    static std::string placesCollection(const std::vector<rapidaid::osm::Place>& places);

    // Serializes a vector of snapped places into an RFC 7946 GeoJSON FeatureCollection string.
    // Each snapped place is converted to a Point feature with coordinates [lon, lat] rounded to 7 decimals,
    // and properties: id, osmType, name, category, tags, snapped, and snapDistanceMeters.
    // Failure behavior: Returns a FeatureCollection with an empty features array if places is empty. Never throws.
    static std::string snappedPlacesCollection(const std::vector<rapidaid::graph::SnappedPlace>& places);

    // Serializes a path on graph g into an RFC 7946 GeoJSON FeatureCollection string.
    // Emits a single LineString feature through the path nodes in order, with properties distanceMeters and nodeCount.
    // Failure behavior: Returns a FeatureCollection with an empty features array if path.found is false,
    // the path has fewer than 2 nodes, or any node index is out of range for g. Never throws.
    static std::string pathCollection(const rapidaid::graph::Graph& g, const rapidaid::graph::PathResult& path);

    // Serializes all undirected edges in graph g into an RFC 7946 GeoJSON FeatureCollection string.
    // Emits every undirected edge exactly once as a 2-point LineString feature with property weightMeters.
    // Failure behavior: Returns a FeatureCollection with an empty features array if g is empty or has no edges. Never throws.
    static std::string roadsCollection(const rapidaid::graph::Graph& g);

    // Writes a GeoJSON string to path atomically via FileStore::save.
    // Creates parent directories if needed.
    // Failure behavior: Returns false if saving fails (e.g. invalid path or write error). Never throws.
    static bool writeFile(const std::string& path, const std::string& geojson);
};

} // namespace rapidaid::geojson

#endif // RAPIDAID_GEOJSON_GEOJSONWRITER_HPP
