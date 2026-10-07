#if __has_include("Config.local.hpp")
    #include "Config.local.hpp"
#else
    #error "Config.local.hpp not found. Copy include/Config.example.hpp to include/Config.local.hpp and set kContact."
#endif

#include "HttpClient.hpp"
#include <iostream>
#include <string>
#include <curl/curl.h>

namespace {
    constexpr const char* USER_AGENT_BASE = "RapidAid/0.1";

    // RAII helper to ensure curl_global_init and curl_global_cleanup are managed properly
    struct CurlGlobalGuard {
        CurlGlobalGuard() {
            curl_global_init(CURL_GLOBAL_DEFAULT);
        }
        ~CurlGlobalGuard() {
            curl_global_cleanup();
        }
    };
}

namespace rapidaid::net {

    std::size_t HttpClient::writeCallback(void* contents, std::size_t size, std::size_t nmemb, void* userp) {
        std::size_t byteSize = size * nmemb;
        std::string* str = static_cast<std::string*>(userp);
        str->append(static_cast<const char*>(contents), byteSize);
        return byteSize;
    }

    std::string HttpClient::performRequest(const std::string& url, const std::string* body) {
        // Function-local static object ensures curl_global_init runs once per process
        static const CurlGlobalGuard curlGlobalGuard;
        // Function-local static string ensures User-Agent is built once per process
        static const std::string userAgent = std::string(USER_AGENT_BASE) + " (" + rapidaid::config::kContact + ")";

        CURL* handle = curl_easy_init();
        if (!handle) {
            std::cerr << "Failed to initialize CURL (" << url << ")\n";
            return "";
        }

        std::string response;
        curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &HttpClient::writeCallback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(handle, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(handle, CURLOPT_USERAGENT, userAgent.c_str());

        if (body != nullptr) {
            curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body->c_str());
        }

        CURLcode res = curl_easy_perform(handle);
        long status = 0;
        if (res == CURLE_OK) {
            curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        }

        // Cleanup must happen on every path after init succeeds
        curl_easy_cleanup(handle);

        if (res != CURLE_OK) {
            std::cerr << "CURL error: " << curl_easy_strerror(res) << " (" << url << ")\n";
            return "";
        }

        if (status >= 400) {
            std::cerr << "HTTP error " << status << " (" << url << ")\n";
            return "";
        }

        return response;
    }

    std::string HttpClient::get(const std::string& url) {
        return performRequest(url);
    }

    std::string HttpClient::post(const std::string& url, const std::string& data) {
        return performRequest(url, &data);
    }

} // namespace rapidaid::net