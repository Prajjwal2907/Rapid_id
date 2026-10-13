#ifndef RAPIDAID_IO_FILESTORE_HPP
#define RAPIDAID_IO_FILESTORE_HPP

#include <string>

namespace rapidaid::io {

class FileStore {
public:
    // Saves text to path atomically via a temporary file (path + ".tmp").
    // Creates parent directories if the parent path exists and is not empty.
    // Overwrites existing target file if present.
    // Failure behavior: returns false on any failure (e.g. directory creation, write, rename). Never throws.
    static bool save(const std::string& path, const std::string& text);

    // Reads the entire file as a binary string.
    // Failure behavior: returns an empty string ("") if the file is missing, unreadable, or on any error. Never throws.
    static std::string load(const std::string& path);

    // Checks if path exists and is a regular file.
    // Failure behavior: returns false if the file does not exist, is a directory, or on any filesystem error. Never throws.
    static bool exists(const std::string& path);

    // Pure function to sanitize arbitrary text into a safe filename.
    // Converts to lowercase, replaces characters outside [a-z0-9] with '_',
    // collapses repeated underscores into a single '_', trims leading/trailing '_',
    // and caps the result at 80 characters.
    // Failure behavior: returns "unnamed" if the sanitized string would be empty. Never throws.
    static std::string sanitizeFileName(const std::string& text);
};

} // namespace rapidaid::io

#endif // RAPIDAID_IO_FILESTORE_HPP
