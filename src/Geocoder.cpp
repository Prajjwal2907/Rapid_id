#include "Geocoder.hpp"
#include "HttpClient.hpp"
#include "json.hpp"

#include <chrono>
#include <thread>
#include <cctype>
#include <algorithm>

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

    std::optional<Location> Geocoder::geocode(const std::string& query) {
        if (isAllWhitespaceOrEmpty(query)) {
            return std::nullopt;
        }

        enforceRateLimit();

        std::string url = buildUrl(query);
        std::string response = rapidaid::net::HttpClient::get(url);
        if (response.empty()) {
            return std::nullopt;
        }

        return parseResponse(response);
    }

} // namespace rapidaid::geo
