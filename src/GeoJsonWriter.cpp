#include "GeoJsonWriter.hpp"
#include "FileStore.hpp"
#include "json.hpp"

#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rapidaid::geojson {

namespace {

double roundCoord7(double value) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::fixed << std::setprecision(7) << value;
    std::istringstream iss(ss.str());
    iss.imbue(std::locale::classic());
    double rounded = 0.0;
    iss >> rounded;
    return rounded;
}

nlohmann::json makePointGeometry(double lat, double lon) {
    nlohmann::json geom;
    geom["type"] = "Point";
    geom["coordinates"] = nlohmann::json::array({roundCoord7(lon), roundCoord7(lat)});
    return geom;
}

nlohmann::json makeEmptyFeatureCollection() {
    nlohmann::json fc;
    fc["type"] = "FeatureCollection";
    fc["features"] = nlohmann::json::array();
    return fc;
}

} // anonymous namespace

std::string GeoJsonWriter::placesCollection(const std::vector<rapidaid::osm::Place>& places) {
    nlohmann::json fc = makeEmptyFeatureCollection();

    for (const auto& place : places) {
        nlohmann::json feature;
        feature["type"] = "Feature";
        feature["geometry"] = makePointGeometry(place.lat, place.lon);

        nlohmann::json props;
        props["id"] = place.id;
        props["osmType"] = place.osmType;
        props["name"] = place.name;
        props["category"] = rapidaid::osm::OverpassClient::categoryName(place.category);
        props["tags"] = place.tags;

        feature["properties"] = std::move(props);
        fc["features"].push_back(std::move(feature));
    }

    return fc.dump();
}

std::string GeoJsonWriter::snappedPlacesCollection(const std::vector<rapidaid::graph::SnappedPlace>& places) {
    nlohmann::json fc = makeEmptyFeatureCollection();

    for (const auto& sp : places) {
        nlohmann::json feature;
        feature["type"] = "Feature";
        feature["geometry"] = makePointGeometry(sp.place.lat, sp.place.lon);

        nlohmann::json props;
        props["id"] = sp.place.id;
        props["osmType"] = sp.place.osmType;
        props["name"] = sp.place.name;
        props["category"] = rapidaid::osm::OverpassClient::categoryName(sp.place.category);
        props["tags"] = sp.place.tags;
        props["snapped"] = sp.snapped;
        props["snapDistanceMeters"] = sp.snapDistanceMeters;

        feature["properties"] = std::move(props);
        fc["features"].push_back(std::move(feature));
    }

    return fc.dump();
}

std::string GeoJsonWriter::pathCollection(const rapidaid::graph::Graph& g, const rapidaid::graph::PathResult& path) {
    if (!path.found || path.nodes.size() < 2) {
        return makeEmptyFeatureCollection().dump();
    }

    int nodeCount = g.nodeCount();
    for (int nodeIdx : path.nodes) {
        if (nodeIdx < 0 || nodeIdx >= nodeCount) {
            return makeEmptyFeatureCollection().dump();
        }
    }

    nlohmann::json fc = makeEmptyFeatureCollection();

    nlohmann::json feature;
    feature["type"] = "Feature";

    nlohmann::json geom;
    geom["type"] = "LineString";
    nlohmann::json coords = nlohmann::json::array();
    for (int nodeIdx : path.nodes) {
        coords.push_back(nlohmann::json::array({roundCoord7(g.lon(nodeIdx)), roundCoord7(g.lat(nodeIdx))}));
    }
    geom["coordinates"] = std::move(coords);
    feature["geometry"] = std::move(geom);

    nlohmann::json props;
    props["distanceMeters"] = path.distanceMeters;
    props["nodeCount"] = static_cast<int>(path.nodes.size());
    feature["properties"] = std::move(props);

    fc["features"].push_back(std::move(feature));
    return fc.dump();
}

std::string GeoJsonWriter::roadsCollection(const rapidaid::graph::Graph& g) {
    nlohmann::json fc = makeEmptyFeatureCollection();

    int n = g.nodeCount();
    for (int u = 0; u < n; ++u) {
        const auto& neighbors = g.neighbors(u);
        for (const auto& edge : neighbors) {
            int v = edge.to;
            if (u < v) {
                nlohmann::json feature;
                feature["type"] = "Feature";

                nlohmann::json geom;
                geom["type"] = "LineString";
                geom["coordinates"] = nlohmann::json::array({
                    nlohmann::json::array({roundCoord7(g.lon(u)), roundCoord7(g.lat(u))}),
                    nlohmann::json::array({roundCoord7(g.lon(v)), roundCoord7(g.lat(v))})
                });
                feature["geometry"] = std::move(geom);

                nlohmann::json props;
                props["weightMeters"] = edge.weightMeters;
                feature["properties"] = std::move(props);

                fc["features"].push_back(std::move(feature));
            }
        }
    }

    return fc.dump();
}

bool GeoJsonWriter::writeFile(const std::string& path, const std::string& geojson) {
    return rapidaid::io::FileStore::save(path, geojson);
}

} // namespace rapidaid::geojson
