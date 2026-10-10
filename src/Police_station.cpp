#include "Police_station.hpp"

#include <utility>

namespace rapidaid::service {

PoliceStation::PoliceStation(int id, std::string name, double latitude, double longitude)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude) {}

int PoliceStation::getId() const { return id; }
const std::string& PoliceStation::getName() const { return name; }
double PoliceStation::getLatitude() const { return latitude; }
double PoliceStation::getLongitude() const { return longitude; }
long long PoliceStation::getGraphNodeId() const { return graphNodeId; }
void PoliceStation::setGraphNodeId(long long value) { graphNodeId = value; }

void PoliceStation::addUnit(PoliceUnit unit) {
    units.push_back(std::move(unit));
}

std::vector<PoliceUnit*> PoliceStation::getAvailableUnits() {
    std::vector<PoliceUnit*> available;
    for (PoliceUnit& unit : units) {
        if (unit.isAvailable()) {
            available.push_back(&unit);
        }
    }
    return available;
}

PoliceUnit* PoliceStation::findUnitById(int unitId) {
    for (PoliceUnit& unit : units) {
        if (unit.getId() == unitId) {
            return &unit;
        }
    }
    return nullptr;
}

}
