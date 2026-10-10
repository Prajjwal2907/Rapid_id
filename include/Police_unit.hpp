#pragma once

#include <string>

namespace rapidaid::service {

class PoliceUnit {
public:
    enum class Status { Available, Dispatched, Unavailable };

    PoliceUnit(int id, std::string name, double latitude, double longitude,
               int homeStationId, int officerCount);

    int getId() const;
    const std::string& getName() const;
    double getLatitude() const;
    double getLongitude() const;
    long long getGraphNodeId() const;
    void setGraphNodeId(long long graphNodeId);

    Status getStatus() const;
    int getCurrentIncidentId() const;
    int getHomeStationId() const;
    int getOfficerCount() const;
    bool isAvailable() const;
    bool assign(int incidentId);
    void release();
    void markUnavailable();
    static std::string statusToString(Status status);

private:
    int id;
    std::string name;
    double latitude;
    double longitude;
    long long graphNodeId = -1;
    Status status = Status::Available;
    int currentIncidentId = -1;
    int homeStationId;
    int officerCount;
};

}