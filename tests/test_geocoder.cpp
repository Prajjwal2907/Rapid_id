#include "Geocoder.hpp"
#include <iostream>

int main() {
    std::cout << "Testing live geocoding for 'Eiffel Tower, Paris'...\n";
    auto loc = rapidaid::geo::Geocoder::geocode("Eiffel Tower, Paris");
    if (loc) {
        std::cout << "Geocode successful!\n"
                  << "DisplayName: " << loc->displayName << "\n"
                  << "Latitude: " << loc->lat << "\n"
                  << "Longitude: " << loc->lon << "\n";
    } else {
        std::cout << "Geocode failed (no result or network/parse error).\n";
    }
    return 0;
}
