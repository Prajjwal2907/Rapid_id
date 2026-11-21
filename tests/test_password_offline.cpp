#include "PasswordHasher.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using rapidaid::auth::PasswordHasher;

static void check(bool condition, const std::string& name, int& failed) {
    if (condition) {
        std::cout << "[PASS] " << name << "\n";
    } else {
        std::cout << "[FAIL] " << name << "\n";
        failed++;
    }
}

static std::string hexEncode(const unsigned char* data, std::size_t len) {
    static const char hexChars[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(hexChars[(data[i] >> 4) & 0x0F]);
        out.push_back(hexChars[data[i] & 0x0F]);
    }
    return out;
}

int main() {
    int failed = 0;
    std::cout << "Starting PasswordHasher Offline Tests...\n";

    const std::string pepper = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const std::string wrongPepper = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";
    const std::string password = "CorrectHorseBatteryStaple-99";
    const std::string wrongPassword = "WrongPasswordAttempt-00";
    const int testIterations = 1000;

    // Test 1: Round trip
    std::string encoded = PasswordHasher::hash(password, pepper, testIterations);
    check(!encoded.empty(), "hash produces non-empty encoded string", failed);
    check(PasswordHasher::verify(password, pepper, encoded), "verify returns true for correct password and pepper", failed);

    // Test 2: Wrong password fails
    check(!PasswordHasher::verify(wrongPassword, pepper, encoded), "verify returns false for wrong password", failed);

    // Test 3: Same password hashes differently (salts differ)
    std::string encoded2 = PasswordHasher::hash(password, pepper, testIterations);
    check(!encoded2.empty(), "second hash produces non-empty string", failed);
    check(encoded != encoded2, "subsequent hash of same password produces different salt and hash", failed);
    check(PasswordHasher::verify(password, pepper, encoded2), "second hash verifies successfully", failed);

    // Test 4: Different pepper fails
    check(!PasswordHasher::verify(password, wrongPepper, encoded), "verify returns false with different pepper", failed);

    // Test 5: needsRehash
    check(!PasswordHasher::needsRehash(encoded, testIterations), "needsRehash returns false when target matches stored iterations", failed);
    check(PasswordHasher::needsRehash(encoded, 2000), "needsRehash returns true when target differs from stored iterations", failed);
    check(PasswordHasher::needsRehash("malformed_hash", testIterations), "needsRehash returns true on malformed hash string", failed);

    // Test 6: Malformed strings fail safely without crashing
    std::vector<std::string> malformedCases = {
        "",
        "pbkdf2-sha256",
        "pbkdf2-sha256$",
        "pbkdf2-sha256$1000",
        "pbkdf2-sha256$1000$abcd",
        "pbkdf2-sha256$abc$12345678901234567890123456789012$1234567890123456789012345678901212345678901234567890123456789012",
        "pbkdf2-sha256$0$12345678901234567890123456789012$1234567890123456789012345678901212345678901234567890123456789012",
        "pbkdf2-sha256$1000$nothex_salt_string_length32chars!$1234567890123456789012345678901212345678901234567890123456789012",
        "pbkdf2-sha256$1000$12345678901234567890123456789012$nothex_hash_string_length64characters_not_valid_hex_characters_here",
        "pbkdf2-sha256$1000$1234$5678",
        "pbkdf2-sha256$1000$12345678901234567890123456789012$1234567890123456789012345678901212345678901234567890123456789012$extra_field",
        "sha256$1000$12345678901234567890123456789012$1234567890123456789012345678901212345678901234567890123456789012"
    };

    bool malformedSafe = true;
    for (const auto& m : malformedCases) {
        if (PasswordHasher::verify(password, pepper, m)) {
            malformedSafe = false;
        }
    }
    check(malformedSafe, "Malformed encoded strings return false without crashing", failed);

    // Test 7: randomHex
    std::string hex16 = PasswordHasher::randomHex(16);
    check(hex16.size() == 32, "randomHex(16) produces 32 hex characters", failed);
    std::string hex32 = PasswordHasher::randomHex(32);
    check(hex32.size() == 64, "randomHex(32) produces 64 hex characters", failed);
    check(hex16 != hex32.substr(0, 32), "randomHex produces distinct non-trivial outputs", failed);
    bool validHexChars = true;
    for (char c : hex32) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            validHexChars = false;
        }
    }
    check(validHexChars, "randomHex produces lowercase hex characters", failed);

    // Test 8: randomPassword length and alphabet (unambiguous rejection sampling)
    const std::string allowedAlphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    bool randPassOk = true;
    for (std::size_t len : {10, 16, 24, 32}) {
        std::string p = PasswordHasher::randomPassword(len);
        if (p.size() != len) randPassOk = false;
        for (char c : p) {
            if (allowedAlphabet.find(c) == std::string::npos) {
                randPassOk = false;
            }
        }
    }
    check(randPassOk, "randomPassword generates exact requested lengths with unambiguous characters", failed);

    // Test 9: Raw OpenSSL PBKDF2-HMAC-SHA256 known answer test
    // password "password", salt "salt", 1 iteration, 32 bytes output:
    // RFC 6070 test vector: 120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b
    unsigned char rawOut[32];
    int rawStatus = PKCS5_PBKDF2_HMAC(
        "password", 8,
        reinterpret_cast<const unsigned char*>("salt"), 4,
        1,
        EVP_sha256(),
        sizeof(rawOut), rawOut
    );
    check(rawStatus == 1, "raw PKCS5_PBKDF2_HMAC succeeds", failed);
    std::string rawHex = hexEncode(rawOut, sizeof(rawOut));
    const std::string expectedKat = "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b";
    check(rawHex == expectedKat, "PBKDF2-HMAC-SHA256 known answer matches RFC test vector", failed);

    std::cout << "\nPasswordHasher Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed == 0 ? 0 : 1;
}
