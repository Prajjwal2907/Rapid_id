#ifndef RAPIDAID_DISPATCH_INCIDENT_HPP
#define RAPIDAID_DISPATCH_INCIDENT_HPP

#include "json.hpp"

#include <string>
#include <vector>

namespace rapidaid::dispatch {

// Represents the kind of emergency service unit.
enum class UnitKind {
    Ambulance,
    FireTruck,
    PoliceUnit,
    Hospital,
    Shelter
};

// Returns the lowercase string identifier for a given UnitKind.
// Failure behavior: Returns an empty string ("") if kind is not a known enumerator.
std::string unitKindName(UnitKind kind);

// Parses a lowercase string identifier into a UnitKind.
// Recognized strings: "ambulance", "fire_truck", "police_unit", "hospital", "shelter".
// Failure behavior: Returns false and leaves outKind unmodified if the string does not match.
bool parseUnitKind(const std::string& name, UnitKind& outKind);

// Represents the assignment of an emergency unit to an incident.
struct Assignment {
    std::string username;
    std::string name;
    std::string kind;
    std::string role;
    int alertId = -1;
    double distanceMeters = 0.0;
    int attempt = 1;
    std::string state; // mirrors alert state, or "Cancelled"
};

// Represents an audit or status update log entry on an incident.
struct LogEntry {
    long long time = 0;
    std::string text;
};

// Represents an emergency incident report and its dispatch lifecycle state.
struct Incident {
    int id = 0;
    std::string type;
    int severity = 3;
    int peopleAffected = 0;
    std::string description;
    std::string callerName;
    std::string callerPhone;
    std::string locationText;
    std::string locationSource;
    double lat = 0.0;
    double lon = 0.0;
    double snapMeters = 0.0;
    long long nodeOsmId = 0;
    long long createdAt = 0;
    std::string status = "New";
    std::vector<Assignment> assignments;
    std::vector<std::string> warnings;
    std::vector<LogEntry> log;
};

// Calculates priority score for incident queue ordering.
// Priority formula: severity * 10 + peopleAffected * 2 + waitingMinutes.
// Failure behavior: Never fails.
double priorityScore(int severity, int people, double waitingMinutes);

// JSON serialization helpers for Assignment, LogEntry, and Incident.
// Failure behavior: In from_json, missing or malformed fields fall back to default values; never throws.
void to_json(nlohmann::json& j, const Assignment& a);
void from_json(const nlohmann::json& j, Assignment& a);

void to_json(nlohmann::json& j, const LogEntry& l);
void from_json(const nlohmann::json& j, LogEntry& l);

void to_json(nlohmann::json& j, const Incident& inc);
void from_json(const nlohmann::json& j, Incident& inc);

} // namespace rapidaid::dispatch

#endif // RAPIDAID_DISPATCH_INCIDENT_HPP
