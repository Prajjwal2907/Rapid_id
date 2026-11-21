#include "Login.hpp"
#include "FileStore.hpp"
#include "PasswordHasher.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>

using rapidaid::auth::PasswordHasher;
using rapidaid::io::FileStore;

namespace {

std::string sanitizeField(const std::string& input) {
    std::string s;
    s.reserve(input.size());
    for (char c : input) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 0x20 && uc != 0x7F) {
            s.push_back(c);
        }
    }
    if (s.size() > 200) {
        s.resize(200);
    }
    return s;
}

std::string trimString(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        start++;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        end--;
    }
    return s.substr(start, end - start);
}

const std::vector<std::string>& getCommonPasswords() {
    static const std::vector<std::string> kList = {
        "1234567890", "password123", "admin12345", "administrator",
        "qwertyuiop", "welcome123", "iloveyou123", "sunshine123",
        "princess123", "football123", "monkey1234", "charlie123",
        "donald1234", "password1234", "letmein1234", "trustno1123",
        "dragon1234", "baseball12", "superman123", "master1234",
        "michael123", "shadow1234", "jordan1234", "secret1234",
        "computer12", "dispatch123", "rapidaid123", "changeme123"
    };
    return kList;
}

} // namespace

// ============================================================================
// Logger implementation
// ============================================================================

Logger::Logger(std::string f) : filename(std::move(f)) {}

void Logger::log(std::string type, std::string details) {
    try {
        std::string cleanType = sanitizeField(type);
        std::string cleanDetails = sanitizeField(details);

        std::time_t t = std::time(nullptr);
        std::string ts = std::ctime(&t);
        if (!ts.empty() && ts.back() == '\n') {
            ts.pop_back();
        }

        std::string entry = ts + " | " + cleanType + " | " + cleanDetails + "\n";
        std::ofstream out(filename, std::ios::app | std::ios::binary);
        if (out.is_open()) {
            out.write(entry.data(), static_cast<std::streamsize>(entry.size()));
            out.close();

            std::error_code ec;
#ifndef _WIN32
            std::filesystem::permissions(
                filename,
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace,
                ec
            );
#else
            std::filesystem::permissions(
                filename,
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace,
                ec
            );
#endif
        }
    } catch (...) {}
}

// ============================================================================
// Auth implementation
// ============================================================================

Auth::Auth(AuthOptions opts) : opts_(std::move(opts)) {
    init();
}

Auth::Auth(std::string usersPath) {
    opts_.usersPath = std::move(usersPath);
    init();
}

long long Auth::now() const {
    if (opts_.clock) {
        return opts_.clock();
    }
    return static_cast<long long>(std::time(nullptr));
}

std::string Auth::toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::size_t Auth::hashPass(const std::string& p) const {
    return std::hash<std::string>{}(p);
}

bool Auth::ready() const {
    return ready_;
}

std::string Auth::notReadyReason() const {
    return notReadyReason_;
}

void Auth::init() {
    try {
        std::string existingKey = FileStore::load(opts_.keyPath);
        std::string trimmedKey = trimString(existingKey);

        bool keyValid = (trimmedKey.size() == 64);
        if (keyValid) {
            for (char c : trimmedKey) {
                if (!std::isxdigit(static_cast<unsigned char>(c))) {
                    keyValid = false;
                    break;
                }
            }
        }

        std::string usersContent = FileStore::load(opts_.usersPath);
        bool hasPbkdf2Users = (usersContent.find("pbkdf2-sha256$") != std::string::npos);

        if (keyValid) {
            pepper_ = trimmedKey;
        } else {
            if (hasPbkdf2Users) {
                ready_ = false;
                notReadyReason_ = "Missing or unreadable auth key for existing PBKDF2 users";
                return;
            }

            pepper_ = PasswordHasher::randomHex(32);
            if (pepper_.empty() || !FileStore::saveSecret(opts_.keyPath, pepper_ + "\n")) {
                ready_ = false;
                notReadyReason_ = "Failed to create auth key file";
                return;
            }
        }

        dummyHash_ = PasswordHasher::hash("timing_dummy_defense_password", pepper_, opts_.pbkdf2Iterations);
        load();
    } catch (...) {
        ready_ = false;
        notReadyReason_ = "Initialization error";
    }
}

void Auth::load() {
    if (!ready_) return;

    users_.clear();
    userOrder_.clear();

    if (!FileStore::exists(opts_.usersPath)) {
        std::string adminPw;
        bool mustChange = false;
        if (!opts_.bootstrapAdminPassword.empty()) {
            adminPw = opts_.bootstrapAdminPassword;
            mustChange = false;
        } else {
            adminPw = PasswordHasher::randomPassword(16);
            mustChange = true;
            if (opts_.announceBootstrap) {
                std::cout << "========================================\n"
                          << "[RapidAid Auth Bootstrap]\n"
                          << "Initial admin account created:\n"
                          << "  Username: admin\n"
                          << "  Password: " << adminPw << "\n"
                          << "This password MUST be changed on first login.\n"
                          << "========================================\n";
            }
        }

        UserRecord adminRec;
        adminRec.originalUsername = "admin";
        adminRec.isLegacy = false;
        adminRec.hash = PasswordHasher::hash(adminPw, pepper_, opts_.pbkdf2Iterations);
        adminRec.role = "admin";
        adminRec.status = "active";
        adminRec.mustChangePassword = mustChange;
        adminRec.createdAt = now();

        users_["admin"] = adminRec;
        userOrder_.push_back("admin");
        save();
        return;
    }

    std::string content = FileStore::load(opts_.usersPath);
    if (trimString(content).empty()) {
        std::string adminPw;
        bool mustChange = false;
        if (!opts_.bootstrapAdminPassword.empty()) {
            adminPw = opts_.bootstrapAdminPassword;
            mustChange = false;
        } else {
            adminPw = PasswordHasher::randomPassword(16);
            mustChange = true;
            if (opts_.announceBootstrap) {
                std::cout << "========================================\n"
                          << "[RapidAid Auth Bootstrap]\n"
                          << "Initial admin account created:\n"
                          << "  Username: admin\n"
                          << "  Password: " << adminPw << "\n"
                          << "This password MUST be changed on first login.\n"
                          << "========================================\n";
            }
        }

        UserRecord adminRec;
        adminRec.originalUsername = "admin";
        adminRec.isLegacy = false;
        adminRec.hash = PasswordHasher::hash(adminPw, pepper_, opts_.pbkdf2Iterations);
        adminRec.role = "admin";
        adminRec.status = "active";
        adminRec.mustChangePassword = mustChange;
        adminRec.createdAt = now();

        users_["admin"] = adminRec;
        userOrder_.push_back("admin");
        save();
        return;
    }

    std::istringstream stream(content);
    std::string line;
    std::size_t admin123Hash = std::hash<std::string>{}("admin123");

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::string trimmed = trimString(line);
        if (trimmed.empty()) {
            continue;
        }

        std::istringstream lineStream(trimmed);
        std::vector<std::string> tokens;
        std::string tok;
        while (lineStream >> tok) {
            tokens.push_back(tok);
        }

        if (tokens.size() == 6) {
            const std::string& uname = tokens[0];
            const std::string& h = tokens[1];
            const std::string& role = tokens[2];
            const std::string& status = tokens[3];
            const std::string& mustChangeStr = tokens[4];
            const std::string& createdAtStr = tokens[5];

            if (h.rfind("pbkdf2-sha256$", 0) != 0) {
                ready_ = false;
                notReadyReason_ = "Invalid hash algorithm in users file";
                return;
            }
            if (role != "admin" && role != "dispatcher" && role != "unit") {
                ready_ = false;
                notReadyReason_ = "Invalid role in users file";
                return;
            }
            if (status != "active" && status != "pending" && status != "disabled") {
                ready_ = false;
                notReadyReason_ = "Invalid status in users file";
                return;
            }
            if (mustChangeStr != "0" && mustChangeStr != "1") {
                ready_ = false;
                notReadyReason_ = "Invalid mustChange flag in users file";
                return;
            }

            long long createdEpoch = 0;
            try {
                createdEpoch = std::stoll(createdAtStr);
            } catch (...) {
                ready_ = false;
                notReadyReason_ = "Invalid createdAt timestamp in users file";
                return;
            }

            std::string lowerName = toLower(uname);
            if (users_.find(lowerName) != users_.end()) {
                ready_ = false;
                notReadyReason_ = "Duplicate username in users file";
                return;
            }

            UserRecord rec;
            rec.originalUsername = uname;
            rec.isLegacy = false;
            rec.hash = h;
            rec.role = role;
            rec.status = status;
            rec.mustChangePassword = (mustChangeStr == "1");
            rec.createdAt = createdEpoch;

            users_[lowerName] = rec;
            userOrder_.push_back(lowerName);
        } else if (tokens.size() == 3) {
            const std::string& uname = tokens[0];
            const std::string& hashNumStr = tokens[1];
            const std::string& role = tokens[2];

            if (role != "admin" && role != "dispatcher" && role != "unit") {
                ready_ = false;
                notReadyReason_ = "Invalid role in legacy user record";
                return;
            }

            std::size_t legacyH = 0;
            try {
                for (char c : hashNumStr) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) {
                        ready_ = false;
                        notReadyReason_ = "Invalid legacy hash in users file";
                        return;
                    }
                }
                legacyH = static_cast<std::size_t>(std::stoull(hashNumStr));
            } catch (...) {
                ready_ = false;
                notReadyReason_ = "Invalid legacy hash in users file";
                return;
            }

            std::string lowerName = toLower(uname);
            if (users_.find(lowerName) != users_.end()) {
                ready_ = false;
                notReadyReason_ = "Duplicate username in users file";
                return;
            }

            UserRecord rec;
            rec.originalUsername = uname;
            rec.isLegacy = true;
            rec.legacyHash = legacyH;
            rec.role = role;
            rec.status = "active";
            rec.mustChangePassword = (legacyH == admin123Hash);
            rec.createdAt = now();

            users_[lowerName] = rec;
            userOrder_.push_back(lowerName);
        } else {
            ready_ = false;
            notReadyReason_ = "Malformed line in users file";
            return;
        }
    }
}

void Auth::save() {
    if (!ready_) return;

    std::ostringstream out;
    out.imbue(std::locale::classic());

    for (const auto& key : userOrder_) {
        auto it = users_.find(key);
        if (it == users_.end()) continue;
        const auto& u = it->second;

        if (u.isLegacy) {
            out << u.originalUsername << " " << u.legacyHash << " " << u.role << "\n";
        } else {
            out << u.originalUsername << " " << u.hash << " " << u.role << " "
                << u.status << " " << (u.mustChangePassword ? "1" : "0") << " "
                << u.createdAt << "\n";
        }
    }

    FileStore::saveSecret(opts_.usersPath, out.str());
}

void Auth::trackFailure(const std::string& lowerUser, long long currentTime) {
    if (lockouts_.size() >= 10000) {
        for (auto it = lockouts_.begin(); it != lockouts_.end(); ) {
            if (currentTime - it->second.lastFailureTime > 900 && it->second.lockedUntil <= currentTime) {
                it = lockouts_.erase(it);
            } else {
                ++it;
            }
        }
        if (lockouts_.size() >= 10000) {
            auto oldestIt = lockouts_.begin();
            for (auto it = lockouts_.begin(); it != lockouts_.end(); ++it) {
                if (it->second.lastFailureTime < oldestIt->second.lastFailureTime) {
                    oldestIt = it;
                }
            }
            lockouts_.erase(oldestIt);
        }
    }

    auto& info = lockouts_[lowerUser];
    if (info.failureCount == 0 || (currentTime - info.firstFailureTime > 900)) {
        info.failureCount = 1;
        info.firstFailureTime = currentTime;
        info.lockedUntil = 0;
    } else {
        info.failureCount++;
    }
    info.lastFailureTime = currentTime;

    if (info.failureCount >= 5) {
        int extra = info.failureCount - 5;
        if (extra > 10) extra = 10;
        long long duration = 30LL * (1LL << extra);
        if (duration > 900) duration = 900;
        info.lockedUntil = currentTime + duration;
    }
}

void Auth::resetLockout(const std::string& lowerUser) {
    lockouts_.erase(lowerUser);
}

int Auth::getRetryAfter(const std::string& lowerUser, long long currentTime) const {
    auto it = lockouts_.find(lowerUser);
    if (it != lockouts_.end() && it->second.lockedUntil > currentTime) {
        return static_cast<int>(it->second.lockedUntil - currentTime);
    }
    return 0;
}

LoginResult Auth::login2(const std::string& user, const std::string& pw) {
    LoginResult result;
    result.status = LoginResult::BadCredentials;
    result.mustChangePassword = false;
    result.retryAfterSeconds = 0;

    if (!ready_) {
        return result;
    }

    long long t = now();
    std::string lowerUser = toLower(user);

    int retryAfter = getRetryAfter(lowerUser, t);
    if (retryAfter > 0) {
        result.status = LoginResult::Locked;
        result.retryAfterSeconds = retryAfter;
        return result;
    }

    auto it = users_.find(lowerUser);
    if (it == users_.end()) {
        PasswordHasher::verify(pw, pepper_, dummyHash_);
        trackFailure(lowerUser, t);
        int after = getRetryAfter(lowerUser, t);
        if (after > 0) {
            result.status = LoginResult::Locked;
            result.retryAfterSeconds = after;
        } else {
            result.status = LoginResult::BadCredentials;
        }
        return result;
    }

    UserRecord& rec = it->second;
    bool match = false;
    if (rec.isLegacy) {
        // Comment: legacy check exists only for one-time upgrade
        match = (hashPass(pw) == rec.legacyHash);
        PasswordHasher::verify(pw, pepper_, dummyHash_);
    } else {
        match = PasswordHasher::verify(pw, pepper_, rec.hash);
    }

    if (!match) {
        trackFailure(lowerUser, t);
        int after = getRetryAfter(lowerUser, t);
        if (after > 0) {
            result.status = LoginResult::Locked;
            result.retryAfterSeconds = after;
        } else {
            result.status = LoginResult::BadCredentials;
        }
        return result;
    }

    resetLockout(lowerUser);

    if (rec.status == "pending") {
        result.status = LoginResult::Pending;
        result.role = rec.role;
        result.mustChangePassword = rec.mustChangePassword;
        return result;
    }
    if (rec.status == "disabled") {
        result.status = LoginResult::Disabled;
        result.role = rec.role;
        result.mustChangePassword = rec.mustChangePassword;
        return result;
    }

    if (rec.isLegacy) {
        rec.hash = PasswordHasher::hash(pw, pepper_, opts_.pbkdf2Iterations);
        rec.isLegacy = false;
        save();
    }

    result.status = LoginResult::Ok;
    result.role = rec.role;
    result.mustChangePassword = rec.mustChangePassword;
    return result;
}

bool Auth::login(const std::string& user, const std::string& pw) {
    return login2(user, pw).status == LoginResult::Ok;
}

std::string Auth::getRole(const std::string& user) const {
    auto it = users_.find(toLower(user));
    if (it == users_.end()) {
        return "";
    }
    return it->second.role;
}

SignUpResult Auth::signUp(const std::string& user, const std::string& pw, const std::string& role, bool active) {
    if (!ready_) {
        return {false, "not_ready", notReadyReason_};
    }
    if (!isValidUsername(user)) {
        return {false, "invalid_username", "Username must be 3-32 characters of [A-Za-z0-9._-]"};
    }
    std::string lowerUser = toLower(user);
    if (users_.find(lowerUser) != users_.end()) {
        return {false, "duplicate_username", "Username already exists"};
    }
    if (role != "admin" && role != "dispatcher" && role != "unit") {
        return {false, "invalid_role", "Invalid role"};
    }

    auto pc = checkPasswordPolicy(pw, user);
    if (!pc.ok) {
        return {false, "weak_password", pc.reason};
    }

    UserRecord rec;
    rec.originalUsername = user;
    rec.isLegacy = false;
    rec.hash = PasswordHasher::hash(pw, pepper_, opts_.pbkdf2Iterations);
    rec.role = role;
    rec.status = active ? "active" : "pending";
    rec.mustChangePassword = false;
    rec.createdAt = now();

    users_[lowerUser] = rec;
    userOrder_.push_back(lowerUser);
    save();

    return {true, "ok", ""};
}

bool Auth::signUp(const std::string& user, const std::string& pw, const std::string& role) {
    return signUp(user, pw, role, true).ok;
}

bool Auth::changePassword(const std::string& user, const std::string& oldPw, const std::string& newPw, std::string& err) {
    if (!ready_) {
        err = notReadyReason_;
        return false;
    }
    std::string lowerUser = toLower(user);
    auto it = users_.find(lowerUser);
    if (it == users_.end()) {
        err = "User not found";
        return false;
    }

    UserRecord& rec = it->second;
    bool match = false;
    if (rec.isLegacy) {
        match = (hashPass(oldPw) == rec.legacyHash);
    } else {
        match = PasswordHasher::verify(oldPw, pepper_, rec.hash);
    }
    if (!match) {
        err = "Invalid current password";
        return false;
    }

    auto pc = checkPasswordPolicy(newPw, user);
    if (!pc.ok) {
        err = pc.reason;
        return false;
    }

    rec.hash = PasswordHasher::hash(newPw, pepper_, opts_.pbkdf2Iterations);
    rec.isLegacy = false;
    rec.mustChangePassword = false;
    save();
    return true;
}

bool Auth::adminResetPassword(const std::string& user, std::string& tempOut, std::string& err) {
    if (!ready_) {
        err = notReadyReason_;
        return false;
    }
    std::string lowerUser = toLower(user);
    auto it = users_.find(lowerUser);
    if (it == users_.end()) {
        err = "User not found";
        return false;
    }

    tempOut = PasswordHasher::randomPassword(16);
    it->second.hash = PasswordHasher::hash(tempOut, pepper_, opts_.pbkdf2Iterations);
    it->second.isLegacy = false;
    it->second.mustChangePassword = true;
    save();
    return true;
}

bool Auth::setStatus(const std::string& user, const std::string& status, std::string& err) {
    if (!ready_) {
        err = notReadyReason_;
        return false;
    }
    if (status != "active" && status != "disabled") {
        err = "Invalid status";
        return false;
    }
    std::string lowerUser = toLower(user);
    auto it = users_.find(lowerUser);
    if (it == users_.end()) {
        err = "User not found";
        return false;
    }

    if (status == "disabled" && it->second.role == "admin" && it->second.status == "active") {
        int activeAdmins = 0;
        for (const auto& pair : users_) {
            if (pair.second.role == "admin" && pair.second.status == "active") {
                activeAdmins++;
            }
        }
        if (activeAdmins <= 1) {
            err = "Cannot disable last active admin";
            return false;
        }
    }

    it->second.status = status;
    save();
    return true;
}

bool Auth::approve(const std::string& user, std::string& err) {
    if (!ready_) {
        err = notReadyReason_;
        return false;
    }
    std::string lowerUser = toLower(user);
    auto it = users_.find(lowerUser);
    if (it == users_.end()) {
        err = "User not found";
        return false;
    }
    if (it->second.status != "pending") {
        err = "User is not pending";
        return false;
    }
    it->second.status = "active";
    save();
    return true;
}

std::vector<UserInfo> Auth::listUsers() const {
    std::vector<UserInfo> list;
    list.reserve(userOrder_.size());
    for (const auto& key : userOrder_) {
        auto it = users_.find(key);
        if (it != users_.end()) {
            UserInfo info;
            info.username = it->second.originalUsername;
            info.role = it->second.role;
            info.status = it->second.status;
            info.mustChangePassword = it->second.mustChangePassword;
            info.createdAt = it->second.createdAt;
            list.push_back(info);
        }
    }
    return list;
}

bool Auth::isActive(const std::string& user) const {
    auto it = users_.find(toLower(user));
    if (it == users_.end()) {
        return false;
    }
    return it->second.status == "active";
}

PasswordCheck Auth::checkPasswordPolicy(const std::string& pw, const std::string& username) {
    if (pw.size() < 10 || pw.size() > 128) {
        return {false, "Password must be between 10 and 128 characters"};
    }

    if (username.size() >= 3) {
        std::string lowerPw = toLower(pw);
        std::string lowerUser = toLower(username);
        if (lowerPw.find(lowerUser) != std::string::npos) {
            return {false, "Password must not contain username"};
        }
    }

    std::string lowerPw = toLower(pw);
    for (const auto& common : getCommonPasswords()) {
        if (lowerPw == common) {
            return {false, "Password is too common"};
        }
    }

    return {true, ""};
}

bool Auth::isValidUsername(const std::string& u) {
    if (u.size() < 3 || u.size() > 32) {
        return false;
    }
    for (char c : u) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Console menu & HTTP Request handlers
// ============================================================================

void dispatcherMenu(Auth& auth, Logger& logger, std::string user) {
    bool isAdmin = (auth.getRole(user) == "admin");
    int choice;

    while (true) {
        std::cout << "\n--- Rapid Aid Menu (" << user << ") ---\n"
                  << "1. New incident report\n"
                  << "2. Active incidents\n"
                  << "3. Past reports\n"
                  << "4. Statistics\n";
        if (isAdmin) std::cout << "5. Add new dispatcher\n";
        std::cout << "0. Logout\nChoice: ";
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::string discard;
            std::cin >> discard;
            continue;
        }

        if (choice == 0) {
            logger.log("LOGOUT", user);
            return;
        } else if (choice == 5 && isAdmin) {
            std::string u, p;
            std::cout << "New dispatcher username: ";
            std::cin >> u;
            std::cout << "New dispatcher password: ";
            std::cin >> p;
            auto res = auth.signUp(u, p, "dispatcher", true);
            if (res.ok) {
                std::cout << "Dispatcher added.\n";
                logger.log("USER_ADDED", u + " added by " + user);
            } else {
                std::cout << "Failed to add dispatcher: " << res.error << "\n";
            }
        } else if (choice >= 1 && choice <= 4) {
            std::cout << "Coming soon (teammates' modules connect here)\n";
        } else {
            std::cout << "Invalid choice.\n";
        }
    }
}

#ifdef LOGIN_STANDALONE
int main() {
    Auth auth;
    Logger logger;

    std::cout << "=== Rapid Aid - Dispatcher Login ===\n";

    for (int attempt = 1; attempt <= 3; attempt++) {
        std::string u, p;
        std::cout << "\nUsername: ";
        std::cin >> u;
        std::cout << "Password: ";
        std::cin >> p;

        auto res = auth.login2(u, p);
        if (res.status == LoginResult::Ok) {
            logger.log("LOGIN_SUCCESS", u);
            std::cout << "Welcome, " << u << "!\n";
            dispatcherMenu(auth, logger, u);
            return 0;
        }

        logger.log("LOGIN_FAILED", u);
        std::cout << "Wrong username or password. "
                  << 3 - attempt << " attempts left.\n";
    }

    logger.log("LOGIN_LOCKED", "3 failed attempts");
    std::cout << "Too many failed attempts. Exiting.\n";
    return 0;
}
#endif

std::string handleLoginRequest(Auth& auth, const std::string& requestJson) {
    try {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(requestJson);
        } catch (...) {
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

        LoginResult res = auth.login2(username, password);
        Logger logger;

        if (res.status == LoginResult::Ok) {
            logger.log("LOGIN_SUCCESS", username);
            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["username"] = username;
            resp["role"] = res.role;
            resp["mustChangePassword"] = res.mustChangePassword;
            return resp.dump();
        } else if (res.status == LoginResult::Locked) {
            logger.log("LOGIN_LOCKED", username);
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Account is temporarily locked";
            resp["code"] = "locked";
            resp["retryAfterSeconds"] = res.retryAfterSeconds;
            return resp.dump();
        } else if (res.status == LoginResult::Pending) {
            logger.log("LOGIN_PENDING", username);
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Account pending approval";
            resp["code"] = "pending";
            resp["retryAfterSeconds"] = 0;
            return resp.dump();
        } else if (res.status == LoginResult::Disabled) {
            logger.log("LOGIN_DISABLED", username);
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Account is disabled";
            resp["code"] = "disabled";
            resp["retryAfterSeconds"] = 0;
            return resp.dump();
        } else {
            logger.log("LOGIN_FAILED", username);
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = "Invalid username or password";
            resp["code"] = "bad_credentials";
            resp["retryAfterSeconds"] = 0;
            return resp.dump();
        }
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
        } catch (...) {
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

        SignUpResult sr = auth.signUp(username, password, role, true);
        Logger logger;

        if (sr.ok) {
            logger.log("USER_SIGNUP", username + " (" + role + ")");
            nlohmann::ordered_json resp;
            resp["ok"] = true;
            resp["username"] = username;
            resp["role"] = role;
            return resp.dump();
        } else {
            logger.log("SIGNUP_FAILED", username + ": " + sr.error);
            nlohmann::ordered_json resp;
            resp["ok"] = false;
            resp["error"] = sr.error;
            resp["code"] = sr.code;
            return resp.dump();
        }
    } catch (...) {
        nlohmann::ordered_json resp;
        resp["ok"] = false;
        resp["error"] = "Bad request";
        return resp.dump();
    }
}
