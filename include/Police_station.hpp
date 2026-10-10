#pragma once

#include "Police_unit.hpp"

#include <string>
#include <vector>

namespace rapidaid::service {

class PoliceStation {
public:
    PoliceStation(int id, std::string name, double latitude, double longitude);

    int getId() const;
    const std::string& getName() const;
    double getLatitude() const;
    double getLongitude() const;
    long long getGraphNodeId() const;
    void setGraphNodeId(long long graphNodeId);

    void addUnit(PoliceUnit unit);
    std::vector<PoliceUnit*> getAvailableUnits();
    PoliceUnit* findUnitById(int id);

private:
    int id;
    std::string name;
    double latitude;
    double longitude;
    long long graphNodeId = -1;
    std::vector<PoliceUnit> units;
};

}
