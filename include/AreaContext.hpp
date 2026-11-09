#ifndef RAPIDAID_AREA_AREACONTEXT_HPP
#define RAPIDAID_AREA_AREACONTEXT_HPP

#include "DataCache.hpp"
#include "Graph.hpp"
#include "GraphBuilder.hpp"
#include "OverpassClient.hpp"
#include "RoadClient.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rapidaid::area {

struct AreaInfo {
    std::string name;
    std::string displayName;
    double lat = 0.0;
    double lon = 0.0;
    int radiusMeters = 0;

    bool operator==(const AreaInfo& other) const {
        return name == other.name &&
               displayName == other.displayName &&
               lat == other.lat &&
               lon == other.lon &&
               radiusMeters == other.radiusMeters;
    }

    bool operator!=(const AreaInfo& other) const {
        return !(*this == other);
    }
};

class AreaContext {
public:
    AreaContext() = default;

    // Builds an AreaContext for the given AreaInfo using the provided DataCache.
    // Queries road network and place categories: "hospital", "fire_station", "police", "clinic", "pharmacy", "shelter".
    // If allowNetwork is false: checks that roads and all place category cache files exist via DataCache path functions
    // (using FileStore::exists) and fails immediately if any are missing, never performing network calls.
    // If allowNetwork is true: fetches missing cache data via network.
    // Builds graph via GraphBuilder::build(roads) and snaps places via GraphBuilder::snapPlaces(graph, places).
    // Failure behavior: Returns false if any required cache file is missing (when allowNetwork is false),
    // or if the road graph contains no nodes, or on parse/fetch failure. Populates errorOut with a descriptive
    // message if errorOut is non-null. Does not mutate out on failure. Never throws exceptions.
    static bool build(rapidaid::data::DataCache& cache,
                      const AreaInfo& info,
                      bool allowNetwork,
                      AreaContext& out,
                      std::string* errorOut = nullptr);

    // Atomically saves AreaInfo to path as formatted JSON using a temporary file (path + ".tmp") and rename.
    // Tolerant of missing parent directories (creates them if needed). Overwrites existing file.
    // Failure behavior: Returns false on serialization error, write error, or rename failure.
    // Cleans up any temporary file on failure. Never throws exceptions.
    static bool saveInfo(const std::string& path, const AreaInfo& info);

    // Loads AreaInfo from the specified JSON file.
    // Tolerant of missing fields, key naming variations ("displayName"/"display_name", "radiusMeters"/"radius", "name"/"query"),
    // and coordinates/radius formatted as strings or numbers.
    // Failure behavior: Returns std::nullopt if the file does not exist, cannot be read, contains malformed JSON,
    // or cannot be parsed. Never throws exceptions.
    static std::optional<AreaInfo> loadInfo(const std::string& path);

    // Returns a const reference to the AreaInfo.
    // Failure behavior: Never fails.
    const AreaInfo& info() const;

    // Returns a const reference to the routing graph.
    // Failure behavior: Never fails.
    const rapidaid::graph::Graph& graph() const;

    // Returns a const reference to the snapped places vector.
    // Failure behavior: Never fails.
    const std::vector<rapidaid::graph::SnappedPlace>& snappedPlaces() const;

    // Returns a const reference to the raw places vector.
    // Failure behavior: Never fails.
    const std::vector<rapidaid::osm::Place>& places() const;

    // Returns a const reference to the road network data.
    // Failure behavior: Never fails.
    const rapidaid::osm::RoadData& roads() const;

private:
    AreaInfo m_info;
    rapidaid::osm::RoadData m_roads;
    rapidaid::graph::Graph m_graph;
    std::vector<rapidaid::osm::Place> m_places;
    std::vector<rapidaid::graph::SnappedPlace> m_snappedPlaces;
};

} // namespace rapidaid::area

#endif // RAPIDAID_AREA_AREACONTEXT_HPP
