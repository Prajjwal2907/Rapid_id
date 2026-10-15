#ifndef RAPIDAID_UI_UIBRIDGE_HPP
#define RAPIDAID_UI_UIBRIDGE_HPP

#include "DataCache.hpp"
#include "Login.hpp"

#include <string>

namespace rapidaid::ui {

/**
 * @brief Checks if permanent GeoJSON files exist in the specified output directory.
 * @param outDir Directory path where permanent GeoJSON files are stored (default "data/out").
 * @return true if either roads.geojson or places.geojson exists and is non-empty; false otherwise.
 * Failure behavior: Returns false if the directory does not exist or files cannot be accessed. Never throws.
 */
bool checkGeoJsonExists(const std::string& outDir = "data/out");

/**
 * @brief Returns JSON status of permanent GeoJSON files.
 * @param outDir Directory path where permanent GeoJSON files are stored (default "data/out").
 * @return JSON response string: {"ok":true,"exists":bool,"roadsExists":bool,"placesExists":bool,"roadsFile":string,"placesFile":string}
 * Failure behavior: Never throws; returns valid JSON even if paths do not exist.
 */
std::string handleAreaStatusRequest(const std::string& outDir = "data/out");

/**
 * @brief Geocodes an area query string into coordinates and display name.
 * @param cache Reference to initialized DataCache.
 * @param requestJson JSON payload: {"query":"Paris"} or {"name":"Paris"}
 * @return JSON response string:
 *         Success: {"ok":true,"query":"..","lat":..,"lon":..,"displayName":".."}
 *         Failure: {"ok":false,"error":"Location not found" or "Bad request"}
 * Failure behavior: Never throws; returns error JSON on malformed JSON or geocoding miss.
 */
std::string handleGeocodeRequest(rapidaid::data::DataCache& cache, const std::string& requestJson);

/**
 * @brief Executes the full area pipeline: geocode, fetch places, fetch roads, build graph, snap places, and save GeoJSON.
 * @param cache Reference to initialized DataCache.
 * @param requestJson JSON payload: {"name":"..", "radius":..., "category":"...", optional "lat":..., "lon":...}
 * @param outDir Directory path where output GeoJSON files will be permanently saved (default "data/out").
 * @return JSON response string with operation stats and generated GeoJSON.
 * Failure behavior: Never throws; returns {"ok":false,"error":"..."} on invalid inputs or pipeline failures.
 */
std::string handleCreateAreaRequest(rapidaid::data::DataCache& cache, const std::string& requestJson, const std::string& outDir = "data/out");

/**
 * @brief Reads and returns stored GeoJSON file content.
 * @param type Layer type ("roads" or "places").
 * @param outDir Directory path where output GeoJSON files are stored.
 * @return Raw GeoJSON string or error JSON.
 * Failure behavior: Never throws; returns {"ok":false,"error":"File not found"} if missing.
 */
std::string handleGetGeoJsonRequest(const std::string& type, const std::string& outDir = "data/out");

/**
 * @brief Returns incident report records and under-development notice.
 * @return JSON response string.
 * Failure behavior: Never throws.
 */
std::string handleGetIncidentsRequest();

/**
 * @brief Records a new incident report (mock backend with in-memory persistence).
 * @param requestJson JSON payload describing the incident.
 * @return JSON response string confirming receipt.
 * Failure behavior: Never throws; returns {"ok":false,"error":"Bad request"} on malformed JSON.
 */
std::string handleAddIncidentRequest(const std::string& requestJson);

/**
 * @brief Returns past dispatch statistics and under-development notice.
 * @return JSON response string.
 * Failure behavior: Never throws.
 */
std::string handleGetStatsRequest();

/**
 * @brief Unified router processing any frontend bridge command.
 * @param auth Reference to active Auth instance.
 * @param cache Reference to active DataCache instance.
 * @param messageJson JSON payload formatted as: {"action":"login"|"signup"|"logout"|"health"|"area_status"|"geocode"|"create_area"|"get_geojson"|"get_incidents"|"add_incident"|"get_stats", "payload":{...}}
 * @param outDir Output directory for permanent GeoJSON.
 * @return JSON response string for the requested action.
 * Failure behavior: Never throws; returns {"ok":false,"error":"Unknown action"} if action is unhandled.
 */
std::string handleBridgeMessage(Auth& auth, rapidaid::data::DataCache& cache, const std::string& messageJson, const std::string& outDir = "data/out");

} // namespace rapidaid::ui

#endif // RAPIDAID_UI_UIBRIDGE_HPP
