#ifndef RAPIDAID_GEO_GEOMATH_HPP
#define RAPIDAID_GEO_GEOMATH_HPP

namespace rapidaid::geo {

// Computes the great-circle distance in meters between two coordinates (lat1, lon1) and (lat2, lon2)
// using the Haversine formula with Earth radius R = 6371000.0 meters.
// Handles antimeridian crossings by wrapping longitude differences to [-pi, pi].
// Failure behavior: returns 0.0 for identical coordinates. Never throws.
double haversineMeters(double lat1, double lon1, double lat2, double lon2);

} // namespace rapidaid::geo

#endif // RAPIDAID_GEO_GEOMATH_HPP
