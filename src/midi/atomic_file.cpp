#include "atomic_file.h"
#include <filesystem>
#include <fstream>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace midi {
namespace {
static uint32_t nextTempSuffix() {
    static uint32_t counter = 0;
    return ++counter;
}
}

bool atomicWrite(const std::string& path, const std::function<bool(std::ostream&)>& write) {
    if (path.empty()) return false;

    std::filesystem::path temporary;
    std::error_code error;
    for (int attempt = 0; attempt < 32; ++attempt) {
        temporary = std::filesystem::path(path + ".saving-" + std::to_string(nextTempSuffix()));
        if (std::filesystem::create_directory(temporary, error)) break;
        temporary.clear();
        if (error && error != std::errc::file_exists) return false;
    }
    if (temporary.empty()) return false;

    const auto payload = temporary / "data";
    bool success = false;
    {
        std::ofstream stream(payload, std::ios::binary | std::ios::trunc);
        success = stream && write(stream);
        stream.flush();
        success = success && stream.good();
        stream.close();
        success = success && !stream.fail();
    }

#ifdef _WIN32
    if (success) {
        HANDLE file = CreateFileW(payload.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        success = file != INVALID_HANDLE_VALUE;
        if (success) {
            success = FlushFileBuffers(file);
            CloseHandle(file);
        }
    }
    if (success) {
        success = MoveFileExW(payload.c_str(), std::filesystem::path(path).c_str(),
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
#else
    if (success) {
        int fd = ::open(payload.c_str(), O_RDONLY);
        success = fd >= 0;
        if (fd >= 0) {
            success = ::fsync(fd) == 0;
            ::close(fd);
        }
    }
    if (success) {
        std::filesystem::rename(payload, path, error);
        success = !error;
    }
    if (success) {
        auto parent = std::filesystem::path(path).parent_path();
        int fd = ::open(parent.empty() ? "." : parent.c_str(), O_RDONLY);
        if (fd >= 0) {
            ::fsync(fd);
            ::close(fd);
        }
    }
#endif

    std::filesystem::remove_all(temporary, error);
    return success;
}
}
