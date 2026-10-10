#include "Shelter.hpp"

#include <stdexcept>
#include <utility>

namespace rapidaid::service {

Shelter::Shelter(int id, std::string name, double latitude, double longitude,
                 int capacity, int occupied)
    : id(id),
      name(std::move(name)),
      latitude(latitude),
      longitude(longitude),
      capacity(capacity),
      occupied(occupied) {
    if (capacity < 0 || occupied < 0 || occupied > capacity) {
        throw std::invalid_argument("shelter capacity and occupancy are invalid");
    }
}

int Shelter::getId() const { return id; }
const std::string& Shelter::getName() const { return name; }
double Shelter::getLatitude() const { return latitude; }
double Shelter::getLongitude() const { return longitude; }
long long Shelter::getGraphNodeId() const { return graphNodeId; }
void Shelter::setGraphNodeId(long long value) { graphNodeId = value; }
int Shelter::getCapacity() const { return capacity; }
int Shelter::getOccupied() const { return occupied; }

int Shelter::availableSpace() const {
    return capacity - occupied;
}

bool Shelter::admit(int count) {
    if (count < 0) {
        throw std::invalid_argument("admission count cannot be negative");
    }
    if (count > availableSpace()) {
        return false;
    }
    occupied += count;
    return true;
}

void Shelter::leave(int count) {
    if (count < 0) {
        throw std::invalid_argument("departure count cannot be negative");
    }
    occupied = count >= occupied ? 0 : occupied - count;
}

bool Shelter::isFull() const {
    return occupied == capacity;
}

}