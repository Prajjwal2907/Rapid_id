#pragma once

#include "Ambulance.hpp"

#include <string>
#include <vector>

namespace rapidaid::service {

class Hospital {
public:
    Hospital(int id, std::string name, double latitude, double longitude,
             int totalBeds, int occupiedBeds = 0);

    int getId() const;
    const std::string& getName() const;
    double getLatitude() const;
    double getLongitude() const;
    long long getGraphNodeId() const;
    void setGraphNodeId(long long graphNodeId);
    int getTotalBeds() const;
    int getOccupiedBeds() const;

    void addAmbulance(Ambulance ambulance);
    std::vector<Ambulance*> getAvailableAmbulances();
    int availableBeds() const;
    bool admitPatients(int count);
    void dischargePatients(int count);
    Ambulance* findAmbulanceById(int id);

private:
    int id;
    std::string name;
    double latitude;
    double longitude;
    long long graphNodeId = -1;
    int totalBeds;
    int occupiedBeds;
    std::vector<Ambulance> ambulances;
};

}