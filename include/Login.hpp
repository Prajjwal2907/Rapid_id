#ifndef LOGIN_HPP
#define LOGIN_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @class Logger
 * @brief Timestamped event logger for authentication events with control character sanitization.
 */
class Logger {
    std::string filename;
public:
    /**
     * @brief Constructs a Logger with a target output file.
     * @param f File path to append log records to (defaults to "log.txt").
     * Failure behavior: Does not fail; initializes member with given path.
     */
    Logger(std::string f = "log.txt");

    /**
     * @brief Appends a sanitized, timestamped log entry: "<timestamp> | <type> | <details>\n".
     *        Control characters (<0x20 and 0x7F) are stripped, fields capped at 200 chars.
     *        Owner-only file permissions are applied where supported.
     * @param type Event category (e.g., "LOGIN_SUCCESS", "LOGIN_FAILED", "LOGOUT").
     * @param details Additional context or username associated with the event.
     * Failure behavior: Fails silently without throwing exceptions if file cannot be opened.
     */
    void log(std::string type, std::string details);
};

/**
 * @struct AuthOptions
 * @brief Configuration parameters for Auth initialization.
 */
struct AuthOptions {
    std::string usersPath = "users.txt";
    std::string keyPath = "data/auth.key";
    std::string bootstrapAdminPassword;
    int pbkdf2Iterations = 600000;
    std::function<long long()> clock;
    bool announceBootstrap = true;
};

/**
 * @struct LoginResult
 * @brief Result object returned by Auth::login2.
 */
struct LoginResult {
    enum Status { Ok, BadCredentials, Locked, Pending, Disabled } status;
    std::string role;
    bool mustChangePassword = false;
    int retryAfterSeconds = 0;
};

/**
 * @struct SignUpResult
 * @brief Detailed result object returned by Auth::signUp.
 */
struct SignUpResult {
    bool ok;
    std::string code;
    std::string error;
};

/**
 * @struct UserInfo
 * @brief User record representation for user listing queries.
 */
struct UserInfo {
    std::string username;
    std::string role;
    std::string status; // "active", "pending", "disabled"
    bool mustChangePassword;
    long long createdAt;
};

/**
 * @struct PasswordCheck
 * @brief Result object returned by checkPasswordPolicy.
 */
struct PasswordCheck {
    bool ok;
    std::string reason;
};

/**
 * @class Auth
 * @brief Manages authentication, user storage, credential validation, and lockout policies.
 */
class Auth {
public:
    /**
     * @brief Constructs an Auth instance using comprehensive AuthOptions.
     * @param opts Configuration options.
     * Failure behavior: If users file is malformed or required keys are missing, ready() becomes false. Never throws.
     */
    explicit Auth(AuthOptions opts);

    /**
     * @brief Constructs an Auth instance with default options targeting the given users file path.
     * @param usersPath Path to users credentials file.
     * Failure behavior: If users file is malformed, ready() becomes false. Never throws.
     */
    explicit Auth(std::string usersPath = "users.txt");

    /**
     * @brief Checks if authentication subsystem is operational and valid.
     * @return True if ready for operations; false if corrupted or key missing.
     * Failure behavior: Pure accessor. Never throws.
     */
    bool ready() const;

    /**
     * @brief Describes the reason why the system is not ready.
     * @return Descriptive error string, or empty string if ready.
     * Failure behavior: Pure accessor. Never throws.
     */
    std::string notReadyReason() const;

    /**
     * @brief Authenticates credentials with lockout enforcement and constant-time defense.
     * @param user Username.
     * @param pw Plaintext password.
     * @return LoginResult with detailed status, role, mustChangePassword, and retryAfterSeconds.
     * Failure behavior: Never throws; returns BadCredentials or Locked on failure.
     */
    LoginResult login2(const std::string& user, const std::string& pw);

    /**
     * @brief Backward-compatible login check.
     * @param user Username.
     * @param pw Plaintext password.
     * @return True only when login2 status is Ok; false otherwise.
     * Failure behavior: Returns false on any failure. Never throws.
     */
    bool login(const std::string& user, const std::string& pw);

    /**
     * @brief Retrieves role designation for user without inserting into the map.
     * @param user Username.
     * @return Role string ("admin", "dispatcher", "unit") or "" if unknown.
     * Failure behavior: Case-insensitive lookup; never mutates storage. Never throws.
     */
    std::string getRole(const std::string& user) const;

    /**
     * @brief Registers a new user with specified role and active status flag.
     * @param user Username (3..32 characters [A-Za-z0-9._-]).
     * @param pw Plaintext password satisfying security policy.
     * @param role Role ("admin", "dispatcher", "unit").
     * @param active True if account is active immediately; false for pending.
     * @return SignUpResult indicating success or specific error code.
     * Failure behavior: Returns ok=false on policy violation, duplicate user, or storage failure. Never throws.
     */
    SignUpResult signUp(const std::string& user, const std::string& pw, const std::string& role, bool active);

    /**
     * @brief Backward-compatible registration helper creating an active user.
     * @param user Username.
     * @param pw Plaintext password.
     * @param role Role designation.
     * @return True on success; false on failure.
     * Failure behavior: Never throws.
     */
    bool signUp(const std::string& user, const std::string& pw, const std::string& role);

    /**
     * @brief Changes a user's password after verifying existing password and validating new policy.
     * @param user Username.
     * @param oldPw Current plaintext password.
     * @param newPw New plaintext password.
     * @param err Output error message on failure.
     * @return True on success; false on failure.
     * Failure behavior: Returns false if old password invalid or new password violates policy. Never throws.
     */
    bool changePassword(const std::string& user, const std::string& oldPw, const std::string& newPw, std::string& err);

    /**
     * @brief Generates a random temporary password for a user, setting mustChange=1.
     * @param user Username.
     * @param tempOut Output string receiving generated temporary password.
     * @param err Output error message on failure.
     * @return True on success; false on failure.
     * Failure behavior: Returns false if user does not exist. Never throws.
     */
    bool adminResetPassword(const std::string& user, std::string& tempOut, std::string& err);

    /**
     * @brief Updates status of a user ("active" or "disabled"). Last active admin cannot be disabled.
     * @param user Username.
     * @param status New status ("active" | "disabled").
     * @param err Output error message on failure.
     * @return True on success; false on failure.
     * Failure behavior: Returns false if user not found, status invalid, or disabling last active admin. Never throws.
     */
    bool setStatus(const std::string& user, const std::string& status, std::string& err);

    /**
     * @brief Approves a pending user by setting status to "active".
     * @param user Username.
     * @param err Output error message on failure.
     * @return True on success; false on failure.
     * Failure behavior: Returns false if user not found or not pending. Never throws.
     */
    bool approve(const std::string& user, std::string& err);

    /**
     * @brief Enumerates all registered users.
     * @return Vector of UserInfo structures.
     * Failure behavior: Never throws.
     */
    std::vector<UserInfo> listUsers() const;

    /**
     * @brief Checks if a user exists and is active.
     * @param user Username.
     * @return True if exists and status == "active"; false otherwise.
     * Failure behavior: Never throws.
     */
    bool isActive(const std::string& user) const;

    /**
     * @brief Validates password against length (10..128), username inclusion, and common passwords.
     * @param pw Password to evaluate.
     * @param username Associated username.
     * @return PasswordCheck result with ok flag and reason string.
     * Failure behavior: Never throws.
     */
    static PasswordCheck checkPasswordPolicy(const std::string& pw, const std::string& username);

    /**
     * @brief Validates username character set [A-Za-z0-9._-] and length (3..32).
     * @param u Username to evaluate.
     * @return True if valid; false otherwise.
     * Failure behavior: Never throws.
     */
    static bool isValidUsername(const std::string& u);

private:
    struct UserRecord {
        std::string originalUsername;
        bool isLegacy = false;
        std::size_t legacyHash = 0;
        std::string hash;
        std::string role;
        std::string status;
        bool mustChangePassword = false;
        long long createdAt = 0;
    };

    struct LockoutInfo {
        int failureCount = 0;
        long long firstFailureTime = 0;
        long long lastFailureTime = 0;
        long long lockedUntil = 0;
    };

    AuthOptions opts_;
    bool ready_ = true;
    std::string notReadyReason_;
    std::string pepper_;
    std::string dummyHash_;

    std::unordered_map<std::string, UserRecord> users_;
    std::vector<std::string> userOrder_;
    std::unordered_map<std::string, LockoutInfo> lockouts_;

    long long now() const;
    void init();
    void load();
    void save();
    void trackFailure(const std::string& lowerUser, long long currentTime);
    void resetLockout(const std::string& lowerUser);
    int getRetryAfter(const std::string& lowerUser, long long currentTime) const;
    std::size_t hashPass(const std::string& p) const;
    static std::string toLower(const std::string& s);
};

/**
 * @brief Interactive dispatcher console menu.
 * @param auth Reference to active Auth instance.
 * @param logger Reference to active Logger instance.
 * @param user Name of the authenticated user.
 * Failure behavior: On invalid input choices, prints an error message to stdout and reprompts.
 */
void dispatcherMenu(Auth& auth, Logger& logger, std::string user);

/**
 * @brief Processes a JSON-formatted login request and returns a JSON response string.
 * @param auth Reference to active Auth instance.
 * @param requestJson JSON payload formatted as: {"username":"..","password":".."}
 * @return JSON response string with ok, role, mustChangePassword on success; code and retryAfterSeconds on failure.
 * Failure behavior: Never throws. Returns Bad request on malformed JSON. Never outputs password or hash.
 */
std::string handleLoginRequest(Auth& auth, const std::string& requestJson);

/**
 * @brief Processes a logout notification for a user and returns a JSON response string.
 * @param auth Reference to active Auth instance.
 * @param username Username of the logging-out user.
 * @return JSON response string: {"ok":true}
 * Failure behavior: Never throws. Always returns {"ok":true}. Logs logout without passwords.
 */
std::string handleLogoutRequest(Auth& auth, const std::string& username);

/**
 * @brief Health check probe for web UI / HTTP server integration.
 * @return JSON response string: {"ok":true,"service":"login"}
 * Failure behavior: Never throws. Always returns {"ok":true,"service":"login"}.
 */
std::string handleHealthRequest();

/**
 * @brief Processes a JSON-formatted signup request and returns a JSON response string.
 * @param auth Reference to active Auth instance.
 * @param requestJson JSON payload formatted as: {"username":"..","password":"..","role":".."}
 * @return JSON response string.
 * Failure behavior: Never throws. Returns Bad request or policy rejection. Never outputs password or hash.
 */
std::string handleSignUpRequest(Auth& auth, const std::string& requestJson);

#endif // LOGIN_HPP
