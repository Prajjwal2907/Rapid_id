#include "UnitRegistry.hpp"
#include "FileStore.hpp"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

namespace rapidaid::ui {

std::string RegisteredUnitRecord::getStatusString() const {
    if (profile.kind == "ambulance" && ambulance) {
        return rapidaid::service::Ambulance::statusToString(ambulance->getStatus());
    }
    if (profile.kind == "fire_truck" && fireTruck) {
        return rapidaid::service::FireTruck::statusToString(fireTruck->getStatus());
    }
    if (profile.kind == "police_unit" && policeUnit) {
        return rapidaid::service::PoliceUnit::statusToString(policeUnit->getStatus());
    }
    if (openAlertId != -1) {
        return "Dispatched";
    }
    if (profile.kind == "shelter" && shelter && shelter->isFull()) {
        return "Unavailable";
    }
    return "Available";
}

bool RegisteredUnitRecord::isAvailable() const {
    if (openAlertId != -1) {
        return false;
    }
    if (profile.kind == "ambulance" && ambulance) {
        return ambulance->isAvailable();
    }
    if (profile.kind == "fire_truck" && fireTruck) {
        return fireTruck->isAvailable();
    }
    if (profile.kind == "police_unit" && policeUnit) {
        return policeUnit->isAvailable();
    }
    if (profile.kind == "shelter" && shelter) {
        return !shelter->isFull();
    }
    return true;
}

UnitRegistry::UnitRegistry(std::string dataPath)
    : dataPath_(std::move(dataPath)) {}

void UnitRegistry::setDataPath(const std::string& path) {
    dataPath_ = path;
    loaded_ = false;
}

void UnitRegistry::ensureLoaded() {
    if (!loaded_) {
        load();
    }
}

void UnitRegistry::clear() {
    units_.clear();
    alerts_.clear();
    nextAlertId_ = 1;
    loaded_ = true;
}

bool UnitRegistry::load(const std::string& path) {
    std::string target = path.empty() ? dataPath_ : path;
    loaded_ = true;
    units_.clear();

    std::error_code ec;
    if (!fs::exists(target, ec)) {
        return true;
    }

    std::string content = rapidaid::io::FileStore::load(target);
    if (content.empty()) {
        return true;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(content);
        if (!j.is_array()) {
            return false;
        }

        for (const auto& item : j) {
            if (!item.is_object()) continue;
            UnitProfile prof;
            prof.username = item.value("username", "");
            prof.kind = item.value("kind", "");
            prof.name = item.value("name", "");
            prof.lat = item.value("lat", 0.0);
            prof.lon = item.value("lon", 0.0);
            prof.count = item.value("count", 0);

            if (prof.username.empty() || prof.kind.empty()) continue;

            RegisteredUnitRecord rec;
            rec.profile = prof;

            try {
                if (prof.kind == "ambulance") {
                    rec.ambulance = std::make_unique<rapidaid::service::Ambulance>(
                        1, prof.name, prof.lat, prof.lon, 0, prof.count);
                    std::string st = item.value("status", "Available");
                    if (st == "Unavailable") {
                        rec.ambulance->markUnavailable();
                    }
                } else if (prof.kind == "fire_truck") {
                    rec.fireTruck = std::make_unique<rapidaid::service::FireTruck>(
                        1, prof.name, prof.lat, prof.lon, 0, prof.count);
                    std::string st = item.value("status", "Available");
                    if (st == "Unavailable") {
                        rec.fireTruck->markUnavailable();
                    }
                } else if (prof.kind == "police_unit") {
                    rec.policeUnit = std::make_unique<rapidaid::service::PoliceUnit>(
                        1, prof.name, prof.lat, prof.lon, 0, prof.count);
                    std::string st = item.value("status", "Available");
                    if (st == "Unavailable") {
                        rec.policeUnit->markUnavailable();
                    }
                } else if (prof.kind == "hospital") {
                    int occ = item.value("occupiedBeds", 0);
                    rec.hospital = std::make_unique<rapidaid::service::Hospital>(
                        1, prof.name, prof.lat, prof.lon, prof.count, 0);
                    if (occ > 0) {
                        rec.hospital->admitPatients(occ);
                    }
                    if (item.contains("subunits") && item["subunits"].is_array()) {
                        for (const auto& sub : item["subunits"]) {
                            int sid = sub.value("id", rec.nextSubunitId++);
                            std::string sname = sub.value("name", "Ambulance");
                            int scount = sub.value("count", 0);
                            bool savail = sub.value("available", true);
                            rapidaid::service::Ambulance amb(sid, sname, prof.lat, prof.lon, 1, scount);
                            if (!savail) {
                                amb.markUnavailable();
                            }
                            rec.hospital->addAmbulance(std::move(amb));
                            rec.subunits.push_back({sid, sname, scount, savail});
                            if (sid >= rec.nextSubunitId) rec.nextSubunitId = sid + 1;
                        }
                    }
                } else if (prof.kind == "fire_station") {
                    rec.fireStation = std::make_unique<rapidaid::service::FireStation>(
                        1, prof.name, prof.lat, prof.lon);
                    if (item.contains("subunits") && item["subunits"].is_array()) {
                        for (const auto& sub : item["subunits"]) {
                            int sid = sub.value("id", rec.nextSubunitId++);
                            std::string sname = sub.value("name", "Truck");
                            int scount = sub.value("count", 0);
                            bool savail = sub.value("available", true);
                            rapidaid::service::FireTruck truck(sid, sname, prof.lat, prof.lon, 1, scount);
                            if (!savail) {
                                truck.markUnavailable();
                            }
                            rec.fireStation->addTruck(std::move(truck));
                            rec.subunits.push_back({sid, sname, scount, savail});
                            if (sid >= rec.nextSubunitId) rec.nextSubunitId = sid + 1;
                        }
                    }
                } else if (prof.kind == "police_station") {
                    rec.policeStation = std::make_unique<rapidaid::service::PoliceStation>(
                        1, prof.name, prof.lat, prof.lon);
                    if (item.contains("subunits") && item["subunits"].is_array()) {
                        for (const auto& sub : item["subunits"]) {
                            int sid = sub.value("id", rec.nextSubunitId++);
                            std::string sname = sub.value("name", "Unit");
                            int scount = sub.value("count", 0);
                            bool savail = sub.value("available", true);
                            rapidaid::service::PoliceUnit unit(sid, sname, prof.lat, prof.lon, 1, scount);
                            if (!savail) {
                                unit.markUnavailable();
                            }
                            rec.policeStation->addUnit(std::move(unit));
                            rec.subunits.push_back({sid, sname, scount, savail});
                            if (sid >= rec.nextSubunitId) rec.nextSubunitId = sid + 1;
                        }
                    }
                } else if (prof.kind == "shelter") {
                    int occ = item.value("occupied", 0);
                    rec.shelter = std::make_unique<rapidaid::service::Shelter>(
                        1, prof.name, prof.lat, prof.lon, prof.count, 0);
                    if (occ > 0) {
                        rec.shelter->admit(occ);
                    }
                }
                units_[prof.username] = std::move(rec);
            } catch (...) {
                // Ignore single malformed entry
            }
        }
        return true;
    } catch (...) {
        units_.clear();
        return false;
    }
}

bool UnitRegistry::save(const std::string& path) const {
    std::string target = path.empty() ? dataPath_ : path;
    try {
        nlohmann::ordered_json root = nlohmann::json::array();
        for (const auto& [u, rec] : units_) {
            (void)u;
            nlohmann::ordered_json j;
            j["username"] = rec.profile.username;
            j["kind"] = rec.profile.kind;
            j["name"] = rec.profile.name;
            j["lat"] = rec.profile.lat;
            j["lon"] = rec.profile.lon;
            j["count"] = rec.profile.count;
            j["status"] = rec.getStatusString();

            if (rec.profile.kind == "hospital" && rec.hospital) {
                j["occupiedBeds"] = rec.hospital->getOccupiedBeds();
            } else if (rec.profile.kind == "shelter" && rec.shelter) {
                j["occupied"] = rec.shelter->getOccupied();
            }

            if (!rec.subunits.empty()) {
                nlohmann::ordered_json subs = nlohmann::json::array();
                for (const auto& sub : rec.subunits) {
                    subs.push_back({
                        {"id", sub.id},
                        {"name", sub.name},
                        {"count", sub.count},
                        {"available", sub.available}
                    });
                }
                j["subunits"] = subs;
            }
            root.push_back(j);
        }

        fs::path p(target);
        std::error_code ec;
        if (p.has_parent_path()) {
            fs::create_directories(p.parent_path(), ec);
        }

        std::string tmpFile = target + ".tmp";
        std::ofstream out(tmpFile, std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << root.dump(2);
        out.close();

        fs::rename(tmpFile, target, ec);
        return !ec;
    } catch (...) {
        return false;
    }
}

bool UnitRegistry::hasUnit(const std::string& username) {
    ensureLoaded();
    return units_.find(username) != units_.end();
}

std::optional<UnitProfile> UnitRegistry::getProfile(const std::string& username) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        return std::nullopt;
    }
    return it->second.profile;
}

bool UnitRegistry::registerUnit(const UnitProfile& profile, std::string& errorMsg) {
    ensureLoaded();

    if (profile.username.empty()) {
        errorMsg = "Username cannot be empty";
        return false;
    }
    if (std::any_of(profile.username.begin(), profile.username.end(), [](unsigned char c) { return std::isspace(c); })) {
        errorMsg = "Username cannot contain whitespace";
        return false;
    }
    if (profile.name.empty()) {
        errorMsg = "Display name cannot be empty";
        return false;
    }
    if (profile.lat < -90.0 || profile.lat > 90.0) {
        errorMsg = "Latitude must be between -90 and 90";
        return false;
    }
    if (profile.lon < -180.0 || profile.lon > 180.0) {
        errorMsg = "Longitude must be between -180 and 180";
        return false;
    }
    if (profile.count < 0) {
        errorMsg = "Resource count cannot be negative";
        return false;
    }

    const std::vector<std::string> validKinds = {
        "ambulance", "fire_truck", "police_unit", "hospital", "fire_station", "police_station", "shelter"
    };
    if (std::find(validKinds.begin(), validKinds.end(), profile.kind) == validKinds.end()) {
        errorMsg = "Unknown unit kind: " + profile.kind;
        return false;
    }

    if (units_.find(profile.username) != units_.end()) {
        errorMsg = "Unit username already exists";
        return false;
    }

    RegisteredUnitRecord rec;
    rec.profile = profile;

    try {
        if (profile.kind == "ambulance") {
            rec.ambulance = std::make_unique<rapidaid::service::Ambulance>(
                1, profile.name, profile.lat, profile.lon, 0, profile.count);
        } else if (profile.kind == "fire_truck") {
            rec.fireTruck = std::make_unique<rapidaid::service::FireTruck>(
                1, profile.name, profile.lat, profile.lon, 0, profile.count);
        } else if (profile.kind == "police_unit") {
            rec.policeUnit = std::make_unique<rapidaid::service::PoliceUnit>(
                1, profile.name, profile.lat, profile.lon, 0, profile.count);
        } else if (profile.kind == "hospital") {
            rec.hospital = std::make_unique<rapidaid::service::Hospital>(
                1, profile.name, profile.lat, profile.lon, profile.count, 0);
        } else if (profile.kind == "fire_station") {
            rec.fireStation = std::make_unique<rapidaid::service::FireStation>(
                1, profile.name, profile.lat, profile.lon);
        } else if (profile.kind == "police_station") {
            rec.policeStation = std::make_unique<rapidaid::service::PoliceStation>(
                1, profile.name, profile.lat, profile.lon);
        } else if (profile.kind == "shelter") {
            rec.shelter = std::make_unique<rapidaid::service::Shelter>(
                1, profile.name, profile.lat, profile.lon, profile.count, 0);
        }
    } catch (const std::invalid_argument& e) {
        errorMsg = e.what();
        return false;
    } catch (...) {
        errorMsg = "Failed to construct unit model";
        return false;
    }

    units_[profile.username] = std::move(rec);
    save();
    return true;
}

bool UnitRegistry::setAvailability(const std::string& username, bool available, std::string& errorMsg) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        errorMsg = "Unit not found";
        return false;
    }

    RegisteredUnitRecord& rec = it->second;
    if (rec.profile.kind == "ambulance" && rec.ambulance) {
        if (rec.ambulance->getStatus() == rapidaid::service::Ambulance::Status::Dispatched) {
            errorMsg = "Cannot change availability while dispatched";
            return false;
        }
        if (available) {
            rec.ambulance->release();
        } else {
            rec.ambulance->markUnavailable();
        }
    } else if (rec.profile.kind == "fire_truck" && rec.fireTruck) {
        if (rec.fireTruck->getStatus() == rapidaid::service::FireTruck::Status::Dispatched) {
            errorMsg = "Cannot change availability while dispatched";
            return false;
        }
        if (available) {
            rec.fireTruck->release();
        } else {
            rec.fireTruck->markUnavailable();
        }
    } else if (rec.profile.kind == "police_unit" && rec.policeUnit) {
        if (rec.policeUnit->getStatus() == rapidaid::service::PoliceUnit::Status::Dispatched) {
            errorMsg = "Cannot change availability while dispatched";
            return false;
        }
        if (available) {
            rec.policeUnit->release();
        } else {
            rec.policeUnit->markUnavailable();
        }
    } else {
        errorMsg = "Only mobile service units support availability toggling";
        return false;
    }

    save();
    return true;
}

bool UnitRegistry::updateLocation(const std::string& username, double lat, double lon, std::string& errorMsg) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        errorMsg = "Unit not found";
        return false;
    }

    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        errorMsg = "Coordinates out of valid range";
        return false;
    }

    it->second.profile.lat = lat;
    it->second.profile.lon = lon;
    save();
    return true;
}

void UnitRegistry::reinstantiateMobile(RegisteredUnitRecord& rec, int newCount) {
    rec.profile.count = newCount;
    if (rec.profile.kind == "ambulance" && rec.ambulance) {
        auto oldStatus = rec.ambulance->getStatus();
        int incId = rec.ambulance->getCurrentIncidentId();
        rec.ambulance = std::make_unique<rapidaid::service::Ambulance>(
            1, rec.profile.name, rec.profile.lat, rec.profile.lon, 0, newCount);
        if (oldStatus == rapidaid::service::Ambulance::Status::Dispatched && incId >= 0) {
            rec.ambulance->assign(incId);
        } else if (oldStatus == rapidaid::service::Ambulance::Status::Unavailable) {
            rec.ambulance->markUnavailable();
        }
    } else if (rec.profile.kind == "fire_truck" && rec.fireTruck) {
        auto oldStatus = rec.fireTruck->getStatus();
        int incId = rec.fireTruck->getCurrentIncidentId();
        rec.fireTruck = std::make_unique<rapidaid::service::FireTruck>(
            1, rec.profile.name, rec.profile.lat, rec.profile.lon, 0, newCount);
        if (oldStatus == rapidaid::service::FireTruck::Status::Dispatched && incId >= 0) {
            rec.fireTruck->assign(incId);
        } else if (oldStatus == rapidaid::service::FireTruck::Status::Unavailable) {
            rec.fireTruck->markUnavailable();
        }
    } else if (rec.profile.kind == "police_unit" && rec.policeUnit) {
        auto oldStatus = rec.policeUnit->getStatus();
        int incId = rec.policeUnit->getCurrentIncidentId();
        rec.policeUnit = std::make_unique<rapidaid::service::PoliceUnit>(
            1, rec.profile.name, rec.profile.lat, rec.profile.lon, 0, newCount);
        if (oldStatus == rapidaid::service::PoliceUnit::Status::Dispatched && incId >= 0) {
            rec.policeUnit->assign(incId);
        } else if (oldStatus == rapidaid::service::PoliceUnit::Status::Unavailable) {
            rec.policeUnit->markUnavailable();
        }
    }
}

bool UnitRegistry::updateResources(const std::string& username, const std::string& action, int value, std::string& errorMsg) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        errorMsg = "Unit not found";
        return false;
    }

    RegisteredUnitRecord& rec = it->second;
    try {
        if (rec.profile.kind == "hospital" && rec.hospital) {
            if (value < 0) {
                errorMsg = "Count cannot be negative";
                return false;
            }
            if (action == "admit") {
                if (!rec.hospital->admitPatients(value)) {
                    errorMsg = "Not enough available beds";
                    return false;
                }
            } else if (action == "discharge") {
                if (value > rec.hospital->getOccupiedBeds()) {
                    errorMsg = "Cannot discharge more patients than currently occupied";
                    return false;
                }
                rec.hospital->dischargePatients(value);
            } else {
                errorMsg = "Unknown action for hospital: must be admit or discharge";
                return false;
            }
        } else if (rec.profile.kind == "shelter" && rec.shelter) {
            if (value < 0) {
                errorMsg = "Count cannot be negative";
                return false;
            }
            if (action == "admit") {
                if (!rec.shelter->admit(value)) {
                    errorMsg = "Admission exceeds shelter capacity";
                    return false;
                }
            } else if (action == "leave") {
                if (value > rec.shelter->getOccupied()) {
                    errorMsg = "Cannot leave more occupants than currently in shelter";
                    return false;
                }
                rec.shelter->leave(value);
            } else {
                errorMsg = "Unknown action for shelter: must be admit or leave";
                return false;
            }
        } else if (rec.profile.kind == "ambulance" || rec.profile.kind == "fire_truck" || rec.profile.kind == "police_unit") {
            if (value < 0) {
                errorMsg = "Resource count cannot be negative";
                return false;
            }
            reinstantiateMobile(rec, value);
        } else {
            errorMsg = "Resource updates not supported for this unit";
            return false;
        }
    } catch (const std::invalid_argument& e) {
        errorMsg = e.what();
        return false;
    }

    save();
    return true;
}

int UnitRegistry::addSubunit(const std::string& username, const std::string& name, int count, std::string& errorMsg) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        errorMsg = "Unit not found";
        return -1;
    }

    RegisteredUnitRecord& rec = it->second;
    if (name.empty()) {
        errorMsg = "Subunit name cannot be empty";
        return -1;
    }
    if (count < 0) {
        errorMsg = "Count cannot be negative";
        return -1;
    }

    int subId = rec.nextSubunitId++;
    try {
        if (rec.profile.kind == "hospital" && rec.hospital) {
            rapidaid::service::Ambulance amb(subId, name, rec.profile.lat, rec.profile.lon, 1, count);
            rec.hospital->addAmbulance(std::move(amb));
        } else if (rec.profile.kind == "fire_station" && rec.fireStation) {
            rapidaid::service::FireTruck truck(subId, name, rec.profile.lat, rec.profile.lon, 1, count);
            rec.fireStation->addTruck(std::move(truck));
        } else if (rec.profile.kind == "police_station" && rec.policeStation) {
            rapidaid::service::PoliceUnit unit(subId, name, rec.profile.lat, rec.profile.lon, 1, count);
            rec.policeStation->addUnit(std::move(unit));
        } else {
            errorMsg = "This unit kind does not support child subunits";
            return -1;
        }
    } catch (const std::invalid_argument& e) {
        errorMsg = e.what();
        return -1;
    }

    rec.subunits.push_back({subId, name, count, true});
    save();
    return subId;
}

bool UnitRegistry::setSubunitAvailability(const std::string& username, int subunitId, bool available, std::string& errorMsg) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        errorMsg = "Unit not found";
        return false;
    }

    RegisteredUnitRecord& rec = it->second;
    auto subIt = std::find_if(rec.subunits.begin(), rec.subunits.end(), [subunitId](const SubUnitInfo& s) {
        return s.id == subunitId;
    });
    if (subIt == rec.subunits.end()) {
        errorMsg = "Subunit not found";
        return false;
    }

    if (rec.profile.kind == "hospital" && rec.hospital) {
        rapidaid::service::Ambulance* amb = rec.hospital->findAmbulanceById(subunitId);
        if (!amb) {
            errorMsg = "Ambulance subunit not found";
            return false;
        }
        if (available) amb->release();
        else amb->markUnavailable();
    } else if (rec.profile.kind == "fire_station" && rec.fireStation) {
        rapidaid::service::FireTruck* truck = rec.fireStation->findTruckById(subunitId);
        if (!truck) {
            errorMsg = "Truck subunit not found";
            return false;
        }
        if (available) truck->release();
        else truck->markUnavailable();
    } else if (rec.profile.kind == "police_station" && rec.policeStation) {
        rapidaid::service::PoliceUnit* u = rec.policeStation->findUnitById(subunitId);
        if (!u) {
            errorMsg = "Police subunit not found";
            return false;
        }
        if (available) u->release();
        else u->markUnavailable();
    } else {
        errorMsg = "Unit does not manage subunits";
        return false;
    }

    subIt->available = available;
    save();
    return true;
}

nlohmann::ordered_json UnitRegistry::getUnitStateJson(const std::string& username) {
    ensureLoaded();
    auto it = units_.find(username);
    if (it == units_.end()) {
        return {{"ok", false}, {"error", "Unit not found"}};
    }

    RegisteredUnitRecord& rec = it->second;
    nlohmann::ordered_json resp;
    resp["ok"] = true;

    nlohmann::ordered_json prof;
    prof["username"] = rec.profile.username;
    prof["kind"] = rec.profile.kind;
    prof["name"] = rec.profile.name;
    prof["lat"] = rec.profile.lat;
    prof["lon"] = rec.profile.lon;
    prof["count"] = rec.profile.count;
    resp["profile"] = prof;

    resp["status"] = rec.getStatusString();

    nlohmann::ordered_json resources;
    if (rec.profile.kind == "hospital" && rec.hospital) {
        resources["totalBeds"] = rec.hospital->getTotalBeds();
        resources["occupiedBeds"] = rec.hospital->getOccupiedBeds();
        resources["availableBeds"] = rec.hospital->availableBeds();
    } else if (rec.profile.kind == "shelter" && rec.shelter) {
        resources["capacity"] = rec.shelter->getCapacity();
        resources["occupied"] = rec.shelter->getOccupied();
        resources["availableSpace"] = rec.shelter->availableSpace();
        resources["isFull"] = rec.shelter->isFull();
    } else if (rec.profile.kind == "ambulance" && rec.ambulance) {
        resources["paramedics"] = rec.ambulance->getParamedicCount();
    } else if (rec.profile.kind == "fire_truck" && rec.fireTruck) {
        resources["waterLitres"] = rec.fireTruck->getWaterCapacityLitres();
    } else if (rec.profile.kind == "police_unit" && rec.policeUnit) {
        resources["officers"] = rec.policeUnit->getOfficerCount();
    }
    resp["resources"] = resources;

    nlohmann::ordered_json subs = nlohmann::json::array();
    for (const auto& s : rec.subunits) {
        std::string subStatus = s.available ? "Available" : "Unavailable";
        if (rec.profile.kind == "hospital" && rec.hospital) {
            auto* amb = rec.hospital->findAmbulanceById(s.id);
            if (amb) subStatus = rapidaid::service::Ambulance::statusToString(amb->getStatus());
        } else if (rec.profile.kind == "fire_station" && rec.fireStation) {
            auto* tr = rec.fireStation->findTruckById(s.id);
            if (tr) subStatus = rapidaid::service::FireTruck::statusToString(tr->getStatus());
        } else if (rec.profile.kind == "police_station" && rec.policeStation) {
            auto* pu = rec.policeStation->findUnitById(s.id);
            if (pu) subStatus = rapidaid::service::PoliceUnit::statusToString(pu->getStatus());
        }
        subs.push_back({
            {"id", s.id},
            {"name", s.name},
            {"count", s.count},
            {"available", s.available},
            {"status", subStatus}
        });
    }
    resp["subunits"] = subs;

    // Open alert
    int64_t now = std::time(nullptr);
    Alert* openA = nullptr;
    for (auto& a : alerts_) {
        if (a.toUser == username && a.state != "Declined" && a.state != "Cleared") {
            openA = &a;
            break;
        }
    }

    if (openA) {
        int elapsed = static_cast<int>(now - openA->createdAt);
        int secondsLeft = std::max(0, openA->ackSeconds - elapsed);
        nlohmann::ordered_json aj;
        aj["id"] = openA->id;
        aj["toUser"] = openA->toUser;
        aj["type"] = openA->type;
        aj["severity"] = openA->severity;
        aj["peopleAffected"] = openA->peopleAffected;
        aj["locationText"] = openA->locationText;
        if (openA->lat.has_value()) aj["lat"] = *openA->lat;
        if (openA->lon.has_value()) aj["lon"] = *openA->lon;
        aj["note"] = openA->note;
        aj["createdAt"] = openA->createdAt;
        aj["ackSeconds"] = openA->ackSeconds;
        aj["secondsLeft"] = secondsLeft;
        aj["state"] = openA->state;
        if (!openA->reason.empty()) aj["reason"] = openA->reason;
        resp["openAlert"] = aj;
    } else {
        resp["openAlert"] = nullptr;
    }

    // Recent 5 closed alerts (Declined or Cleared)
    nlohmann::ordered_json recents = nlohmann::json::array();
    int countClosed = 0;
    for (auto itA = alerts_.rbegin(); itA != alerts_.rend(); ++itA) {
        if (itA->toUser == username && (itA->state == "Declined" || itA->state == "Cleared")) {
            nlohmann::ordered_json cj;
            cj["id"] = itA->id;
            cj["toUser"] = itA->toUser;
            cj["type"] = itA->type;
            cj["severity"] = itA->severity;
            cj["peopleAffected"] = itA->peopleAffected;
            cj["locationText"] = itA->locationText;
            if (itA->lat.has_value()) cj["lat"] = *itA->lat;
            if (itA->lon.has_value()) cj["lon"] = *itA->lon;
            cj["note"] = itA->note;
            cj["createdAt"] = itA->createdAt;
            cj["state"] = itA->state;
            if (!itA->reason.empty()) cj["reason"] = itA->reason;
            recents.push_back(cj);
            countClosed++;
            if (countClosed >= 5) break;
        }
    }
    resp["recentAlerts"] = recents;

    return resp;
}

nlohmann::ordered_json UnitRegistry::listUnitsJson() {
    ensureLoaded();
    nlohmann::ordered_json list = nlohmann::json::array();

    for (const auto& [username, rec] : units_) {
        nlohmann::ordered_json u;
        u["username"] = username;
        u["kind"] = rec.profile.kind;
        u["name"] = rec.profile.name;
        u["status"] = rec.getStatusString();
        u["lat"] = rec.profile.lat;
        u["lon"] = rec.profile.lon;

        int availCount = 0;
        if (rec.profile.kind == "ambulance" && rec.ambulance) {
            availCount = rec.ambulance->getParamedicCount();
        } else if (rec.profile.kind == "fire_truck" && rec.fireTruck) {
            availCount = rec.fireTruck->getWaterCapacityLitres();
        } else if (rec.profile.kind == "police_unit" && rec.policeUnit) {
            availCount = rec.policeUnit->getOfficerCount();
        } else if (rec.profile.kind == "hospital" && rec.hospital) {
            availCount = rec.hospital->availableBeds();
        } else if (rec.profile.kind == "shelter" && rec.shelter) {
            availCount = rec.shelter->availableSpace();
        } else if (rec.profile.kind == "fire_station" && rec.fireStation) {
            availCount = static_cast<int>(rec.fireStation->getAvailableTrucks().size());
        } else if (rec.profile.kind == "police_station" && rec.policeStation) {
            availCount = static_cast<int>(rec.policeStation->getAvailableUnits().size());
        }
        u["availableCount"] = availCount;
        list.push_back(u);
    }

    nlohmann::ordered_json resp;
    resp["ok"] = true;
    resp["units"] = list;
    return resp;
}

int UnitRegistry::sendAlert(const Alert& alert, std::string& errorMsg) {
    ensureLoaded();
    auto it = units_.find(alert.toUser);
    if (it == units_.end()) {
        errorMsg = "Target unit '" + alert.toUser + "' not found";
        return -1;
    }

    RegisteredUnitRecord& rec = it->second;

    // Check open alert
    for (const auto& a : alerts_) {
        if (a.toUser == alert.toUser && a.state != "Declined" && a.state != "Cleared") {
            errorMsg = "Unit already has an active alert in progress";
            return -1;
        }
    }

    // Check available and not dispatched
    if (!rec.isAvailable()) {
        errorMsg = "Target unit is unavailable or already dispatched";
        return -1;
    }

    Alert newA = alert;
    newA.id = nextAlertId_++;
    newA.state = "Pending";
    newA.ackSeconds = (alert.ackSeconds > 0) ? alert.ackSeconds : 60;
    newA.createdAt = (alert.createdAt > 0) ? alert.createdAt : std::time(nullptr);

    rec.openAlertId = newA.id;
    alerts_.push_back(newA);
    return newA.id;
}

bool UnitRegistry::respondToAlert(int alertId, const std::string& username, const std::string& action, const std::string& reason, std::string& errorMsg) {
    ensureLoaded();

    auto alertIt = std::find_if(alerts_.begin(), alerts_.end(), [alertId](const Alert& a) {
        return a.id == alertId;
    });
    if (alertIt == alerts_.end()) {
        errorMsg = "Alert not found";
        return false;
    }

    Alert& alert = *alertIt;
    if (alert.toUser != username) {
        errorMsg = "Not allowed: cannot respond to alert assigned to another unit";
        return false;
    }

    auto unitIt = units_.find(username);
    if (unitIt == units_.end()) {
        errorMsg = "Receiving unit not found";
        return false;
    }

    RegisteredUnitRecord& rec = unitIt->second;
    bool isMobile = (rec.profile.kind == "ambulance" || rec.profile.kind == "fire_truck" || rec.profile.kind == "police_unit");

    if (alert.state == "Pending") {
        if (action == "accept") {
            try {
                if (rec.profile.kind == "ambulance" && rec.ambulance) {
                    rec.ambulance->assign(alertId);
                } else if (rec.profile.kind == "fire_truck" && rec.fireTruck) {
                    rec.fireTruck->assign(alertId);
                } else if (rec.profile.kind == "police_unit" && rec.policeUnit) {
                    rec.policeUnit->assign(alertId);
                }
            } catch (const std::invalid_argument& e) {
                errorMsg = e.what();
                return false;
            }
            alert.state = "Accepted";
            rec.openAlertId = alert.id;
        } else if (action == "decline") {
            alert.state = "Declined";
            alert.reason = reason;
            rec.openAlertId = -1;
        } else {
            errorMsg = "Invalid transition from Pending: must be accept or decline";
            return false;
        }
    } else if (alert.state == "Accepted") {
        if (action == "clear") {
            if (rec.profile.kind == "ambulance" && rec.ambulance) {
                rec.ambulance->release();
            } else if (rec.profile.kind == "fire_truck" && rec.fireTruck) {
                rec.fireTruck->release();
            } else if (rec.profile.kind == "police_unit" && rec.policeUnit) {
                rec.policeUnit->release();
            }
            alert.state = "Cleared";
            rec.openAlertId = -1;
        } else if (isMobile && action == "en_route") {
            alert.state = "EnRoute";
        } else {
            errorMsg = isMobile ? "Invalid transition from Accepted: must be en_route or clear"
                                : "Invalid transition from Accepted: facility units can only clear";
            return false;
        }
    } else if (alert.state == "EnRoute") {
        if (!isMobile) {
            errorMsg = "Facility units cannot be in EnRoute state";
            return false;
        }
        if (action == "on_scene") {
            alert.state = "OnScene";
        } else if (action == "clear") {
            if (rec.profile.kind == "ambulance" && rec.ambulance) rec.ambulance->release();
            else if (rec.profile.kind == "fire_truck" && rec.fireTruck) rec.fireTruck->release();
            else if (rec.profile.kind == "police_unit" && rec.policeUnit) rec.policeUnit->release();
            alert.state = "Cleared";
            rec.openAlertId = -1;
        } else {
            errorMsg = "Invalid transition from EnRoute: must be on_scene or clear";
            return false;
        }
    } else if (alert.state == "OnScene") {
        if (!isMobile) {
            errorMsg = "Facility units cannot be in OnScene state";
            return false;
        }
        if (action == "transporting") {
            if (rec.profile.kind != "ambulance") {
                errorMsg = "Only ambulances can enter transporting state";
                return false;
            }
            alert.state = "Transporting";
        } else if (action == "clear") {
            if (rec.profile.kind == "ambulance" && rec.ambulance) rec.ambulance->release();
            else if (rec.profile.kind == "fire_truck" && rec.fireTruck) rec.fireTruck->release();
            else if (rec.profile.kind == "police_unit" && rec.policeUnit) rec.policeUnit->release();
            alert.state = "Cleared";
            rec.openAlertId = -1;
        } else {
            errorMsg = (rec.profile.kind == "ambulance") ? "Invalid transition from OnScene: must be transporting or clear"
                                                         : "Invalid transition from OnScene: must be clear";
            return false;
        }
    } else if (alert.state == "Transporting") {
        if (action == "clear") {
            if (rec.ambulance) rec.ambulance->release();
            alert.state = "Cleared";
            rec.openAlertId = -1;
        } else {
            errorMsg = "Invalid transition from Transporting: must be clear";
            return false;
        }
    } else {
        errorMsg = "Cannot respond to alert in final state (" + alert.state + ")";
        return false;
    }

    save();
    return true;
}

nlohmann::ordered_json UnitRegistry::listAlertsJson() const {
    nlohmann::ordered_json list = nlohmann::json::array();
    for (const auto& a : alerts_) {
        nlohmann::ordered_json aj;
        aj["id"] = a.id;
        aj["toUser"] = a.toUser;
        aj["type"] = a.type;
        aj["severity"] = a.severity;
        aj["peopleAffected"] = a.peopleAffected;
        aj["locationText"] = a.locationText;
        if (a.lat.has_value()) aj["lat"] = *a.lat;
        if (a.lon.has_value()) aj["lon"] = *a.lon;
        aj["note"] = a.note;
        aj["createdAt"] = a.createdAt;
        aj["ackSeconds"] = a.ackSeconds;
        aj["state"] = a.state;
        if (!a.reason.empty()) aj["reason"] = a.reason;
        list.push_back(aj);
    }

    nlohmann::ordered_json resp;
    resp["ok"] = true;
    resp["alerts"] = list;
    return resp;
}

} // namespace rapidaid::ui
