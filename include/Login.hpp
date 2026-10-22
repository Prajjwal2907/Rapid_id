#ifndef LOGIN_HPP
#define LOGIN_HPP

#include <cstddef>
#include <string>
#include <unordered_map>

/**
 * @class Logger
 * @brief Simple timestamped event logger for login/logout actions.
 */
class Logger {
    std::string filename;
public:
    /**
     * @brief Constructs a Logger with a target output file.
     * @param f File path to append log records to (defaults to "log.txt").
     * Failure behavior: Does not fail; initializes filename member with given path.
     */
    Logger(std::string f = "log.txt");

    /**
     * @brief Appends a timestamped log entry in the format: "<timestamp> | <type> | <details>\n".
     * @param type Event category (e.g., "LOGIN_SUCCESS", "LOGIN_FAILED", "LOGOUT").
     * @param details Additional context or username associated with the event.
     * Failure behavior: If the file cannot be opened or written to, the write fails silently
     *                    without throwing exceptions.
     */
    void log(std::string type, std::string details);
};

/**
 * @struct User
 * @brief Internal user record storing password hash and role designation.
 */
struct User {
    std::size_t passHash;
    std::string role;
};

/**
 * @class Auth
 * @brief Manages authentication, user storage, and credential validation.
 */
class Auth {
    std::unordered_map<std::string, User> users;
    std::string filename;

    /**
     * @brief Computes a std::hash for the given password string.
     * Failure behavior: Does not fail; returns std::size_t hash code.
     */
    std::size_t hashPass(const std::string& p);

public:
    /**
     * @brief Constructs an Auth instance and loads users from disk.
     *        If no users are found after loading, automatically creates default "admin" account.
     * @param f File path for user credentials storage (defaults to "users.txt").
     * Failure behavior: If the file cannot be opened or read, users remains empty,
     *                    and the default admin account ("admin", "admin123", "admin") is created and saved.
     */
    Auth(std::string f = "users.txt");

    /**
     * @brief Loads user records from the configured file.
     * Failure behavior: If the file cannot be opened or data is malformed, reading ceases;
     *                    any users loaded prior to the error remain in memory.
     */
    void load();

    /**
     * @brief Persists all user records to the configured file.
     * Failure behavior: If the file cannot be opened for writing, no data is written;
     *                    fails silently without throwing exceptions.
     */
    void save();

    /**
     * @brief Registers a new user with the specified credentials and role.
     * @param u Desired username.
     * @param p Plaintext password.
     * @param role User role designation (e.g., "admin", "dispatcher").
     * @return true if the user was successfully registered; false if the username already exists.
     * Failure behavior: Returns false if the username is already registered in the users map.
     */
    bool signUp(std::string u, std::string p, std::string role);

    /**
     * @brief Validates login credentials against existing user records.
     * @param u Username to verify.
     * @param p Plaintext password to verify against stored hash.
     * @return true if credentials match; false otherwise.
     * Failure behavior: Returns false if username is not found or password hash does not match.
     */
    bool login(std::string u, std::string p);

    /**
     * @brief Retrieves the role associated with the specified username.
     * @param u Username to query.
     * @return The user's role string.
     * Failure behavior: If the username does not exist in the map, accessing users[u] default-inserts
     *                    an entry with hash 0 and returns an empty string ("").
     */
    std::string getRole(std::string u);
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
 * @return JSON response string:
 *         Success: {"ok":true,"username":"..","role":".."}
 *         Auth Failure: {"ok":false,"error":"Invalid username or password"}
 *         Bad Request:  {"ok":false,"error":"Bad request"}
 * Failure behavior: Never throws. Returns {"ok":false,"error":"Bad request"} on malformed JSON,
 *                    non-object JSON, missing "username"/"password", or non-string values.
 *                    Returns {"ok":false,"error":"Invalid username or password"} if credentials
 *                    do not match. Logs attempts via Logger without logging passwords.
 */
std::string handleLoginRequest(Auth& auth, const std::string& requestJson);

/**
 * @brief Processes a logout notification for a user and returns a JSON response string.
 * @param auth Reference to active Auth instance.
 * @param username Username of the logging-out user.
 * @return JSON response string: {"ok":true}
 * Failure behavior: Never throws. Always returns {"ok":true}. Logs the logout event via Logger.
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
 * @return JSON response string:
 *         Success: {"ok":true,"username":"..","role":".."}
 *         Username exists: {"ok":false,"error":"Username already exists"}
 *         Bad Request:  {"ok":false,"error":"Bad request"}
 * Failure behavior: Never throws. Returns {"ok":false,"error":"Bad request"} on malformed JSON,
 *                    non-object JSON, missing "username"/"password", empty strings, or non-string values.
 *                    Returns {"ok":false,"error":"Username already exists"} if user already exists.
 *                    Logs attempts via Logger without logging passwords.
 */
std::string handleSignUpRequest(Auth& auth, const std::string& requestJson);

#endif // LOGIN_HPP
