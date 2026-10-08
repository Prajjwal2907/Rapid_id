#include "HttpClient.hpp"
#include <iostream>
#include <string>

int main() {
    using rapidaid::net::HttpClient;

    // Test GET request to Nominatim API
    const std::string getUrl = "https://nominatim.openstreetmap.org/search?q=Eiffel+Tower&format=json&limit=1";
    std::cout << "Sending GET request to: " << getUrl << "\n";
    std::string getResult = HttpClient::get(getUrl);

    if (getResult.empty()) {
        std::cerr << "GET request failed (empty result)\n";
        return 1;
    }

    std::cout << "Response size: " << getResult.size() << " bytes\n";
    std::cout << "First 300 characters:\n" << getResult.substr(0, 300) << "\n\n";

    // Test POST request to Overpass API with "data=" and URL-encoded query: [out:json];node(1);out;
    const std::string postUrl = "https://overpass-api.de/api/interpreter";
    const std::string postBody = "data=%5Bout%3Ajson%5D%3Bnode%281%29%3Bout%3B";
    std::cout << "Sending POST request to: " << postUrl << "\n";
    std::string postResult = HttpClient::post(postUrl, postBody);

    if (postResult.empty()) {
        std::cerr << "POST request failed (empty result)\n";
        return 1;
    }

    std::cout << "Response size: " << postResult.size() << " bytes\n";
    std::cout << "First 300 characters:\n" << postResult.substr(0, 300) << "\n";

    return 0;
}
