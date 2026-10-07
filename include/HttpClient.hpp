#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include <string>
#include <cstddef>

namespace rapidaid::net{
    class HttpClient{
        public:
            static std::string get(const std::string& url);
            static std::string post(const std::string& url, const std::string& data);
        private:
            static std::string performRequest(const std::string& url, const std::string* body = nullptr);
            static std::size_t writeCallback(void* contents, std::size_t size, std::size_t nmemb, void* userp);
    };
}

#endif