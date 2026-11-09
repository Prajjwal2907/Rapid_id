#ifndef RAPIDAID_LOCATE_LOCATIONRESOLVER_HPP
#define RAPIDAID_LOCATE_LOCATIONRESOLVER_HPP

#include "Graph.hpp"
#include "OverpassClient.hpp"
#include "RoadClient.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rapidaid::locate {

struct LocationCandidate {
    std::string label;
    double lat = 0.0;
    double lon = 0.0;
    std::string source; /* place|intersection|address */
};

class LocationResolver {
public:
    // Constructs a resolver referencing the road graph, OSM places, and road network data.
    // Builds internal lookup indexes.
    // Failure behavior: Safely handles empty inputs; subsequent searches return empty vectors.
    LocationResolver(const rapidaid::graph::Graph& graph,
                     const std::vector<rapidaid::osm::Place>& places,
                     const rapidaid::osm::RoadData& roadData);

    // Searches places using case-insensitive matching against place names and place labels.
    // Exact matches are returned first, followed by partial (substring) matches, up to max results.
    // Failure behavior: Returns an empty vector if query is empty or if no matches are found.
    std::vector<LocationCandidate> searchPlaces(const std::string& query, size_t max = 10) const;

    // Searches intersections by parsing query strings like "Elm and 5th", "Elm & 5th", "Elm / 5th",
    // or "corner of Elm and 5th". Street names are normalized (lowercase, punctuation removed, spaces collapsed).
    // Intersections are road nodes shared by two differently named ways.
    // Prefers exact normalized matches; falls back to substring matches of at least 3 characters.
    // De-duplicates candidates closer than 15 meters using haversine distance. Exact matches first.
    // Failure behavior: Returns an empty vector on invalid/unparseable queries, empty queries, or no matches.
    std::vector<LocationCandidate> searchIntersections(const std::string& query, size_t max = 10) const;

    // Produces a human-readable label for an OSM place. Returns place.name if non-empty;
    // otherwise returns "Unnamed <category>" plus " (<operator>)" or " (<addr:street>)" when those tags exist.
    // Failure behavior: Never throws; falls back to "Unnamed <category>" or "Unnamed unknown".
    static std::string placeLabel(const rapidaid::osm::Place& place);

private:
    struct IntersectionRecord {
        long long nodeId = 0;
        double lat = 0.0;
        double lon = 0.0;
        std::string streetA;
        std::string streetB;
        std::string normStreetA;
        std::string normStreetB;
    };

    const rapidaid::graph::Graph& m_graph;
    std::vector<rapidaid::osm::Place> m_places;
    const rapidaid::osm::RoadData& m_roadData;
    std::vector<IntersectionRecord> m_intersections;

    void buildIntersectionIndex();
    static bool parseIntersectionQuery(const std::string& query, std::string& street1, std::string& street2);
    static std::string normalizeStreetName(const std::string& input);
};

} // namespace rapidaid::locate

#endif // RAPIDAID_LOCATE_LOCATIONRESOLVER_HPP
