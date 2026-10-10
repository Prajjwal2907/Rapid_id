#include "Hospital.hpp"

#include <stdexcept>
#include <utility>

namespace rapidaid::service {

Hospital::Hospital(int id, std::string name, double latitude, double longitude,
                   int totalBeds, int occupiedBeds)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude),
      totalBeds(totalBeds),
      occupiedBeds(occupiedBeds) {
    if (totalBeds < 0 || occupiedBeds < 0 || occupiedBeds > totalBeds) {
        throw std::invalid_argument("hospital bed counts are invalid");
    }
}

int Hospital::getId() const { return id; }
const std::string& Hospital::getName() const { return name; }
double Hospital::getLatitude() const { return latitude; }
double Hospital::getLongitude() const { return longitude; }
long long Hospital::getGraphNodeId() const { return graphNodeId; }
void Hospital::setGraphNodeId(long long value) { graphNodeId = value; }
int Hospital::getTotalBeds() const { return totalBeds; }
int Hospital::getOccupiedBeds() const { return occupiedBeds; }

void Hospital::addAmbulance(Ambulance ambulance) {
    ambulances.push_back(std::move(ambulance));
}

std::vector<Ambulance*> Hospital::getAvailableAmbulances() {
    std::vector<Ambulance*> available;
    for (Ambulance& ambulance : ambulances) {
        if (ambulance.isAvailable()) {
            available.push_back(&ambulance);
        }
    }
    return available;
}

int Hospital::availableBeds() const {
    return totalBeds - occupiedBeds;
}

bool Hospital::admitPatients(int count) {
    if (count < 0) {
        throw std::invalid_argument("patient count cannot be negative");
    }
    if (count > availableBeds()) {
        return false;
    }
    occupiedBeds += count;
    return true;
}

void Hospital::dischargePatients(int count) {
    if (count < 0) {
        throw std::invalid_argument("patient count cannot be negative");
    }
    occupiedBeds = count >= occupiedBeds ? 0 : occupiedBeds - count;
}

Ambulance* Hospital::findAmbulanceById(int ambulanceId) {
    for (Ambulance& ambulance : ambulances) {
        if (ambulance.getId() == ambulanceId) {
            return &ambulance;
        }
    }
    return nullptr;
}

}