#ifndef RAPIDAID_UI_UNITREGISTRY_HPP
#define RAPIDAID_UI_UNITREGISTRY_HPP

#include "Ambulance.hpp"
#include "DispatchEngine.hpp"
#include "Fire_station.hpp"
#include "Fire_truck.hpp"
#include "Graph.hpp"
#include "Hospital.hpp"
#include "Incident.hpp"
#include "Police_station.hpp"
#include "Police_unit.hpp"
#include "Shelter.hpp"
#include "json.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rapidaid::ui {

/**
 * @struct UnitProfile
 * @brief Core profile configuration of a registered emergency service unit.
 */
struct UnitProfile {
    std::string username;
    std::string kind;
    std::string name;
    double lat{0.0};
    double lon{0.0};
    int count{0};
};

/**
 * @struct SubUnitInfo
 * @brief Metadata tracking child units (e.g., ambulances owned by hospitals, trucks by fire stations).
 */
struct SubUnitInfo {
    int id{0};
    std::string name;
    int count{0};
    bool available{true};
};

/**
 * @struct Alert
 * @brief Represents an emergency dispatch alert assigned to a unit.
 */
struct Alert {
    int id{0};
    std::string toUser;
    std::string type{"Medical"};
    int severity{3};
    int peopleAffected{0};
    std::string locationText;
    std::optional<double> lat;
    std::optional<double> lon;
    std::string note;
    int64_t createdAt{0};
    int ackSeconds{60};
    std::string state{"Pending"}; // Pending, Accepted, Declined, EnRoute, OnScene, Transporting, Cleared
    std::string reason;

    int incidentId{0};
    std::string role; // "primary", "standby", "support"
    double distanceMeters{0.0};
    std::vector<std::vector<double>> routeCoordinates; // [[lon, lat], ...]
};

/**
 * @brief Serializes Alert to JSON.
 * @param j JSON output object.
 * @param a Alert to serialize.
 * Failure behavior: Never throws.
 */
void to_json(nlohmann::json& j, const Alert& a);

/**
 * @brief Deserializes Alert from JSON with backwards compatibility.
 * @param j JSON input object.
 * @param a Alert to populate.
 * Failure behavior: Tolerates missing fields with default values. Never throws.
 */
void from_json(const nlohmann::json& j, Alert& a);

/**
 * @brief Serializes Alert to ordered_json.
 * @param j JSON output object.
 * @param a Alert to serialize.
 * Failure behavior: Never throws.
 */
void to_json(nlohmann::ordered_json& j, const Alert& a);

/**
 * @brief Deserializes Alert from ordered_json with backwards compatibility.
 * @param j JSON input object.
 * @param a Alert to populate.
 * Failure behavior: Tolerates missing fields with default values. Never throws.
 */
void from_json(const nlohmann::ordered_json& j, Alert& a);

/**
 * @struct RegisteredUnitRecord
 * @brief Encapsulates a unit's profile, domain service instance, owned sub-units, and alert pointer.
 */
struct RegisteredUnitRecord {
    UnitProfile profile;
    std::unique_ptr<rapidaid::service::Ambulance> ambulance;
    std::unique_ptr<rapidaid::service::FireTruck> fireTruck;
    std::unique_ptr<rapidaid::service::PoliceUnit> policeUnit;
    std::unique_ptr<rapidaid::service::Hospital> hospital;
    std::unique_ptr<rapidaid::service::FireStation> fireStation;
    std::unique_ptr<rapidaid::service::PoliceStation> policeStation;
    std::unique_ptr<rapidaid::service::Shelter> shelter;

    std::vector<SubUnitInfo> subunits;
    int nextSubunitId{1};
    int openAlertId{-1};

    /**
     * @brief Computes human-readable operational status string.
     * @return "Available", "Dispatched", or "Unavailable". Never throws.
     */
    std::string getStatusString() const;

    /**
     * @brief Checks if unit is currently capable of receiving alerts.
     * @return true if status is Available and no open alert exists; false otherwise. Never throws.
     */
    bool isAvailable() const;
};

/**
 * @class UnitRegistry
 * @brief Manages unit accounts, domain service model instances, and dispatch alerts.
 */
class UnitRegistry {
public:
    /**
     * @brief Constructs registry with target persistence JSON path.
     * @param dataPath File path to store unit configurations (default "data/units.json").
     * Failure behavior: Does not throw; lazy-loads on first query.
     */
    explicit UnitRegistry(std::string dataPath = "data/units.json");

    /**
     * @brief Sets file path for persistence.
     * @param path New target path.
     * Failure behavior: Does not throw; resets loaded state.
     */
    void setDataPath(const std::string& path);

    /**
     * @brief Ensures unit profiles are loaded from disk if not yet initialized.
     * Failure behavior: Tolerates missing or corrupted JSON files gracefully without throwing.
     */
    void ensureLoaded();

    /**
     * @brief Deserializes registered units from disk.
     * @param path Optional explicit file path.
     * @return true on successful load or benign missing file; false if file corrupt or unreadable. Never throws.
     */
    bool load(const std::string& path = "");

    /**
     * @brief Serializes registered units to disk using an atomic temp-file rename.
     * @param path Optional explicit file path.
     * @return true on success; false on write failure. Never throws.
     */
    bool save(const std::string& path = "") const;

    /**
     * @brief Clears all registered units and alerts from memory.
     * Failure behavior: Never throws.
     */
    void clear();

    /**
     * @brief Checks if a username has an active unit registration.
     * @param username Username to check.
     * @return true if unit exists; false otherwise. Never throws.
     */
    bool hasUnit(const std::string& username);

    /**
     * @brief Registers a new unit profile, initializes service model, and persists to disk.
     * @param profile Unit profile details.
     * @param errorMsg Output error message on validation or model failure.
     * @return true on successful registration; false on failure. Never throws.
     */
    bool registerUnit(const UnitProfile& profile, std::string& errorMsg);

    /**
     * @brief Retrieves unit profile by username.
     * @param username Unit username.
     * @return Optional UnitProfile containing details, or std::nullopt if not found. Never throws.
     */
    std::optional<UnitProfile> getProfile(const std::string& username);

    /**
     * @brief Toggles availability for a mobile service unit.
     * @param username Unit username.
     * @param available Desired availability state.
     * @param errorMsg Output error message on failure.
     * @return true on success; false if unit not mobile, dispatched, or not found. Never throws.
     */
    bool setAvailability(const std::string& username, bool available, std::string& errorMsg);

    /**
     * @brief Updates geographic location coordinates of a unit.
     * @param username Unit username.
     * @param lat Latitude in [-90, 90].
     * @param lon Longitude in [-180, 180].
     * @param errorMsg Output error message on failure.
     * @return true on success; false on invalid coordinates or unit not found. Never throws.
     */
    bool updateLocation(const std::string& username, double lat, double lon, std::string& errorMsg);

    /**
     * @brief Updates capacity or counts for facilities and mobile units.
     * @param username Unit username.
     * @param action Resource action (e.g., "admit", "discharge", "leave", or empty).
     * @param value Resource delta count or absolute value.
     * @param errorMsg Output error message on failure.
     * @return true on success; false on failure. Never throws.
     */
    bool updateResources(const std::string& username, const std::string& action, int value, std::string& errorMsg);

    /**
     * @brief Adds a child sub-unit to a facility (Hospital, FireStation, PoliceStation).
     * @param username Facility unit username.
     * @param name Name of sub-unit.
     * @param count Capacity or staffing of sub-unit.
     * @param errorMsg Output error message on failure.
     * @return Assigned sub-unit ID on success, or -1 on failure. Never throws.
     */
    int addSubunit(const std::string& username, const std::string& name, int count, std::string& errorMsg);

    /**
     * @brief Toggles availability of an owned sub-unit.
     * @param username Facility unit username.
     * @param subunitId ID of sub-unit.
     * @param available Desired availability state.
     * @param errorMsg Output error message on failure.
     * @return true on success; false on failure. Never throws.
     */
    bool setSubunitAvailability(const std::string& username, int subunitId, bool available, std::string& errorMsg);

    /**
     * @brief Produces JSON representation of unit's state, resources, sub-units, and alerts.
     * @param username Unit username.
     * @return JSON response object. Never throws.
     */
    nlohmann::ordered_json getUnitStateJson(const std::string& username);

    /**
     * @brief Lists all registered units with summary metrics for dispatchers.
     * @return JSON response object containing array of units. Never throws.
     */
    nlohmann::ordered_json listUnitsJson();

    /**
     * @brief Issues a test dispatch alert to an eligible unit.
     * @param alert Alert parameters.
     * @param errorMsg Output error message on failure.
     * @return Alert ID on success, or -1 on failure. Never throws.
     */
    int sendAlert(const Alert& alert, std::string& errorMsg);

    /**
     * @brief Processes an alert state transition from the receiving unit.
     * @param alertId Alert ID.
     * @param username Responding unit username.
     * @param action Requested state transition action.
     * @param reason Optional decline explanation.
     * @param errorMsg Output error message on failure.
     * @return true on success; false on invalid transition or unauthorized unit. Never throws.
     */
    bool respondToAlert(int alertId, const std::string& username, const std::string& action, const std::string& reason, std::string& errorMsg);

    /**
     * @brief Lists all alerts currently tracked in memory.
     * @return JSON response object containing alerts array. Never throws.
     */
    nlohmann::ordered_json listAlertsJson() const;

    /**
     * @brief Collects and evaluates all registered units as dispatch candidates against the graph.
     * @param graph Road network graph used to locate nearest road nodes.
     * @return Vector of Candidate objects. Never throws.
     * Failure behavior: For units further than 1500m from any graph node or if nearestNode returns -1,
     * candidate nodeIndex is set to -1.
     */
    std::vector<rapidaid::dispatch::Candidate> listCandidates(const rapidaid::graph::Graph& graph);

    /**
     * @brief Creates and sends an incident dispatch alert to a specific unit.
     * @param username Receiving unit username.
     * @param incidentId Associated incident ID.
     * @param role Dispatch role ("primary", "standby", "support").
     * @param distanceMeters Distance in meters along the road network.
     * @param routeCoordinates List of [lon, lat] coordinates forming the route polyline.
     * @param type Incident type (e.g., "Medical", "Fire", "Police").
     * @param severity Incident severity level (1-5).
     * @param peopleAffected Count of people requiring assistance.
     * @param locationText Human-readable location description.
     * @param lat Incident latitude.
     * @param lon Incident longitude.
     * @param note Optional dispatch note.
     * @param ackSeconds Timeout in seconds to acknowledge dispatch (default 60).
     * @return Alert instance on success, or std::nullopt if unit not found, busy, or unavailable. Never throws.
     * Failure behavior: Returns std::nullopt if the unit does not exist, is unavailable,
     * or already has an active pending/accepted alert.
     */
    std::optional<Alert> sendDispatchAlert(const std::string& username,
                                           int incidentId,
                                           const std::string& role,
                                           double distanceMeters,
                                           const std::vector<std::vector<double>>& routeCoordinates,
                                           const std::string& type,
                                           int severity,
                                           int peopleAffected,
                                           const std::string& locationText,
                                           double lat,
                                           double lon,
                                           const std::string& note,
                                           int ackSeconds = 60);

    /**
     * @brief Closes a specific alert with a terminal state and releases the assigned unit.
     * @param alertId Alert ID to close.
     * @param finalState Final state ("Declined", "Cleared", or "Cancelled").
     * @param reason Optional explanation for closure or decline.
     * @return true on success; false if alertId not found or finalState is invalid. Never throws.
     * Failure behavior: Returns false if alertId is not found or finalState is not a valid terminal state.
     */
    bool closeAlert(int alertId, const std::string& finalState, const std::string& reason);

    /**
     * @brief Closes all active alerts associated with a specific incident.
     * @param incidentId Incident ID whose active alerts should be closed.
     * @param reason Reason for closure (e.g. "Incident resolved").
     * @return Count of alerts closed (>= 0). Never throws.
     * Failure behavior: If no matching active alerts exist, returns 0 without modifying state.
     */
    int closeAlertsForIncident(int incidentId, const std::string& reason);

    /**
     * @brief Finds an alert by ID (const overload).
     * @param alertId Alert ID to look up.
     * @return Pointer to Alert if found; nullptr otherwise. Never throws.
     * Failure behavior: Returns nullptr if alertId is not found.
     */
    const Alert* findAlert(int alertId) const;

    /**
     * @brief Finds an alert by ID (non-const overload).
     * @param alertId Alert ID to look up.
     * @return Pointer to Alert if found; nullptr otherwise. Never throws.
     * Failure behavior: Returns nullptr if alertId is not found.
     */
    Alert* findAlert(int alertId);

    /**
     * @brief Retrieves all alerts associated with a specific incident ID.
     * @param incidentId Incident ID.
     * @return Vector of Alert instances matching incidentId. Never throws.
     * Failure behavior: Returns an empty vector if no alerts match incidentId.
     */
    std::vector<Alert> alertsForIncident(int incidentId) const;

private:
    std::string dataPath_;
    std::unordered_map<std::string, RegisteredUnitRecord> units_;
    std::vector<Alert> alerts_;
    int nextAlertId_{1};
    bool loaded_{false};

    void reinstantiateMobile(RegisteredUnitRecord& rec, int newCount);
    std::string getAlertsPath(const std::string& target) const;
    void reconcileAlertsAndUnits();
};

} // namespace rapidaid::ui

#endif // RAPIDAID_UI_UNITREGISTRY_HPP
