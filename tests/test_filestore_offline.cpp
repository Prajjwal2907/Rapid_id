#include "FileStore.hpp"

#include <filesystem>
#include <iostream>
#include <string>

using rapidaid::io::FileStore;

int main() {
    int failed = 0;
    const std::string testDir = "build/test_tmp_filestore";

    std::error_code ec;
    std::filesystem::remove_all(testDir, ec);

    // Case 1: save then load round-trips text with newlines and non-ASCII bytes
    {
        std::string path = testDir + "/roundtrip.bin";
        std::string original = "Line 1\r\nLine 2\nLine 3\nCaf\xC3\xA9 \xE2\x82\xAC \x80\xFF\xAA";
        original.push_back('\0');
        original.append("null_byte_following\r\n");

        if (FileStore::save(path, original)) {
            std::string loaded = FileStore::load(path);
            if (loaded == original) {
                std::cout << "[PASS] Case 1: save then load round-trips text with newlines and non-ASCII bytes\n";
            } else {
                std::cout << "[FAIL] Case 1: loaded text did not match original\n";
                failed++;
            }
        } else {
            std::cout << "[FAIL] Case 1: save failed\n";
            failed++;
        }
    }

    // Case 2: save creates nested directories
    {
        std::string nestedPath = testDir + "/a/b/c.json";
        std::string content = "{\"nested\": true}";
        if (FileStore::save(nestedPath, content) &&
            FileStore::exists(nestedPath) &&
            FileStore::load(nestedPath) == content) {
            std::cout << "[PASS] Case 2: save creates nested directories\n";
        } else {
            std::cout << "[FAIL] Case 2: save failed to create nested directories\n";
            failed++;
        }
    }

    // Case 3: load of a missing file returns ""
    {
        std::string missingPath = testDir + "/missing_file_xyz.txt";
        std::string result = FileStore::load(missingPath);
        if (result.empty()) {
            std::cout << "[PASS] Case 3: load of a missing file returns \"\"\n";
        } else {
            std::cout << "[FAIL] Case 3: load of missing file did not return empty string\n";
            failed++;
        }
    }

    // Case 4: exists returns true for existing file, false for non-existing or directory
    {
        std::string filePath = testDir + "/exists_test.txt";
        FileStore::save(filePath, "exists");

        bool fileExists = FileStore::exists(filePath);
        bool missingExists = FileStore::exists(testDir + "/nonexistent.txt");
        bool dirExists = FileStore::exists(testDir);

        if (fileExists && !missingExists && !dirExists) {
            std::cout << "[PASS] Case 4: exists returns true for existing file, false for non-existing or directory\n";
        } else {
            std::cout << "[FAIL] Case 4: exists check failed (fileExists=" << fileExists
                      << ", missingExists=" << missingExists
                      << ", dirExists=" << dirExists << ")\n";
            failed++;
        }
    }

    // Case 5: save overwrites an existing file
    {
        std::string overwritePath = testDir + "/overwrite.txt";
        FileStore::save(overwritePath, "initial");
        bool savedNew = FileStore::save(overwritePath, "overwritten");
        std::string loaded = FileStore::load(overwritePath);
        if (savedNew && loaded == "overwritten") {
            std::cout << "[PASS] Case 5: save overwrites an existing file\n";
        } else {
            std::cout << "[FAIL] Case 5: save did not overwrite existing file (loaded=\"" << loaded << "\")\n";
            failed++;
        }
    }

    // Case 6: no ".tmp" file left behind after save
    {
        std::string targetPath = testDir + "/tmp_check.txt";
        std::string tmpPath = targetPath + ".tmp";
        FileStore::save(targetPath, "check tmp");
        if (!std::filesystem::exists(tmpPath)) {
            std::cout << "[PASS] Case 6: no .tmp file left behind after save\n";
        } else {
            std::cout << "[FAIL] Case 6: .tmp file was left behind\n";
            failed++;
        }
    }

    // Case 7: sanitizeFileName cases
    {
        bool s1 = (FileStore::sanitizeFileName("Eiffel Tower, Paris") == "eiffel_tower_paris");
        bool s2 = (FileStore::sanitizeFileName("  !!A  b  ") == "a_b");
        bool s3 = (FileStore::sanitizeFileName("") == "unnamed");

        std::string longInput(200, 'x');
        std::string s4Res = FileStore::sanitizeFileName(longInput);
        bool s4 = (s4Res.size() == 80 && s4Res == std::string(80, 'x'));

        if (s1 && s2 && s3 && s4) {
            std::cout << "[PASS] Case 7: sanitizeFileName cases\n";
        } else {
            std::cout << "[FAIL] Case 7: sanitizeFileName failed (s1=" << s1
                      << ", s2=" << s2 << ", s3=" << s3 << ", s4=" << s4
                      << ", s4Len=" << s4Res.size() << ")\n";
            failed++;
        }
    }

    // Cleanup temp directory
    std::filesystem::remove_all(testDir, ec);
    if (!std::filesystem::exists(testDir)) {
        std::cout << "[PASS] Cleaned up temporary test directory\n";
    } else {
        std::cout << "[FAIL] Failed to clean up temporary test directory\n";
        failed++;
    }

    std::cout << "\nFileStore Test Summary: "
              << (failed == 0 ? "ALL PASSED" : "FAILURES DETECTED")
              << " (" << failed << " failures)\n";

    return failed > 0 ? 1 : 0;
}
