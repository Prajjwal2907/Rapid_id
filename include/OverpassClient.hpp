#ifndef OVERPASS_CLIENT_HPP
#define OVERPASS_CLIENT_HPP

#include <string>
#include <vector>
#include <map>

namespace rapidaid::osm {

enum class Category {
    Hospital,
    Clinic,
    Pharmacy,
    FireStation,
    Police,
    Shelter,
    Water
};

struct Place {
    long long id;
    std::string osmType;
    double lat;
    double lon;
    std::string name;
    std::map<std::string, std::string> tags;
    Category category;
};

class OverpassClient {
public:
    // Finds places of a category within radiusMeters of (lat, lon).
    // Returns an empty vector on failure (invalid input, network failure, or JSON parse error).
    // Validates radius (1..50000), lat ([-90, 90]), and lon ([-180, 180]).
    // Retries up to 3 times total with exponential backoff (2s, 4s) when HttpClient returns "" (504/timeouts).
    static std::vector<Place> findNearby(Category category, double lat, double lon, int radiusMeters);

    // Pure functions, no network:

    // Builds the Overpass QL query string for the requested category, location, and radius.
    // Query format uses [out:json][timeout:25] and around filters for node, way, relation.
    static std::string buildQuery(Category category, double lat, double lon, int radiusMeters);

    // Formats query into form-encoded POST body: "data=" + HttpClient::urlEncode(query).
    // Returns "" if urlEncode fails.
    static std::string buildRequestBody(const std::string& query);

    // Parses Overpass JSON response body.
    // Returns an empty vector on empty string, invalid JSON, missing elements, or parse error.
    static std::vector<Place> parseResponse(const std::string& body, Category category);

    // Returns the canonical name string of the category (e.g. "hospital", "clinic").
    static std::string categoryName(Category category);
};

} // namespace rapidaid::osm

#endif // OVERPASS_CLIENT_HPP
