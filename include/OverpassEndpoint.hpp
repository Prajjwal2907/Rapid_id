#ifndef RAPIDAID_NET_OVERPASS_ENDPOINT_HPP
#define RAPIDAID_NET_OVERPASS_ENDPOINT_HPP

namespace rapidaid::net {

// Overpass API interpreter endpoints.
// Primary endpoint for OSM data queries.
inline constexpr const char* kOverpassPrimaryUrl = "https://overpass-api.de/api/interpreter";
// Fallback mirror endpoint used when primary endpoint attempts fail.
inline constexpr const char* kOverpassFallbackUrl = "https://overpass.kumi.systems/api/interpreter";

// Aliases for convenience
inline constexpr const char* kPrimaryInterpreterUrl = kOverpassPrimaryUrl;
inline constexpr const char* kFallbackInterpreterUrl = kOverpassFallbackUrl;

} // namespace rapidaid::net

#endif // RAPIDAID_NET_OVERPASS_ENDPOINT_HPP
