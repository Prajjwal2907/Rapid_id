#include "RoadClient.hpp"
#include <iostream>

using rapidaid::osm::RoadClient;
using rapidaid::osm::RoadData;

int main() {
    std::cout << "Fetching live road data around Eiffel Tower (48.8584, 2.2945, 1000m)...\n";
    std::string raw = RoadClient::fetchRaw(48.8584, 2.2945, 1000);
    if (raw.empty()) {
        std::cerr << "Failed to fetch road data from Overpass API.\n";
        return 1;
    }

    RoadData data = RoadClient::parseRoads(raw);
    std::cout << "Fetched " << data.nodes.size() << " nodes, " << data.ways.size() << " ways.\n";

    std::cout << "First 5 way names:\n";
    size_t count = 0;
    for (const auto& way : data.ways) {
        if (!way.name.empty()) {
            std::cout << "  - " << way.name << " (" << way.highway << ")\n";
            count++;
            if (count >= 5) {
                break;
            }
        }
    }

    if (count == 0) {
        std::cout << "  (No named ways found in first ways)\n";
    }

    return 0;
}
