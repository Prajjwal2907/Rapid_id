#ifndef RAPIDAID_GEO_GEOCODER_HPP
#define RAPIDAID_GEO_GEOCODER_HPP

#include <string>
#include <optional>

namespace rapidaid::geo {
    struct Location {
        double lat;
        double lon;
        std::string displayName;
    };

    class Geocoder {
    public:
        // Geocodes a free-text query via Nominatim. Returns std::nullopt on network failure, parse failure, or no results.
        static std::optional<Location> geocode(const std::string& query);

        // Pure function, no network. Parses a Nominatim format=json response body (a JSON array).
        // Returns nullopt on empty string, invalid JSON, empty array, or missing/non-numeric lat/lon.
        static std::optional<Location> parseResponse(const std::string& body);

        // Pure function. Builds the full request URL for a query (uses HttpClient::urlEncode), with format=json and limit=1.
        static std::string buildUrl(const std::string& query);

    private:
        // Sleeps so >= 1 second passes between geocode() calls; uses a function-local static std::chrono::steady_clock time_point.
        static void enforceRateLimit();
    };
}

#endif // RAPIDAID_GEO_GEOCODER_HPP
