#include "GeoMath.hpp"

#include <cmath>
#include <iostream>

using rapidaid::geo::haversineMeters;

int main() {
    int failed = 0;

    // Case 1: identical points -> 0.0
    {
        double d1 = haversineMeters(0.0, 0.0, 0.0, 0.0);
        double d2 = haversineMeters(48.8584, 2.2945, 48.8584, 2.2945);
        double d3 = haversineMeters(-33.8688, 151.2093, -33.8688, 151.2093);

        if (std::abs(d1) < 1e-6 && std::abs(d2) < 1e-6 && std::abs(d3) < 1e-6) {
            std::cout << "[PASS] Case 1: identical points -> 0.0\n";
        } else {
            std::cout << "[FAIL] Case 1: identical points did not yield 0.0 (d1="
                      << d1 << ", d2=" << d2 << ", d3=" << d3 << ")\n";
            failed++;
        }
    }

    // Case 2: (0,0) to (0,1) is within 1 m of 111195 m
    {
        double dist = haversineMeters(0.0, 0.0, 0.0, 1.0);
        if (std::abs(dist - 111195.0) <= 1.0) {
            std::cout << "[PASS] Case 2: (0,0) to (0,1) is within 1 m of 111195 m (dist=" << dist << ")\n";
        } else {
            std::cout << "[FAIL] Case 2: (0,0) to (0,1) expected within 1m of 111195 m, got " << dist << "\n";
            failed++;
        }
    }

    // Case 3: symmetry: haversineMeters(a, b, c, d) == haversineMeters(c, d, a, b)
    {
        bool sym1 = (haversineMeters(12.34, 56.78, -23.45, 123.45) ==
                     haversineMeters(-23.45, 123.45, 12.34, 56.78));
        bool sym2 = (haversineMeters(0.0, 179.9, 0.0, -179.9) ==
                     haversineMeters(0.0, -179.9, 0.0, 179.9));
        bool sym3 = (haversineMeters(51.5074, -0.1278, 40.7128, -74.0060) ==
                     haversineMeters(40.7128, -74.0060, 51.5074, -0.1278));

        if (sym1 && sym2 && sym3) {
            std::cout << "[PASS] Case 3: symmetry haversineMeters(a, b, c, d) == haversineMeters(c, d, a, b)\n";
        } else {
            std::cout << "[FAIL] Case 3: symmetry check failed (sym1=" << sym1
                      << ", sym2=" << sym2 << ", sym3=" << sym3 << ")\n";
            failed++;
        }
    }

    // Case 4: antimeridian crossing: (lat 0, lon 179.9) to (lat 0, lon -179.9) is ~22 km (~22239 m), not ~40000 km
    {
        double dist = haversineMeters(0.0, 179.9, 0.0, -179.9);
        if (std::abs(dist - 22239.0) <= 50.0 && dist < 40000.0) {
            std::cout << "[PASS] Case 4: antimeridian crossing is ~22 km (dist=" << dist << ")\n";
        } else {
            std::cout << "[FAIL] Case 4: antimeridian crossing expected ~22239 m, got " << dist << "\n";
            failed++;
        }
    }

    std::cout << "\nGeoMath Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed > 0 ? 1 : 0;
}
