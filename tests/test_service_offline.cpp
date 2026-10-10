#include "Ambulance.hpp"
#include "Fire_station.hpp"
#include "Fire_truck.hpp"
#include "Hospital.hpp"
#include "Police_station.hpp"
#include "Police_unit.hpp"
#include "Shelter.hpp"

#include <cassert>
#include <stdexcept>

using namespace rapidaid::service;

int main() {
    Ambulance ambulance(1, "Ambulance 1", 0.0, 0.0, 10, 2);
    assert(ambulance.getGraphNodeId() == -1);
    ambulance.setGraphNodeId(42);
    assert(ambulance.getGraphNodeId() == 42);
    assert(ambulance.isAvailable());
    assert(ambulance.assign(100));
    assert(!ambulance.assign(101));
    assert(ambulance.getCurrentIncidentId() == 100);

    Hospital hospital(10, "General Hospital", 1.0, 2.0, 5, 2);
    hospital.addAmbulance(ambulance);
    assert(hospital.getAvailableAmbulances().empty());
    assert(hospital.findAmbulanceById(1) != nullptr);
    assert(hospital.admitPatients(3));
    assert(hospital.availableBeds() == 0);
    assert(!hospital.admitPatients(1));
    assert(hospital.getOccupiedBeds() == 5);
    hospital.dischargePatients(2);
    assert(hospital.availableBeds() == 2);
    hospital.dischargePatients(10);
    assert(hospital.getOccupiedBeds() == 0);

    FireStation fireStation(20, "Central Fire Station", 3.0, 4.0);
    fireStation.addTruck(FireTruck(2, "Truck 1", 3.0, 4.0, 20, 5000));
    assert(fireStation.getAvailableTrucks().size() == 1);
    assert(fireStation.findTruckById(2)->assign(200));
    assert(fireStation.getAvailableTrucks().empty());

    PoliceStation policeStation(30, "Central Police Station", 5.0, 6.0);
    policeStation.addUnit(PoliceUnit(3, "Unit 1", 5.0, 6.0, 30, 4));
    assert(policeStation.getAvailableUnits().size() == 1);
    assert(policeStation.findUnitById(3)->assign(300));
    assert(policeStation.getAvailableUnits().empty());

    ambulance.release();
    assert(ambulance.isAvailable());
    assert(ambulance.getCurrentIncidentId() == -1);

    Shelter shelter(40, "North Shelter", 7.0, 8.0, 10, 7);
    assert(shelter.availableSpace() == 3);
    assert(!shelter.isFull());
    assert(shelter.admit(3));
    assert(shelter.isFull());
    assert(!shelter.admit(1));
    assert(shelter.getOccupied() == 10);
    shelter.leave(4);
    assert(shelter.availableSpace() == 4);
    shelter.leave(20);
    assert(shelter.getOccupied() == 0);

    bool rejectedNegativeCount = false;
    try {
        Shelter invalidShelter(41, "Invalid Shelter", 0.0, 0.0, -1);
    } catch (const std::invalid_argument&) {
        rejectedNegativeCount = true;
    }
    assert(rejectedNegativeCount);
}
