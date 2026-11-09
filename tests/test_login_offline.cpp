#include "Login.hpp"
#include "json.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

static void check(bool condition, const std::string& testName, int& failed) {
    if (condition) {
        std::cout << "[PASS] " << testName << "\n";
    } else {
        std::cout << "[FAIL] " << testName << "\n";
        failed++;
    }
}

int main() {
    int failed = 0;
    fs::path origDir = fs::current_path();
    fs::path tempDir = origDir / "build" / "test_tmp_login";

    // Setup isolated temporary environment
    fs::create_directories(tempDir);
    fs::current_path(tempDir);

    std::vector<std::string> allResponses;
    const std::string pass1 = "DispatcherSecr3t!P@ss";
    const std::string passAdmin = "admin123";
    const std::string passWrong = "wrong_password_attempt";

    {
        // Pass temporary path to Auth constructor
        std::string tempUsersFile = (tempDir / "users.txt").string();
        Auth auth(tempUsersFile);

        // Case 1: Existing Auth sign-up and login via handleLoginRequest
        bool signedUp = auth.signUp("dispatcher_alpha", pass1, "dispatcher");
        check(signedUp, "Auth::signUp creates a new dispatcher user", failed);

        std::string reqSuccess = "{\"username\":\"dispatcher_alpha\",\"password\":\"" + pass1 + "\"}";
        std::string resSuccess = handleLoginRequest(auth, reqSuccess);
        allResponses.push_back(resSuccess);

        bool successOk = false;
        try {
            auto j = json::parse(resSuccess);
            successOk = (j["ok"] == true && j["username"] == "dispatcher_alpha" && j["role"] == "dispatcher");
        } catch (...) {
            successOk = false;
        }
        check(successOk, "handleLoginRequest succeeds with correct username and role", failed);

        // Verify default admin account login as well
        std::string reqAdmin = "{\"username\":\"admin\",\"password\":\"" + passAdmin + "\"}";
        std::string resAdmin = handleLoginRequest(auth, reqAdmin);
        allResponses.push_back(resAdmin);

        bool adminOk = false;
        try {
            auto j = json::parse(resAdmin);
            adminOk = (j["ok"] == true && j["username"] == "admin" && j["role"] == "admin");
        } catch (...) {
            adminOk = false;
        }
        check(adminOk, "handleLoginRequest succeeds for default admin account", failed);

        // Case 2: Wrong password and unknown username give ok=false with identical error text
        std::string reqWrongPass = "{\"username\":\"dispatcher_alpha\",\"password\":\"" + passWrong + "\"}";
        std::string resWrongPass = handleLoginRequest(auth, reqWrongPass);
        allResponses.push_back(resWrongPass);

        std::string reqUnknownUser = "{\"username\":\"ghost_user\",\"password\":\"" + pass1 + "\"}";
        std::string resUnknownUser = handleLoginRequest(auth, reqUnknownUser);
        allResponses.push_back(resUnknownUser);

        bool wrongPassOk = false;
        std::string errWrong;
        try {
            auto j = json::parse(resWrongPass);
            wrongPassOk = (j["ok"] == false && j.contains("error"));
            if (wrongPassOk) errWrong = j["error"].get<std::string>();
        } catch (...) {
            wrongPassOk = false;
        }

        bool unknownUserOk = false;
        std::string errUnknown;
        try {
            auto j = json::parse(resUnknownUser);
            unknownUserOk = (j["ok"] == false && j.contains("error"));
            if (unknownUserOk) errUnknown = j["error"].get<std::string>();
        } catch (...) {
            unknownUserOk = false;
        }

        check(wrongPassOk && errWrong == "Invalid username or password",
              "Wrong password gives ok=false and error='Invalid username or password'", failed);
        check(unknownUserOk && errUnknown == "Invalid username or password",
              "Unknown username gives ok=false and error='Invalid username or password'", failed);
        check(errWrong == errUnknown,
              "Error text is exactly identical for wrong password and unknown username", failed);

        // Case 3: Invalid JSON, missing 'password' field, and numeric 'username' give {"ok":false,"error":"Bad request"}
        std::string resBadJson = handleLoginRequest(auth, "not a valid {json");
        allResponses.push_back(resBadJson);
        check(resBadJson == "{\"ok\":false,\"error\":\"Bad request\"}",
              "Invalid JSON gives exact Bad request response", failed);

        std::string resMissingPass = handleLoginRequest(auth, "{\"username\":\"dispatcher_alpha\"}");
        allResponses.push_back(resMissingPass);
        check(resMissingPass == "{\"ok\":false,\"error\":\"Bad request\"}",
              "Missing password field gives exact Bad request response", failed);

        std::string resMissingUser = handleLoginRequest(auth, "{\"password\":\"" + pass1 + "\"}");
        allResponses.push_back(resMissingUser);
        check(resMissingUser == "{\"ok\":false,\"error\":\"Bad request\"}",
              "Missing username field gives exact Bad request response", failed);

        std::string resNumericUser = handleLoginRequest(auth, "{\"username\":12345,\"password\":\"" + pass1 + "\"}");
        allResponses.push_back(resNumericUser);
        check(resNumericUser == "{\"ok\":false,\"error\":\"Bad request\"}",
              "Numeric username field gives exact Bad request response", failed);

        std::string resNumericPass = handleLoginRequest(auth, "{\"username\":\"dispatcher_alpha\",\"password\":99999}");
        allResponses.push_back(resNumericPass);
        check(resNumericPass == "{\"ok\":false,\"error\":\"Bad request\"}",
              "Numeric password field gives exact Bad request response", failed);

        std::string resArrayJson = handleLoginRequest(auth, "[1, 2, 3]");
        allResponses.push_back(resArrayJson);
        check(resArrayJson == "{\"ok\":false,\"error\":\"Bad request\"}",
              "Non-object JSON array gives exact Bad request response", failed);

        // Case 4: The response never contains the password text
        bool responseLeakedPassword = false;
        for (const auto& resp : allResponses) {
            if (resp.find(pass1) != std::string::npos ||
                resp.find(passAdmin) != std::string::npos ||
                resp.find(passWrong) != std::string::npos) {
                responseLeakedPassword = true;
                break;
            }
        }
        check(!responseLeakedPassword, "Login responses never contain plaintext password strings", failed);

        // Case 5: handleLogoutRequest returns ok=true
        std::string resLogout = handleLogoutRequest(auth, "dispatcher_alpha");
        check(resLogout == "{\"ok\":true}", "handleLogoutRequest returns exact {\"ok\":true}", failed);

        // Case 6: handleHealthRequest returns ok=true
        std::string resHealth = handleHealthRequest();
        check(resHealth == "{\"ok\":true,\"service\":\"login\"}",
              "handleHealthRequest returns exact {\"ok\":true,\"service\":\"login\"}", failed);

        // Case 7: The log file (if one is written) never contains the password text
        fs::path logFilePath = tempDir / "log.txt";
        bool logFileFound = fs::exists(logFilePath);
        if (logFileFound) {
            std::ifstream logIn(logFilePath);
            std::string logContent((std::istreambuf_iterator<char>(logIn)),
                                   std::istreambuf_iterator<char>());
            bool logLeakedPassword = (logContent.find(pass1) != std::string::npos ||
                                      logContent.find(passAdmin) != std::string::npos ||
                                      logContent.find(passWrong) != std::string::npos);
            check(!logLeakedPassword, "Log file exists and never contains plaintext passwords", failed);

            bool loggedEvents = (logContent.find("LOGIN_SUCCESS") != std::string::npos &&
                                 logContent.find("LOGIN_FAILED") != std::string::npos &&
                                 logContent.find("LOGOUT") != std::string::npos);
            check(loggedEvents, "Log file contains expected LOGIN_SUCCESS, LOGIN_FAILED, and LOGOUT entries", failed);
        } else {
            check(true, "Log file verification skipped as no log file was written", failed);
        }
    }

    // Restore working directory
    fs::current_path(origDir);

    // Verify workspace was never touched
    bool workspaceUsersExists = fs::exists(origDir / "users.txt");
    bool workspaceLogExists = fs::exists(origDir / "log.txt");
    check(!workspaceUsersExists, "Workspace root users.txt was never created or accessed", failed);
    check(!workspaceLogExists, "Workspace root log.txt was never created or accessed", failed);

    // Cleanup temp directory
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    check(!fs::exists(tempDir), "Temporary test directory build/test_tmp_login/ cleanly removed", failed);

    std::cout << "\nTest Summary: " << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed == 0 ? 0 : 1;
}
