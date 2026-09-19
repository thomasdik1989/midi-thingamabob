#include "file_browser.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>

std::string defaultBrowseDirectory() {
    if (const char* home = std::getenv("HOME")) return home;
    return ".";
}

std::string parentDirectory(const std::string& path) {
    if (path.empty()) return defaultBrowseDirectory();
    std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) return defaultBrowseDirectory();
    return parent.string();
}

bool pathIsDirectory(const std::string& path) {
    std::error_code error;
    return std::filesystem::is_directory(path, error);
}

std::vector<FileBrowserEntry> listDirectory(const std::string& path) {
    std::vector<FileBrowserEntry> entries;
    std::error_code error;
    if (!std::filesystem::exists(path, error) || !std::filesystem::is_directory(path, error)) {
        return entries;
    }

    for (const auto& entry : std::filesystem::directory_iterator(path, error)) {
        if (error) break;
        FileBrowserEntry item;
        item.name = entry.path().filename().string();
        if (item.name.empty() || item.name == ".") continue;
        item.is_directory = entry.is_directory(error);
        entries.push_back(item);
    }

    std::sort(entries.begin(), entries.end(), [](const FileBrowserEntry& a, const FileBrowserEntry& b) {
        if (a.is_directory != b.is_directory) return a.is_directory > b.is_directory;
        return a.name < b.name;
    });
    return entries;
}
