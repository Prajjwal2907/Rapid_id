#include <algorithm>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

int minimumSeverityFor(const std::string& priority) {
    if (priority == "Critical") return 5;
    if (priority == "High") return 4;
    if (priority == "Low") return 2;
    return 3;
}
struct Incident {
    int id{0};
    int severity{1};
    int peopleAffected{0};
    int waitingTimeMinutes{0};
    std::string priority{"Medium"};
    std::string status;
    std::string location;
    std::string type;

    long long calculatePriority() const {
        return static_cast<long long>(std::max(severity, minimumSeverityFor(priority))) * 10 +
               static_cast<long long>(peopleAffected) * 2 +
               waitingTimeMinutes;
    }
};

struct CompareIncident {
    bool operator()(const Incident& left, const Incident& right) const {
        const long long leftScore = left.calculatePriority();
        const long long rightScore = right.calculatePriority();
        if (leftScore == rightScore) {
            return left.id > right.id;
        }
        return leftScore < rightScore;
    }
};

int readInteger(const std::string& prompt, int minimum, int maximum) {
    while (true) {
        std::cout << prompt;
        std::string input;
        if (!std::getline(std::cin, input)) {
            throw std::runtime_error("Input ended");
        }
        try {
            std::size_t parsed = 0;
            const int value = std::stoi(input, &parsed);
            if (parsed == input.size() && value >= minimum && value <= maximum) {
                return value;
            }
        } catch (...) {
            // Ask again when the input is not a valid integer in range.
        }
        std::cout << "Enter a whole number from " << minimum << " to " << maximum << ".\n";
    }
}

std::string readText(const std::string& prompt) {
    while (true) {
        std::cout << prompt;
        std::string value;
        if (!std::getline(std::cin, value)) {
            throw std::runtime_error("Input ended");
        }
        if (value.find_first_not_of(" \t\r\n") != std::string::npos) {
            return value;
        }
        std::cout << "This field cannot be empty.\n";
    }
}

std::string readPriority() {
    while (true) {
        const std::string priority = readText("Priority (Low/Medium/High/Critical): ");
        if (priority == "Low" || priority == "Medium" || priority == "High" || priority == "Critical") {
            return priority;
        }
        std::cout << "Choose Low, Medium, High, or Critical.\n";
    }
}
Incident readIncident() {
    Incident incident;
    incident.id = readInteger("Incident ID: ", 1, 1000000000);
    incident.severity = readInteger("Severity (1-5): ", 1, 5);
    incident.peopleAffected = readInteger("People affected: ", 0, 1000000);
    incident.waitingTimeMinutes = readInteger("Waiting time in minutes: ", 0, 1000000);
    incident.priority = readPriority();
    incident.status = readText("Status: ");
    incident.location = readText("Location: ");
    incident.type = readText("Incident type: ");
    return incident;
}

int main() {
    std::priority_queue<Incident, std::vector<Incident>, CompareIncident> incidents;

    while (true) {
        std::cout << "\n1. Add incident\n2. Show priority queue and exit\n";
        try {
            const int choice = readInteger("Choice: ", 1, 2);
            if (choice == 2) break;
            const Incident incident = readIncident();
            const long long addedScore = incident.calculatePriority();
            incidents.push(incident);
            std::cout << "Incident " << incident.id << " added with score " << addedScore << ".\n"
                      << "Current highest queue score: " << incidents.top().calculatePriority() << ".\n";
        } catch (const std::runtime_error&) {
            std::cout << "\nInput ended.\n";
            break;
        }
    }

    if (incidents.empty()) {
        std::cout << "No incidents were entered.\n";
        return 0;
    }

    std::cout << "\nIncidents in dispatch priority order (highest score first):\n";
    while (!incidents.empty()) {
        const Incident incident = incidents.top();
        incidents.pop();
        std::cout << "\nIncident ID: " << incident.id
                  << "\nPriority score: " << incident.calculatePriority()
                  << "\nEntered severity: " << incident.severity
                  << "\nSeverity used for score: " << std::max(incident.severity, minimumSeverityFor(incident.priority))
                  << "\nPeople affected: " << incident.peopleAffected
                  << "\nWaiting time: " << incident.waitingTimeMinutes << " minutes"
                  << "\nStatus: " << incident.status
                  << "\nType: " << incident.type
                  << "\nLocation: " << incident.location << '\n';
    }
    return 0;
}
