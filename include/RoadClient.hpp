#ifndef RAPIDAID_OSM_ROAD_CLIENT_HPP
#define RAPIDAID_OSM_ROAD_CLIENT_HPP

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace rapidaid::osm {

struct RoadNode {
    long long id;
    double lat;
    double lon;
};

struct RoadWay {
    long long id;
    std::vector<long long> nodeIds;
    std::string highway;
    std::string name;
    std::map<std::string, std::string> tags;
};

struct RoadData {
    std::unordered_map<long long, RoadNode> nodes;
    std::vector<RoadWay> ways;
    bool empty() const; // empty() true when there are no ways (i.e. ways.empty())
};

class RoadClient {
public:
    // Builds Overpass QL query string for drivable roads within radiusMeters of (lat, lon).
    // Validates radius (1..5000), lat ([-90, 90]), lon ([-180, 180]).
    // Returns "" on invalid input.
    static std::string buildRoadQuery(double lat, double lon, int radiusMeters);

    // Formats query into form-encoded POST body: "data=" + HttpClient::urlEncode(query).
    // Returns "" if urlEncode fails on non-empty query.
    static std::string buildRequestBody(const std::string& query);

    // Fetches raw response from Overpass API.
    // Validates radius (1..5000), lat ([-90, 90]), lon ([-180, 180]); invalid -> "" with no network call.
    // Retries up to 3 attempts with 2s and 4s sleep between attempts.
    // Returns the first non-empty response body, or "" if all attempts fail.
    static std::string fetchRaw(double lat, double lon, int radiusMeters);

    // Parses Overpass JSON response body.
    // Returns an empty RoadData on empty string, invalid JSON, or missing elements.
    static RoadData parseRoads(const std::string& body);

    // Checks whether tags describe a drivable road according to highway and access restrictions.
    // Returns false if highway tag is missing, not drivable, or restricted.
    static bool isDrivable(const std::map<std::string, std::string>& tags);
};

} // namespace rapidaid::osm

#endif // RAPIDAID_OSM_ROAD_CLIENT_HPP
