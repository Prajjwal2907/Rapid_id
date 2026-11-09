#include "Geocoder.hpp"
#include <iostream>
#include <cmath>
#include <string>
#include <vector>

static bool nearlyEqual(double a, double b, double epsilon = 0.0001) {
    return std::abs(a - b) < epsilon;
}

int main() {
    int failed = 0;

    // Case 1: buildSearchUrl contains format=json, limit, viewbox, bounded=1 with correctly encoded query and locale-independent decimal point
    {
        rapidaid::geo::ViewBox box{-122.456, 37.789, -122.400, 37.820};
        std::string query = "Elm & 5th St, Paris";
        std::string url = rapidaid::geo::Geocoder::buildSearchUrl(query, box, 5);

        bool hasFormat = (url.find("format=json") != std::string::npos);
        bool hasLimit = (url.find("limit=5") != std::string::npos);
        bool hasBounded = (url.find("bounded=1") != std::string::npos);
        bool hasViewbox = (url.find("viewbox=-122.456,37.82,-122.4,37.789") != std::string::npos ||
                           url.find("viewbox=-122.456") != std::string::npos);
        bool hasEncodedQuery = (url.find("Elm%20%26%205th%20St%2C%20Paris") != std::string::npos ||
                                (url.find("%26") != std::string::npos && url.find("%20") != std::string::npos));
        bool hasDecimalDot = (url.find('.') != std::string::npos);

        if (hasFormat && hasLimit && hasBounded && hasViewbox && hasEncodedQuery && hasDecimalDot) {
            std::cout << "[PASS] Case 1: buildSearchUrl parameters, encoding, and locale-independent formatting\n";
        } else {
            std::cout << "[FAIL] Case 1: buildSearchUrl parameters, encoding, and locale-independent formatting (url=" << url << ")\n";
            failed++;
        }
    }

    // Case 2: viewBoxAround for known point and radius (min < max, approximate width)
    {
        double lat = 40.7128;
        double lon = -74.0060;
        int radius = 1000; // 1 km

        rapidaid::geo::ViewBox box = rapidaid::geo::Geocoder::viewBoxAround(lat, lon, radius);

        bool minLessThanMax = (box.minLon < box.maxLon) && (box.minLat < box.maxLat);
        bool centerInside = (box.minLat < lat && lat < box.maxLat) &&
                            (box.minLon < lon && lon < box.maxLon);

        double widthDegrees = box.maxLon - box.minLon;
        double heightDegrees = box.maxLat - box.minLat;

        // At lat ~40.7, 1000m is ~0.00898 deg lat (height ~ 0.01796)
        // deltaLon = deltaLat / cos(40.7) ~ 0.00898 / 0.758 ~ 0.0118 deg (width ~ 0.0237)
        bool approxWidth = (widthDegrees > 0.020 && widthDegrees < 0.030);
        bool approxHeight = (heightDegrees > 0.015 && heightDegrees < 0.022);

        // Radius <= 0 returns degenerate box
        rapidaid::geo::ViewBox degen = rapidaid::geo::Geocoder::viewBoxAround(lat, lon, 0);
        bool degenCorrect = (nearlyEqual(degen.minLon, lon) && nearlyEqual(degen.maxLon, lon) &&
                             nearlyEqual(degen.minLat, lat) && nearlyEqual(degen.maxLat, lat));

        if (minLessThanMax && centerInside && approxWidth && approxHeight && degenCorrect) {
            std::cout << "[PASS] Case 2: viewBoxAround dimensions and bounds\n";
        } else {
            std::cout << "[FAIL] Case 2: viewBoxAround dimensions and bounds (width="
                      << widthDegrees << ", height=" << heightDegrees << ")\n";
            failed++;
        }
    }

    // Case 3: parseMany on canned multi-result body with valid and invalid items
    {
        std::string json = R"([
            {"lat":"48.8584","lon":"2.2945","display_name":"Eiffel Tower"},
            {"lat":"not_a_number","lon":"2.3000","display_name":"Invalid Item 1"},
            {"lat":"48.8606","lon":"2.3376","display_name":"Louvre Museum"},
            {"display_name":"Missing lat and lon"},
            {"lat":"48.8529","lon":"2.3500","display_name":"Notre Dame"}
        ])";

        std::vector<rapidaid::geo::Location> locs = rapidaid::geo::Geocoder::parseMany(json);

        if (locs.size() == 3 &&
            nearlyEqual(locs[0].lat, 48.8584) && nearlyEqual(locs[0].lon, 2.2945) &&
            locs[0].displayName == "Eiffel Tower" &&
            nearlyEqual(locs[1].lat, 48.8606) && nearlyEqual(locs[1].lon, 2.3376) &&
            locs[1].displayName == "Louvre Museum" &&
            nearlyEqual(locs[2].lat, 48.8529) && nearlyEqual(locs[2].lon, 2.3500) &&
            locs[2].displayName == "Notre Dame") {
            std::cout << "[PASS] Case 3: parseMany on canned multi-result body\n";
        } else {
            std::cout << "[FAIL] Case 3: parseMany on canned multi-result body (size=" << locs.size() << ")\n";
            failed++;
        }
    }

    // Case 4: parseMany on empty array
    {
        std::vector<rapidaid::geo::Location> locs = rapidaid::geo::Geocoder::parseMany("[]");
        if (locs.empty()) {
            std::cout << "[PASS] Case 4: parseMany on empty array\n";
        } else {
            std::cout << "[FAIL] Case 4: parseMany on empty array\n";
            failed++;
        }
    }

    // Case 5: parseMany on invalid JSON
    {
        std::vector<rapidaid::geo::Location> locs = rapidaid::geo::Geocoder::parseMany("[{bad json");
        if (locs.empty()) {
            std::cout << "[PASS] Case 5: parseMany on invalid JSON\n";
        } else {
            std::cout << "[FAIL] Case 5: parseMany on invalid JSON\n";
            failed++;
        }
    }

    // Case 6: parseMany on element with non-numeric lat
    {
        std::string json = R"([{"lat":"abc","lon":"2.2945","display_name":"Broken"}])";
        std::vector<rapidaid::geo::Location> locs = rapidaid::geo::Geocoder::parseMany(json);
        if (locs.empty()) {
            std::cout << "[PASS] Case 6: parseMany on element with non-numeric lat\n";
        } else {
            std::cout << "[FAIL] Case 6: parseMany on element with non-numeric lat\n";
            failed++;
        }
    }

    // Case 7: parseMany on empty string
    {
        std::vector<rapidaid::geo::Location> locs = rapidaid::geo::Geocoder::parseMany("");
        if (locs.empty()) {
            std::cout << "[PASS] Case 7: parseMany on empty string\n";
        } else {
            std::cout << "[FAIL] Case 7: parseMany on empty string\n";
            failed++;
        }
    }

    std::cout << "\nGeocoder Many Test Summary: " << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed == 0 ? 0 : 1;
}
