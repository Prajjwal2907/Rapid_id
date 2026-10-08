#include "OverpassClient.hpp"
#include <iostream>
#include <vector>

using rapidaid::osm::Category;
using rapidaid::osm::OverpassClient;
using rapidaid::osm::Place;

int main() {
    std::cout << "Searching for hospitals near Eiffel Tower (48.8584, 2.2945, radius 3000m)...\n";

    std::vector<Place> places = OverpassClient::findNearby(Category::Hospital, 48.8584, 2.2945, 3000);

    std::cout << "Found " << places.size() << " places:\n";
    for (const auto& place : places) {
        std::cout << "- [" << place.osmType << "/" << place.id << "] "
                  << (place.name.empty() ? "(unnamed)" : place.name)
                  << " (" << place.lat << ", " << place.lon << ")\n";
    }

    return 0;
}
