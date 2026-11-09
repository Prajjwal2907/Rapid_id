#include "Geocoder.hpp"
#include "HttpClient.hpp"
#include "json.hpp"

#include <chrono>
#include <thread>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <locale>
#include <iomanip>

namespace rapidaid::geo {

    static bool isAllWhitespaceOrEmpty(const std::string& str) {
        return std::all_of(str.begin(), str.end(), [](unsigned char c) {
            return std::isspace(c);
        });
    }

    void Geocoder::enforceRateLimit() {
        static std::chrono::steady_clock::time_point lastCall;
        static bool initialized = false;

        auto now = std::chrono::steady_clock::now();
        if (initialized) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCall);
            if (elapsed < std::chrono::milliseconds(1000)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000) - elapsed);
            }
        } else {
            initialized = true;
        }
        lastCall = std::chrono::steady_clock::now();
    }

    std::string Geocoder::buildUrl(const std::string& query) {
        return "https://nominatim.openstreetmap.org/search?q=" +
               rapidaid::net::HttpClient::urlEncode(query) +
               "&format=json&limit=1";
    }

    std::optional<Location> Geocoder::parseResponse(const std::string& body) {
        if (body.empty()) {
            return std::nullopt;
        }

        try {
            auto parsed = nlohmann::json::parse(body);
            if (!parsed.is_array() || parsed.empty()) {
                return std::nullopt;
            }

            const auto& first = parsed[0];
            if (!first.is_object() || !first.contains("lat") || !first.contains("lon")) {
                return std::nullopt;
            }

            const auto& latVal = first["lat"];
            const auto& lonVal = first["lon"];

            if (!latVal.is_string() || !lonVal.is_string()) {
                return std::nullopt;
            }

            std::string latStr = latVal.get<std::string>();
            std::string lonStr = lonVal.get<std::string>();

            std::size_t latIdx = 0;
            std::size_t lonIdx = 0;
            double lat = std::stod(latStr, &latIdx);
            double lon = std::stod(lonStr, &lonIdx);

            if (latIdx != latStr.size() || lonIdx != lonStr.size()) {
                return std::nullopt;
            }

            std::string displayName;
            if (first.contains("display_name") && first["display_name"].is_string()) {
                displayName = first["display_name"].get<std::string>();
            }

            return Location{lat, lon, displayName};
        } catch (const nlohmann::json::exception&) {
            return std::nullopt;
        } catch (const std::invalid_argument&) {
            return std::nullopt;
        } catch (const std::out_of_range&) {
            return std::nullopt;
        }
    }

    std::string Geocoder::fetchRaw(const std::string& query) {
        if (isAllWhitespaceOrEmpty(query)) {
            return "";
        }

        enforceRateLimit();

        std::string url = buildUrl(query);
        return rapidaid::net::HttpClient::get(url);
    }

    std::optional<Location> Geocoder::geocode(const std::string& query) {
        return parseResponse(fetchRaw(query));
    }

    ViewBox Geocoder::viewBoxAround(double lat, double lon, int radiusMeters) {
        if (radiusMeters <= 0) {
            return ViewBox{lon, lat, lon, lat};
        }

        constexpr double EARTH_RADIUS_M = 6378137.0;
        constexpr double PI = 3.14159265358979323846;

        double deltaLatRad = static_cast<double>(radiusMeters) / EARTH_RADIUS_M;
        double deltaLat = deltaLatRad * (180.0 / PI);

        double latRad = lat * (PI / 180.0);
        double cosLat = std::cos(latRad);
        double deltaLon = 0.0;
        if (std::abs(cosLat) > 1e-12) {
            deltaLon = deltaLat / std::abs(cosLat);
        } else {
            deltaLon = deltaLat;
        }

        double minLon = lon - deltaLon;
        double minLat = lat - deltaLat;
        double maxLon = lon + deltaLon;
        double maxLat = lat + deltaLat;

        return ViewBox{minLon, minLat, maxLon, maxLat};
    }

    std::string Geocoder::buildSearchUrl(const std::string& query, const ViewBox& box, int limit) {
        std::ostringstream ss;
        ss.imbue(std::locale::classic());

        ss << "https://nominatim.openstreetmap.org/search?q="
           << rapidaid::net::HttpClient::urlEncode(query)
           << "&format=json&limit=" << limit
           << "&viewbox=" << box.minLon << "," << box.maxLat << "," << box.maxLon << "," << box.minLat
           << "&bounded=1";

        return ss.str();
    }

    std::string Geocoder::fetchRawMany(const std::string& query, const ViewBox& box, int limit) {
        if (isAllWhitespaceOrEmpty(query)) {
            return "";
        }

        enforceRateLimit();

        std::string url = buildSearchUrl(query, box, limit);
        return rapidaid::net::HttpClient::get(url);
    }

    std::vector<Location> Geocoder::parseMany(const std::string& body) {
        if (body.empty()) {
            return {};
        }

        std::vector<Location> results;
        try {
            auto parsed = nlohmann::json::parse(body);
            if (!parsed.is_array()) {
                return {};
            }

            for (const auto& item : parsed) {
                if (!item.is_object() || !item.contains("lat") || !item.contains("lon")) {
                    continue;
                }

                const auto& latVal = item["lat"];
                const auto& lonVal = item["lon"];

                double lat = 0.0;
                double lon = 0.0;

                if (latVal.is_string() && lonVal.is_string()) {
                    std::string latStr = latVal.get<std::string>();
                    std::string lonStr = lonVal.get<std::string>();

                    std::size_t latIdx = 0;
                    std::size_t lonIdx = 0;
                    try {
                        lat = std::stod(latStr, &latIdx);
                        lon = std::stod(lonStr, &lonIdx);
                    } catch (...) {
                        continue;
                    }

                    if (latIdx != latStr.size() || lonIdx != lonStr.size()) {
                        continue;
                    }
                } else if (latVal.is_number() && lonVal.is_number()) {
                    lat = latVal.get<double>();
                    lon = lonVal.get<double>();
                } else {
                    continue;
                }

                std::string displayName;
                if (item.contains("display_name") && item["display_name"].is_string()) {
                    displayName = item["display_name"].get<std::string>();
                }

                results.push_back(Location{lat, lon, displayName});
            }
        } catch (const nlohmann::json::exception&) {
            return {};
        }

        return results;
    }

} // namespace rapidaid::geo
