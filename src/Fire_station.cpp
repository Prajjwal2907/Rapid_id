#include "Fire_station.hpp"

#include <utility>

namespace rapidaid::service {

FireStation::FireStation(int id, std::string name, double latitude, double longitude)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude) {}

int FireStation::getId() const { return id; }
const std::string& FireStation::getName() const { return name; }
double FireStation::getLatitude() const { return latitude; }
double FireStation::getLongitude() const { return longitude; }
long long FireStation::getGraphNodeId() const { return graphNodeId; }
void FireStation::setGraphNodeId(long long value) { graphNodeId = value; }

void FireStation::addTruck(FireTruck truck) {
    trucks.push_back(std::move(truck));
}

std::vector<FireTruck*> FireStation::getAvailableTrucks() {
    std::vector<FireTruck*> available;
    for (FireTruck& truck : trucks) {
        if (truck.isAvailable()) {
            available.push_back(&truck);
        }
    }
    return available;
}

FireTruck* FireStation::findTruckById(int truckId) {
    for (FireTruck& truck : trucks) {
        if (truck.getId() == truckId) {
            return &truck;
        }
    }
    return nullptr;
}

}
