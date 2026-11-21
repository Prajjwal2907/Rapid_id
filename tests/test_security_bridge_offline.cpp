#include "DataCache.hpp"
#include "Login.hpp"
#include "PasswordHasher.hpp"
#include "UiBridge.hpp"
#include "json.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void check(bool cond, const std::string& desc, int& failures) {
    if (cond) {
        std::cout << "[PASS] " << desc << "\n";
    } else {
        std::cout << "[FAIL] " << desc << "\n";
        failures++;
    }
}

} // namespace

int main() {
    int failed = 0;
    fs::path origDir = fs::current_path();
    fs::path tempDir = origDir / "build" / "test_tmp_security";

    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);
    fs::current_path(tempDir, ec);

    AuthOptions opts;
    opts.usersPath = "users.txt";
    opts.keyPath = "auth.key";
    opts.bootstrapAdminPassword = "Root-Bootstrap-Pass-9!";
    opts.pbkdf2Iterations = 1000;
    opts.announceBootstrap = false;
    Auth auth(opts);

    rapidaid::data::DataCache cache("cache_data", false);
    std::string outDir = "data/out";
    fs::create_directories(outDir, ec);

    rapidaid::ui::resetBridgeState();

    std::cout << "Starting Security & Bridge Hardening Offline Tests...\n";

    // =========================================================================
    // 1. mustChangePassword gating on bootstrap admin
    // =========================================================================
    std::string adminTok;
    {
        std::cout << "\n--- 1. mustChangePassword gating ---\n";

        // Generate temporary password via adminResetPassword to trigger mustChangePassword flag
        std::string tempAdminPw, resetErr;
        bool rOk = auth.adminResetPassword("admin", tempAdminPw, resetErr);
        check(rOk, "adminResetPassword sets mustChangePassword for admin", failed);

        // Admin login returns mustChangePassword: true
        nlohmann::ordered_json logReq;
        logReq["action"] = "login";
        logReq["payload"] = {{"username", "admin"}, {"password", tempAdminPw}};
        auto loginJ = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, logReq.dump(), outDir));
        check(loginJ.value("ok", false), "Admin bootstrap login succeeds", failed);
        check(loginJ.value("mustChangePassword", false) == true, "Admin mustChangePassword is true on bootstrap", failed);
        adminTok = loginJ.value("token", "");
        check(!adminTok.empty() && adminTok.length() == 64, "Admin token is 64 hex chars", failed);

        // Session action is permitted
        nlohmann::ordered_json sessReq;
        sessReq["action"] = "session";
        sessReq["token"] = adminTok;
        auto sessJ = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sessReq.dump(), outDir));
        check(sessJ.value("ok", false), "session action permitted during mustChangePassword", failed);
        check(sessJ.value("mustChangePassword", false) == true, "Session reflects mustChangePassword = true", failed);

        // Sensitive actions are blocked
        nlohmann::ordered_json listUsersReq;
        listUsersReq["action"] = "list_users";
        listUsersReq["token"] = adminTok;
        auto listUsersJ = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, listUsersReq.dump(), outDir));
        check(!listUsersJ.value("ok", true), "list_users blocked when mustChangePassword is true", failed);
        check(listUsersJ.value("code", "") == "must_change_password", "Error code is 'must_change_password'", failed);

        nlohmann::ordered_json getStatsReq;
        getStatsReq["action"] = "get_stats";
        getStatsReq["token"] = adminTok;
        auto getStatsJ = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, getStatsReq.dump(), outDir));
        check(!getStatsJ.value("ok", true) && getStatsJ.value("code", "") == "must_change_password",
              "get_stats blocked when mustChangePassword is true", failed);

        // Change password clears mustChangePassword
        nlohmann::ordered_json chgPwReq;
        chgPwReq["action"] = "change_password";
        chgPwReq["token"] = adminTok;
        chgPwReq["payload"] = {
            {"currentPassword", tempAdminPw},
            {"newPassword", "Root-Strong-New-Pass-99!"}
        };
        auto chgPwJ = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, chgPwReq.dump(), outDir));
        check(chgPwJ.value("ok", false), "change_password succeeds for bootstrap admin", failed);

        // Now sensitive actions are allowed
        auto listUsersAfterJ = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, listUsersReq.dump(), outDir));
        check(listUsersAfterJ.value("ok", false), "list_users allowed after password change", failed);

        auto sessAfterJ = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sessReq.dump(), outDir));
        check(sessAfterJ.value("mustChangePassword", true) == false, "mustChangePassword is now false", failed);
    }

    // =========================================================================
    // 2. Idle (1800s) and Absolute (28800s) Session Expiry with Mock Clock
    // =========================================================================
    {
        std::cout << "\n--- 2. Idle and Absolute Session Expiry ---\n";

        long long mockTime = 1000000;
        rapidaid::ui::setClockForTesting([&mockTime]() -> long long { return mockTime; });

        // Login as admin at mockTime
        std::string loginReq = R"({"action":"login","payload":{"username":"admin","password":"Root-Strong-New-Pass-99!"}})";
        auto loginJ = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, loginReq, outDir));
        std::string testTok = loginJ.value("token", "");
        check(!testTok.empty(), "Obtained test token at mockTime", failed);

        // Access at mockTime + 1790 (under 1800s idle timeout)
        mockTime += 1790;
        nlohmann::ordered_json sessReq;
        sessReq["action"] = "session";
        sessReq["token"] = testTok;
        auto sessJ1 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sessReq.dump(), outDir));
        check(sessJ1.value("ok", false), "Session valid at mockTime + 1790s (idle not reached)", failed);

        // Advance by 1801 seconds from lastSeen -> idle timeout triggered
        mockTime += 1801;
        auto sessJ2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sessReq.dump(), outDir));
        check(!sessJ2.value("ok", true), "Session rejected after 1801s idle", failed);
        check(sessJ2.value("code", "") == "session_expired", "Code is 'session_expired' for idle timeout", failed);

        // New session for absolute timeout test
        mockTime = 2000000;
        auto loginJ2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, loginReq, outDir));
        std::string absTok = loginJ2.value("token", "");
        check(!absTok.empty(), "Obtained new session for absolute timeout test", failed);

        // Keep session active with periodic requests up to 28790s (every 1000s, well within 1800s idle)
        for (long long t = 2000000 + 1000; t <= 2000000 + 28000; t += 1000) {
            mockTime = t;
            sessReq["token"] = absTok;
            rapidaid::ui::handleBridgeMessage(auth, cache, sessReq.dump(), outDir);
        }
        mockTime = 2000000 + 28790;
        sessReq["token"] = absTok;
        auto sessJ3 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sessReq.dump(), outDir));
        check(sessJ3.value("ok", false), "Session valid just before absolute timeout (28790s)", failed);

        // Advance past 28800s from creation
        mockTime = 2000000 + 28801;
        auto sessJ4 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sessReq.dump(), outDir));
        check(!sessJ4.value("ok", true), "Session rejected after 28801s absolute timeout", failed);
        check(sessJ4.value("code", "") == "session_expired", "Code is 'session_expired' for absolute timeout", failed);

        // Reset clock
        rapidaid::ui::setClockForTesting(nullptr);
    }

    // =========================================================================
    // 3. Max 5 Concurrent Sessions per User (Oldest Dropped)
    // =========================================================================
    {
        std::cout << "\n--- 3. Max 5 Concurrent Sessions per User ---\n";

        // Create dedicated user_cap for concurrency cap test (starts with 0 active sessions)
        auth.signUp("user_cap", "Cap-User-Pass-123!", "dispatcher", true);

        long long simTime = 3000000;
        rapidaid::ui::setClockForTesting([&simTime]() -> long long { return simTime; });

        std::vector<std::string> tokens;
        for (int i = 0; i < 6; ++i) {
            simTime += 10; // strictly increasing createdAt
            std::string logReq = R"({"action":"login","payload":{"username":"user_cap","password":"Cap-User-Pass-123!"}})";
            auto j = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, logReq, outDir));
            tokens.push_back(j.value("token", ""));
        }

        check(tokens.size() == 6, "Logged in 6 times", failed);

        // Token 0 (oldest) should have been evicted
        nlohmann::ordered_json s0;
        s0["action"] = "session";
        s0["token"] = tokens[0];
        auto j0 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, s0.dump(), outDir));
        check(!j0.value("ok", true), "Oldest session (token 0) was evicted", failed);

        // Tokens 1 through 5 should remain valid
        bool all1to5Valid = true;
        for (int i = 1; i < 6; ++i) {
            nlohmann::ordered_json s;
            s["action"] = "session";
            s["token"] = tokens[i];
            auto js = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, s.dump(), outDir));
            if (!js.value("ok", false)) {
                all1to5Valid = false;
                break;
            }
        }
        check(all1to5Valid, "Sessions 1 through 5 remain active and valid", failed);

        rapidaid::ui::setClockForTesting(nullptr);
    }

    // =========================================================================
    // 4. Password Change Revokes All Other Sessions of User
    // =========================================================================
    {
        std::cout << "\n--- 4. Password Change Revokes Other Sessions ---\n";

        // Admin logs in twice to obtain two active sessions: tokA and tokB
        std::string logReq = R"({"action":"login","payload":{"username":"admin","password":"Root-Strong-New-Pass-99!"}})";
        auto jA = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, logReq, outDir));
        auto jB = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, logReq, outDir));
        std::string tokA = jA.value("token", "");
        std::string tokB = jB.value("token", "");
        check(!tokA.empty() && !tokB.empty() && tokA != tokB, "Obtained distinct sessions tokA and tokB", failed);

        // Change password using tokA
        nlohmann::ordered_json chgReq;
        chgReq["action"] = "change_password";
        chgReq["token"] = tokA;
        chgReq["payload"] = {
            {"currentPassword", "Root-Strong-New-Pass-99!"},
            {"newPassword", "Root-Strong-Pass-Round3!"}
        };
        auto jChg = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, chgReq.dump(), outDir));
        check(jChg.value("ok", false), "change_password succeeds with tokA", failed);

        // tokA remains valid
        nlohmann::ordered_json sA;
        sA["action"] = "session";
        sA["token"] = tokA;
        auto jsA = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sA.dump(), outDir));
        check(jsA.value("ok", false), "tokA used to change password remains valid", failed);

        // tokB is revoked
        nlohmann::ordered_json sB;
        sB["action"] = "session";
        sB["token"] = tokB;
        auto jsB = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sB.dump(), outDir));
        check(!jsB.value("ok", true), "tokB was revoked by password change", failed);

        adminTok = tokA;
    }

    // =========================================================================
    // 5. Admin User Management: Approve, Disable, Enable, Admin Reset
    // =========================================================================
    {
        std::cout << "\n--- 5. User Management & Admin Actions ---\n";

        // Dispatcher signs up
        std::string suDisp = R"({"action":"signup","payload":{"username":"bob_disp","password":"Bob-Dispatcher-Pass-1!","role":"dispatcher"}})";
        auto jSu = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, suDisp, outDir));
        check(jSu.value("ok", false), "bob_disp signs up", failed);

        // bob_disp login fails as account_pending
        std::string bobLog = R"({"action":"login","payload":{"username":"bob_disp","password":"Bob-Dispatcher-Pass-1!"}})";
        auto jBobPend = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, bobLog, outDir));
        check(!jBobPend.value("ok", true) && jBobPend.value("code", "") == "account_pending",
              "bob_disp cannot login while pending", failed);

        // Non-admin token cannot approve
        // Create another approved dispatcher alice to test non-admin denial
        std::string suAlice = R"({"action":"signup","payload":{"username":"alice_disp","password":"Alice-Dispatcher-Pass-1!","role":"dispatcher"}})";
        rapidaid::ui::handleBridgeMessage(auth, cache, suAlice, outDir);
        std::string appErr;
        auth.approve("alice_disp", appErr);

        std::string aliceLog = R"({"action":"login","payload":{"username":"alice_disp","password":"Alice-Dispatcher-Pass-1!"}})";
        auto jAliceLog = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, aliceLog, outDir));
        std::string aliceTok = jAliceLog.value("token", "");
        check(!aliceTok.empty(), "alice_disp logged in", failed);

        nlohmann::ordered_json nonAdminApprove;
        nonAdminApprove["action"] = "approve_user";
        nonAdminApprove["token"] = aliceTok;
        nonAdminApprove["payload"] = {{"username", "bob_disp"}};
        auto jNA = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, nonAdminApprove.dump(), outDir));
        check(!jNA.value("ok", true) && jNA.value("code", "") == "not_allowed",
              "Non-admin cannot approve users", failed);

        // Admin approves bob_disp via bridge
        nlohmann::ordered_json adminApprove;
        adminApprove["action"] = "approve_user";
        adminApprove["token"] = adminTok;
        adminApprove["payload"] = {{"username", "bob_disp"}};
        auto jAdminApp = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, adminApprove.dump(), outDir));
        check(jAdminApp.value("ok", false), "Admin approves bob_disp via bridge", failed);

        // bob_disp can now login
        auto jBobLog = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, bobLog, outDir));
        check(jBobLog.value("ok", false), "bob_disp logs in successfully after approval", failed);
        std::string bobTok = jBobLog.value("token", "");

        // Admin disables bob_disp
        nlohmann::ordered_json adminDis;
        adminDis["action"] = "disable_user";
        adminDis["token"] = adminTok;
        adminDis["payload"] = {{"username", "bob_disp"}};
        auto jAdminDis = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, adminDis.dump(), outDir));
        check(jAdminDis.value("ok", false), "Admin disables bob_disp", failed);

        // bob's active session is revoked immediately
        nlohmann::ordered_json bobSess;
        bobSess["action"] = "session";
        bobSess["token"] = bobTok;
        auto jBobSess = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, bobSess.dump(), outDir));
        check(!jBobSess.value("ok", true), "Disabled user active session was revoked", failed);

        // bob cannot login
        auto jBobLog2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, bobLog, outDir));
        check(!jBobLog2.value("ok", true) && jBobLog2.value("code", "") == "account_disabled",
              "bob cannot login while disabled", failed);

        // Admin cannot disable last admin
        nlohmann::ordered_json disSelf;
        disSelf["action"] = "disable_user";
        disSelf["token"] = adminTok;
        disSelf["payload"] = {{"username", "admin"}};
        auto jDisSelf = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, disSelf.dump(), outDir));
        check(!jDisSelf.value("ok", true), "Cannot disable last admin", failed);

        // Admin re-enables bob_disp
        nlohmann::ordered_json adminEn;
        adminEn["action"] = "enable_user";
        adminEn["token"] = adminTok;
        adminEn["payload"] = {{"username", "bob_disp"}};
        auto jAdminEn = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, adminEn.dump(), outDir));
        check(jAdminEn.value("ok", false), "Admin re-enables bob_disp", failed);

        // Admin resets bob_disp password
        nlohmann::ordered_json adminReset;
        adminReset["action"] = "admin_reset_password";
        adminReset["token"] = adminTok;
        adminReset["payload"] = {{"username", "bob_disp"}};
        auto jReset = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, adminReset.dump(), outDir));
        check(jReset.value("ok", false), "admin_reset_password succeeds", failed);
        std::string tempPw = jReset.value("temporaryPassword", "");
        check(!tempPw.empty(), "admin_reset_password returns temporaryPassword", failed);

        // bob logs in with temporaryPassword and gets mustChangePassword: true
        nlohmann::ordered_json bobTempLog;
        bobTempLog["action"] = "login";
        bobTempLog["payload"] = {{"username", "bob_disp"}, {"password", tempPw}};
        auto jBobTemp = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, bobTempLog.dump(), outDir));
        check(jBobTemp.value("ok", false), "bob logs in with temporaryPassword", failed);
        check(jBobTemp.value("mustChangePassword", false) == true, "bob mustChangePassword is true", failed);

        // No hash or salt leaked in list_users response
        nlohmann::ordered_json listReq;
        listReq["action"] = "list_users";
        listReq["token"] = adminTok;
        std::string listRes = rapidaid::ui::handleBridgeMessage(auth, cache, listReq.dump(), outDir);
        check(listRes.find("hash") == std::string::npos, "list_users does not contain 'hash'", failed);
        check(listRes.find("salt") == std::string::npos, "list_users does not contain 'salt'", failed);
        check(listRes.find("pepper") == std::string::npos, "list_users does not contain 'pepper'", failed);
    }

    // =========================================================================
    // 6. Contract C: Defensive Stats on Empty Dataset
    // =========================================================================
    {
        std::cout << "\n--- 6. Contract C: Defensive Stats on Empty Dataset ---\n";

        nlohmann::ordered_json statsReq;
        statsReq["action"] = "get_stats";
        statsReq["token"] = adminTok;
        std::string statsRes = rapidaid::ui::handleBridgeMessage(auth, cache, statsReq.dump(), outDir);
        auto sj = nlohmann::json::parse(statsRes);

        check(sj.value("ok", false), "get_stats succeeds on empty dataset", failed);
        check(sj.contains("generatedAt") && sj["generatedAt"].is_number(), "contains numeric generatedAt", failed);

        // Incidents object defensive checks
        check(sj.contains("incidents") && sj["incidents"].is_object(), "contains incidents object", failed);
        auto inc = sj["incidents"];
        check(inc.value("total", -1) == 0, "incidents total is 0", failed);
        check(inc["avgSeverity"].is_null(), "avgSeverity is null when 0 incidents", failed);
        check(inc["avgPeopleAffected"].is_null(), "avgPeopleAffected is null when 0 incidents", failed);
        check(inc["avgMinutesToDispatch"].is_null(), "avgMinutesToDispatch is null when 0 incidents", failed);
        check(inc["avgMinutesToComplete"].is_null(), "avgMinutesToComplete is null when 0 incidents", failed);
        check(inc["byStatus"]["New"] == 0, "byStatus.New is 0", failed);
        check(inc["byType"]["Medical"] == 0, "byType.Medical is 0", failed);

        // Alerts object defensive checks per Contract C
        check(sj.contains("alerts") && sj["alerts"].is_object(), "contains alerts object", failed);
        auto al = sj["alerts"];
        check(al.value("total", -1) == 0, "alerts total is 0", failed);
        check(al.value("accepted", -1) == 0, "alerts accepted is 0", failed);
        check(al.value("declined", -1) == 0, "alerts declined is 0", failed);
        check(al.value("cleared", -1) == 0, "alerts cleared is 0", failed);
        check(al.value("timedOut", -1) == 0, "alerts timedOut is 0", failed);
        check(al["acceptRate"].is_null(), "acceptRate is null when 0 alerts", failed);
        check(al["avgAckSeconds"].is_null(), "avgAckSeconds is null when 0 alerts", failed);

        // Recent items array
        check(sj.contains("recent") && sj["recent"].is_array() && sj["recent"].empty(),
              "recent items array is empty", failed);
    }

    // Restore working directory and cleanup
    fs::current_path(origDir, ec);
    fs::remove_all(tempDir, ec);

    std::cout << "\nSecurity Bridge Offline Test Summary: "
              << (failed == 0 ? "ALL PASSED (0 failures)" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed == 0 ? 0 : 1;
}
