# Rapid Aid

Rapid Aid is a high-performance emergency response dispatching and coordination platform built in modern C++ (C++17) with an interactive single-page web UI.

The system integrates real-time emergency incident intake, dynamic incident prioritization, geographic road network modeling via OpenStreetMap, Dijkstra shortest-path route computation, automated dispatch recommendations for emergency response units (Ambulances, Fire Trucks, Police Units), and dedicated operational views for dispatchers and field service teams.

---

## 1. System Architecture

Rapid Aid is structured into four decoupled layers:

```
+-------------------------------------------------------------------------+
|                               Web UI                                    |
|   index.html | app.js | board.js | dispatch.js | unit.js | styles.css   |
|   Leaflet Map | Role Routing | Incident Queue | Field Unit Console      |
+------------------------------------+------------------------------------+
                                     | JSON-RPC HTTP / API Bridge
+------------------------------------+------------------------------------+
|                         Server & API Bridge                             |
|   main.cpp | UiBridge | Login / Auth Subsystem | FileStore              |
|   HTTP Server | Session Auth | Security Headers | Background Ticker     |
+------------------------------------+------------------------------------+
                  |                                    |
+-----------------+------------------+ +---------------+------------------+
|           Geo Backend              | |      Dispatch & Domain Engine     |
| Geocoder | OverpassClient          | | DispatchEngine | Incident         |
| RoadClient | DataCache             | | IncidentStore | UnitRegistry      |
| Graph | GraphBuilder               | | Ambulance | Fire_truck            |
| GeoMath | LocationResolver         | | Police_unit | Hospital            |
| GeoJsonWriter                      | | Fire_station | Shelter            |
+------------------------------------+ +----------------------------------+
```

### 1.1 Geo & Spatial Backend
- **Geocoding (`Geocoder`)**: Converts free-text location names into geographic coordinates using the OpenStreetMap Nominatim API, with built-in rate-limiting compliance.
- **Facility & Road Extraction (`OverpassClient`, `RoadClient`)**: Queries the Overpass API for emergency facilities (hospitals, fire stations, police stations, shelters) and drivable highway ways.
- **Caching Layer (`DataCache`)**: Persists raw OSM queries to disk (`data/`) to eliminate redundant network round-trips and enable completely offline operation after initial fetch.
- **Topological Routing (`Graph`, `GraphBuilder`, `GeoMath`)**: Constructs an undirected graph of the road network with edge weights computed from endpoint coordinates via the Haversine formula, extracts the largest connected component, snaps facilities to the nearest road vertices, and computes optimal routes using single-source and multi-destination Dijkstra algorithms.
- **GeoJSON Serialization (`GeoJsonWriter`)**: Serializes road networks and snapped facilities to GeoJSON feature collections (`data/out/`) for browser rendering.

### 1.2 Dispatch & Domain Service Engine
- **Incident Prioritization (`Incident`, `IncidentStore`)**: Dynamically scores emergency incidents based on severity, number of persons affected, and elapsed waiting time.
- **Dispatch Recommendations (`DispatchEngine`)**: Determines required vehicle slots and minimum facility capacities per incident category (Medical, Fire, Crime, Traffic, Hazmat, Natural Disaster). Ranks eligible units by road network travel distance and suggests optimal replacement units when candidates are busy or unavailable.
- **Service Unit Management (`UnitRegistry`, Service Models)**: Tracks real-time states (`available`, `dispatched`, `on_scene`, `returning`) for Ambulances, Fire Trucks, and Police Units, as well as hospital bed availability and shelter occupancy.

### 1.3 Server & API Bridge
- **Embedded Web Server (`main.cpp`)**: Multi-threaded HTTP/1.1 server (using WinSock2 on Windows, POSIX sockets on Linux/macOS) serving static web assets and handling JSON-RPC endpoints (`/api/bridge`, `/api/create_area`, `/api/pipeline`).
- **Background Dispatch Loop**: Detached background thread executing periodic tick evaluations (`tickDispatch()`) every second.
- **Authentication & Security (`Login`, `UiBridge`)**: Enforces PBKDF2 password derivation with per-user salt and system pepper, session token verification, role-based access control, host header verification, and HTTP security headers (CSP, nosniff, frame-ancestors).

### 1.4 Interactive Web Frontend
- Single-page application built with vanilla JavaScript and Leaflet.js.
- **Dispatcher/Admin Interface**: Operational area creation and map preview, real-time incident queue, interactive dispatch recommendations with road route overlays, and user account approval management.
- **Service Unit Console**: Streamlined mobile-friendly dashboard for vehicle crews to review assigned emergencies, examine driving routes, verify hospital bed/shelter availability, and update operational status.

---

## 2. Directory & Folder Layout

```
Rapid_id/
├── .gitattributes            # Enforces LF line endings and binary file handling
├── .gitignore                # Excludes build objects, caches, and secrets
├── Makefile                  # Build targets for application and test suites
├── README.md                 # Primary system documentation
├── config.example.json       # Template for optional frontend tile/map configurations
├── index.html                # Single-page web application entry point
├── styles.css                # Application styling and responsive UI rules
├── app.js                    # Core frontend client, authentication, and area management
├── board.js                  # Live incident map, layer overlays, and telemetry rendering
├── dispatch.js               # Dispatch proposal cards, unit allocation, and status flows
├── unit.js                   # Dedicated field service unit interface
├── main.cpp                  # Server entry point, socket loop, and CLI argument parsing
├── Login.cpp                 # User authentication, credential storage, and session logic
├── project_context.md        # Extended system architecture and API reference
├── docs/
│   ├── SECURITY.md           # Security architecture, controls, and operational limitations
│   └── TESTING.md            # Exhaustive test catalog, offline runner, and live test guide
├── include/
│   ├── Ambulance.hpp         # Ambulance service model declaration
│   ├── AreaContext.hpp       # Area context and operational data management
│   ├── Config.example.hpp    # Template for OSM User-Agent contact configuration
│   ├── DataCache.hpp         # Persistent OSM request caching
│   ├── DispatchEngine.hpp    # Resource allocation rules and Dijkstra recommendation engine
│   ├── FileStore.hpp         # Atomic file I/O operations
│   ├── Fire_station.hpp      # Fire station model declaration
│   ├── Fire_truck.hpp        # Fire truck service model declaration
│   ├── Geocoder.hpp          # Nominatim geocoding client
│   ├── GeoJsonWriter.hpp     # GeoJSON export and formatting
│   ├── GeoMath.hpp           # Haversine distance calculations
│   ├── Graph.hpp             # Graph data structure and Dijkstra routing
│   ├── GraphBuilder.hpp      # OSM road network to graph compilation
│   ├── Hospital.hpp          # Hospital service model with bed tracking
│   ├── HttpClient.hpp        # libcurl HTTP wrapper
│   ├── Incident.hpp          # Incident domain entity and priority scoring
│   ├── IncidentStore.hpp     # Incident persistence and queue management
│   ├── json.hpp              # Bundled nlohmann/json single-header library
│   ├── LocationResolver.hpp  # Spatial place queries and street intersection resolver
│   ├── Login.hpp             # Authentication, role checking, and logger declarations
│   ├── OverpassClient.hpp    # Overpass API facility query builder and parser
│   ├── Police_station.hpp    # Police station model declaration
│   ├── Police_unit.hpp       # Police patrol vehicle service model declaration
│   ├── RoadClient.hpp        # Overpass API road network query builder and filter
│   ├── Shelter.hpp           # Shelter service model with occupancy tracking
│   ├── UiBridge.hpp          # JSON-RPC bridge router between web UI and C++ engine
│   └── UnitRegistry.hpp      # Registered emergency units and station lookup registry
├── src/                      # Implementation files corresponding to include/ headers
├── tests/                    # 26 automated unit, domain, and integration test suites
└── data/                     # Runtime data directory (git-ignored)
    ├── auth.key              # Cryptographic master pepper and key material
    ├── incidents.json        # Incident store persistence
    ├── units.json            # Service unit store persistence
    ├── geocode_*.json        # Cached Nominatim geocoding responses
    ├── places_*.json         # Cached Overpass place queries
    ├── roads_*.json          # Cached Overpass road networks
    └── out/                  # Generated operational artifacts
        ├── area.json         # Current active operational area metadata
        ├── roads.geojson     # Generated road network GeoJSON
        ├── places.geojson    # Snapped facility points GeoJSON
        ├── roads.js          # JavaScript wrapper for roads GeoJSON
        └── places.js         # JavaScript wrapper for places GeoJSON
```

---

## 3. Build Prerequisites per Operating System

### 3.1 Windows (Tested & Supported)
- **Environment**: MSYS2 MinGW-w64 (64-bit).
- **Required Packages**:
  ```bash
  pacman -S --needed mingw-w64-x86_64-gcc \
                     mingw-w64-x86_64-make \
                     mingw-w64-x86_64-curl \
                     mingw-w64-x86_64-openssl
  ```
- **JSON Library**: Bundled single-header `nlohmann/json` is already included in `include/json.hpp`.

### 3.2 Linux (Debian / Ubuntu / Fedora - Marked Untested)
- **Prerequisites**: C++17 compliant `g++`, GNU Make, libcurl, and OpenSSL development headers.
- **Debian / Ubuntu**:
  ```bash
  sudo apt-get update
  sudo apt-get install build-essential libcurl4-openssl-dev libssl-dev
  ```
- **Fedora / RHEL**:
  ```bash
  sudo dnf install gcc-c++ make libcurl-devel openssl-devel
  ```

### 3.3 macOS (Marked Untested)
- **Prerequisites**: Xcode Command Line Tools, GNU Make, libcurl, and OpenSSL via Homebrew.
  ```bash
  xcode-select --install
  brew install curl openssl make
  ```

---

## 4. Configuration Setup

OpenStreetMap Nominatim and Overpass APIs require a descriptive `User-Agent` identifying the application and an administrative contact URL or email.

1. Copy the configuration template:
   ```bash
   cp include/Config.example.hpp include/Config.local.hpp
   ```
2. Edit `include/Config.local.hpp` with your project URL or contact email:
   ```cpp
   #ifndef CONFIG_HPP
   #define CONFIG_HPP

   namespace rapidaid::config {
       constexpr const char* kContact = "emergency-ops@example.com";
   }

   #endif // CONFIG_HPP
   ```
3. `include/Config.local.hpp` is excluded in `.gitignore` and should never be committed to source control.

---

## 5. Build & Run Commands

Rapid Aid uses GNU Make for builds.

### 5.1 Build the Server Application
```bash
make app
```
Compiles the application binary to `build/rapidaid.exe` (or `build/rapidaid` on Unix).

### 5.2 Build & Run the Offline Test Suite
```bash
make offline
```
Compiles all 21 offline test executables and executes them sequentially. This is the primary verification target.

### 5.3 Build All Tests
```bash
make tests
```
Compiles all test programs (both offline tests and live integration probes).

### 5.4 Clean Build Artifacts
```bash
make clean
```
Removes the `build/` directory and generated compilation artifacts.

### 5.5 Starting the Server
```bash
./build/rapidaid.exe
```

- **Default Binding**: Binds exclusively to the loopback interface (`http://localhost:8080`). If port 8080 is occupied, the server automatically attempts port `8081`.
- **Browser Launch**: Upon successful startup, the server automatically opens the web UI in your default browser.
- **Custom Project Root**:
  ```bash
  ./build/rapidaid.exe --root /path/to/project
  ```
- **LAN Access Flag (`--lan`)**:
  ```bash
  ./build/rapidaid.exe --lan
  ```
  > [!WARNING]
  > Binding to all interfaces (`--lan`) exposes the plain HTTP server to your local network. Passwords, session tokens, and incident logs will travel unencrypted. Do not use `--lan` on untrusted networks or without a TLS-terminating reverse proxy.

---

## 6. First-Run Onboarding & Authentication

### 6.1 Initial Administrator Password
- When launched for the first time with no existing user store, the server generates an initial `admin` account with a cryptographically secure random temporary password.
- **Printed Once**: The temporary password is printed directly to the terminal stdout **once** during startup.
- **Mandatory Change**: The administrator must log in immediately and update their password.

### 6.2 New Account Approval Workflow
- When new users register via the sign-up form, accounts are set to a **pending approval** status.
- Pending users cannot log in or perform dispatch operations.
- An administrator must log in, open the **Users** management screen in the navigation bar, and approve or reject the request.

### 6.3 Seamless Legacy Account Migration
- Existing installations running older versions with `std::hash` in `users.txt` are supported.
- When a legacy user logs in for the first time, their credentials are automatically migrated and upgraded to PBKDF2 hashing in `users.txt`.

---

## 7. Account Roles & Operational Guide

| Role | Interface View | Responsibilities & Capabilities |
|---|---|---|
| **Administrator (`admin`)** | Full Dispatcher UI + Users Management | Reviews and approves/rejects pending user registrations, manages roles, configures operational areas, and performs all dispatch operations. |
| **Dispatcher (`dispatcher`)** | Operational Map, Incident Queue, Live Board | Defines operational response areas, triggers OSM road/facility pipelines, logs emergency incidents, reviews algorithmic dispatch proposals, assigns emergency units, and tracks units on the live board. |
| **Field Unit (`unit`)** | Service Unit Console | Dedicated interface for emergency vehicle crews (Ambulance, Fire Truck, Police Unit). Displays assigned emergency incidents, navigation routes, hospital bed / shelter telemetry, and status controls (`available`, `dispatched`, `on_scene`, `returning`). |

---

## 8. Data Folder Contents & Cache Management

The `data/` folder stores persistent application state and cached OpenStreetMap queries:

- `data/auth.key`: High-entropy system pepper used by PBKDF2 for password derivation. **Must be backed up alongside `users.txt`**.
- `data/incidents.json`: Active and resolved emergency incident records.
- `data/units.json`: Registered emergency response vehicles, crews, and station assignments.
- `data/geocode_<query>.json`: Cached Nominatim geocoding responses.
- `data/places_<category>_<lat>_<lon>_<radius>.json`: Cached Overpass API facility search results.
- `data/roads_<lat>_<lon>_<radius>.json`: Cached Overpass API road network responses.
- `data/out/`: Operational artifacts generated for the active response area:
  - `area.json`: Metadata for the active zone (center coordinates, radius, name).
  - `roads.geojson` / `roads.js`: Processed drivable road network.
  - `places.geojson` / `places.js`: Snapped emergency facilities and shelters.

### Refreshing Cached Map Data
If street geometry, hospital capacity, or emergency stations change in OpenStreetMap:
1. **Via Web UI**: In the **Area Setup** tab, re-enter the area query and click **Set Operational Area** with refresh enabled.
2. **Via Filesystem**: Remove the cached JSON files in `data/` (or specific `data/roads_*.json` / `data/places_*.json` files) and restart the server. The next pipeline run will fetch fresh data from OpenStreetMap.

---

## 9. Security & Testing References

For detailed specifications, see:
- [Security Architecture & Limitations](file:///D:/Coding/Projects/pbl/rapidaid-f-d/docs/SECURITY.md)
- [Test Suite & Verification Guide](file:///D:/Coding/Projects/pbl/rapidaid-f-d/docs/TESTING.md)
