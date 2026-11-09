#include "IncidentStore.hpp"
#include "FileStore.hpp"

#include <filesystem>
#include <utility>

namespace rapidaid::dispatch {

IncidentStore::IncidentStore(std::string path)
    : m_path(std::move(path)) {}

bool IncidentStore::load() {
    m_incidents.clear();
    m_nextId = 1;

    std::error_code ec;
    if (!std::filesystem::exists(m_path, ec) || ec) {
        return true;
    }

    std::string content = rapidaid::io::FileStore::load(m_path);
    if (content.empty()) {
        return false;
    }

    try {
        nlohmann::json j = nlohmann::json::parse(content);
        if (!j.is_array()) {
            return false;
        }

        for (const auto& item : j) {
            if (!item.is_object()) {
                continue;
            }
            Incident inc = item.get<Incident>();
            if (inc.id >= m_nextId) {
                m_nextId = inc.id + 1;
            }
            m_incidents.push_back(std::move(inc));
        }
        return true;
    } catch (...) {
        m_incidents.clear();
        m_nextId = 1;
        return false;
    }
}

bool IncidentStore::save() const {
    try {
        nlohmann::json root = nlohmann::json::array();
        for (const auto& inc : m_incidents) {
            root.push_back(inc);
        }
        return rapidaid::io::FileStore::save(m_path, root.dump(2));
    } catch (...) {
        return false;
    }
}

int IncidentStore::add(Incident incident) {
    if (incident.id <= 0) {
        incident.id = m_nextId++;
    } else if (incident.id >= m_nextId) {
        m_nextId = incident.id + 1;
    }
    m_incidents.push_back(std::move(incident));
    return m_incidents.back().id;
}

Incident* IncidentStore::find(int id) {
    for (auto& inc : m_incidents) {
        if (inc.id == id) {
            return &inc;
        }
    }
    return nullptr;
}

const std::vector<Incident>& IncidentStore::all() const {
    return m_incidents;
}

void IncidentStore::clear() {
    m_incidents.clear();
    m_nextId = 1;
}

} // namespace rapidaid::dispatch
