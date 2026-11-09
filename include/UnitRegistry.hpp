#ifndef RAPIDAID_UI_UNITREGISTRY_HPP
#define RAPIDAID_UI_UNITREGISTRY_HPP

#include "Ambulance.hpp"
#include "Fire_station.hpp"
#include "Fire_truck.hpp"
#include "Hospital.hpp"
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
};

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

private:
    std::string dataPath_;
    std::unordered_map<std::string, RegisteredUnitRecord> units_;
    std::vector<Alert> alerts_;
    int nextAlertId_{1};
    bool loaded_{false};

    void reinstantiateMobile(RegisteredUnitRecord& rec, int newCount);
};

} // namespace rapidaid::ui

#endif // RAPIDAID_UI_UNITREGISTRY_HPP
