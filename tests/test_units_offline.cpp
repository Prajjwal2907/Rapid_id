#include "DataCache.hpp"
#include "Login.hpp"
#include "UiBridge.hpp"
#include "UnitRegistry.hpp"
#include "json.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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
    fs::path tempDir = origDir / "build" / "test_tmp_units";

    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);
    fs::current_path(tempDir, ec);

    // Initialize Auth and Cache inside temporary directory
    Auth auth("users.txt");
    rapidaid::data::DataCache cache("cache_data", false);
    std::string outDir = "data/out";
    fs::create_directories(outDir, ec);

    rapidaid::ui::resetBridgeState();

    std::cout << "Starting Unit & Dispatch Offline Tests...\n";

    // =========================================================================
    // Case 1: Signup rules & validation
    // =========================================================================
    {
        std::cout << "\n--- Case 1: Signup validation & rules ---\n";

        // Dispatcher signup works
        std::string req1 = R"({"action":"signup","payload":{"username":"disp1","password":"pw1","role":"dispatcher"}})";
        std::string res1 = rapidaid::ui::handleBridgeMessage(auth, cache, req1, outDir);
        auto j1 = nlohmann::json::parse(res1);
        check(j1.value("ok", false), "Case 1: signup of a dispatcher works", failed);

        // Role admin without admin token is rejected
        std::string req2 = R"({"action":"signup","payload":{"username":"admin2","password":"pw2","role":"admin"}})";
        std::string res2 = rapidaid::ui::handleBridgeMessage(auth, cache, req2, outDir);
        auto j2 = nlohmann::json::parse(res2);
        check(!j2.value("ok", true), "Case 1: signup role admin without admin token is rejected", failed);

        // Username with space is rejected
        std::string req3 = R"({"action":"signup","payload":{"username":"bad user","password":"pw3","role":"dispatcher"}})";
        std::string res3 = rapidaid::ui::handleBridgeMessage(auth, cache, req3, outDir);
        auto j3 = nlohmann::json::parse(res3);
        check(!j3.value("ok", true), "Case 1: username with a space is rejected", failed);

        // Invalid unit profiles rejected and account not created
        // a) Negative count
        std::string req4 = R"({"action":"signup","payload":{"username":"bad_amb1","password":"pw","role":"unit","unit":{"kind":"ambulance","name":"A1","lat":48.8,"lon":2.2,"count":-5}}})";
        std::string res4 = rapidaid::ui::handleBridgeMessage(auth, cache, req4, outDir);
        auto j4 = nlohmann::json::parse(res4);
        check(!j4.value("ok", true), "Case 1: signup unit with negative count rejected", failed);

        std::string loginCheck1 = R"({"action":"login","payload":{"username":"bad_amb1","password":"pw"}})";
        std::string lres1 = rapidaid::ui::handleBridgeMessage(auth, cache, loginCheck1, outDir);
        auto lj1 = nlohmann::json::parse(lres1);
        check(!lj1.value("ok", true), "Case 1: failed signup with negative count did not create account", failed);

        // b) Bad latitude (> 90)
        std::string req5 = R"({"action":"signup","payload":{"username":"bad_amb2","password":"pw","role":"unit","unit":{"kind":"ambulance","name":"A2","lat":120.0,"lon":2.2,"count":2}}})";
        std::string res5 = rapidaid::ui::handleBridgeMessage(auth, cache, req5, outDir);
        auto j5 = nlohmann::json::parse(res5);
        check(!j5.value("ok", true), "Case 1: signup unit with bad lat rejected", failed);

        std::string loginCheck2 = R"({"action":"login","payload":{"username":"bad_amb2","password":"pw"}})";
        auto lj2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, loginCheck2, outDir));
        check(!lj2.value("ok", true), "Case 1: failed signup with bad lat did not create account", failed);

        // c) Empty name
        std::string req6 = R"({"action":"signup","payload":{"username":"bad_amb3","password":"pw","role":"unit","unit":{"kind":"ambulance","name":"","lat":48.8,"lon":2.2,"count":2}}})";
        std::string res6 = rapidaid::ui::handleBridgeMessage(auth, cache, req6, outDir);
        auto j6 = nlohmann::json::parse(res6);
        check(!j6.value("ok", true), "Case 1: signup unit with empty name rejected", failed);

        std::string loginCheck3 = R"({"action":"login","payload":{"username":"bad_amb3","password":"pw"}})";
        auto lj3 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, loginCheck3, outDir));
        check(!lj3.value("ok", true), "Case 1: failed signup with empty name did not create account", failed);

        // d) Unknown kind
        std::string req7 = R"({"action":"signup","payload":{"username":"bad_amb4","password":"pw","role":"unit","unit":{"kind":"helicopter","name":"H1","lat":48.8,"lon":2.2,"count":2}}})";
        std::string res7 = rapidaid::ui::handleBridgeMessage(auth, cache, req7, outDir);
        auto j7 = nlohmann::json::parse(res7);
        check(!j7.value("ok", true), "Case 1: signup unit with unknown kind rejected", failed);

        std::string loginCheck4 = R"({"action":"login","payload":{"username":"bad_amb4","password":"pw"}})";
        auto lj4 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, loginCheck4, outDir));
        check(!lj4.value("ok", true), "Case 1: failed signup with unknown kind did not create account", failed);
    }

    // =========================================================================
    // Case 2: One account for each of the 7 kinds & login verification
    // =========================================================================
    std::string dispToken;
    std::string ambToken;
    std::string truckToken;
    std::string copToken;
    std::string hospToken;
    std::string fireStToken;
    std::string policeStToken;
    std::string shelterToken;

    {
        std::cout << "\n--- Case 2: Signup & login for 7 service kinds ---\n";

        // Login dispatcher to obtain token
        std::string dLogReq = R"({"action":"login","payload":{"username":"disp1","password":"pw1"}})";
        std::string dLogRes = rapidaid::ui::handleBridgeMessage(auth, cache, dLogReq, outDir);
        auto dLogJ = nlohmann::json::parse(dLogRes);
        dispToken = dLogJ.value("token", "");
        check(!dispToken.empty(), "Case 2: dispatcher login returned session token", failed);

        struct KindSpec {
            std::string user;
            std::string kind;
            int count;
            std::string* tokenOut;
        };

        KindSpec kinds[] = {
            {"amb_user", "ambulance", 2, &ambToken},
            {"truck_user", "fire_truck", 4000, &truckToken},
            {"cop_user", "police_unit", 3, &copToken},
            {"hosp_user", "hospital", 30, &hospToken},
            {"fire_st_user", "fire_station", 0, &fireStToken},
            {"police_st_user", "police_station", 0, &policeStToken},
            {"shelter_user", "shelter", 40, &shelterToken}
        };

        for (const auto& k : kinds) {
            std::string pw = "pass_" + k.user;
            nlohmann::ordered_json suReq;
            suReq["action"] = "signup";
            suReq["payload"] = {
                {"username", k.user},
                {"password", pw},
                {"role", "unit"},
                {"unit", {
                    {"kind", k.kind},
                    {"name", "Display " + k.user},
                    {"lat", 48.85},
                    {"lon", 2.29},
                    {"count", k.count}
                }}
            };

            std::string suRes = rapidaid::ui::handleBridgeMessage(auth, cache, suReq.dump(), outDir);
            auto suJ = nlohmann::json::parse(suRes);
            check(suJ.value("ok", false), "Case 2: signup succeeded for kind " + k.kind, failed);

            // Login
            nlohmann::ordered_json logReq;
            logReq["action"] = "login";
            logReq["payload"] = {{"username", k.user}, {"password", pw}};
            std::string logRes = rapidaid::ui::handleBridgeMessage(auth, cache, logReq.dump(), outDir);
            auto logJ = nlohmann::json::parse(logRes);

            check(logJ.value("ok", false), "Case 2: login succeeded for kind " + k.kind, failed);
            check(logJ.value("role", "") == "unit", "Case 2: role is 'unit' for " + k.kind, failed);

            std::string tok = logJ.value("token", "");
            check(tok.length() == 32, "Case 2: 32-char token returned for " + k.kind, failed);
            *(k.tokenOut) = tok;

            // Responses never leak password or hash
            check(logRes.find(pw) == std::string::npos, "Case 2: response does not contain password for " + k.kind, failed);
        }
    }

    // =========================================================================
    // Case 3: Authorization enforcement
    // =========================================================================
    {
        std::cout << "\n--- Case 3: Authorization checks ---\n";

        // No token
        std::string rNoTok = R"({"action":"session"})";
        auto jNoTok = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, rNoTok, outDir));
        check(!jNoTok.value("ok", true) && jNoTok.value("error", "") == "Not logged in", "Case 3: no token rejected on session", failed);

        std::string rNoTok2 = R"({"action":"list_units"})";
        auto jNoTok2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, rNoTok2, outDir));
        check(!jNoTok2.value("ok", true) && jNoTok2.value("error", "") == "Not logged in", "Case 3: no token rejected on list_units", failed);

        // Fake token
        std::string rFake = R"({"action":"session","token":"badtoken000000000000000000000000"})";
        auto jFake = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, rFake, outDir));
        check(!jFake.value("ok", true) && jFake.value("error", "") == "Not logged in", "Case 3: fake token rejected on session", failed);

        // Unit token rejected on dispatcher actions
        nlohmann::ordered_json u1;
        u1["action"] = "create_area";
        u1["token"] = ambToken;
        u1["payload"] = {{"name", "Paris"}};
        auto ju1 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, u1.dump(), outDir));
        check(!ju1.value("ok", true) && ju1.value("error", "") == "Not allowed", "Case 3: unit token rejected on create_area", failed);

        nlohmann::ordered_json u2;
        u2["action"] = "get_incidents";
        u2["token"] = ambToken;
        auto ju2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, u2.dump(), outDir));
        check(!ju2.value("ok", true) && ju2.value("error", "") == "Not allowed", "Case 3: unit token rejected on get_incidents", failed);

        nlohmann::ordered_json u3;
        u3["action"] = "list_units";
        u3["token"] = ambToken;
        auto ju3 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, u3.dump(), outDir));
        check(!ju3.value("ok", true) && ju3.value("error", "") == "Not allowed", "Case 3: unit token rejected on list_units", failed);

        nlohmann::ordered_json u4;
        u4["action"] = "send_test_alert";
        u4["token"] = ambToken;
        u4["payload"] = {{"toUser", "cop_user"}};
        auto ju4 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, u4.dump(), outDir));
        check(!ju4.value("ok", true) && ju4.value("error", "") == "Not allowed", "Case 3: unit token rejected on send_test_alert", failed);

        // Dispatcher token rejected on unit actions
        nlohmann::ordered_json d1;
        d1["action"] = "unit_get_state";
        d1["token"] = dispToken;
        auto jd1 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, d1.dump(), outDir));
        check(!jd1.value("ok", true) && jd1.value("error", "") == "Not allowed", "Case 3: dispatcher token rejected on unit_get_state", failed);

        nlohmann::ordered_json d2;
        d2["action"] = "unit_set_availability";
        d2["token"] = dispToken;
        d2["payload"] = {{"available", false}};
        auto jd2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, d2.dump(), outDir));
        check(!jd2.value("ok", true) && jd2.value("error", "") == "Not allowed", "Case 3: dispatcher token rejected on unit_set_availability", failed);
    }

    // =========================================================================
    // Case 4: Ambulance alert lifecycle & transitions
    // =========================================================================
    {
        std::cout << "\n--- Case 4: Ambulance alert lifecycle ---\n";

        // Ambulance: set unavailable -> test alert rejected
        nlohmann::ordered_json setUnavail;
        setUnavail["action"] = "unit_set_availability";
        setUnavail["token"] = ambToken;
        setUnavail["payload"] = {{"available", false}};
        auto jUnavail = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, setUnavail.dump(), outDir));
        check(jUnavail.value("ok", false), "Case 4: ambulance set unavailable succeeds", failed);

        nlohmann::ordered_json sendFail;
        sendFail["action"] = "send_test_alert";
        sendFail["token"] = dispToken;
        sendFail["payload"] = {{"toUser", "amb_user"}, {"locationText", "10 Rue A"}};
        auto jSendFail = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sendFail.dump(), outDir));
        check(!jSendFail.value("ok", true), "Case 4: test alert to unavailable ambulance rejected", failed);

        // Set available -> test alert accepted
        nlohmann::ordered_json setAvail;
        setAvail["action"] = "unit_set_availability";
        setAvail["token"] = ambToken;
        setAvail["payload"] = {{"available", true}};
        auto jAvail = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, setAvail.dump(), outDir));
        check(jAvail.value("ok", false), "Case 4: ambulance set available succeeds", failed);

        nlohmann::ordered_json sendOk;
        sendOk["action"] = "send_test_alert";
        sendOk["token"] = dispToken;
        sendOk["payload"] = {{"toUser", "amb_user"}, {"locationText", "10 Rue A"}, {"type", "Medical"}, {"severity", 4}};
        auto jSendOk = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sendOk.dump(), outDir));
        check(jSendOk.value("ok", false), "Case 4: test alert to available ambulance accepted", failed);
        int alertId = jSendOk.value("alertId", 0);
        check(alertId > 0, "Case 4: valid alert ID returned", failed);

        // Second alert to same unit is rejected
        nlohmann::ordered_json sendSecond;
        sendSecond["action"] = "send_test_alert";
        sendSecond["token"] = dispToken;
        sendSecond["payload"] = {{"toUser", "amb_user"}, {"locationText", "20 Rue B"}};
        auto jSendSecond = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sendSecond.dump(), outDir));
        check(!jSendSecond.value("ok", true), "Case 4: second alert to unit rejected", failed);

        // Invalid transition: Pending -> OnScene rejected
        nlohmann::ordered_json badTrans;
        badTrans["action"] = "unit_alert_respond";
        badTrans["token"] = ambToken;
        badTrans["payload"] = {{"alertId", alertId}, {"action", "on_scene"}};
        auto jBadTrans = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, badTrans.dump(), outDir));
        check(!jBadTrans.value("ok", true), "Case 4: invalid transition Pending -> OnScene rejected", failed);

        // Another unit cannot respond to this alert
        nlohmann::ordered_json otherResp;
        otherResp["action"] = "unit_alert_respond";
        otherResp["token"] = copToken;
        otherResp["payload"] = {{"alertId", alertId}, {"action", "accept"}};
        auto jOtherResp = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, otherResp.dump(), outDir));
        check(!jOtherResp.value("ok", true), "Case 4: other unit cannot respond to alert", failed);

        // Accept alert: Pending -> Accepted (status becomes Dispatched)
        nlohmann::ordered_json acceptReq;
        acceptReq["action"] = "unit_alert_respond";
        acceptReq["token"] = ambToken;
        acceptReq["payload"] = {{"alertId", alertId}, {"action", "accept"}};
        auto jAccept = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, acceptReq.dump(), outDir));
        check(jAccept.value("ok", false), "Case 4: Pending -> Accepted succeeded", failed);
        check(jAccept.value("status", "") == "Dispatched", "Case 4: ambulance status became Dispatched", failed);

        // Accepted -> EnRoute
        nlohmann::ordered_json enRouteReq;
        enRouteReq["action"] = "unit_alert_respond";
        enRouteReq["token"] = ambToken;
        enRouteReq["payload"] = {{"alertId", alertId}, {"action", "en_route"}};
        auto jEnRoute = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, enRouteReq.dump(), outDir));
        check(jEnRoute.value("ok", false), "Case 4: Accepted -> EnRoute succeeded", failed);
        check(jEnRoute["openAlert"].value("state", "") == "EnRoute", "Case 4: alert state is EnRoute", failed);

        // EnRoute -> OnScene
        nlohmann::ordered_json onSceneReq;
        onSceneReq["action"] = "unit_alert_respond";
        onSceneReq["token"] = ambToken;
        onSceneReq["payload"] = {{"alertId", alertId}, {"action", "on_scene"}};
        auto jOnScene = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, onSceneReq.dump(), outDir));
        check(jOnScene.value("ok", false), "Case 4: EnRoute -> OnScene succeeded", failed);

        // OnScene -> Transporting (Ambulance only)
        nlohmann::ordered_json transReq;
        transReq["action"] = "unit_alert_respond";
        transReq["token"] = ambToken;
        transReq["payload"] = {{"alertId", alertId}, {"action", "transporting"}};
        auto jTrans = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, transReq.dump(), outDir));
        check(jTrans.value("ok", false), "Case 4: OnScene -> Transporting succeeded for ambulance", failed);

        // Transporting -> Cleared (status becomes Available)
        nlohmann::ordered_json clearReq;
        clearReq["action"] = "unit_alert_respond";
        clearReq["token"] = ambToken;
        clearReq["payload"] = {{"alertId", alertId}, {"action", "clear"}};
        auto jClear = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, clearReq.dump(), outDir));
        check(jClear.value("ok", false), "Case 4: Transporting -> Cleared succeeded", failed);
        check(jClear.value("status", "") == "Available", "Case 4: ambulance status became Available again", failed);

        // Fire truck cannot use "transporting"
        nlohmann::ordered_json ftAlert;
        ftAlert["action"] = "send_test_alert";
        ftAlert["token"] = dispToken;
        ftAlert["payload"] = {{"toUser", "truck_user"}, {"locationText", "Fire at 5 Blvd"}};
        auto jFtAlert = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, ftAlert.dump(), outDir));
        int ftAlertId = jFtAlert.value("alertId", 0);

        nlohmann::ordered_json ftAcc;
        ftAcc["action"] = "unit_alert_respond";
        ftAcc["token"] = truckToken;
        ftAcc["payload"] = {{"alertId", ftAlertId}, {"action", "accept"}};
        rapidaid::ui::handleBridgeMessage(auth, cache, ftAcc.dump(), outDir);

        nlohmann::ordered_json ftEnR;
        ftEnR["action"] = "unit_alert_respond";
        ftEnR["token"] = truckToken;
        ftEnR["payload"] = {{"alertId", ftAlertId}, {"action", "en_route"}};
        rapidaid::ui::handleBridgeMessage(auth, cache, ftEnR.dump(), outDir);

        nlohmann::ordered_json ftOnS;
        ftOnS["action"] = "unit_alert_respond";
        ftOnS["token"] = truckToken;
        ftOnS["payload"] = {{"alertId", ftAlertId}, {"action", "on_scene"}};
        rapidaid::ui::handleBridgeMessage(auth, cache, ftOnS.dump(), outDir);

        nlohmann::ordered_json ftTrans;
        ftTrans["action"] = "unit_alert_respond";
        ftTrans["token"] = truckToken;
        ftTrans["payload"] = {{"alertId", ftAlertId}, {"action", "transporting"}};
        auto jFtTrans = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, ftTrans.dump(), outDir));
        check(!jFtTrans.value("ok", true), "Case 4: fire truck cannot use transporting", failed);

        nlohmann::ordered_json ftClr;
        ftClr["action"] = "unit_alert_respond";
        ftClr["token"] = truckToken;
        ftClr["payload"] = {{"alertId", ftAlertId}, {"action", "clear"}};
        rapidaid::ui::handleBridgeMessage(auth, cache, ftClr.dump(), outDir);
    }

    // =========================================================================
    // Case 5: Decline alert leaves unit Available
    // =========================================================================
    {
        std::cout << "\n--- Case 5: Decline alert ---\n";

        nlohmann::ordered_json sendOk2;
        sendOk2["action"] = "send_test_alert";
        sendOk2["token"] = dispToken;
        sendOk2["payload"] = {{"toUser", "amb_user"}, {"locationText", "30 Ave C"}};
        auto jSendOk2 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sendOk2.dump(), outDir));
        int alertId2 = jSendOk2.value("alertId", 0);

        nlohmann::ordered_json declReq;
        declReq["action"] = "unit_alert_respond";
        declReq["token"] = ambToken;
        declReq["payload"] = {{"alertId", alertId2}, {"action", "decline"}, {"reason", "Fuel refill"}};
        auto jDecl = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, declReq.dump(), outDir));
        check(jDecl.value("ok", false), "Case 5: alert decline succeeded", failed);
        check(jDecl.value("status", "") == "Available", "Case 5: decline leaves the unit Available", failed);
        check(jDecl["openAlert"].is_null(), "Case 5: no open alert remains after decline", failed);
    }

    // =========================================================================
    // Case 6: Facility resource management & subunits
    // =========================================================================
    {
        std::cout << "\n--- Case 6: Facility resources & subunits ---\n";

        // Hospital: 30 total beds
        // Admit > free beds is rejected
        nlohmann::ordered_json hAdmitOver;
        hAdmitOver["action"] = "unit_update_resources";
        hAdmitOver["token"] = hospToken;
        hAdmitOver["payload"] = {{"action", "admit"}, {"count", 35}};
        auto jHOver = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, hAdmitOver.dump(), outDir));
        check(!jHOver.value("ok", true), "Case 6: hospital admit more patients than free beds rejected", failed);

        // Admit 10
        nlohmann::ordered_json hAdmit10;
        hAdmit10["action"] = "unit_update_resources";
        hAdmit10["token"] = hospToken;
        hAdmit10["payload"] = {{"action", "admit"}, {"count", 10}};
        auto jH10 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, hAdmit10.dump(), outDir));
        check(jH10.value("ok", false), "Case 6: hospital admit 10 patients succeeded", failed);
        check(jH10["resources"].value("occupiedBeds", 0) == 10, "Case 6: hospital occupied beds is 10", failed);

        // Discharge > occupied is rejected
        nlohmann::ordered_json hDiscOver;
        hDiscOver["action"] = "unit_update_resources";
        hDiscOver["token"] = hospToken;
        hDiscOver["payload"] = {{"action", "discharge"}, {"count", 15}};
        auto jHDiscOver = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, hDiscOver.dump(), outDir));
        check(!jHDiscOver.value("ok", true), "Case 6: hospital discharge more than occupied is rejected", failed);

        // Discharge 5
        nlohmann::ordered_json hDisc5;
        hDisc5["action"] = "unit_update_resources";
        hDisc5["token"] = hospToken;
        hDisc5["payload"] = {{"action", "discharge"}, {"count", 5}};
        auto jHDisc5 = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, hDisc5.dump(), outDir));
        check(jHDisc5.value("ok", false), "Case 6: hospital discharge 5 patients succeeded", failed);
        check(jHDisc5["resources"].value("occupiedBeds", 0) == 5, "Case 6: hospital occupied beds is now 5", failed);

        // Add ambulance to hospital and toggle it
        nlohmann::ordered_json hAddAmb;
        hAddAmb["action"] = "unit_add_subunit";
        hAddAmb["token"] = hospToken;
        hAddAmb["payload"] = {{"name", "Hosp Amb 1"}, {"count", 2}};
        auto jHAddAmb = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, hAddAmb.dump(), outDir));
        check(jHAddAmb.value("ok", false), "Case 6: hospital add ambulance succeeded", failed);
        int subAmbId = jHAddAmb.value("subunitId", 0);
        check(subAmbId > 0, "Case 6: valid subunit ID returned", failed);

        nlohmann::ordered_json hTog;
        hTog["action"] = "unit_set_subunit_availability";
        hTog["token"] = hospToken;
        hTog["payload"] = {{"subunitId", subAmbId}, {"available", false}};
        auto jHTog = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, hTog.dump(), outDir));
        check(jHTog.value("ok", false), "Case 6: toggle hospital ambulance to unavailable succeeded", failed);
        check(jHTog["subunits"][0].value("available", true) == false, "Case 6: hospital subunit available is false", failed);

        // Shelter: capacity 40
        // Admit beyond capacity rejected
        nlohmann::ordered_json sAdmitOver;
        sAdmitOver["action"] = "unit_update_resources";
        sAdmitOver["token"] = shelterToken;
        sAdmitOver["payload"] = {{"action", "admit"}, {"count", 45}};
        auto jSOver = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sAdmitOver.dump(), outDir));
        check(!jSOver.value("ok", true), "Case 6: shelter admit beyond capacity rejected", failed);

        // Admit 40 -> isFull becomes true
        nlohmann::ordered_json sAdmitFull;
        sAdmitFull["action"] = "unit_update_resources";
        sAdmitFull["token"] = shelterToken;
        sAdmitFull["payload"] = {{"action", "admit"}, {"count", 40}};
        auto jSFull = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sAdmitFull.dump(), outDir));
        check(jSFull.value("ok", false), "Case 6: shelter admit 40 succeeded", failed);
        check(jSFull["resources"].value("isFull", false) == true, "Case 6: shelter isFull becomes true when full", failed);

        // Fire station & Police station: add and toggle subunits
        nlohmann::ordered_json fsAdd;
        fsAdd["action"] = "unit_add_subunit";
        fsAdd["token"] = fireStToken;
        fsAdd["payload"] = {{"name", "Engine 1"}, {"count", 3000}};
        auto jFsAdd = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, fsAdd.dump(), outDir));
        check(jFsAdd.value("ok", false), "Case 6: fire station add truck succeeded", failed);
        int truckSubId = jFsAdd.value("subunitId", 0);

        nlohmann::ordered_json fsTog;
        fsTog["action"] = "unit_set_subunit_availability";
        fsTog["token"] = fireStToken;
        fsTog["payload"] = {{"subunitId", truckSubId}, {"available", false}};
        auto jFsTog = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, fsTog.dump(), outDir));
        check(jFsTog.value("ok", false), "Case 6: fire truck subunit toggle succeeded", failed);

        nlohmann::ordered_json psAdd;
        psAdd["action"] = "unit_add_subunit";
        psAdd["token"] = policeStToken;
        psAdd["payload"] = {{"name", "Patrol Car 1"}, {"count", 2}};
        auto jPsAdd = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, psAdd.dump(), outDir));
        check(jPsAdd.value("ok", false), "Case 6: police station add unit succeeded", failed);
        int policeSubId = jPsAdd.value("subunitId", 0);

        nlohmann::ordered_json psTog;
        psTog["action"] = "unit_set_subunit_availability";
        psTog["token"] = policeStToken;
        psTog["payload"] = {{"subunitId", policeSubId}, {"available", false}};
        auto jPsTog = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, psTog.dump(), outDir));
        check(jPsTog.value("ok", false), "Case 6: police unit subunit toggle succeeded", failed);
    }

    // =========================================================================
    // Case 7: Persistence round-trip & tolerance of corrupt file
    // =========================================================================
    {
        std::cout << "\n--- Case 7: Persistence & corrupt file tolerance ---\n";

        // Recreate registry from disk file
        rapidaid::ui::UnitRegistry reg2("data/units.json");
        reg2.load("data/units.json");

        check(reg2.hasUnit("amb_user"), "Case 7: persisted ambulance found after reload", failed);
        check(reg2.hasUnit("hosp_user"), "Case 7: persisted hospital found after reload", failed);
        check(reg2.hasUnit("shelter_user"), "Case 7: persisted shelter found after reload", failed);

        auto hospSt = reg2.getUnitStateJson("hosp_user");
        check(hospSt["resources"].value("occupiedBeds", 0) == 5, "Case 7: hospital occupied beds restored", failed);
        check(hospSt["subunits"].size() == 1, "Case 7: hospital subunit restored", failed);

        auto shelterSt = reg2.getUnitStateJson("shelter_user");
        check(shelterSt["resources"].value("isFull", false) == true, "Case 7: shelter isFull restored", failed);

        // Corrupt file tolerance
        std::ofstream corruptOut("data/corrupt_units.json");
        corruptOut << "{INVALID JSON SYNTAX %%%";
        corruptOut.close();

        rapidaid::ui::UnitRegistry corruptReg("data/corrupt_units.json");
        bool cRes = corruptReg.load("data/corrupt_units.json");
        check(!cRes, "Case 7: load returns false on corrupt file", failed);
        check(!corruptReg.hasUnit("amb_user"), "Case 7: corrupt units.json yields empty registry without crashing", failed);
    }

    // =========================================================================
    // Case 8: Session action returns profile for valid token, error after logout
    // =========================================================================
    {
        std::cout << "\n--- Case 8: Session action and logout ---\n";

        nlohmann::ordered_json sessReq;
        sessReq["action"] = "session";
        sessReq["token"] = ambToken;
        auto jSess = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sessReq.dump(), outDir));
        check(jSess.value("ok", false), "Case 8: session action succeeded for valid unit token", failed);
        check(jSess["unit"].value("kind", "") == "ambulance", "Case 8: session returned unit profile", failed);
        check(jSess.value("role", "") == "unit", "Case 8: session returned correct role", failed);

        // Logout
        nlohmann::ordered_json logOutReq;
        logOutReq["action"] = "logout";
        logOutReq["token"] = ambToken;
        auto jLogout = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, logOutReq.dump(), outDir));
        check(jLogout.value("ok", false), "Case 8: logout succeeded", failed);

        // Session after logout returns error
        auto jSessAfter = nlohmann::json::parse(rapidaid::ui::handleBridgeMessage(auth, cache, sessReq.dump(), outDir));
        check(!jSessAfter.value("ok", true) && jSessAfter.value("error", "") == "Not logged in",
              "Case 8: session action returns 'Not logged in' error after logout", failed);
    }

    // Restore working directory and cleanup temp test directory
    fs::current_path(origDir, ec);
    fs::remove_all(tempDir, ec);
    check(!fs::exists(tempDir), "Cleaned up temporary test directory build/test_tmp_units/", failed);

    std::cout << "\nUnit Offline Test Summary: " << (failed == 0 ? "ALL PASSED (0 failures)" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed == 0 ? 0 : 1;
}
