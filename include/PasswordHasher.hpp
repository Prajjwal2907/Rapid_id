#ifndef RAPIDAID_AUTH_PASSWORDHASHER_HPP
#define RAPIDAID_AUTH_PASSWORDHASHER_HPP

#include <cstddef>
#include <string>

namespace rapidaid::auth {

/**
 * @class PasswordHasher
 * @brief Cryptographic password hashing and verification using PBKDF2-HMAC-SHA256 with pepper.
 */
class PasswordHasher {
public:
    /**
     * @brief Hashes a password using PBKDF2-HMAC-SHA256 with a pepper key.
     *        First computes HMAC-SHA256(pepper, password) as a lowercase hex string,
     *        then derives a 32-byte key via PBKDF2 using a fresh 16-byte salt.
     * @param password Plaintext password.
     * @param pepper Pepper secret string.
     * @param iterations Number of PBKDF2 iterations (defaults to 600000).
     * @return Formatted hash string: "pbkdf2-sha256$<iter>$<salthex>$<hashhex>".
     * Failure behavior: Returns empty string ("") on OpenSSL failure, empty inputs, or invalid iterations. Never throws.
     */
    static std::string hash(const std::string& password, const std::string& pepper, int iterations = 600000);

    /**
     * @brief Verifies a plaintext password against an encoded hash string using constant-time comparison.
     * @param password Plaintext password.
     * @param pepper Pepper secret string.
     * @param encoded Stored hash string ("pbkdf2-sha256$<iter>$<salthex>$<hashhex>").
     * @return True if credentials match; false otherwise.
     * Failure behavior: Returns false on malformed encoded strings, bad hex characters, or OpenSSL errors. Never throws.
     */
    static bool verify(const std::string& password, const std::string& pepper, const std::string& encoded);

    /**
     * @brief Checks if a stored hash needs rehashing (e.g., iterations count differs from target).
     * @param encoded Stored hash string.
     * @param targetIterations Target iteration count.
     * @return True if stored iterations != targetIterations or string is malformed; false if up to date.
     * Failure behavior: Returns true if encoded string cannot be parsed. Never throws.
     */
    static bool needsRehash(const std::string& encoded, int targetIterations);

    /**
     * @brief Generates cryptographically secure random bytes formatted as a lowercase hex string.
     * @param nBytes Number of random bytes to generate.
     * @return Hex string of length 2 * nBytes.
     * Failure behavior: Returns empty string ("") if nBytes == 0 or on OpenSSL failure. Never throws.
     */
    static std::string randomHex(std::size_t nBytes);

    /**
     * @brief Generates a random password from an unambiguous alphabet using rejection sampling (no modulo bias).
     * @param length Desired password character length (defaults to 16).
     * @return Random password string.
     * Failure behavior: Returns empty string ("") if length == 0 or on OpenSSL failure. Never throws.
     */
    static std::string randomPassword(std::size_t length = 16);
};

} // namespace rapidaid::auth

#endif // RAPIDAID_AUTH_PASSWORDHASHER_HPP
