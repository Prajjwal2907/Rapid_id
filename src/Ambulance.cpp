#include "Ambulance.hpp"

#include <stdexcept>
#include <utility>

namespace rapidaid::service {

Ambulance::Ambulance(int id, std::string name, double latitude, double longitude,
                     int homeStationId, int paramedicCount)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude),
      homeStationId(homeStationId),
      paramedicCount(paramedicCount) {
    if (paramedicCount < 0) {
        throw std::invalid_argument("paramedic count cannot be negative");
    }
}

int Ambulance::getId() const { return id; }
const std::string& Ambulance::getName() const { return name; }
double Ambulance::getLatitude() const { return latitude; }
double Ambulance::getLongitude() const { return longitude; }
long long Ambulance::getGraphNodeId() const { return graphNodeId; }
void Ambulance::setGraphNodeId(long long value) { graphNodeId = value; }
Ambulance::Status Ambulance::getStatus() const { return status; }
int Ambulance::getCurrentIncidentId() const { return currentIncidentId; }
int Ambulance::getHomeStationId() const { return homeStationId; }
int Ambulance::getParamedicCount() const { return paramedicCount; }
bool Ambulance::isAvailable() const { return status == Status::Available; }

bool Ambulance::assign(int incidentId) {
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

void Ambulance::release() {
    status = Status::Available;
    currentIncidentId = -1;
}

void Ambulance::markUnavailable() {
    status = Status::Unavailable;
    currentIncidentId = -1;
}

std::string Ambulance::statusToString(Status status) {
    switch (status) {
    case Status::Available: return "Available";
    case Status::Dispatched: return "Dispatched";
    case Status::Unavailable: return "Unavailable";
    }
    throw std::invalid_argument("invalid ambulance status");
}

}