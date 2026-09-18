#pragma once
#include "types.h"
#include <string>
namespace midi {
bool saveRecovery(const std::string& path, const Project& project);
bool loadRecovery(const std::string& path, Project& project);
}
