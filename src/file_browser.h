#pragma once

#include <string>
#include <vector>

struct FileBrowserEntry {
    std::string name;
    bool is_directory = false;
};

std::string defaultBrowseDirectory();
std::string parentDirectory(const std::string& path);
std::vector<FileBrowserEntry> listDirectory(const std::string& path);
bool pathIsDirectory(const std::string& path);
