#include "AreaContext.hpp"
#include "FileStore.hpp"
#include "json.hpp"

#include <locale>
#include <sstream>
#include <utility>

namespace rapidaid::area {

namespace {

const rapidaid::osm::Category kRequiredCategories[] = {
    rapidaid::osm::Category::Hospital,
    rapidaid::osm::Category::FireStation,
    rapidaid::osm::Category::Police,
    rapidaid::osm::Category::Clinic,
    rapidaid::osm::Category::Pharmacy,
    rapidaid::osm::Category::Shelter
};

bool parseDoubleValue(const nlohmann::json& val, double& outVal) {
    if (val.is_number()) {
        outVal = val.get<double>();
        return true;
    }
    if (val.is_string()) {
        std::istringstream ss(val.get<std::string>());
        ss.imbue(std::locale::classic());
        return static_cast<bool>(ss >> outVal);
    }
    return false;
}

bool parseIntValue(const nlohmann::json& val, int& outVal) {
    if (val.is_number_integer()) {
        outVal = val.get<int>();
        return true;
    }
    if (val.is_number()) {
        outVal = static_cast<int>(val.get<double>());
        return true;
    }
    if (val.is_string()) {
        std::istringstream ss(val.get<std::string>());
        ss.imbue(std::locale::classic());
        return static_cast<bool>(ss >> outVal);
    }
    return false;
}

} // anonymous namespace

bool AreaContext::build(rapidaid::data::DataCache& cache,
                        const AreaInfo& info,
                        bool allowNetwork,
                        AreaContext& out,
                        std::string* errorOut) {
    if (errorOut) {
        errorOut->clear();
    }

    const std::string roadsPath = cache.roadsPath(info.lat, info.lon, info.radiusMeters);

    if (!allowNetwork) {
        if (!rapidaid::io::FileStore::exists(roadsPath)) {
            if (errorOut) {
                *errorOut = "Missing roads cache file: " + roadsPath;
            }
            return false;
        }

        for (const auto cat : kRequiredCategories) {
            const std::string pPath = cache.placesPath(cat, info.lat, info.lon, info.radiusMeters);
            if (!rapidaid::io::FileStore::exists(pPath)) {
                if (errorOut) {
                    *errorOut = "Missing places cache file for category " +
                                rapidaid::osm::OverpassClient::categoryName(cat) + ": " + pPath;
                }
                return false;
            }
        }
    }

    rapidaid::osm::RoadData roads;
    std::vector<rapidaid::osm::Place> allPlaces;

    if (!allowNetwork) {
        const std::string roadsJson = rapidaid::io::FileStore::load(roadsPath);
        roads = rapidaid::osm::RoadClient::parseRoads(roadsJson);

        for (const auto cat : kRequiredCategories) {
            const std::string pPath = cache.placesPath(cat, info.lat, info.lon, info.radiusMeters);
            const std::string placesJson = rapidaid::io::FileStore::load(pPath);
            auto catPlaces = rapidaid::osm::OverpassClient::parseResponse(placesJson, cat);
            allPlaces.insert(allPlaces.end(), catPlaces.begin(), catPlaces.end());
        }
    } else {
        roads = cache.roads(info.lat, info.lon, info.radiusMeters);
        for (const auto cat : kRequiredCategories) {
            auto catPlaces = cache.places(cat, info.lat, info.lon, info.radiusMeters);
            allPlaces.insert(allPlaces.end(), catPlaces.begin(), catPlaces.end());
        }
    }

    rapidaid::graph::Graph graph = rapidaid::graph::GraphBuilder::build(roads);
    if (graph.nodeCount() == 0) {
        if (errorOut) {
            *errorOut = "Failed to build graph: no valid road segments found";
        }
        return false;
    }

    auto snapped = rapidaid::graph::GraphBuilder::snapPlaces(graph, allPlaces);

    out.m_info = info;
    out.m_roads = std::move(roads);
    out.m_graph = std::move(graph);
    out.m_places = std::move(allPlaces);
    out.m_snappedPlaces = std::move(snapped);

    return true;
}

bool AreaContext::saveInfo(const std::string& path, const AreaInfo& info) {
    try {
        nlohmann::ordered_json j;
        j["name"] = info.name;
        j["displayName"] = info.displayName;
        j["lat"] = info.lat;
        j["lon"] = info.lon;
        j["radiusMeters"] = info.radiusMeters;
        std::string text = j.dump(2);
        return rapidaid::io::FileStore::save(path, text);
    } catch (...) {
        return false;
    }
}

std::optional<AreaInfo> AreaContext::loadInfo(const std::string& path) {
    if (!rapidaid::io::FileStore::exists(path)) {
        return std::nullopt;
    }

    const std::string text = rapidaid::io::FileStore::load(path);
    if (text.empty()) {
        return std::nullopt;
    }

    try {
        auto j = nlohmann::json::parse(text, nullptr, false);
        if (j.is_discarded() || !j.is_object()) {
            return std::nullopt;
        }

        AreaInfo info;
        if (j.contains("name") && j["name"].is_string()) {
            info.name = j["name"].get<std::string>();
        } else if (j.contains("query") && j["query"].is_string()) {
            info.name = j["query"].get<std::string>();
        }

        if (j.contains("displayName") && j["displayName"].is_string()) {
            info.displayName = j["displayName"].get<std::string>();
        } else if (j.contains("display_name") && j["display_name"].is_string()) {
            info.displayName = j["display_name"].get<std::string>();
        } else {
            info.displayName = info.name;
        }

        if (j.contains("lat")) {
            parseDoubleValue(j["lat"], info.lat);
        }
        if (j.contains("lon")) {
            parseDoubleValue(j["lon"], info.lon);
        }
        if (j.contains("radiusMeters")) {
            parseIntValue(j["radiusMeters"], info.radiusMeters);
        } else if (j.contains("radius")) {
            parseIntValue(j["radius"], info.radiusMeters);
        }

        return info;
    } catch (...) {
        return std::nullopt;
    }
}

const AreaInfo& AreaContext::info() const {
    return m_info;
}

const rapidaid::graph::Graph& AreaContext::graph() const {
    return m_graph;
}

const std::vector<rapidaid::graph::SnappedPlace>& AreaContext::snappedPlaces() const {
    return m_snappedPlaces;
}

const std::vector<rapidaid::osm::Place>& AreaContext::places() const {
    return m_places;
}

const rapidaid::osm::RoadData& AreaContext::roads() const {
    return m_roads;
}

} // namespace rapidaid::area
