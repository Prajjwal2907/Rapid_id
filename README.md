# ResQNet — Service Classes Module

Basic overview of each file in this module.

## include/

| File | Overview |
|---|---|
| `Status.h` | Shared enums: `UnitStatus`, `UnitType`, `FacilityType`. No logic, just definitions used everywhere else. |
| `ResponseUnit.h` | Abstract base class for all mobile units. Declares id, status, location, and dispatch-related methods. |
| `Ambulance.h` | Ambulance class, extends `ResponseUnit`. Adds medical staff count and life support flag. |
| `FireTruck.h` | Fire truck class, extends `ResponseUnit`. Adds water capacity and ladder flag. |
| `PoliceUnit.h` | Police unit class, extends `ResponseUnit`. Adds officer count and crowd control gear flag. |
| `Facility.h` | Abstract base class for stationary locations. Declares id, capacity, and admit/release methods. |
| `Hospital.h` | Hospital class, extends `Facility`. Adds bed counts, specialties, triage-based admission. |
| `Shelter.h` | Shelter class, extends `Facility`. Adds pet-friendly flag, medical station flag, supply inventory. |
| `Supply.h` | Standalone class for a trackable resource (quantity, consume/restock, low-stock check). |
| `ServiceRegistry.h` | Central class that owns all units, facilities, and supplies, and provides lookup/query methods. |

## src/

| File | Overview |
|---|---|
| `ResponseUnit.cpp` | Implements base unit behavior: dispatch, arrive, becomeAvailable, status checks. |
| `Ambulance.cpp` | Implements Ambulance-specific behavior (e.g. transportPatient). |
| `FireTruck.cpp` | Implements FireTruck-specific behavior (e.g. deployHose). |
| `PoliceUnit.cpp` | Implements PoliceUnit-specific behavior (e.g. securePerimeter). |
| `Facility.cpp` | Implements base facility behavior: admit, release, capacity checks. |
| `Hospital.cpp` | Implements Hospital-specific admission logic based on severity. |
| `Shelter.cpp` | Implements Shelter-specific behavior; mostly reuses base Facility logic. |
| `Supply.cpp` | Implements consume/restock/low-stock logic. |
| `ServiceRegistry.cpp` | Implements registration and lookup logic (find nearest facility, list available units, etc). |

## main.cpp

Entry point / test file — creates sample units, facilities, and supplies to demonstrate the classes working together.

## Configuration

Copy `include/Config.example.hpp` to `include/Config.local.hpp` and set `kContact` to your project URL or contact information:
```cpp
namespace rapidaid::config {
    constexpr const char* kContact = "<your project URL or contact>";
}
```
`include/Config.local.hpp` is git-ignored and must never be committed to the repository. This contact information is sent to OpenStreetMap services in the `User-Agent` header.

## Building

Build requirements:
- MSYS2 MinGW-w64 64-bit (`g++` C++17, `make`)
- libcurl

### Build Commands

```bash
# Build all tests
make tests

# Build a specific test by name (e.g., test_http)
make test_http

# Build and run offline tests only
make offline

# Build the main application (requires main.cpp)
make app

# Clean build artifacts
make clean

# Build with custom include directory (e.g. dummy config)
make EXTRA_INCLUDES=-I/tmp/dummycfg tests
```
