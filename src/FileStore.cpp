#include "FileStore.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace rapidaid::io {

bool FileStore::save(const std::string& path, const std::string& text) {
    std::string tmpPath = path + ".tmp";
    try {
        std::filesystem::path targetPath(path);
        std::filesystem::path parent = targetPath.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }

        {
            std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                return false;
            }
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!out.good()) {
                out.close();
                std::error_code ec;
                std::filesystem::remove(tmpPath, ec);
                return false;
            }
            out.close();
            if (out.fail()) {
                std::error_code ec;
                std::filesystem::remove(tmpPath, ec);
                return false;
            }
        }

        std::filesystem::rename(tmpPath, targetPath);
        return true;
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
}

std::string FileStore::load(const std::string& path) {
    try {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) {
            return "";
        }

        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            return "";
        }

        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        return content;
    } catch (...) {
        return "";
    }
}

bool FileStore::exists(const std::string& path) {
    try {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec) && !ec;
    } catch (...) {
        return false;
    }
}

std::string FileStore::sanitizeFileName(const std::string& text) {
    std::string s;
    s.reserve(text.size());

    // 1. Convert to lowercase; characters not in [a-z0-9] become '_'
    for (unsigned char ch : text) {
        if (ch >= 'A' && ch <= 'Z') {
            s.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            s.push_back(static_cast<char>(ch));
        } else {
            s.push_back('_');
        }
    }

    // 2. Collapse repeated '_' into one (e.g. "___" -> "_")
    std::string collapsed;
    collapsed.reserve(s.size());
    bool prevWasUnderscore = false;
    for (char c : s) {
        if (c == '_') {
            if (!prevWasUnderscore) {
                collapsed.push_back('_');
                prevWasUnderscore = true;
            }
        } else {
            collapsed.push_back(c);
            prevWasUnderscore = false;
        }
    }

    // 3. Trim leading and trailing '_'
    size_t start = 0;
    while (start < collapsed.size() && collapsed[start] == '_') {
        ++start;
    }
    size_t end = collapsed.size();
    while (end > start && collapsed[end - 1] == '_') {
        --end;
    }

    std::string trimmed = collapsed.substr(start, end - start);

    // 4. Cap at 80 characters
    if (trimmed.size() > 80) {
        trimmed.resize(80);
    }

    // 5. Return "unnamed" if the result would be empty
    if (trimmed.empty()) {
        return "unnamed";
    }

    return trimmed;
}

} // namespace rapidaid::io
