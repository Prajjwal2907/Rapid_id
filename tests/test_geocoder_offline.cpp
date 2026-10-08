#include "Geocoder.hpp"
#include <iostream>
#include <cmath>

static bool nearlyEqual(double a, double b, double epsilon = 0.0001) {
    return std::abs(a - b) < epsilon;
}

int main() {
    int failed = 0;

    // Case 1: Valid result
    {
        std::string json = R"([{"lat":"48.8584","lon":"2.2945","display_name":"Eiffel Tower"}])";
        auto res = rapidaid::geo::Geocoder::parseResponse(json);
        if (res.has_value() &&
            nearlyEqual(res->lat, 48.8584) &&
            nearlyEqual(res->lon, 2.2945) &&
            res->displayName == "Eiffel Tower") {
            std::cout << "[PASS] Case 1: Valid result\n";
        } else {
            std::cout << "[FAIL] Case 1: Valid result\n";
            failed++;
        }
    }

    // Case 2: Empty array
    {
        std::string json = "[]";
        auto res = rapidaid::geo::Geocoder::parseResponse(json);
        if (!res.has_value()) {
            std::cout << "[PASS] Case 2: Empty array\n";
        } else {
            std::cout << "[FAIL] Case 2: Empty array\n";
            failed++;
        }
    }

    // Case 3: Invalid JSON
    {
        std::string json = "[{lat: 48.8584, invalid";
        auto res = rapidaid::geo::Geocoder::parseResponse(json);
        if (!res.has_value()) {
            std::cout << "[PASS] Case 3: Invalid JSON\n";
        } else {
            std::cout << "[FAIL] Case 3: Invalid JSON\n";
            failed++;
        }
    }

    // Case 4: Missing lat
    {
        std::string json = R"([{"lon":"2.2945","display_name":"Eiffel Tower"}])";
        auto res = rapidaid::geo::Geocoder::parseResponse(json);
        if (!res.has_value()) {
            std::cout << "[PASS] Case 4: Missing lat\n";
        } else {
            std::cout << "[FAIL] Case 4: Missing lat\n";
            failed++;
        }
    }

    // Case 5: Non-numeric lat
    {
        std::string json = R"([{"lat":"abc","lon":"2.2945","display_name":"Eiffel Tower"}])";
        auto res = rapidaid::geo::Geocoder::parseResponse(json);
        if (!res.has_value()) {
            std::cout << "[PASS] Case 5: Non-numeric lat\n";
        } else {
            std::cout << "[FAIL] Case 5: Non-numeric lat\n";
            failed++;
        }
    }

    // Case 6: Empty string
    {
        std::string json = "";
        auto res = rapidaid::geo::Geocoder::parseResponse(json);
        if (!res.has_value()) {
            std::cout << "[PASS] Case 6: Empty string\n";
        } else {
            std::cout << "[FAIL] Case 6: Empty string\n";
            failed++;
        }
    }

    // Case 7: buildUrl encoding of a query with spaces and a comma
    {
        std::string query = "Eiffel Tower, Paris";
        std::string url = rapidaid::geo::Geocoder::buildUrl(query);
        std::string expected = "https://nominatim.openstreetmap.org/search?q=Eiffel%20Tower%2C%20Paris&format=json&limit=1";
        if (url == expected) {
            std::cout << "[PASS] Case 7: buildUrl encoding (" << url << ")\n";
        } else {
            std::cout << "[FAIL] Case 7: buildUrl encoding (expected: " << expected << ", got: " << url << ")\n";
            failed++;
        }
    }

    if (failed == 0) {
        std::cout << "\nAll offline tests PASSED.\n";
        return 0;
    } else {
        std::cout << "\n" << failed << " test(s) FAILED.\n";
        return 1;
    }
}
