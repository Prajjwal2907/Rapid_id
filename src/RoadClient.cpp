#include "RoadClient.hpp"
#include "HttpClient.hpp"
#include "json.hpp"

#include <chrono>
#include <iomanip>
#include <locale>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace rapidaid::osm {

bool RoadData::empty() const {
    return ways.empty();
}

std::string RoadClient::buildRoadQuery(double lat, double lon, int radiusMeters) {
    if (radiusMeters < 1 || radiusMeters > 5000) {
        return "";
    }
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        return "";
    }

    std::ostringstream ss;
    ss.imbue(std::locale::classic());

    ss << "[out:json][timeout:60];way[\"highway\"](around:"
       << radiusMeters << ","
       << std::fixed << std::setprecision(7) << lat << ","
       << std::fixed << std::setprecision(7) << lon
       << ");(._;>;);out body;";

    return ss.str();
}

std::string RoadClient::buildRequestBody(const std::string& query) {
    return "data=" + rapidaid::net::HttpClient::urlEncode(query);
}

std::string RoadClient::fetchRaw(double lat, double lon, int radiusMeters) {
    if (radiusMeters < 1 || radiusMeters > 5000) {
        return "";
    }
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        return "";
    }

    std::string query = buildRoadQuery(lat, lon, radiusMeters);
    if (query.empty()) {
        return "";
    }

    std::string body = buildRequestBody(query);

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
            return response;
        }

        if (attempt < maxAttempts - 1) {
            std::this_thread::sleep_for(backoffTimes[attempt]);
        }
    }

    return "";
}

bool RoadClient::isDrivable(const std::map<std::string, std::string>& tags) {
    auto hwIt = tags.find("highway");
    if (hwIt == tags.end()) {
        return false;
    }

    std::string hw = hwIt->second;
    const std::string suffix = "_link";
    if (hw.length() >= suffix.length() &&
        hw.compare(hw.length() - suffix.length(), suffix.length(), suffix) == 0) {
        hw = hw.substr(0, hw.length() - suffix.length());
    }

    static const std::unordered_set<std::string> drivableTypes = {
        "motorway",
        "trunk",
        "primary",
        "secondary",
        "tertiary",
        "unclassified",
        "residential",
        "living_street",
        "service"
    };

    if (drivableTypes.find(hw) == drivableTypes.end()) {
        return false;
    }

    auto accessIt = tags.find("access");
    if (accessIt != tags.end() && (accessIt->second == "private" || accessIt->second == "no")) {
        return false;
    }

    auto mvIt = tags.find("motor_vehicle");
    if (mvIt != tags.end() && mvIt->second == "no") {
        return false;
    }

    auto vehIt = tags.find("vehicle");
    if (vehIt != tags.end() && vehIt->second == "no") {
        return false;
    }

    auto areaIt = tags.find("area");
    if (areaIt != tags.end() && areaIt->second == "yes") {
        return false;
    }

    return true;
}

RoadData RoadClient::parseRoads(const std::string& body) {
    RoadData result;
    if (body.empty()) {
        return result;
    }

    try {
        nlohmann::json root = nlohmann::json::parse(body);
        if (!root.is_object() || !root.contains("elements") || !root["elements"].is_array()) {
            return result;
        }

        for (const auto& elem : root["elements"]) {
            if (!elem.is_object() || !elem.contains("type") || !elem["type"].is_string()) {
                continue;
            }

            std::string type = elem["type"].get<std::string>();

            if (type == "node") {
                if (!elem.contains("id") || !elem["id"].is_number_integer() ||
                    !elem.contains("lat") || !elem["lat"].is_number() ||
                    !elem.contains("lon") || !elem["lon"].is_number()) {
                    continue;
                }

                RoadNode node;
                node.id = elem["id"].get<long long>();
                node.lat = elem["lat"].get<double>();
                node.lon = elem["lon"].get<double>();
                result.nodes[node.id] = node;
            } else if (type == "way") {
                if (!elem.contains("id") || !elem["id"].is_number_integer()) {
                    continue;
                }

                if (!elem.contains("nodes") || !elem["nodes"].is_array()) {
                    continue;
                }

                std::vector<long long> nodeIds;
                for (const auto& nId : elem["nodes"]) {
                    if (nId.is_number_integer()) {
                        nodeIds.push_back(nId.get<long long>());
                    }
                }

                if (nodeIds.size() < 2) {
                    continue;
                }

                std::map<std::string, std::string> tags;
                if (elem.contains("tags") && elem["tags"].is_object()) {
                    for (auto it = elem["tags"].begin(); it != elem["tags"].end(); ++it) {
                        if (it.value().is_string()) {
                            tags[it.key()] = it.value().get<std::string>();
                        } else {
                            tags[it.key()] = it.value().dump();
                        }
                    }
                }

                if (!isDrivable(tags)) {
                    continue;
                }

                RoadWay way;
                way.id = elem["id"].get<long long>();
                way.nodeIds = std::move(nodeIds);

                auto hwIt = tags.find("highway");
                if (hwIt != tags.end()) {
                    way.highway = hwIt->second;
                }

                auto nameIt = tags.find("name");
                if (nameIt != tags.end()) {
                    way.name = nameIt->second;
                }

                way.tags = std::move(tags);

                result.ways.push_back(std::move(way));
            }
        }
    } catch (const nlohmann::json::exception&) {
        return RoadData{};
    }

    return result;
}

} // namespace rapidaid::osm
