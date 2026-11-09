#include "GeoMath.hpp"

#include <cmath>

namespace rapidaid::geo {

namespace {
    constexpr double EARTH_RADIUS_METERS = 6371000.0;
    constexpr double PI = 3.14159265358979323846;

    inline double degToRad(double deg) {
        return deg * (PI / 180.0);
    }
} // namespace

double haversineMeters(double lat1, double lon1, double lat2, double lon2) {
    if (lat1 == lat2 && lon1 == lon2) {
        return 0.0;
    }

    const double lat1Rad = degToRad(lat1);
    const double lat2Rad = degToRad(lat2);
    const double dlat = degToRad(lat2 - lat1);

    double dlon = degToRad(lon2 - lon1);
    while (dlon > PI) {
        dlon -= 2.0 * PI;
    }
    while (dlon < -PI) {
        dlon += 2.0 * PI;
    }

    const double sinHalfDLat = std::sin(dlat / 2.0);
    const double sinHalfDLon = std::sin(dlon / 2.0);

    double a = sinHalfDLat * sinHalfDLat +
               std::cos(lat1Rad) * std::cos(lat2Rad) * sinHalfDLon * sinHalfDLon;

    if (a < 0.0) {
        a = 0.0;
    } else if (a > 1.0) {
        a = 1.0;
    }

    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return EARTH_RADIUS_METERS * c;
}

} // namespace rapidaid::geo
