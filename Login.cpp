#include "Login.hpp"
#include "json.hpp"

#include <iostream>
#include <fstream>
#include <unordered_map>
#include <string>
#include <ctime>
using namespace std;

Logger::Logger(string f) : filename(f) {}

void Logger::log(string type, string details) {
    ofstream out(filename, ios::app);
    time_t t = time(nullptr);
    string ts = ctime(&t);
    ts.pop_back();
    out << ts << " | " << type << " | " << details << "\n";
}

size_t Auth::hashPass(const string& p) {
    return hash<string>{}(p);
}

Auth::Auth(string f) : filename(f) {
    load();
    if (users.empty()) {
        signUp("admin", "admin123", "admin");
    }
}

void Auth::load() {
    ifstream in(filename);
    string name, role;
    size_t h;
    while (in >> name >> h >> role) {
        users[name] = {h, role};
    }
}

void Auth::save() {
    ofstream out(filename);
    for (auto& p : users) {
        out << p.first << " " << p.second.passHash
            << " " << p.second.role << "\n";
    }
}

bool Auth::signUp(string u, string p, string role) {
    if (users.count(u)) return false;
    users[u] = {hashPass(p), role};
    save();
    return true;
}

bool Auth::login(string u, string p) {
    auto it = users.find(u);
    if (it == users.end()) return false;
    return it->second.passHash == hashPass(p);
}

string Auth::getRole(string u) { return users[u].role; }

void dispatcherMenu(Auth& auth, Logger& logger, string user) {
    bool isAdmin = (auth.getRole(user) == "admin");
    int choice;

    while (true) {
        cout << "\n--- Rapid Aid Menu (" << user << ") ---\n"
             << "1. New incident report\n"
             << "2. Active incidents\n"
             << "3. Past reports\n"
             << "4. Statistics\n";
        if (isAdmin) cout << "5. Add new dispatcher\n";
        cout << "0. Logout\nChoice: ";
        cin >> choice;

        if (choice == 0) {
            logger.log("LOGOUT", user);
            return;
        }
        else if (choice == 5 && isAdmin) {
            string u, p;
            cout << "New dispatcher username: ";
            cin >> u;
            cout << "New dispatcher password: ";
            cin >> p;
            if (auth.signUp(u, p, "dispatcher")) {
                cout << "Dispatcher added.\n";
                logger.log("USER_ADDED", u + " added by " + user);
            } else {
                cout << "Username already exists.\n";
            }
        }
        else if (choice >= 1 && choice <= 4) {
            cout << "Coming soon (teammates' modules connect here)\n";
        }
        else {
            cout << "Invalid choice.\n";
        }
    }
}

#ifdef LOGIN_STANDALONE
int main() {
    Auth auth;
    Logger logger;

    cout << "=== Rapid Aid - Dispatcher Login ===\n";

    for (int attempt = 1; attempt <= 3; attempt++) {
        string u, p;
        cout << "\nUsername: ";
        cin >> u;
        cout << "Password: ";
        cin >> p;

        if (auth.login(u, p)) {
            logger.log("LOGIN_SUCCESS", u);
            cout << "Welcome, " << u << "!\n";
            dispatcherMenu(auth, logger, u);
            return 0;
        }

        logger.log("LOGIN_FAILED", u);
        cout << "Wrong username or password. "
             << 3 - attempt << " attempts left.\n";
    }

    logger.log("LOGIN_LOCKED", "3 failed attempts");
    cout << "Too many failed attempts. Exiting.\n";
    return 0;
}
#endif

std::string handleLoginRequest(Auth& auth, const std::string& requestJson) {
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(requestJson);
        } catch (const nlohmann::json::exception&) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j.is_object() || !j.contains("username") || !j.contains("password")) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j["username"].is_string() || !j["password"].is_string()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        std::string username = j["username"].get<std::string>();
        std::string password = j["password"].get<std::string>();

        if (auth.login(username, password)) {
            Logger logger;
            logger.log("LOGIN_SUCCESS", username);
            std::string role = auth.getRole(username);
            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["username"] = username;
            resp["role"] = role;
            return resp.dump();
        } else {
            Logger logger;
            logger.log("LOGIN_FAILED", username);
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Invalid username or password";
            return resp.dump();
        }
    } catch (const nlohmann::json::exception&) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Bad request";
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Bad request";
        return resp.dump();
    }
}

std::string handleLogoutRequest(Auth& auth, const std::string& username) {
    (void)auth;
    try {
        Logger logger;
        logger.log("LOGOUT", username);
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        return resp.dump();
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        return resp.dump();
    }
}

std::string handleHealthRequest() {
    try {
        nlohmann::ordered_json resp;
        resp["ok"] = true;
        resp["service"] = "login";
        return resp.dump();
    } catch (...) {
        return "{\"ok\":true,\"service\":\"login\"}";
    }
}

std::string handleSignUpRequest(Auth& auth, const std::string& requestJson) {
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(requestJson);
        } catch (const nlohmann::json::exception&) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j.is_object() || !j.contains("username") || !j.contains("password")) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        if (!j["username"].is_string() || !j["password"].is_string()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        std::string username = j["username"].get<std::string>();
        std::string password = j["password"].get<std::string>();
        if (username.empty() || password.empty()) {
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Bad request";
            return resp.dump();
        }

        std::string role = "dispatcher";
        if (j.contains("role") && j["role"].is_string()) {
            role = j["role"].get<std::string>();
        }

        if (auth.signUp(username, password, role)) {
            Logger logger;
            logger.log("USER_SIGNUP", username + " (" + role + ")");
            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["username"] = username;
            resp["role"] = role;
            return resp.dump();
        } else {
            Logger logger;
            logger.log("SIGNUP_FAILED", username + " already exists");
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Username already exists";
            return resp.dump();
        }
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Bad request";
        return resp.dump();
    }
}