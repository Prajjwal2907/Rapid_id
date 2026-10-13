#include "DataCache.hpp"
#include "FileStore.hpp"

#include <cctype>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

namespace rapidaid::data {

namespace {

std::string toLower(std::string str) {
    for (char& ch : str) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return str;
}

std::string formatCoord4(double value) {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << std::fixed << std::setprecision(4) << value;
    return ss.str();
}

} // anonymous namespace

DataCache::DataCache(std::string dir, bool refresh)
    : m_dir(std::move(dir)), m_refresh(refresh) {}

std::string DataCache::geocodePath(const std::string& query) const {
    return m_dir + "/geocode_" + rapidaid::io::FileStore::sanitizeFileName(query) + ".json";
}

std::string DataCache::placesPath(rapidaid::osm::Category c, double lat, double lon, int radiusMeters) const {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << m_dir << "/places_"
       << toLower(rapidaid::osm::OverpassClient::categoryName(c)) << "_"
       << formatCoord4(lat) << "_"
       << formatCoord4(lon) << "_"
       << radiusMeters << ".json";
    return ss.str();
}

std::string DataCache::roadsPath(double lat, double lon, int radiusMeters) const {
    std::ostringstream ss;
    ss.imbue(std::locale::classic());
    ss << m_dir << "/roads_"
       << formatCoord4(lat) << "_"
       << formatCoord4(lon) << "_"
       << radiusMeters << ".json";
    return ss.str();
}

template <typename Result, typename Parser, typename Fetcher, typename IsEmpty>
Result DataCache::getCachedOrFetch(const std::string& path,
                                   Parser&& parser,
                                   Fetcher&& fetcher,
                                   IsEmpty&& isEmpty) {
    if (!m_refresh && rapidaid::io::FileStore::exists(path)) {
        std::string cached = rapidaid::io::FileStore::load(path);
        if (!cached.empty()) {
            Result result = parser(cached);
            if (!isEmpty(result)) {
                return result;
            }
        }
    }

    std::string raw = fetcher();
    if (raw.empty()) {
        return Result{};
    }

    Result result = parser(raw);
    if (!isEmpty(result)) {
        rapidaid::io::FileStore::save(path, raw);
    }
    return result;
}

std::optional<rapidaid::geo::Location> DataCache::geocode(const std::string& query) {
    return getCachedOrFetch<std::optional<rapidaid::geo::Location>>(
        geocodePath(query),
        [](const std::string& body) {
            return rapidaid::geo::Geocoder::parseResponse(body);
        },
        [&query]() {
            return rapidaid::geo::Geocoder::fetchRaw(query);
        },
        [](const std::optional<rapidaid::geo::Location>& loc) {
            return !loc.has_value();
        }
    );
}

std::vector<rapidaid::osm::Place> DataCache::places(rapidaid::osm::Category c,
                                                   double lat,
                                                   double lon,
                                                   int radiusMeters) {
    return getCachedOrFetch<std::vector<rapidaid::osm::Place>>(
        placesPath(c, lat, lon, radiusMeters),
        [c](const std::string& body) {
            return rapidaid::osm::OverpassClient::parseResponse(body, c);
        },
        [c, lat, lon, radiusMeters]() {
            return rapidaid::osm::OverpassClient::fetchRaw(c, lat, lon, radiusMeters);
        },
        [](const std::vector<rapidaid::osm::Place>& pls) {
            return pls.empty();
        }
    );
}

rapidaid::osm::RoadData DataCache::roads(double lat, double lon, int radiusMeters) {
    return getCachedOrFetch<rapidaid::osm::RoadData>(
        roadsPath(lat, lon, radiusMeters),
        [](const std::string& body) {
            return rapidaid::osm::RoadClient::parseRoads(body);
        },
        [lat, lon, radiusMeters]() {
            return rapidaid::osm::RoadClient::fetchRaw(lat, lon, radiusMeters);
        },
        [](const rapidaid::osm::RoadData& rds) {
            return rds.empty();
        }
    );
}

} // namespace rapidaid::data
