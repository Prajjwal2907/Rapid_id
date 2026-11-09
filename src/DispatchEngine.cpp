#include "DispatchEngine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>

namespace rapidaid::dispatch {

std::vector<Slot> requiredSlots(const std::string& type, int severity, int people) {
    std::string lowerType = type;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    std::vector<Slot> slots;

    if (lowerType == "medical") {
        int ambCount = std::clamp((people + 1) / 2, 1, 3);
        for (int i = 0; i < ambCount; ++i) {
            slots.push_back(Slot{UnitKind::Ambulance, "primary", 0});
        }
        slots.push_back(Slot{UnitKind::Hospital, "support", std::max(0, people)});
    } else if (lowerType == "fire") {
        int truckCount = (severity >= 4) ? 2 : 1;
        for (int i = 0; i < truckCount; ++i) {
            slots.push_back(Slot{UnitKind::FireTruck, "primary", 0});
        }
        if (people > 0 || severity >= 3) {
            slots.push_back(Slot{UnitKind::Ambulance, "standby", 0});
        }
        if (severity >= 4) {
            slots.push_back(Slot{UnitKind::PoliceUnit, "support", 0});
        }
        if (people >= 10) {
            slots.push_back(Slot{UnitKind::Shelter, "support", people});
        }
    } else if (lowerType == "police") {
        int policeCount = (severity >= 4) ? 2 : 1;
        for (int i = 0; i < policeCount; ++i) {
            slots.push_back(Slot{UnitKind::PoliceUnit, "primary", 0});
        }
        if (people > 0) {
            slots.push_back(Slot{UnitKind::Ambulance, "standby", 0});
        }
    } else {
        // Unrecognized or "Other" type
        slots.push_back(Slot{UnitKind::PoliceUnit, "primary", 0});
    }

    return slots;
}

namespace {

struct EvaluatedCandidate {
    const Candidate* candidate = nullptr;
    double dist = 0.0;
};

Pick evaluateSlot(const Slot& slot,
                  const Incident& incident,
                  const rapidaid::graph::Graph& graph,
                  int incidentNode,
                  const rapidaid::graph::DistanceMap& distMap,
                  const std::vector<Candidate>& candidates,
                  const std::set<std::string>& excludedUsernames,
                  bool* capacityWarningOut) {
    if (capacityWarningOut != nullptr) {
        *capacityWarningOut = false;
    }

    Pick pick;
    pick.slot = slot;
    pick.found = false;

    if (distMap.dist.empty() || incidentNode < 0 || incidentNode >= graph.nodeCount()) {
        return pick;
    }

    // Filter reachable candidates matching unit kind
    std::vector<EvaluatedCandidate> eligible;
    for (const auto& c : candidates) {
        if (c.kind != slot.kind) {
            continue;
        }
        if (!c.available) {
            continue;
        }
        if (excludedUsernames.find(c.username) != excludedUsernames.end()) {
            continue;
        }
        if (c.nodeIndex < 0 || c.nodeIndex >= graph.nodeCount()) {
            continue;
        }
        double d = distMap.dist[c.nodeIndex];
        if (std::isinf(d)) {
            continue;
        }
        eligible.push_back({&c, d});
    }

    if (eligible.empty()) {
        return pick;
    }

    std::string lowerType = incident.type;
    std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    bool isMedicalHospital = (slot.kind == UnitKind::Hospital && slot.role == "support" &&
                             (lowerType == "medical" || lowerType.empty()));

    std::vector<EvaluatedCandidate> qualifying;

    if (slot.kind == UnitKind::Hospital || slot.kind == UnitKind::Shelter) {
        for (const auto& ec : eligible) {
            if (ec.candidate->capacity >= slot.minCapacity) {
                qualifying.push_back(ec);
            }
        }

        if (qualifying.empty() && isMedicalHospital) {
            // Medical hospital fallback: take hospital with most free beds
            qualifying = eligible;
            auto compBeds = [](const EvaluatedCandidate& a, const EvaluatedCandidate& b) {
                if (a.candidate->capacity != b.candidate->capacity) {
                    return a.candidate->capacity > b.candidate->capacity;
                }
                if (std::abs(a.dist - b.dist) > 1e-7) {
                    return a.dist < b.dist;
                }
                return a.candidate->username < b.candidate->username;
            };
            std::sort(qualifying.begin(), qualifying.end(), compBeds);
            if (capacityWarningOut != nullptr) {
                *capacityWarningOut = true;
            }
        } else if (!qualifying.empty()) {
            auto compDist = [](const EvaluatedCandidate& a, const EvaluatedCandidate& b) {
                if (std::abs(a.dist - b.dist) > 1e-7) {
                    return a.dist < b.dist;
                }
                return a.candidate->username < b.candidate->username;
            };
            std::sort(qualifying.begin(), qualifying.end(), compDist);
        }
    } else {
        for (const auto& ec : eligible) {
            if (ec.candidate->capacity >= slot.minCapacity) {
                qualifying.push_back(ec);
            }
        }
        auto compDist = [](const EvaluatedCandidate& a, const EvaluatedCandidate& b) {
            if (std::abs(a.dist - b.dist) > 1e-7) {
                return a.dist < b.dist;
            }
            return a.candidate->username < b.candidate->username;
        };
        std::sort(qualifying.begin(), qualifying.end(), compDist);
    }

    if (qualifying.empty()) {
        return pick;
    }

    pick.found = true;
    pick.candidate = *(qualifying[0].candidate);
    pick.distanceMeters = qualifying[0].dist;
    pick.pathNodes = graph.pathFromMap(distMap, incidentNode, pick.candidate.nodeIndex);
    std::reverse(pick.pathNodes.begin(), pick.pathNodes.end());

    size_t altCount = std::min(static_cast<size_t>(qualifying.size() - 1), static_cast<size_t>(3));
    for (size_t i = 1; i <= altCount; ++i) {
        pick.alternatives.push_back(*(qualifying[i].candidate));
        pick.alternativeDistances.push_back(qualifying[i].dist);
    }

    return pick;
}

} // anonymous namespace

Proposal propose(const Incident& incident,
                 const rapidaid::graph::Graph& graph,
                 int incidentNode,
                 const std::vector<Candidate>& candidates,
                 const std::set<std::string>& excludedUsernames) {
    Proposal proposal;
    std::vector<Slot> slots = requiredSlots(incident.type, incident.severity, incident.peopleAffected);

    rapidaid::graph::DistanceMap distMap = graph.shortestPathsFrom(incidentNode);

    std::set<std::string> effectiveExcluded = excludedUsernames;

    for (const auto& slot : slots) {
        bool capacityWarning = false;
        Pick pick = evaluateSlot(slot, incident, graph, incidentNode, distMap,
                                 candidates, effectiveExcluded, &capacityWarning);

        if (pick.found) {
            effectiveExcluded.insert(pick.candidate.username);
            if (capacityWarning) {
                proposal.warnings.push_back("Hospital capacity below requested");
            }
        } else {
            proposal.warnings.push_back("No available " + unitKindName(slot.kind) + " for " + slot.role);
        }

        proposal.picks.push_back(std::move(pick));
    }

    return proposal;
}

Pick pickReplacement(const Slot& slot,
                     int incidentNode,
                     const Incident& incident,
                     const rapidaid::graph::Graph& graph,
                     const std::vector<Candidate>& candidates,
                     const std::set<std::string>& excludedUsernames) {
    rapidaid::graph::DistanceMap distMap = graph.shortestPathsFrom(incidentNode);
    bool capacityWarning = false;
    return evaluateSlot(slot, incident, graph, incidentNode, distMap,
                        candidates, excludedUsernames, &capacityWarning);
}

} // namespace rapidaid::dispatch
