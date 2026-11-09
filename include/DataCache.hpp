#ifndef RAPIDAID_DATA_DATACACHE_HPP
#define RAPIDAID_DATA_DATACACHE_HPP

#include "Geocoder.hpp"
#include "OverpassClient.hpp"
#include "RoadClient.hpp"

#include <optional>
#include <string>
#include <vector>

namespace rapidaid::data {

class DataCache {
public:
    // Constructs a DataCache instance targeting the given directory and refresh policy.
    // Failure behavior: Never throws.
    explicit DataCache(std::string dir = "data", bool refresh = false);

    // Geocodes a free-text query via Nominatim, using cache if valid and refresh is false.
    // Failure behavior: Returns std::nullopt on empty query, network failure, or parse failure. Never throws.
    std::optional<rapidaid::geo::Location> geocode(const std::string& query);

    // Finds places of category c within radiusMeters of (lat, lon) via Overpass, using cache if valid and refresh is false.
    // Failure behavior: Returns an empty vector on invalid input, network failure, or parse failure. Never throws.
    std::vector<rapidaid::osm::Place> places(rapidaid::osm::Category c, double lat, double lon, int radiusMeters);

    // Fetches drivable road network within radiusMeters of (lat, lon) via Overpass, using cache if valid and refresh is false.
    // Failure behavior: Returns an empty RoadData on invalid input, network failure, or parse failure. Never throws.
    rapidaid::osm::RoadData roads(double lat, double lon, int radiusMeters);

    // Computes the cache file path for a geocode query:
    // dir + "/geocode_" + FileStore::sanitizeFileName(query) + ".json"
    // Failure behavior: Falls back to sanitized name if query is empty; never throws.
    std::string geocodePath(const std::string& query) const;

    // Computes the cache file path for a places query:
    // dir + "/places_<categoryName lowercase>_<lat4>_<lon4>_<radius>.json"
    // Failure behavior: Formats coordinates with 4 decimals in classic locale; never throws.
    std::string placesPath(rapidaid::osm::Category c, double lat, double lon, int radiusMeters) const;

    // Computes the cache file path for a roads query:
    // dir + "/roads_<lat4>_<lon4>_<radius>.json"
    // Failure behavior: Formats coordinates with 4 decimals in classic locale; never throws.
    std::string roadsPath(double lat, double lon, int radiusMeters) const;

private:
    std::string m_dir;
    bool m_refresh;

    template <typename Result, typename Parser, typename Fetcher, typename IsEmpty>
    Result getCachedOrFetch(const std::string& path,
                            Parser&& parser,
                            Fetcher&& fetcher,
                            IsEmpty&& isEmpty);
};

} // namespace rapidaid::data

#endif // RAPIDAID_DATA_DATACACHE_HPP
