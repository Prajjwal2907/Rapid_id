// Copy this file to Config.local.hpp, set kContact to your own contact,
// and never commit Config.local.hpp.
// The contact is sent to OpenStreetMap services in the User-Agent header.

#ifndef CONFIG_HPP
#define CONFIG_HPP

namespace rapidaid::config {
    constexpr const char* kContact = "<your project URL or contact>";
}

#endif // CONFIG_HPP

