#include "Incident.hpp"

namespace rapidaid::dispatch {

std::string unitKindName(UnitKind kind) {
    switch (kind) {
        case UnitKind::Ambulance:
            return "ambulance";
        case UnitKind::FireTruck:
            return "fire_truck";
        case UnitKind::PoliceUnit:
            return "police_unit";
        case UnitKind::Hospital:
            return "hospital";
        case UnitKind::Shelter:
            return "shelter";
    }
    return "";
}

bool parseUnitKind(const std::string& name, UnitKind& outKind) {
    if (name == "ambulance") {
        outKind = UnitKind::Ambulance;
        return true;
    }
    if (name == "fire_truck") {
        outKind = UnitKind::FireTruck;
        return true;
    }
    if (name == "police_unit") {
        outKind = UnitKind::PoliceUnit;
        return true;
    }
    if (name == "hospital") {
        outKind = UnitKind::Hospital;
        return true;
    }
    if (name == "shelter") {
        outKind = UnitKind::Shelter;
        return true;
    }
    return false;
}

double priorityScore(int severity, int people, double waitingMinutes) {
    return static_cast<double>(severity) * 10.0 +
           static_cast<double>(people) * 2.0 +
           waitingMinutes;
}

void to_json(nlohmann::json& j, const Assignment& a) {
    j = nlohmann::json{
        {"username", a.username},
        {"name", a.name},
        {"kind", a.kind},
        {"role", a.role},
        {"alertId", a.alertId},
        {"distanceMeters", a.distanceMeters},
        {"attempt", a.attempt},
        {"state", a.state}
    };
}

void from_json(const nlohmann::json& j, Assignment& a) {
    try {
        a.username = j.value("username", "");
        a.name = j.value("name", "");
        a.kind = j.value("kind", "");
        a.role = j.value("role", "");
        a.alertId = j.value("alertId", -1);
        a.distanceMeters = j.value("distanceMeters", 0.0);
        a.attempt = j.value("attempt", 1);
        a.state = j.value("state", "");
    } catch (...) {}
}

void to_json(nlohmann::json& j, const LogEntry& l) {
    j = nlohmann::json{
        {"time", l.time},
        {"text", l.text}
    };
}

void from_json(const nlohmann::json& j, LogEntry& l) {
    try {
        l.time = j.value("time", 0LL);
        l.text = j.value("text", "");
    } catch (...) {}
}

void to_json(nlohmann::json& j, const Incident& inc) {
    j = nlohmann::json{
        {"id", inc.id},
        {"type", inc.type},
        {"severity", inc.severity},
        {"peopleAffected", inc.peopleAffected},
        {"description", inc.description},
        {"callerName", inc.callerName},
        {"callerPhone", inc.callerPhone},
        {"locationText", inc.locationText},
        {"locationSource", inc.locationSource},
        {"lat", inc.lat},
        {"lon", inc.lon},
        {"snapMeters", inc.snapMeters},
        {"nodeOsmId", inc.nodeOsmId},
        {"createdAt", inc.createdAt},
        {"status", inc.status},
        {"assignments", inc.assignments},
        {"warnings", inc.warnings},
        {"log", inc.log}
    };
}

void from_json(const nlohmann::json& j, Incident& inc) {
    try {
        inc.id = j.value("id", 0);
        inc.type = j.value("type", "");
        inc.severity = j.value("severity", 3);
        inc.peopleAffected = j.value("peopleAffected", 0);
        inc.description = j.value("description", "");
        inc.callerName = j.value("callerName", "");
        inc.callerPhone = j.value("callerPhone", "");
        inc.locationText = j.value("locationText", "");
        inc.locationSource = j.value("locationSource", "");
        inc.lat = j.value("lat", 0.0);
        inc.lon = j.value("lon", 0.0);
        inc.snapMeters = j.value("snapMeters", 0.0);
        inc.nodeOsmId = j.value("nodeOsmId", 0LL);
        inc.createdAt = j.value("createdAt", 0LL);
        inc.status = j.value("status", "New");

        inc.assignments.clear();
        if (j.contains("assignments") && j["assignments"].is_array()) {
            for (const auto& item : j["assignments"]) {
                try {
                    inc.assignments.push_back(item.get<Assignment>());
                } catch (...) {}
            }
        }

        inc.warnings.clear();
        if (j.contains("warnings") && j["warnings"].is_array()) {
            for (const auto& item : j["warnings"]) {
                if (item.is_string()) {
                    inc.warnings.push_back(item.get<std::string>());
                }
            }
        }

        inc.log.clear();
        if (j.contains("log") && j["log"].is_array()) {
            for (const auto& item : j["log"]) {
                try {
                    inc.log.push_back(item.get<LogEntry>());
                } catch (...) {}
            }
        }
    } catch (...) {}
}

} // namespace rapidaid::dispatch
