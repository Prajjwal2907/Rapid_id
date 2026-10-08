#include <iostream>
#include <fstream>
#include <unordered_map>
#include <string>
#include <ctime>
using namespace std;

class Logger {
    string filename;
public:
    Logger(string f = "log.txt") : filename(f) {}

    void log(string type, string details) {
        ofstream out(filename, ios::app);
        time_t t = time(nullptr);
        string ts = ctime(&t);
        ts.pop_back();
        out << ts << " | " << type << " | " << details << "\n";
    }
};

struct User {
    size_t passHash;
    string role;
};

class Auth {
    unordered_map<string, User> users;
    string filename;

    size_t hashPass(const string& p) {
        return hash<string>{}(p);
    }

public:
    Auth(string f = "users.txt") : filename(f) {
        load();
        if (users.empty()) {
            signUp("admin", "admin123", "admin");
        }
    }

    void load() {
        ifstream in(filename);
        string name, role;
        size_t h;
        while (in >> name >> h >> role) {
            users[name] = {h, role};
        }
    }

    void save() {
        ofstream out(filename);
        for (auto& p : users) {
            out << p.first << " " << p.second.passHash
                << " " << p.second.role << "\n";
        }
    }

    bool signUp(string u, string p, string role) {
        if (users.count(u)) return false;
        users[u] = {hashPass(p), role};
        save();
        return true;
    }

    bool login(string u, string p) {
        auto it = users.find(u);
        if (it == users.end()) return false;
        return it->second.passHash == hashPass(p);
    }

    string getRole(string u) { return users[u].role; }
};

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