#include "LocationResolver.hpp"
#include "GeoMath.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace rapidaid::locate {

namespace {

struct StreetInfo {
    std::string original;
    std::string normalized;
};

} // anonymous namespace

LocationResolver::LocationResolver(const rapidaid::graph::Graph& graph,
                                   const std::vector<rapidaid::osm::Place>& places,
                                   const rapidaid::osm::RoadData& roadData)
    : m_graph(graph), m_places(places), m_roadData(roadData) {
    buildIntersectionIndex();
}

void LocationResolver::buildIntersectionIndex() {
    std::unordered_map<long long, std::vector<StreetInfo>> nodeStreets;

    for (const auto& way : m_roadData.ways) {
        if (way.name.empty()) {
            continue;
        }
        std::string norm = normalizeStreetName(way.name);
        if (norm.empty()) {
            continue;
        }

        std::unordered_set<long long> seenNodes;
        for (long long nodeId : way.nodeIds) {
            if (!seenNodes.insert(nodeId).second) {
                continue;
            }

            auto& list = nodeStreets[nodeId];
            bool alreadyExists = false;
            for (const auto& info : list) {
                if (info.normalized == norm) {
                    alreadyExists = true;
                    break;
                }
            }
            if (!alreadyExists) {
                list.push_back(StreetInfo{way.name, norm});
            }
        }
    }

    for (const auto& entry : nodeStreets) {
        long long nodeId = entry.first;
        const auto& streets = entry.second;

        if (streets.size() < 2) {
            continue;
        }

        double lat = 0.0;
        double lon = 0.0;
        bool foundCoord = false;

        auto nodeIt = m_roadData.nodes.find(nodeId);
        if (nodeIt != m_roadData.nodes.end()) {
            lat = nodeIt->second.lat;
            lon = nodeIt->second.lon;
            foundCoord = true;
        } else {
            int gIdx = m_graph.indexOf(nodeId);
            if (gIdx >= 0) {
                lat = m_graph.lat(gIdx);
                lon = m_graph.lon(gIdx);
                foundCoord = true;
            }
        }

        if (!foundCoord) {
            continue;
        }

        for (size_t i = 0; i < streets.size(); ++i) {
            for (size_t j = i + 1; j < streets.size(); ++j) {
                IntersectionRecord rec;
                rec.nodeId = nodeId;
                rec.lat = lat;
                rec.lon = lon;
                rec.streetA = streets[i].original;
                rec.streetB = streets[j].original;
                rec.normStreetA = streets[i].normalized;
                rec.normStreetB = streets[j].normalized;
                m_intersections.push_back(std::move(rec));
            }
        }
    }
}

std::string LocationResolver::placeLabel(const rapidaid::osm::Place& place) {
    if (!place.name.empty()) {
        return place.name;
    }

    std::string category = rapidaid::osm::OverpassClient::categoryName(place.category);
    std::string label = "Unnamed " + category;

    auto opIt = place.tags.find("operator");
    if (opIt != place.tags.end() && !opIt->second.empty()) {
        label += " (" + opIt->second + ")";
    } else {
        auto streetIt = place.tags.find("addr:street");
        if (streetIt != place.tags.end() && !streetIt->second.empty()) {
            label += " (" + streetIt->second + ")";
        }
    }

    return label;
}

std::string LocationResolver::normalizeStreetName(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    bool lastWasSpace = true;

    for (char ch : input) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::ispunct(c)) {
            continue;
        }
        if (std::isspace(c)) {
            if (!lastWasSpace) {
                result.push_back(' ');
                lastWasSpace = true;
            }
        } else {
            result.push_back(static_cast<char>(std::tolower(c)));
            lastWasSpace = false;
        }
    }

    if (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }

    return result;
}

bool LocationResolver::parseIntersectionQuery(const std::string& query, std::string& street1, std::string& street2) {
    size_t start = 0;
    while (start < query.size() && std::isspace(static_cast<unsigned char>(query[start]))) {
        start++;
    }
    size_t end = query.size();
    while (end > start && std::isspace(static_cast<unsigned char>(query[end - 1]))) {
        end--;
    }
    if (start >= end) {
        return false;
    }
    std::string trimmed = query.substr(start, end - start);

    const std::string cornerPrefix = "corner of";
    if (trimmed.size() >= cornerPrefix.size()) {
        bool matchPrefix = true;
        for (size_t i = 0; i < cornerPrefix.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(trimmed[i])) != cornerPrefix[i]) {
                matchPrefix = false;
                break;
            }
        }
        if (matchPrefix) {
            size_t after = cornerPrefix.size();
            while (after < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[after]))) {
                after++;
            }
            trimmed = trimmed.substr(after);
        }
    }

    if (trimmed.empty()) {
        return false;
    }

    size_t slashPos = trimmed.find('/');
    size_t ampPos = trimmed.find('&');

    std::string lowerTrimmed = trimmed;
    for (char& c : lowerTrimmed) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    size_t andPos = lowerTrimmed.find(" and ");

    size_t delimPos = std::string::npos;
    size_t delimLen = 0;

    if (slashPos != std::string::npos) {
        delimPos = slashPos;
        delimLen = 1;
    } else if (ampPos != std::string::npos) {
        delimPos = ampPos;
        delimLen = 1;
    } else if (andPos != std::string::npos) {
        delimPos = andPos;
        delimLen = 5;
    } else {
        return false;
    }

    street1 = trimmed.substr(0, delimPos);
    street2 = trimmed.substr(delimPos + delimLen);

    auto trimStr = [](std::string& s) {
        size_t b = 0;
        while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
        size_t e = s.size();
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
        s = s.substr(b, e - b);
    };

    trimStr(street1);
    trimStr(street2);

    return !street1.empty() && !street2.empty();
}

std::vector<LocationCandidate> LocationResolver::searchPlaces(const std::string& query, size_t max) const {
    if (query.empty() || max == 0) {
        return {};
    }

    std::string lowerQuery = query;
    for (char& c : lowerQuery) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    std::vector<LocationCandidate> exactMatches;
    std::vector<LocationCandidate> partialMatches;

    for (const auto& place : m_places) {
        std::string label = placeLabel(place);
        std::string name = place.name;

        std::string lowerName = name;
        for (char& c : lowerName) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        std::string lowerLabel = label;
        for (char& c : lowerLabel) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        bool isExact = (!lowerName.empty() && lowerName == lowerQuery) ||
                       (!lowerLabel.empty() && lowerLabel == lowerQuery);

        if (isExact) {
            exactMatches.push_back(LocationCandidate{label, place.lat, place.lon, "place"});
        } else {
            bool isPartial = (!lowerName.empty() && lowerName.find(lowerQuery) != std::string::npos) ||
                             (!lowerLabel.empty() && lowerLabel.find(lowerQuery) != std::string::npos);
            if (isPartial) {
                partialMatches.push_back(LocationCandidate{label, place.lat, place.lon, "place"});
            }
        }
    }

    std::vector<LocationCandidate> results;
    results.reserve(std::min(max, exactMatches.size() + partialMatches.size()));

    for (const auto& cand : exactMatches) {
        if (results.size() >= max) {
            break;
        }
        results.push_back(cand);
    }
    for (const auto& cand : partialMatches) {
        if (results.size() >= max) {
            break;
        }
        results.push_back(cand);
    }

    return results;
}

std::vector<LocationCandidate> LocationResolver::searchIntersections(const std::string& query, size_t max) const {
    if (query.empty() || max == 0) {
        return {};
    }

    std::string s1;
    std::string s2;
    if (!parseIntersectionQuery(query, s1, s2)) {
        return {};
    }

    std::string q1 = normalizeStreetName(s1);
    std::string q2 = normalizeStreetName(s2);
    if (q1.empty() || q2.empty()) {
        return {};
    }

    std::vector<LocationCandidate> exactCandidates;
    std::vector<LocationCandidate> partialCandidates;

    bool allowSubstring = (q1.size() >= 3 && q2.size() >= 3);

    for (const auto& inter : m_intersections) {
        bool matchExact1 = (q1 == inter.normStreetA && q2 == inter.normStreetB);
        bool matchExact2 = (q1 == inter.normStreetB && q2 == inter.normStreetA);

        if (matchExact1 || matchExact2) {
            std::string label = matchExact1 ?
                (inter.streetA + " and " + inter.streetB) :
                (inter.streetB + " and " + inter.streetA);
            exactCandidates.push_back(LocationCandidate{label, inter.lat, inter.lon, "intersection"});
        } else if (allowSubstring) {
            bool sub1 = (inter.normStreetA.find(q1) != std::string::npos &&
                         inter.normStreetB.find(q2) != std::string::npos);
            bool sub2 = (inter.normStreetB.find(q1) != std::string::npos &&
                         inter.normStreetA.find(q2) != std::string::npos);
            if (sub1 || sub2) {
                std::string label = sub1 ?
                    (inter.streetA + " and " + inter.streetB) :
                    (inter.streetB + " and " + inter.streetA);
                partialCandidates.push_back(LocationCandidate{label, inter.lat, inter.lon, "intersection"});
            }
        }
    }

    const auto& candidatesToUse = exactCandidates.empty() ? partialCandidates : exactCandidates;

    std::vector<LocationCandidate> results;
    results.reserve(std::min(max, candidatesToUse.size()));

    for (const auto& cand : candidatesToUse) {
        if (results.size() >= max) {
            break;
        }

        bool isDuplicate = false;
        for (const auto& existing : results) {
            if (rapidaid::geo::haversineMeters(cand.lat, cand.lon, existing.lat, existing.lon) < 15.0) {
                isDuplicate = true;
                break;
            }
        }

        if (!isDuplicate) {
            results.push_back(cand);
        }
    }

    return results;
}

} // namespace rapidaid::locate
