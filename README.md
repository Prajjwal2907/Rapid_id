# ResQNet — Service Classes Module

Basic overview of each file in this module.

## include/

| File | Overview |
|---|---|
| `Status.h` | Shared enums: `UnitStatus`, `UnitType`, `FacilityType`. No logic, just definitions used everywhere else. |
| `ResponseUnit.h` | Abstract base class for all mobile units. Declares id, status, location, and dispatch-related methods. |
| `Ambulance.h` | Ambulance class, extends `ResponseUnit`. Adds medical staff count and life support flag. |
| `FireTruck.h` | Fire truck class, extends `ResponseUnit`. Adds water capacity and ladder flag. |
| `PoliceUnit.h` | Police unit class, extends `ResponseUnit`. Adds officer count and crowd control gear flag. |
| `Facility.h` | Abstract base class for stationary locations. Declares id, capacity, and admit/release methods. |
| `Hospital.h` | Hospital class, extends `Facility`. Adds bed counts, specialties, triage-based admission. |
| `Shelter.h` | Shelter class, extends `Facility`. Adds pet-friendly flag, medical station flag, supply inventory. |
| `Supply.h` | Standalone class for a trackable resource (quantity, consume/restock, low-stock check). |
| `ServiceRegistry.h` | Central class that owns all units, facilities, and supplies, and provides lookup/query methods. |

## src/

| File | Overview |
|---|---|
| `ResponseUnit.cpp` | Implements base unit behavior: dispatch, arrive, becomeAvailable, status checks. |
| `Ambulance.cpp` | Implements Ambulance-specific behavior (e.g. transportPatient). |
| `FireTruck.cpp` | Implements FireTruck-specific behavior (e.g. deployHose). |
| `PoliceUnit.cpp` | Implements PoliceUnit-specific behavior (e.g. securePerimeter). |
| `Facility.cpp` | Implements base facility behavior: admit, release, capacity checks. |
| `Hospital.cpp` | Implements Hospital-specific admission logic based on severity. |
| `Shelter.cpp` | Implements Shelter-specific behavior; mostly reuses base Facility logic. |
| `Supply.cpp` | Implements consume/restock/low-stock logic. |
| `ServiceRegistry.cpp` | Implements registration and lookup logic (find nearest facility, list available units, etc). |

## main.cpp

Entry point / test file — creates sample units, facilities, and supplies to demonstrate the classes working together.
