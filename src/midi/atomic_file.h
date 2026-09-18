#pragma once

#include <string>
#include <functional>
#include <ostream>

namespace midi {
// Write beside the destination, flush, then atomically replace it. Never truncate the destination.
bool atomicWrite(const std::string& path, const std::function<bool(std::ostream&)>& write);
}
