#ifndef RAPIDAID_DISPATCH_INCIDENTSTORE_HPP
#define RAPIDAID_DISPATCH_INCIDENTSTORE_HPP

#include "Incident.hpp"

#include <string>
#include <vector>

namespace rapidaid::dispatch {

// Persistent repository for emergency incidents with atomic JSON storage.
class IncidentStore {
public:
    // Initializes the store with the target file path.
    // Failure behavior: Never fails.
    explicit IncidentStore(std::string path = "data/incidents.json");

    // Loads incidents from the configured JSON file.
    // Tolerates missing file (returns true, resulting in an empty store).
    // Failure behavior: Returns false and clears incidents if the file is corrupt
    // or unreadable. Never throws.
    bool load();

    // Saves all stored incidents atomically to disk via a temporary file and rename.
    // Failure behavior: Returns false on I/O or filesystem failure without leaving
    // any temporary files behind. Never throws.
    bool save() const;

    // Adds an incident to the store.
    // If incident.id <= 0, assigns the next available unique positive integer ID.
    // Failure behavior: Never fails; returns the incident's ID.
    int add(Incident incident);

    // Finds a stored incident by its unique ID.
    // Failure behavior: Returns nullptr if not found.
    Incident* find(int id);

    // Returns a const reference to all stored incidents.
    // Failure behavior: Never fails.
    const std::vector<Incident>& all() const;

    // Clears all stored incidents and resets ID allocation.
    // Failure behavior: Never fails.
    void clear();

private:
    std::string m_path;
    std::vector<Incident> m_incidents;
    int m_nextId = 1;
};

} // namespace rapidaid::dispatch

#endif // RAPIDAID_DISPATCH_INCIDENTSTORE_HPP
