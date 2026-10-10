# Rapid Aid

Rapid Aid is a C++ project for dispatcher and emergency-response support. The current repository contains a dispatcher login/logger prototype, a console incident-priority prototype, OpenStreetMap geocoding and nearby-place lookup modules, emergency service-model classes, and tests. The service model provides dispatchable response units and their owning facilities.

## Project files

### Root

| File | Purpose |
|---|---|
| `.gitignore` | Keeps the local configuration file and generated build files out of Git. |
| `Login.cpp` | Standalone dispatcher login prototype. It loads/saves users in `users.txt`, logs events to `log.txt`, supports an admin-created dispatcher, and presents placeholder incident menu options. Its default first-run admin is `admin` / `admin123`; change this before using it beyond local testing. |
| `Makefile` | Defines C++17 build targets for the app and tests, uses `g++`, and links libcurl. See [Building](#building). |
| `main.cpp` | Current app entry point; it is only a minimal `main` function and does not yet connect the modules. |

### `include/`

| File | Purpose / status |
|---|---|
| `Config.example.hpp` | Template for the local OpenStreetMap contact setting used in the HTTP `User-Agent`. Copy it to `Config.local.hpp` and replace the placeholder contact value. |
| `Geocoder.hpp` | Declares `rapidaid::geo::Geocoder` and `Location` for Nominatim address-to-coordinate lookup, response parsing, and URL construction. |
| `HttpClient.hpp` | Declares `rapidaid::net::HttpClient` helpers for URL encoding and HTTP GET/POST requests. |
| `OverpassClient.hpp` | Declares OpenStreetMap place categories, the `Place` result type, and query, response-parsing, and nearby-search methods. |
| `json.hpp` | Bundled single-header nlohmann JSON library, used to parse Nominatim and Overpass responses. |
| `Ambulance.hpp` | Declares a dispatchable ambulance with availability, incident assignment, and paramedic count. |
| `Fire_truck.hpp` | Declares a dispatchable fire truck with availability, incident assignment, and water capacity. |
| `Fire_station.hpp` | Declares a fire station that owns and looks up fire trucks. |
| `Hospital.hpp` | Declares a hospital that owns ambulances and tracks available beds. |
| `Police_unit.hpp` | Declares a dispatchable police unit with availability, incident assignment, and officer count. |
| `Police_station.hpp` | Declares a police station that owns and looks up police units. |
| `Shelter.hpp` | Declares a shelter with capacity, occupancy, admission, and departure operations. |

### `src/`

| File | Purpose / status |
|---|---|
| `Geocoder.cpp` | Implements Nominatim geocoding, query URL construction, response parsing, and a one-second delay between live geocoding requests. |
| `HttpClient.cpp` | Implements libcurl URL encoding and HTTP GET/POST, with the configured contact in its `User-Agent`. |
| `OverpassClient.cpp` | Builds Overpass queries and form bodies, parses returned OSM places, validates coordinates/radius, and retries failed requests. |
| `priority_queue.cpp` | Standalone console prototype for entering incidents and ordering them by a score calculated from severity, people affected, and waiting time. |
| `Ambulance.cpp` | Implements ambulance availability, incident assignment, and release operations. |
| `Fire_truck.cpp` | Implements fire-truck availability, incident assignment, and release operations. |
| `Fire_station.cpp` | Implements fire-truck ownership, lookup, and available-truck listing for a fire station. |
| `Hospital.cpp` | Implements ambulance ownership, lookup, available-ambulance listing, and bed accounting. |
| `Police_unit.cpp` | Implements police-unit availability, incident assignment, and release operations. |
| `Police_station.cpp` | Implements police-unit ownership, lookup, and available-unit listing for a police station. |
| `Shelter.cpp` | Implements shelter capacity, occupancy, admission, and departure operations. |

### `tests/`

| File | Purpose |
|---|---|
| `test_service_offline.cpp` | Checks response-unit assignment and availability, hospital bed accounting, and shelter capacity behavior without network access. |
| `test_geocoder_offline.cpp` | Checks geocoder response parsing and URL construction using local sample data; it does not need a live geocoding request. |
| `test_overpass_offline.cpp` | Checks category names, query/request-body construction, response parsing, and invalid search inputs using local data. |
| `test_geocoder.cpp` | Example live geocoding request for the Eiffel Tower; requires network access. |
| `test_overpass.cpp` | Example live search for hospitals near the Eiffel Tower; requires network access. |
| `test_http.cpp` | Example live HTTP GET and POST requests to Nominatim and Overpass; requires network access. |

## Configuration

Copy `include/Config.example.hpp` to `include/Config.local.hpp` and set `kContact` to a project URL or contact address:

```cpp
namespace rapidaid::config {
    constexpr const char* kContact = "<your project URL or contact>";
}
```

`Config.local.hpp` is ignored by Git and should not be committed. The contact value is sent to OpenStreetMap services in the HTTP `User-Agent`.

## Building

Build requirements:

- MSYS2 MinGW-w64 64-bit with `g++` supporting C++17 and `make`
- libcurl

From the project directory, the Makefile provides these targets:

```bash
# Build all test executables
make tests

# Build one test executable (for example, test_http)
make test_http

# Build and run tests whose filenames end in _offline.cpp
make offline

# Build the application target
make app

# Remove generated build files
make clean
```

The live network examples contact public OpenStreetMap services. The service-model classes are implemented as standalone models; integration of the prototypes into `main.cpp` is not implemented yet.
