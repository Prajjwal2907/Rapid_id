#include "DispatchEngine.hpp"
#include "Graph.hpp"
#include "Incident.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using rapidaid::dispatch::Candidate;
using rapidaid::dispatch::Incident;
using rapidaid::dispatch::Pick;
using rapidaid::dispatch::Proposal;
using rapidaid::dispatch::Slot;
using rapidaid::dispatch::UnitKind;
using rapidaid::dispatch::pickReplacement;
using rapidaid::dispatch::propose;
using rapidaid::dispatch::requiredSlots;
using rapidaid::graph::Graph;

static int failures = 0;

static void assertTest(bool condition, const std::string& testName) {
    if (condition) {
        std::cout << "[PASS] " << testName << "\n";
    } else {
        std::cout << "[FAIL] " << testName << "\n";
        failures++;
    }
}

int main() {
    std::cout << "Running test_dispatch_engine_offline...\n";

    // 1. requiredSlots for each rule
    {
        // Medical
        auto m0 = requiredSlots("Medical", 3, 0);
        assertTest(m0.size() == 2 && m0[0].kind == UnitKind::Ambulance && m0[0].role == "primary" &&
                   m0[1].kind == UnitKind::Hospital && m0[1].minCapacity == 0,
                   "Medical people=0: 1 ambulance primary, 1 hospital minCapacity=0");

        auto m1 = requiredSlots("Medical", 3, 1);
        assertTest(m1.size() == 2 && m1[0].kind == UnitKind::Ambulance && m1[1].minCapacity == 1,
                   "Medical people=1: 1 ambulance primary, 1 hospital minCapacity=1");

        auto m2 = requiredSlots("medical", 3, 2);
        assertTest(m2.size() == 2 && m2[0].kind == UnitKind::Ambulance && m2[1].minCapacity == 2,
                   "Medical people=2: 1 ambulance primary, 1 hospital minCapacity=2");

        auto m3 = requiredSlots("Medical", 3, 3);
        assertTest(m3.size() == 3 && m3[0].kind == UnitKind::Ambulance && m3[1].kind == UnitKind::Ambulance &&
                   m3[2].kind == UnitKind::Hospital && m3[2].minCapacity == 3,
                   "Medical people=3: 2 ambulance primary, 1 hospital minCapacity=3");

        auto m5 = requiredSlots("Medical", 3, 5);
        assertTest(m5.size() == 4 && m5[0].kind == UnitKind::Ambulance && m5[1].kind == UnitKind::Ambulance &&
                   m5[2].kind == UnitKind::Ambulance && m5[3].kind == UnitKind::Hospital && m5[3].minCapacity == 5,
                   "Medical people=5: 3 ambulance primary (clamped max 3), 1 hospital minCapacity=5");

        auto m10 = requiredSlots("Medical", 3, 10);
        assertTest(m10.size() == 4 && m10[3].minCapacity == 10,
                   "Medical people=10: 3 ambulance primary, 1 hospital minCapacity=10");

        // Fire
        auto f_low = requiredSlots("Fire", 2, 0);
        assertTest(f_low.size() == 1 && f_low[0].kind == UnitKind::FireTruck && f_low[0].role == "primary",
                   "Fire severity=2, people=0: 1 fire_truck primary only");

        auto f_sev3 = requiredSlots("Fire", 3, 0);
        assertTest(f_sev3.size() == 2 && f_sev3[0].kind == UnitKind::FireTruck &&
                   f_sev3[1].kind == UnitKind::Ambulance && f_sev3[1].role == "standby",
                   "Fire severity=3, people=0: 1 fire_truck primary, 1 ambulance standby");

        auto f_peop = requiredSlots("Fire", 2, 4);
        assertTest(f_peop.size() == 2 && f_peop[0].kind == UnitKind::FireTruck &&
                   f_peop[1].kind == UnitKind::Ambulance && f_peop[1].role == "standby",
                   "Fire severity=2, people=4: 1 fire_truck primary, 1 ambulance standby");

        auto f_sev4 = requiredSlots("fire", 4, 0);
        assertTest(f_sev4.size() == 4 && f_sev4[0].kind == UnitKind::FireTruck &&
                   f_sev4[1].kind == UnitKind::FireTruck && f_sev4[2].kind == UnitKind::Ambulance &&
                   f_sev4[3].kind == UnitKind::PoliceUnit && f_sev4[3].role == "support",
                   "Fire severity=4: 2 fire_truck, 1 ambulance standby, 1 police_unit support");

        auto f_shelter = requiredSlots("Fire", 5, 12);
        assertTest(f_shelter.size() == 5 && f_shelter[4].kind == UnitKind::Shelter &&
                   f_shelter[4].role == "support" && f_shelter[4].minCapacity == 12,
                   "Fire severity=5, people=12: includes shelter support with minCapacity=12");

        // Police
        auto p_low = requiredSlots("Police", 2, 0);
        assertTest(p_low.size() == 1 && p_low[0].kind == UnitKind::PoliceUnit && p_low[0].role == "primary",
                   "Police severity=2, people=0: 1 police_unit primary");

        auto p_high = requiredSlots("police", 4, 0);
        assertTest(p_high.size() == 2 && p_high[0].kind == UnitKind::PoliceUnit &&
                   p_high[1].kind == UnitKind::PoliceUnit,
                   "Police severity=4, people=0: 2 police_unit primary");

        auto p_peop = requiredSlots("Police", 4, 3);
        assertTest(p_peop.size() == 3 && p_peop[2].kind == UnitKind::Ambulance && p_peop[2].role == "standby",
                   "Police severity=4, people=3: 2 police_unit primary, 1 ambulance standby");

        // Other
        auto other = requiredSlots("Other", 3, 0);
        assertTest(other.size() == 1 && other[0].kind == UnitKind::PoliceUnit && other[0].role == "primary",
                   "Other: 1 police_unit primary");

        auto unknown = requiredSlots("TrafficJam", 1, 0);
        assertTest(unknown.size() == 1 && unknown[0].kind == UnitKind::PoliceUnit && unknown[0].role == "primary",
                   "Unknown type: 1 police_unit primary");
    }

    // 2. propose on road graph
    {
        Graph g;
        // Nodes 0 to 6
        // 0: incident location
        // 1: amb_1 (dist 100)
        // 2: amb_2 (dist 200)
        // 3: amb_3 (dist 300)
        // 4: amb_4 (dist 400)
        // 5: hosp_small (dist 450, capacity 5)
        // 6: hosp_big (dist 600, capacity 20)
        // 7: isolated node (unreachable)
        for (int i = 0; i <= 7; ++i) {
            g.addNode(1000 + i, 10.0 + i * 0.01, 20.0 + i * 0.01);
        }
        g.addEdge(0, 1, 100.0);
        g.addEdge(0, 2, 200.0);
        g.addEdge(0, 3, 300.0);
        g.addEdge(0, 4, 400.0);
        g.addEdge(0, 5, 450.0);
        g.addEdge(0, 6, 600.0);

        std::vector<Candidate> candidates;
        candidates.push_back({"amb_1", "Ambulance 1", UnitKind::Ambulance, 1, 0, 0, true, 0});
        candidates.push_back({"amb_2", "Ambulance 2", UnitKind::Ambulance, 2, 0, 0, true, 0});
        candidates.push_back({"amb_3", "Ambulance 3", UnitKind::Ambulance, 3, 0, 0, true, 0});
        candidates.push_back({"amb_4", "Ambulance 4", UnitKind::Ambulance, 4, 0, 0, true, 0});
        candidates.push_back({"hosp_small", "City Clinic", UnitKind::Hospital, 5, 0, 0, true, 5});
        candidates.push_back({"hosp_big", "General Hospital", UnitKind::Hospital, 6, 0, 0, true, 20});

        // Case A: Medical with severity=3, people=5 (requires 3 ambulances + 1 hospital with cap>=5)
        Incident incA;
        incA.type = "Medical";
        incA.severity = 3;
        incA.peopleAffected = 5;

        Proposal propA = propose(incA, g, 0, candidates);
        assertTest(propA.picks.size() == 4, "Medical proposal has 4 picks");

        // Check picks 0, 1, 2 are distinct ambulances ordered by distance
        assertTest(propA.picks[0].found && propA.picks[0].candidate.username == "amb_1", "Pick 0 is nearest amb_1");
        assertTest(propA.picks[0].distanceMeters == 100.0, "Pick 0 distance is 100m");
        assertTest(propA.picks[0].pathNodes == std::vector<int>({1, 0}), "Pick 0 path is unit -> incident [1, 0]");

        assertTest(propA.picks[1].found && propA.picks[1].candidate.username == "amb_2", "Pick 1 is amb_2");
        assertTest(propA.picks[1].distanceMeters == 200.0, "Pick 1 distance is 200m");

        assertTest(propA.picks[2].found && propA.picks[2].candidate.username == "amb_3", "Pick 2 is amb_3");
        assertTest(propA.picks[2].distanceMeters == 300.0, "Pick 2 distance is 300m");

        // Hospital slot: minCapacity is 5. Both hosp_small (5) and hosp_big (20) qualify!
        // hosp_small is nearer (450m vs 600m), so hosp_small should be chosen!
        assertTest(propA.picks[3].found && propA.picks[3].candidate.username == "hosp_small",
                   "Hospital pick is nearest qualifying hosp_small (cap 5 >= 5)");
        assertTest(propA.picks[3].distanceMeters == 450.0, "Hospital distance is 450m");
        assertTest(propA.picks[3].alternatives.size() == 1 &&
                   propA.picks[3].alternatives[0].username == "hosp_big",
                   "Hospital has hosp_big as alternative");

        // Alternatives for Amb 1: should have next 3 ambulances
        assertTest(propA.picks[0].alternatives.size() == 3, "Amb 1 has 3 alternatives");
        if (propA.picks[0].alternatives.size() == 3) {
            assertTest(propA.picks[0].alternatives[0].username == "amb_2", "Amb 1 alt 0 is amb_2");
            assertTest(propA.picks[0].alternatives[1].username == "amb_3", "Amb 1 alt 1 is amb_3");
            assertTest(propA.picks[0].alternatives[2].username == "amb_4", "Amb 1 alt 2 is amb_4");
        }

        // Case B: Hospital with higher bed requirement (people = 10)
        // hosp_small (cap 5) does NOT qualify. hosp_big (cap 20 >= 10) MUST be chosen!
        Incident incB;
        incB.type = "Medical";
        incB.severity = 3;
        incB.peopleAffected = 10;

        Proposal propB = propose(incB, g, 0, candidates);
        assertTest(propB.picks[3].found && propB.picks[3].candidate.username == "hosp_big",
                   "Hospital pick with people=10 selects hosp_big (cap 20 >= 10)");
        assertTest(propB.warnings.empty(), "No capacity warning when qualifying hospital exists");

        // Case C: Medical hospital fallback when NO hospital has enough beds (people = 50)
        // hosp_big has 20 beds, hosp_small has 5 beds. Neither has 50.
        // Fallback chooses hosp_big (most beds) and generates warning "Hospital capacity below requested"
        Incident incC;
        incC.type = "Medical";
        incC.severity = 3;
        incC.peopleAffected = 50;

        Proposal propC = propose(incC, g, 0, candidates);
        assertTest(propC.picks[3].found && propC.picks[3].candidate.username == "hosp_big",
                   "Hospital fallback picks hosp_big with most free beds (20)");
        bool hasCapWarning = false;
        for (const auto& w : propC.warnings) {
            if (w == "Hospital capacity below requested") hasCapWarning = true;
        }
        assertTest(hasCapWarning, "Hospital capacity warning generated in fallback");

        // Case D: Excluded candidates
        std::set<std::string> excluded = {"amb_1", "amb_2"};
        Proposal propD = propose(incA, g, 0, candidates, excluded);
        assertTest(propD.picks[0].found && propD.picks[0].candidate.username == "amb_3",
                   "Pick 0 skips excluded amb_1 and amb_2, picks amb_3");
        assertTest(propD.picks[1].found && propD.picks[1].candidate.username == "amb_4",
                   "Pick 1 picks amb_4");
        assertTest(!propD.picks[2].found, "Pick 2 cannot find available ambulance");
        bool hasUnfilledWarn = false;
        for (const auto& w : propD.warnings) {
            if (w == "No available ambulance for primary") hasUnfilledWarn = true;
        }
        assertTest(hasUnfilledWarn, "Warning added for unfilled ambulance slot");

        // Case E: Candidate on unreachable node or unavailable
        std::vector<Candidate> specialCandidates;
        // Unreachable candidate on isolated node 7
        specialCandidates.push_back({"amb_unreachable", "Unreachable", UnitKind::Ambulance, 7, 0, 0, true, 0});
        // Unavailable candidate on node 1
        specialCandidates.push_back({"amb_busy", "Busy", UnitKind::Ambulance, 1, 0, 0, false, 0});

        Proposal propE = propose(incA, g, 0, specialCandidates);
        assertTest(!propE.picks[0].found, "Unreachable and unavailable candidates are not picked");

        // Case F: Empty candidate list
        Proposal propF = propose(incA, g, 0, {});
        assertTest(propF.picks.size() == 4, "Proposal size equals required slots on empty candidates");
        assertTest(!propF.picks[0].found && !propF.picks[3].found, "All picks unfound on empty candidates");
        assertTest(propF.warnings.size() == 4, "All 4 unfilled slots generate warnings");
    }

    // 3. pickReplacement
    {
        Graph g;
        g.addNode(10, 0, 0); // 0: incident
        g.addNode(11, 0, 0); // 1: amb_1
        g.addNode(12, 0, 0); // 2: amb_2
        g.addNode(13, 0, 0); // 3: amb_3
        g.addEdge(0, 1, 150.0);
        g.addEdge(0, 2, 250.0);
        g.addEdge(0, 3, 350.0);

        std::vector<Candidate> candidates;
        candidates.push_back({"amb_1", "Ambulance 1", UnitKind::Ambulance, 1, 0, 0, true, 0});
        candidates.push_back({"amb_2", "Ambulance 2", UnitKind::Ambulance, 2, 0, 0, true, 0});
        candidates.push_back({"amb_3", "Ambulance 3", UnitKind::Ambulance, 3, 0, 0, true, 0});

        Slot ambSlot{UnitKind::Ambulance, "primary", 0};
        Incident inc;
        inc.type = "Medical";

        // Replace when amb_1 is excluded
        Pick rep = pickReplacement(ambSlot, 0, inc, g, candidates, {"amb_1"});
        assertTest(rep.found, "pickReplacement found replacement");
        assertTest(rep.candidate.username == "amb_2", "Replacement candidate is amb_2");
        assertTest(rep.distanceMeters == 250.0, "Replacement distance is 250m");
        assertTest(rep.pathNodes == std::vector<int>({2, 0}), "Replacement path is [2, 0]");
        assertTest(rep.alternatives.size() == 1 && rep.alternatives[0].username == "amb_3",
                   "Replacement alternative is amb_3");

        // When all candidates excluded
        Pick repNone = pickReplacement(ambSlot, 0, inc, g, candidates, {"amb_1", "amb_2", "amb_3"});
        assertTest(!repNone.found, "pickReplacement returns found=false when all excluded");
    }

    return failures == 0 ? 0 : 1;
}
