#pragma once

#include "Fire_truck.hpp"

#include <string>
#include <vector>

namespace rapidaid::service {

class FireStation {
public:
    FireStation(int id, std::string name, double latitude, double longitude);

    int getId() const;
    const std::string& getName() const;
    double getLatitude() const;
    double getLongitude() const;
    long long getGraphNodeId() const;
    void setGraphNodeId(long long graphNodeId);

    void addTruck(FireTruck truck);
    std::vector<FireTruck*> getAvailableTrucks();
    FireTruck* findTruckById(int id);

private:
    int id;
    std::string name;
    double latitude;
    double longitude;
    long long graphNodeId = -1;
    std::vector<FireTruck> trucks;
};

}
