#include "Police_unit.hpp"

#include <stdexcept>
#include <utility>

namespace rapidaid::service {

PoliceUnit::PoliceUnit(int id, std::string name, double latitude, double longitude,
                       int homeStationId, int officerCount)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude),
      homeStationId(homeStationId),
      officerCount(officerCount) {
    if (officerCount < 0) {
        throw std::invalid_argument("officer count cannot be negative");
    }
}

int PoliceUnit::getId() const { return id; }
const std::string& PoliceUnit::getName() const { return name; }
double PoliceUnit::getLatitude() const { return latitude; }
double PoliceUnit::getLongitude() const { return longitude; }
long long PoliceUnit::getGraphNodeId() const { return graphNodeId; }
void PoliceUnit::setGraphNodeId(long long value) { graphNodeId = value; }
PoliceUnit::Status PoliceUnit::getStatus() const { return status; }
int PoliceUnit::getCurrentIncidentId() const { return currentIncidentId; }
int PoliceUnit::getHomeStationId() const { return homeStationId; }
int PoliceUnit::getOfficerCount() const { return officerCount; }
bool PoliceUnit::isAvailable() const { return status == Status::Available; }

bool PoliceUnit::assign(int incidentId) {
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

void PoliceUnit::release() {
    status = Status::Available;
    currentIncidentId = -1;
}

void PoliceUnit::markUnavailable() {
    status = Status::Unavailable;
    currentIncidentId = -1;
}

std::string PoliceUnit::statusToString(Status status) {
    switch (status) {
    case Status::Available: return "Available";
    case Status::Dispatched: return "Dispatched";
    case Status::Unavailable: return "Unavailable";
    }
    throw std::invalid_argument("invalid police unit status");
}

}