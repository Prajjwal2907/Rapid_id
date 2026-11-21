#include "PasswordHasher.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace rapidaid::auth {

namespace {

static const char kUnambiguousAlphabet[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
static constexpr std::size_t kAlphabetLen = sizeof(kUnambiguousAlphabet) - 1; // 56
static constexpr unsigned int kRejectionLimit = 256 - (256 % kAlphabetLen);   // 224

std::string hexEncode(const unsigned char* data, std::size_t len) {
    static const char hexChars[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(hexChars[(data[i] >> 4) & 0x0F]);
        out.push_back(hexChars[data[i] & 0x0F]);
    }
    return out;
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool hexDecode(const std::string& hex, std::vector<unsigned char>& out) {
    if (hex.size() % 2 != 0) return false;
    out.clear();
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int h = hexVal(hex[i]);
        int l = hexVal(hex[i + 1]);
        if (h < 0 || l < 0) return false;
        out.push_back(static_cast<unsigned char>((h << 4) | l));
    }
    return true;
}

std::string computePepperHmac(const std::string& pepper, const std::string& password) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_len = 0;
    if (!HMAC(EVP_sha256(),
              pepper.data(), static_cast<int>(pepper.size()),
              reinterpret_cast<const unsigned char*>(password.data()), password.size(),
              md, &md_len)) {
        return "";
    }
    return hexEncode(md, md_len);
}

bool parseEncoded(const std::string& encoded, int& outIter, std::vector<unsigned char>& outSalt, std::vector<unsigned char>& outHash) {
    // Format: pbkdf2-sha256$<iter>$<salthex>$<hashhex>
    static const std::string kPrefix = "pbkdf2-sha256$";
    if (encoded.size() <= kPrefix.size() || encoded.rfind(kPrefix, 0) != 0) {
        return false;
    }

    std::size_t firstDollar = kPrefix.size() - 1;
    std::size_t secondDollar = encoded.find('$', firstDollar + 1);
    if (secondDollar == std::string::npos) return false;
    std::size_t thirdDollar = encoded.find('$', secondDollar + 1);
    if (thirdDollar == std::string::npos) return false;
    if (encoded.find('$', thirdDollar + 1) != std::string::npos) return false;

    std::string iterStr = encoded.substr(firstDollar + 1, secondDollar - firstDollar - 1);
    std::string saltHex = encoded.substr(secondDollar + 1, thirdDollar - secondDollar - 1);
    std::string hashHex = encoded.substr(thirdDollar + 1);

    if (iterStr.empty() || saltHex.empty() || hashHex.empty()) return false;
    for (char c : iterStr) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }

    int iterations = std::atoi(iterStr.c_str());
    if (iterations <= 0) return false;

    if (!hexDecode(saltHex, outSalt) || outSalt.size() != 16) return false;
    if (!hexDecode(hashHex, outHash) || outHash.size() != 32) return false;

    outIter = iterations;
    return true;
}

} // namespace

std::string PasswordHasher::hash(const std::string& password, const std::string& pepper, int iterations) {
    try {
        if (password.empty() || pepper.empty() || iterations <= 0) {
            return "";
        }

        std::string hmacHex = computePepperHmac(pepper, password);
        if (hmacHex.empty()) {
            return "";
        }

        unsigned char salt[16];
        if (RAND_bytes(salt, sizeof(salt)) != 1) {
            return "";
        }

        unsigned char hashBuf[32];
        if (PKCS5_PBKDF2_HMAC(hmacHex.data(), static_cast<int>(hmacHex.size()),
                              salt, sizeof(salt),
                              iterations,
                              EVP_sha256(),
                              sizeof(hashBuf), hashBuf) != 1) {
            return "";
        }

        std::string saltHex = hexEncode(salt, sizeof(salt));
        std::string hashHex = hexEncode(hashBuf, sizeof(hashBuf));
        return "pbkdf2-sha256$" + std::to_string(iterations) + "$" + saltHex + "$" + hashHex;
    } catch (...) {
        return "";
    }
}

bool PasswordHasher::verify(const std::string& password, const std::string& pepper, const std::string& encoded) {
    try {
        if (password.empty() || pepper.empty() || encoded.empty()) {
            return false;
        }

        int iterations = 0;
        std::vector<unsigned char> salt;
        std::vector<unsigned char> expectedHash;
        if (!parseEncoded(encoded, iterations, salt, expectedHash)) {
            return false;
        }

        std::string hmacHex = computePepperHmac(pepper, password);
        if (hmacHex.empty()) {
            return false;
        }

        unsigned char computedHash[32];
        if (PKCS5_PBKDF2_HMAC(hmacHex.data(), static_cast<int>(hmacHex.size()),
                              salt.data(), static_cast<int>(salt.size()),
                              iterations,
                              EVP_sha256(),
                              sizeof(computedHash), computedHash) != 1) {
            return false;
        }

        return CRYPTO_memcmp(computedHash, expectedHash.data(), 32) == 0;
    } catch (...) {
        return false;
    }
}

bool PasswordHasher::needsRehash(const std::string& encoded, int targetIterations) {
    try {
        int iterations = 0;
        std::vector<unsigned char> salt;
        std::vector<unsigned char> hash;
        if (!parseEncoded(encoded, iterations, salt, hash)) {
            return true;
        }
        return iterations != targetIterations;
    } catch (...) {
        return true;
    }
}

std::string PasswordHasher::randomHex(std::size_t nBytes) {
    try {
        if (nBytes == 0) return "";
        std::vector<unsigned char> buf(nBytes);
        if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1) {
            return "";
        }
        return hexEncode(buf.data(), buf.size());
    } catch (...) {
        return "";
    }
}

std::string PasswordHasher::randomPassword(std::size_t length) {
    try {
        if (length == 0) return "";
        std::string result;
        result.reserve(length);

        while (result.size() < length) {
            unsigned char b = 0;
            if (RAND_bytes(&b, 1) != 1) {
                return "";
            }
            if (b < kRejectionLimit) {
                result.push_back(kUnambiguousAlphabet[b % kAlphabetLen]);
            }
        }
        return result;
    } catch (...) {
        return "";
    }
}

} // namespace rapidaid::auth
