# Rapid Aid Test Suite & Verification Guide

This document catalogs every test program in the Rapid Aid test suite, explains their verification scopes, and details execution procedures for both offline automated test runs and manual live network tests.

---

## 1. Test Suite Overview

Rapid Aid features a dual-layer test suite:
1. **Offline Test Suite (21 test programs)**: Completely self-contained tests that run without network access. They use canned OSM responses, synthesized road networks, mock stores, and isolated temporary files to verify algorithmic correctness, data transformations, serialization, protocol routing, and security invariants.
2. **Live Integration Tests (5 test programs)**: End-to-end integration probes that make real HTTPS requests to OpenStreetMap services (Nominatim and Overpass API). These verify live endpoint schemas, libcurl network pipelines, and production query compatibility.

---

## 2. Test Catalog

| Test Name | Type | What It Proves |
|---|---|---|
| `test_areacontext_offline.cpp` | Offline | Proves AreaContext lifecycle and persistence: saving/loading `area.json` metadata atomically without temporary file leaks, handling corrupt/missing metadata gracefully, querying operational area status, and building road graphs and snapped place indexes from cached datasets. |
| `test_datacache_offline.cpp` | Offline | Proves DataCache file path calculation (locale-independent 4-decimal formatting, query sanitization for special characters/spaces), cache-hit vs. cache-miss semantics, and reading cached JSON files from disk without outbound network calls. |
| `test_dispatch_engine_offline.cpp` | Offline | Proves DispatchEngine slot requirements per emergency category (Medical, Fire, Crime, Traffic, Hazmat, NaturalDisaster) scaling by person count, nearest unit candidate ranking using Dijkstra road network distances, capacity filtering, tie-breaking, and replacement recommendation (`pickReplacement`). |
| `test_dispatch_offline.cpp` | Offline | Proves full incident lifecycle through the UI bridge: incident intake, automated priority score calculation, dispatch proposal recommendation, unit assignment, unit status transitions (`available` &rarr; `dispatched` &rarr; `on_scene` &rarr; `returning` &rarr; `available`), multi-unit coordination, and incident cancellation/resolution. |
| `test_filestore_offline.cpp` | Offline | Proves atomic file I/O operations in FileStore: saving and loading text/binary payloads with newlines and non-ASCII bytes, atomic replacement via `.tmp` file renaming, directory creation, file existence verification, and file deletion. |
| `test_geocoder.cpp` | Live | Proves live Nominatim geocoding over HTTPS: building query URLs, executing live GET requests via libcurl, parsing coordinate results (e.g. Eiffel Tower), and verifying User-Agent header transmission. |
| `test_geocoder_many_offline.cpp` | Offline | Proves Geocoder multi-result parsing (`parseMany`) and bounded viewbox query construction: handling empty arrays, malformed JSON, missing/non-numeric lat/lon coordinates, and locale-independent coordinate formatting. |
| `test_geocoder_offline.cpp` | Offline | Proves Nominatim single-result parsing (`parseResponse`) and URL encoding: validating single result extraction, handling empty arrays, corrupted JSON, missing coordinate attributes, and query parameter sanitization without network calls. |
| `test_geojson_offline.cpp` | Offline | Proves GeoJsonWriter serialization: FeatureCollection construction, strict GeoJSON coordinate ordering (`[longitude, latitude]`), 7-decimal coordinate precision rounding, character escaping in place names, snapped place status tagging, and path LineString generation. |
| `test_geomath_offline.cpp` | Offline | Proves Haversine distance calculations in GeoMath: zero distance for identical coordinates, known reference distance verification (1 degree latitude ~ 111,195 m), mathematical symmetry ($d(a, b) = d(b, a)$), and antimeridian crossing handling (179.9&deg; to -179.9&deg;). |
| `test_graphbuilder_offline.cpp` | Offline | Proves graph construction from OpenStreetMap road data in GraphBuilder: node and edge translation, edge weight computation matching Haversine distances, skipping invalid segments referencing non-existent nodes, and largest connected component filtering (`keepLargestComponent`). |
| `test_graph_multi_offline.cpp` | Offline | Proves multi-destination Dijkstra routing (`shortestPathsFrom`): equivalence to individual point-to-point Dijkstra runs across all node pairs, handling invalid source nodes, and handling disconnected graph components. |
| `test_graph_offline.cpp` | Offline | Proves core Graph data structure and Dijkstra algorithm: node deduplication, undirected edge insertion, self-loop suppression, parallel edge relaxation (retaining lowest weight), edge counting, two-hop vs. direct route optimization, identical source-destination queries, and unreachable node handling. |
| `test_http.cpp` | Live | Proves low-level HttpClient networking via libcurl: live HTTPS GET requests to Nominatim and live HTTPS POST requests (`data=` URL-encoded body) to Overpass API. |
| `test_incident_offline.cpp` | Offline | Proves Incident domain logic and IncidentStore persistence: priority scoring formula based on severity, affected people, and elapsed wait time; unit kind parsing and conversion; JSON serialization/deserialization; auto-increment ID generation; corrupted file recovery. |
| `test_locationresolver_offline.cpp` | Offline | Proves LocationResolver spatial and text matching: human-readable place label resolution (combining name, operator, street address), nearest place lookups, intersection parsing ("Street A and Street B"), and coordinate snapping to the nearest graph node. |
| `test_login_offline.cpp` | Offline | Proves Auth subsystem security and request handling: default admin account initialization, secure sign-up and password hashing, authentication verification, identical error messages on invalid username vs wrong password (preventing user enumeration), credential scrubbing (passwords never echoed in responses or logs), and session health probes. |
| `test_overpass.cpp` | Live | Proves live Overpass API query execution: query generation for emergency categories (e.g. hospitals near Eiffel Tower), live POST request over network, response reception, and parsing OSM nodes/ways into Place objects. |
| `test_overpass_offline.cpp` | Offline | Proves OverpassClient query building and parser resilience: category-to-tag mapping (Hospital, Fire Station, Police Station, Shelter), Overpass QL query construction, URL-encoded request body formulation, parsing canned node/way/relation responses, and handling invalid JSON. |
| `test_pipeline.cpp` | Live | Proves full live integration pipeline: geocoding an area name via Nominatim, fetching nearby facilities and drivable road network via Overpass, building graph, snapping facilities to graph nodes, computing shortest route distance, and exporting GeoJSON files. |
| `test_pipeline_offline.cpp` | Offline | Proves complete pipeline execution using deterministic canned data: cache loading, graph compilation, spatial snapping of emergency facilities to the nearest road vertices, Dijkstra routing between facilities, and GeoJSON export without network access. |
| `test_roadclient.cpp` | Live | Proves live road network fetching from Overpass API: executing live bounding box/radius queries for drivable roads, parsing highway elements, and returning RoadData. |
| `test_roadclient_offline.cpp` | Offline | Proves RoadClient query construction and drivable highway filtering: validating query syntax and coordinate boundary limits; filtering road tags (accepting residential, primary, service; rejecting footways, cycleways, tracks, private access, non-motorized ways); resilience against numeric tag values. |
| `test_service_offline.cpp` | Offline | Proves domain service entities: Ambulance, Fire Truck, Police Unit, Hospital, Fire Station, Police Station, and Shelter state machines; unit assignment/release; hospital bed capacity allocation; shelter occupancy tracking. |
| `test_ui_bridge_offline.cpp` | Offline | Proves UiBridge JSON protocol routing: auth endpoints (login, signup, session), area management (`area_status`, `create_area`), pipeline triggering, coordinate geocoding bridge actions, error handling and response formatting. |
| `test_units_offline.cpp` | Offline | Proves UnitRegistry operations and unit account integration: unit account creation rules, role-based permissions (preventing unauthorized admin creation), input validation (negative counts, coordinate boundaries), unit station association, and status queries. |

---

## 3. Running Offline Tests

The offline test suite requires **no internet connection** and runs completely self-contained. It is recommended for continuous integration, regression testing, and local development verification.

### Run All Offline Tests
To compile and execute all 21 offline test programs:
```bash
make offline
```

GNU Make will compile the library objects and test binaries under `build/`, then sequentially execute each `build/test_*_offline.exe`. If any test fails, execution halts immediately with a non-zero exit code.

### Run a Single Offline Test
To compile and run an individual offline test:
```bash
# Build the specific test binary
make test_dispatch_offline

# Run the test binary
./build/test_dispatch_offline.exe
```

---

## 4. Running Live Integration Tests

Live tests contact public OpenStreetMap services (Nominatim and Overpass API) over the internet.

### Prerequisites for Live Tests
1. **Network Connectivity**: Ensure outgoing HTTPS connections to `nominatim.openstreetmap.org` and `overpass-api.de` are permitted.
2. **Local Configuration**: Copy `include/Config.example.hpp` to `include/Config.local.hpp` and provide a valid contact email or project URL:
   ```cpp
   namespace rapidaid::config {
       constexpr const char* kContact = "operator@example.com";
   }
   ```
3. **OSM Rate Limits**: OpenStreetMap enforces a strict rate limit of no more than 1 request per second for Nominatim. Rapid Aid's `Geocoder` includes a 1-second delay, but live tests should not be looped or hammered in rapid succession.

### Manual Live Test Execution Commands

Each live test executable must be built and run manually:

```bash
# 1. Low-level HTTP Client GET/POST probe
make test_http
./build/test_http.exe

# 2. Live Nominatim geocoding lookup
make test_geocoder
./build/test_geocoder.exe

# 3. Live Overpass nearby emergency facilities search
make test_overpass
./build/test_overpass.exe

# 4. Live Overpass drivable road network fetch
make test_roadclient
./build/test_roadclient.exe

# 5. Full live pipeline (geocoding -> places -> roads -> graph -> GeoJSON)
make test_pipeline
./build/test_pipeline.exe
```
