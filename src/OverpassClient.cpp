#include "OverpassClient.hpp"
#include "HttpClient.hpp"
#include "json.hpp"

#include <chrono>
#include <iomanip>
#include <locale>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace rapidaid::osm {

namespace {

const std::vector<std::pair<std::string, std::string>>& getCategoryTags(Category category) {
    static const std::vector<std::pair<std::string, std::string>> hospitalTags = {
        {"amenity", "hospital"}
    };
    static const std::vector<std::pair<std::string, std::string>> clinicTags = {
        {"amenity", "clinic"},
        {"amenity", "doctors"}
    };
    static const std::vector<std::pair<std::string, std::string>> pharmacyTags = {
        {"amenity", "pharmacy"}
    };
    static const std::vector<std::pair<std::string, std::string>> fireStationTags = {
        {"amenity", "fire_station"}
    };
    static const std::vector<std::pair<std::string, std::string>> policeTags = {
        {"amenity", "police"}
    };
    static const std::vector<std::pair<std::string, std::string>> shelterTags = {
        {"amenity", "shelter"},
        {"emergency", "assembly_point"}
    };
    static const std::vector<std::pair<std::string, std::string>> waterTags = {
        {"amenity", "drinking_water"},
        {"emergency", "water_tank"}
    };
    static const std::vector<std::pair<std::string, std::string>> emptyTags = {};

    switch (category) {
        case Category::Hospital:    return hospitalTags;
        case Category::Clinic:      return clinicTags;
        case Category::Pharmacy:    return pharmacyTags;
        case Category::FireStation: return fireStationTags;
        case Category::Police:      return policeTags;
        case Category::Shelter:     return shelterTags;
        case Category::Water:       return waterTags;
        default:                    return emptyTags;
    }
}

} // anonymous namespace

std::string OverpassClient::categoryName(Category category) {
    switch (category) {
        case Category::Hospital:    return "hospital";
        case Category::Clinic:      return "clinic";
        case Category::Pharmacy:    return "pharmacy";
        case Category::FireStation: return "fire_station";
        case Category::Police:      return "police";
        case Category::Shelter:     return "shelter";
        case Category::Water:       return "water";
        default:                    return "unknown";
    }
}

std::string OverpassClient::buildQuery(Category category, double lat, double lon, int radiusMeters) {
    const auto& tags = getCategoryTags(category);
    if (tags.empty()) {
        return "";
    }

    std::ostringstream ss;
    ss.imbue(std::locale::classic());

    ss << "[out:json][timeout:25]; (";

    static const char* const elementTypes[] = {"node", "way", "relation"};

    for (const char* elem : elementTypes) {
        for (const auto& tag : tags) {
            ss << " " << elem << "[\"" << tag.first << "\"=\"" << tag.second << "\"]"
               << "(around:" << radiusMeters << ","
               << std::fixed << std::setprecision(7) << lat << ","
               << std::fixed << std::setprecision(7) << lon << ");";
        }
    }

    ss << " ); out center tags;";
    return ss.str();
}

std::string OverpassClient::buildRequestBody(const std::string& query) {
    std::string encoded = rapidaid::net::HttpClient::urlEncode(query);
    if (encoded.empty() && !query.empty()) {
        return "";
    }
    return "data=" + encoded;
}

std::vector<Place> OverpassClient::parseResponse(const std::string& body, Category category) {
    std::vector<Place> places;
    if (body.empty()) {
        return places;
    }

    try {
        nlohmann::json root = nlohmann::json::parse(body);
        if (!root.is_object() || !root.contains("elements") || !root["elements"].is_array()) {
            return places;
        }

        for (const auto& element : root["elements"]) {
            if (!element.is_object()) {
                continue;
            }

            if (!element.contains("id") || !element["id"].is_number_integer() ||
                !element.contains("type") || !element["type"].is_string()) {
                continue;
            }

            double lat = 0.0;
            double lon = 0.0;
            bool hasCoords = false;

            std::string osmType = element["type"].get<std::string>();

            if (osmType == "node") {
                if (element.contains("lat") && element["lat"].is_number() &&
                    element.contains("lon") && element["lon"].is_number()) {
                    lat = element["lat"].get<double>();
                    lon = element["lon"].get<double>();
                    hasCoords = true;
                }
            } else if (osmType == "way" || osmType == "relation") {
                if (element.contains("center") && element["center"].is_object()) {
                    const auto& center = element["center"];
                    if (center.contains("lat") && center["lat"].is_number() &&
                        center.contains("lon") && center["lon"].is_number()) {
                        lat = center["lat"].get<double>();
                        lon = center["lon"].get<double>();
                        hasCoords = true;
                    }
                }
            }

            if (!hasCoords) {
                continue;
            }

            Place place;
            place.id = element["id"].get<long long>();
            place.osmType = osmType;
            place.lat = lat;
            place.lon = lon;
            place.category = category;

            if (element.contains("tags") && element["tags"].is_object()) {
                for (auto it = element["tags"].begin(); it != element["tags"].end(); ++it) {
                    if (it.value().is_string()) {
                        place.tags[it.key()] = it.value().get<std::string>();
                    }
                }
                auto nameIt = place.tags.find("name");
                if (nameIt != place.tags.end()) {
                    place.name = nameIt->second;
                }
            }

            places.push_back(std::move(place));
        }
    } catch (const nlohmann::json::exception&) {
        places.clear();
        return places;
    }

    return places;
}

std::vector<Place> OverpassClient::findNearby(Category category, double lat, double lon, int radiusMeters) {
    if (radiusMeters < 1 || radiusMeters > 50000) {
        return {};
    }
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        return {};
    }

    std::string query = buildQuery(category, lat, lon, radiusMeters);
    if (query.empty()) {
        return {};
    }

    std::string body = buildRequestBody(query);
    if (body.empty()) {
        return {};
    }

    static const std::string overpassUrl = "https://overpass-api.de/api/interpreter";

    std::string response;
    const int maxAttempts = 3;
    const std::chrono::seconds backoffTimes[] = {
        std::chrono::seconds(2),
        std::chrono::seconds(4)
    };

    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        response = rapidaid::net::HttpClient::post(overpassUrl, body);
        if (!response.empty()) {
            break;
        }

        if (attempt < maxAttempts - 1) {
            std::this_thread::sleep_for(backoffTimes[attempt]);
        }
    }

    if (response.empty()) {
        return {};
    }

    return parseResponse(response, category);
}

} // namespace rapidaid::osm
