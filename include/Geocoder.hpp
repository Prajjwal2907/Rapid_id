#ifndef RAPIDAID_GEO_GEOCODER_HPP
#define RAPIDAID_GEO_GEOCODER_HPP

#include <string>
#include <optional>
#include <vector>

namespace rapidaid::geo {
    struct Location {
        double lat;
        double lon;
        std::string displayName;
    };

    struct ViewBox {
        double minLon = 0.0;
        double minLat = 0.0;
        double maxLon = 0.0;
        double maxLat = 0.0;
    };

    class Geocoder {
    public:
        // Geocodes a free-text query via Nominatim. Returns std::nullopt on network failure, parse failure, or no results.
        static std::optional<Location> geocode(const std::string& query);

        // Fetches Nominatim JSON body for a query. Returns the raw Nominatim JSON body, or "" on failure.
        // Rejects empty or whitespace-only queries without making a network call.
        static std::string fetchRaw(const std::string& query);

        // Pure function, no network. Parses a Nominatim format=json response body (a JSON array).
        // Returns nullopt on empty string, invalid JSON, empty array, or missing/non-numeric lat/lon.
        static std::optional<Location> parseResponse(const std::string& body);

        // Pure function. Builds the full request URL for a query (uses HttpClient::urlEncode), with format=json and limit=1.
        static std::string buildUrl(const std::string& query);

        // Computes a bounding box around (lat, lon) with the specified radius in meters.
        // Earth radius ~ 6378137.0 m. Uses deltaLat = radius / R, deltaLon = deltaLat / cos(lat).
        // Failure behavior: returns degenerate box with min=max=center if radiusMeters <= 0.
        static ViewBox viewBoxAround(double lat, double lon, int radiusMeters);

        // Pure function. Builds Nominatim search URL with query, bounding viewbox (minLon, maxLat, maxLon, minLat),
        // bounded=1, and limit. Uses locale-independent number formatting and URL-encodes query.
        // Failure behavior: returns standard formatted URL string; never throws.
        static std::string buildSearchUrl(const std::string& query, const ViewBox& box, int limit);

        // Fetches raw Nominatim JSON body for a query bounded by a viewbox.
        // Uses existing rate limiter. Rejects empty or whitespace-only queries by returning "".
        // Failure behavior: returns "" on network failure or empty query.
        static std::string fetchRawMany(const std::string& query, const ViewBox& box, int limit = 5);

        // Pure JSON parsing of Nominatim array response. Skips invalid elements (missing or non-numeric lat/lon).
        // Failure behavior: returns empty vector on empty string, invalid JSON, or empty array.
        static std::vector<Location> parseMany(const std::string& body);

    private:
        // Sleeps so >= 1 second passes between geocode() calls; uses a function-local static std::chrono::steady_clock time_point.
        static void enforceRateLimit();
    };
}

#endif // RAPIDAID_GEO_GEOCODER_HPP
