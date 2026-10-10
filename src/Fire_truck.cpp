#include "Fire_truck.hpp"

#include <stdexcept>
#include <utility>

namespace rapidaid::service {

FireTruck::FireTruck(int id, std::string name, double latitude, double longitude,
                     int homeStationId, int waterCapacityLitres)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude),
      homeStationId(homeStationId),
      waterCapacityLitres(waterCapacityLitres) {
    if (waterCapacityLitres < 0) {
        throw std::invalid_argument("water capacity cannot be negative");
    }
}

int FireTruck::getId() const { return id; }
const std::string& FireTruck::getName() const { return name; }
double FireTruck::getLatitude() const { return latitude; }
double FireTruck::getLongitude() const { return longitude; }
long long FireTruck::getGraphNodeId() const { return graphNodeId; }
void FireTruck::setGraphNodeId(long long value) { graphNodeId = value; }
FireTruck::Status FireTruck::getStatus() const { return status; }
int FireTruck::getCurrentIncidentId() const { return currentIncidentId; }
int FireTruck::getHomeStationId() const { return homeStationId; }
int FireTruck::getWaterCapacityLitres() const { return waterCapacityLitres; }
bool FireTruck::isAvailable() const { return status == Status::Available; }

bool FireTruck::assign(int incidentId) {
    if (incidentId < 0) {
        throw std::invalid_argument("incident id cannot be negative");
    }
    if (!isAvailable()) {
        return false;
    }
    status = Status::Dispatched;
    currentIncidentId = incidentId;
    return true;
}

void FireTruck::release() {
    status = Status::Available;
    currentIncidentId = -1;
}

void FireTruck::markUnavailable() {
    status = Status::Unavailable;
    currentIncidentId = -1;
}

std::string FireTruck::statusToString(Status status) {
    switch (status) {
    case Status::Available: return "Available";
    case Status::Dispatched: return "Dispatched";
    case Status::Unavailable: return "Unavailable";
    }
    throw std::invalid_argument("invalid fire truck status");
}

}