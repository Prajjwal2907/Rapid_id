#include "Login.hpp"
#include "FileStore.hpp"
#include "json.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;
using rapidaid::io::FileStore;

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
    std::cout << "Starting Auth & Login Offline Tests...\n";

    fs::path origDir = fs::current_path();
    fs::path tempDir = origDir / "build" / "test_tmp_login";

    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    long long currentTestTime = 1700000000LL;
    auto clockFunc = [&currentTestTime]() -> long long {
        return currentTestTime;
    };

    const std::string bootstrapPass = "Admin-Test-Pass-9";
    const std::string pass1 = "DispatcherSecr3t!P@ss";
    const std::string passWrong = "wrong_password_attempt";
    std::vector<std::string> allResponses;

    AuthOptions opts;
    opts.usersPath = (tempDir / "users.txt").string();
    opts.keyPath = (tempDir / "auth.key").string();
    opts.bootstrapAdminPassword = bootstrapPass;
    opts.pbkdf2Iterations = 1000;
    opts.clock = clockFunc;
    opts.announceBootstrap = false;

    // Test 1: Bootstrap admin login
    {
        Auth auth(opts);
        check(auth.ready(), "Test 1: Auth is ready on bootstrap", failed);

        LoginResult res = auth.login2("admin", bootstrapPass);
        check(res.status == LoginResult::Ok && res.role == "admin" && !res.mustChangePassword,
              "Test 1: Bootstrap admin login succeeds with correct role", failed);

        std::string reqAdmin = "{\"username\":\"admin\",\"password\":\"" + bootstrapPass + "\"}";
        std::string resAdmin = handleLoginRequest(auth, reqAdmin);
        allResponses.push_back(resAdmin);

        auto j = json::parse(resAdmin);
        check(j["ok"] == true && j["username"] == "admin" && j["role"] == "admin" && j["mustChangePassword"] == false,
              "Test 1: handleLoginRequest succeeds for bootstrap admin", failed);
    }

    // Reopen Auth to ensure persistence and test policy + duplicates
    {
        Auth auth(opts);
        check(auth.ready(), "Auth remains ready after reload", failed);

        // Test 2: Policy rejects
        auto rShort = auth.signUp("user_short", "Short9!", "dispatcher", true);
        check(!rShort.ok && rShort.code == "weak_password", "Test 2: Policy rejects password < 10 chars", failed);

        auto rContainsUser = auth.signUp("myuser", "Pass_myuser_Word!", "dispatcher", true);
        check(!rContainsUser.ok && rContainsUser.code == "weak_password",
              "Test 2: Policy rejects password containing username", failed);

        auto rCommon = auth.signUp("user_common", "password123", "dispatcher", true);
        check(!rCommon.ok && rCommon.code == "weak_password", "Test 2: Policy rejects common password", failed);

        check(!Auth::isValidUsername("ab"), "Test 2: Username < 3 chars invalid", failed);
        check(!Auth::isValidUsername("bad user"), "Test 2: Username with space invalid", failed);
        check(!Auth::isValidUsername("user@bad"), "Test 2: Username with @ invalid", failed);
        check(Auth::isValidUsername("valid.user-123_ok"), "Test 2: Valid characters accepted in username", failed);

        auto rGood = auth.signUp("dispatcher_alpha", pass1, "dispatcher", true);
        check(rGood.ok, "Test 2: Valid signup succeeds", failed);

        // Test 3: Duplicate username case-insensitive
        auto rDup1 = auth.signUp("DISPATCHER_ALPHA", "DifferentValidPass99!", "dispatcher", true);
        check(!rDup1.ok && rDup1.code == "duplicate_username",
              "Test 3: Duplicate username rejected uppercase", failed);

        auto rDup2 = auth.signUp("Dispatcher_Alpha", "DifferentValidPass99!", "dispatcher", true);
        check(!rDup2.ok && rDup2.code == "duplicate_username",
              "Test 3: Duplicate username rejected mixed case", failed);

        // Test 4: Wrong password and unknown user give identical error text
        std::string reqWrongPass = "{\"username\":\"dispatcher_alpha\",\"password\":\"" + passWrong + "\"}";
        std::string resWrongPass = handleLoginRequest(auth, reqWrongPass);
        allResponses.push_back(resWrongPass);

        std::string reqUnknownUser = "{\"username\":\"ghost_user\",\"password\":\"" + pass1 + "\"}";
        std::string resUnknownUser = handleLoginRequest(auth, reqUnknownUser);
        allResponses.push_back(resUnknownUser);

        auto jWrong = json::parse(resWrongPass);
        auto jUnknown = json::parse(resUnknownUser);
        check(jWrong["ok"] == false && jWrong["error"] == "Invalid username or password" && jWrong["code"] == "bad_credentials",
              "Test 4: Wrong password gives expected error and code", failed);
        check(jUnknown["ok"] == false && jUnknown["error"] == "Invalid username or password" && jUnknown["code"] == "bad_credentials",
              "Test 4: Unknown user gives expected error and code", failed);
        check(jWrong["error"] == jUnknown["error"] && jWrong["code"] == jUnknown["code"],
              "Test 4: Error text and code are exactly identical for wrong password and unknown user", failed);

        // Test 5: Lockout after 5 failures and recovery when clock advances
        std::string lockUser = "lockout_tester";
        std::string lockPass = "LockTesterSecr3t!";
        auth.signUp(lockUser, lockPass, "dispatcher", true);

        for (int i = 0; i < 5; ++i) {
            auth.login2(lockUser, "bad_attempt");
        }

        LoginResult resLocked = auth.login2(lockUser, lockPass);
        check(resLocked.status == LoginResult::Locked && resLocked.retryAfterSeconds > 0,
              "Test 5: User is locked out after 5 failures", failed);

        std::string reqLocked = "{\"username\":\"" + lockUser + "\",\"password\":\"" + lockPass + "\"}";
        std::string resJsonLocked = handleLoginRequest(auth, reqLocked);
        allResponses.push_back(resJsonLocked);
        auto jLocked = json::parse(resJsonLocked);
        check(jLocked["ok"] == false && jLocked["code"] == "locked" && jLocked["retryAfterSeconds"].get<int>() > 0,
              "Test 5: handleLoginRequest returns code=locked and retryAfterSeconds > 0", failed);

        // Advance test clock beyond lockout window (30s)
        currentTestTime += 35;

        LoginResult resUnlocked = auth.login2(lockUser, lockPass);
        check(resUnlocked.status == LoginResult::Ok,
              "Test 5: User recovered from lockout when clock advanced", failed);

        // Test 6: Pending/disabled reported only after correct password
        std::string pendUser = "pending_operator";
        std::string pendPass = "OperatorSecr3t!1";
        auth.signUp(pendUser, pendPass, "unit", false);

        LoginResult lrPendWrong = auth.login2(pendUser, "wrong_pw_for_pending");
        check(lrPendWrong.status == LoginResult::BadCredentials,
              "Test 6: Pending user with wrong password returns BadCredentials", failed);

        LoginResult lrPendCorrect = auth.login2(pendUser, pendPass);
        check(lrPendCorrect.status == LoginResult::Pending,
              "Test 6: Pending user with correct password returns Pending", failed);

        std::string disUser = "disabled_operator";
        std::string disPass = "DisabledSecr3t!2";
        auth.signUp(disUser, disPass, "dispatcher", true);
        std::string err;
        auth.setStatus(disUser, "disabled", err);

        LoginResult lrDisWrong = auth.login2(disUser, "wrong_pw_for_disabled");
        check(lrDisWrong.status == LoginResult::BadCredentials,
              "Test 6: Disabled user with wrong password returns BadCredentials", failed);

        LoginResult lrDisCorrect = auth.login2(disUser, disPass);
        check(lrDisCorrect.status == LoginResult::Disabled,
              "Test 6: Disabled user with correct password returns Disabled", failed);

        // Verify last active admin cannot be disabled
        bool disableAdminOk = auth.setStatus("admin", "disabled", err);
        check(!disableAdminOk && err == "Cannot disable last active admin",
              "Test 6: Last active admin cannot be disabled", failed);
    }

    // Test 7: Legacy 3-field lines load, log in and are upgraded in place
    {
        fs::path legDir = tempDir / "legacy_test";
        fs::create_directories(legDir, ec);

        std::string legUsers = (legDir / "users.txt").string();
        std::string legKey = (legDir / "auth.key").string();

        std::string legacyPass = "LegacyUserPass123!";
        std::size_t legacyHash = std::hash<std::string>{}(legacyPass);
        std::string legacyFileContent = "legacy_user " + std::to_string(legacyHash) + " dispatcher\n";
        FileStore::save(legUsers, legacyFileContent);

        AuthOptions legOpts;
        legOpts.usersPath = legUsers;
        legOpts.keyPath = legKey;
        legOpts.pbkdf2Iterations = 1000;
        legOpts.clock = clockFunc;
        legOpts.announceBootstrap = false;

        Auth legAuth(legOpts);
        check(legAuth.ready(), "Test 7: Auth loads legacy 3-field lines successfully", failed);
        check(legAuth.isActive("legacy_user"), "Test 7: Legacy user is active", failed);

        LoginResult lrLeg = legAuth.login2("legacy_user", legacyPass);
        check(lrLeg.status == LoginResult::Ok && !lrLeg.mustChangePassword,
              "Test 7: Legacy user logs in successfully", failed);

        std::string upgradedContent = FileStore::load(legUsers);
        check(upgradedContent.find("pbkdf2-sha256$") != std::string::npos,
              "Test 7: Legacy user upgraded IN PLACE to pbkdf2 format", failed);
    }

    // Test 8: Legacy admin123 forces mustChange
    {
        fs::path admin123Dir = tempDir / "admin123_test";
        fs::create_directories(admin123Dir, ec);

        std::string aUsers = (admin123Dir / "users.txt").string();
        std::string aKey = (admin123Dir / "auth.key").string();

        std::size_t hAdmin123 = std::hash<std::string>{}("admin123");
        std::string aContent = "admin " + std::to_string(hAdmin123) + " admin\n";
        FileStore::save(aUsers, aContent);

        AuthOptions aOpts;
        aOpts.usersPath = aUsers;
        aOpts.keyPath = aKey;
        aOpts.pbkdf2Iterations = 1000;
        aOpts.clock = clockFunc;
        aOpts.announceBootstrap = false;

        Auth aAuth(aOpts);
        check(aAuth.ready(), "Test 8: Auth loads legacy admin123 file", failed);

        LoginResult lrAdmin = aAuth.login2("admin", "admin123");
        check(lrAdmin.status == LoginResult::Ok && lrAdmin.mustChangePassword == true,
              "Test 8: Legacy admin123 forces mustChangePassword=true", failed);
    }

    // Test 9: Malformed users.txt gives ready()==false and file unchanged
    {
        fs::path malDir = tempDir / "malformed_test";
        fs::create_directories(malDir, ec);

        std::string mUsers = (malDir / "users.txt").string();
        std::string mKey = (malDir / "auth.key").string();

        std::string corruptContent = "corrupt_line_only_two_tokens invalid\n";
        FileStore::save(mUsers, corruptContent);

        AuthOptions mOpts;
        mOpts.usersPath = mUsers;
        mOpts.keyPath = mKey;
        mOpts.pbkdf2Iterations = 1000;
        mOpts.clock = clockFunc;
        mOpts.announceBootstrap = false;

        Auth mAuth(mOpts);
        check(!mAuth.ready(), "Test 9: Malformed users.txt gives ready()==false", failed);
        check(!mAuth.notReadyReason().empty(), "Test 9: notReadyReason is set on malformed file", failed);

        std::string checkUnchanged = FileStore::load(mUsers);
        check(checkUnchanged == corruptContent, "Test 9: Malformed users file is NEVER overwritten", failed);
    }

    // Test 10: Missing auth.key with pbkdf2 users gives ready()==false
    {
        fs::path missingKeyDir = tempDir / "missing_key_test";
        fs::create_directories(missingKeyDir, ec);

        std::string mkUsers = (missingKeyDir / "users.txt").string();
        std::string mkKey = (missingKeyDir / "nonexistent.key").string();

        std::string pbkdf2Line = "admin pbkdf2-sha256$1000$0123456789abcdef0123456789abcdef$0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef admin active 0 1700000000\n";
        FileStore::save(mkUsers, pbkdf2Line);

        AuthOptions mkOpts;
        mkOpts.usersPath = mkUsers;
        mkOpts.keyPath = mkKey;
        mkOpts.pbkdf2Iterations = 1000;
        mkOpts.clock = clockFunc;
        mkOpts.announceBootstrap = false;

        Auth mkAuth(mkOpts);
        check(!mkAuth.ready(), "Test 10: Missing auth.key with pbkdf2 users gives ready()==false", failed);
        check(!FileStore::exists(mkKey), "Test 10: Missing key is NEVER regenerated when pbkdf2 users exist", failed);
    }

    // Test 11: users.txt contains no plaintext password
    {
        std::string usersTxtContent = FileStore::load(opts.usersPath);
        bool leakedInUsers = (usersTxtContent.find(bootstrapPass) != std::string::npos ||
                              usersTxtContent.find(pass1) != std::string::npos ||
                              usersTxtContent.find(passWrong) != std::string::npos);
        check(!leakedInUsers, "Test 11: users.txt contains no plaintext passwords", failed);
    }

    // Test 12: Log has no passwords and no raw newline from hostile username
    {
        std::string logFilePath = (tempDir / "test_log.txt").string();
        Logger testLogger(logFilePath);

        std::string hostileUser = "attacker\nadmin\r\nrole";
        testLogger.log("LOGIN_FAILED", hostileUser);

        std::string logContent = FileStore::load(logFilePath);
        check(logContent.find(bootstrapPass) == std::string::npos &&
              logContent.find(pass1) == std::string::npos,
              "Test 12: Log contains no plaintext passwords", failed);

        // Verify there is only one newline at the end of the entry
        size_t newlineCount = 0;
        for (char c : logContent) {
            if (c == '\n') newlineCount++;
        }
        check(newlineCount == 1, "Test 12: Hostile username raw newlines stripped from log", failed);
    }

    // Test 13: Response JSON never contains password or hash
    {
        bool responseLeakedSecret = false;
        for (const auto& resp : allResponses) {
            if (resp.find(bootstrapPass) != std::string::npos ||
                resp.find(pass1) != std::string::npos ||
                resp.find(passWrong) != std::string::npos ||
                resp.find("pbkdf2-sha256$") != std::string::npos) {
                responseLeakedSecret = true;
                break;
            }
        }
        check(!responseLeakedSecret, "Test 13: Response JSON never contains password or hash", failed);
    }

    // Cleanup temp directory
    fs::remove_all(tempDir, ec);
    check(!fs::exists(tempDir), "Temporary test directory cleanly removed", failed);

    std::cout << "\nLogin Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed == 0 ? 0 : 1;
}
