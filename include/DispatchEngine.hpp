#ifndef RAPIDAID_DISPATCH_DISPATCHENGINE_HPP
#define RAPIDAID_DISPATCH_DISPATCHENGINE_HPP

#include "Graph.hpp"
#include "Incident.hpp"

#include <set>
#include <string>
#include <vector>

namespace rapidaid::dispatch {

// Represents an emergency response unit candidate available for dispatch.
struct Candidate {
    std::string username;
    std::string name;
    UnitKind kind = UnitKind::Ambulance;
    int nodeIndex = -1;
    double lat = 0.0;
    double lon = 0.0;
    bool available = true;
    int capacity = 0; /* free beds or free space, 0 for mobile */
};

// Represents a required resource slot to fulfill an incident response.
struct Slot {
    UnitKind kind = UnitKind::Ambulance;
    std::string role; /* primary | standby | support */
    int minCapacity = 0;
};

// Represents the dispatch selection for a single slot, including alternatives.
struct Pick {
    Slot slot;
    bool found = false;
    Candidate candidate;
    double distanceMeters = 0.0;
    std::vector<int> pathNodes; /* unit -> incident */
    std::vector<Candidate> alternatives; /* next best, max 3 */
    std::vector<double> alternativeDistances;
};

// Represents the full dispatch proposal for an incident.
struct Proposal {
    std::vector<Pick> picks;
    std::vector<std::string> warnings;
};

// Determines required response slots based on incident type, severity, and people affected.
// Failure behavior: Returns a default single police_unit primary slot for unrecognized or empty types.
// Never throws.
std::vector<Slot> requiredSlots(const std::string& type, int severity, int people);

// Generates a complete dispatch proposal for an incident across all required slots.
// Computes shortest paths from incidentNode once and ranks candidates by distance.
// Never assigns the same candidate to multiple slots.
// Failure behavior: If incidentNode is out of range or no candidates qualify, returns picks
// with found=false and populates warnings ("No available <kind> for <role>"). Never throws.
Proposal propose(const Incident& incident,
                 const rapidaid::graph::Graph& graph,
                 int incidentNode,
                 const std::vector<Candidate>& candidates,
                 const std::set<std::string>& excludedUsernames = {});

// Selects the single best candidate and up to 3 alternatives for a replacement slot.
// Failure behavior: Returns a Pick with found=false if no candidate qualifies or if
// incidentNode is invalid. Never throws.
Pick pickReplacement(const Slot& slot,
                     int incidentNode,
                     const Incident& incident,
                     const rapidaid::graph::Graph& graph,
                     const std::vector<Candidate>& candidates,
                     const std::set<std::string>& excludedUsernames);

} // namespace rapidaid::dispatch

#endif // RAPIDAID_DISPATCH_DISPATCHENGINE_HPP
