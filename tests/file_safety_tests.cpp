#include "file_safety.h"
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    auto directory = std::filesystem::temp_directory_path() /
                     ("midi-safety-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(directory);
    try {
        App app;
        FileSafety safety(app);
        int actions = 0;

        safety.request([&] { ++actions; });
        check(actions == 1, "clean project should continue immediately");

        { auto edit = app.edit(); app.getProject().tracks[0].name = "Unsaved"; }
        safety.request([&] { ++actions; });
        check(actions == 1 && safety.hasPendingAction(), "dirty project must prompt");

        check(safety.resolve(FileSafety::Decision::Cancel), "cancel failed");
        check(actions == 1 && app.getProject().modified && !safety.hasPendingAction(),
              "cancel lost work or continued");

        safety.request([&] { ++actions; });
        check(!safety.resolve(FileSafety::Decision::Save), "untitled save continued without a path");
        check(!safety.resolve(FileSafety::Decision::Save, (directory / "missing" / "save.mid").string()),
              "failed save continued");
        check(actions == 1 && app.getProject().modified && safety.hasPendingAction(),
              "failed save lost pending state");

        check(safety.resolve(FileSafety::Decision::Save, (directory / "save.mid").string()),
              "save did not continue");
        check(actions == 2 && !app.getProject().modified, "save did not clear dirty state");

        { auto edit = app.edit(); app.getProject().tracks[0].name = "Discard me"; }
        safety.request([&] { ++actions; app.newProject(); });
        safety.request([&] { actions += 100; });
        check(safety.resolve(FileSafety::Decision::Discard), "discard failed");
        check(actions == 3 && !app.getProject().modified, "discard did not execute exactly one action");

        std::filesystem::remove_all(directory);
        std::cout << "All file-safety workflow tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
