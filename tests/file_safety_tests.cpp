#include "file_safety.h"
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void cleanProjectContinuesImmediately() {
    App app;
    FileSafety safety(app);
    int actions = 0;

    safety.request([&] { ++actions; });
    check(actions == 1, "clean project should continue immediately");
}

static void dirtyProjectCancelKeepsWork() {
    App app;
    FileSafety safety(app);
    int actions = 0;

    { auto edit = app.edit(); app.getProject().tracks[0].name = "Unsaved"; }
    safety.request([&] { ++actions; });
    check(actions == 0, "dirty project must prompt");
    check(safety.hasPendingAction(), "dirty project must prompt");

    check(safety.resolve(FileSafety::Decision::Cancel), "cancel failed");
    check(actions == 0, "cancel lost work or continued");
    check(app.getProject().modified, "cancel lost work or continued");
    check(!safety.hasPendingAction(), "cancel lost work or continued");
}

static void untitledSaveDoesNotContinue() {
    App app;
    FileSafety safety(app);
    int actions = 0;

    { auto edit = app.edit(); app.getProject().tracks[0].name = "Unsaved"; }
    safety.request([&] { ++actions; });

    check(!safety.resolve(FileSafety::Decision::Save), "untitled save continued without a path");
    check(actions == 0, "untitled save continued without a path");
}

static void failedSaveKeepsPendingAction(const std::filesystem::path& directory) {
    App app;
    FileSafety safety(app);
    int actions = 0;

    { auto edit = app.edit(); app.getProject().tracks[0].name = "Unsaved"; }
    safety.request([&] { ++actions; });

    check(!safety.resolve(FileSafety::Decision::Save, (directory / "missing" / "save.mid").string()),
          "failed save continued");
    check(actions == 0, "failed save lost pending state");
    check(app.getProject().modified, "failed save lost pending state");
    check(safety.hasPendingAction(), "failed save lost pending state");
}

static void successfulSaveClearsDirtyAndContinues(const std::filesystem::path& directory) {
    App app;
    FileSafety safety(app);
    int actions = 0;

    { auto edit = app.edit(); app.getProject().tracks[0].name = "Unsaved"; }
    safety.request([&] { ++actions; });

    check(safety.resolve(FileSafety::Decision::Save, (directory / "save.mid").string()),
          "save did not continue");
    check(actions == 1, "save did not clear dirty state");
    check(!app.getProject().modified, "save did not clear dirty state");
}

static void discardExecutesExactlyOnePendingAction() {
    App app;
    FileSafety safety(app);
    int actions = 0;

    { auto edit = app.edit(); app.getProject().tracks[0].name = "Discard me"; }
    safety.request([&] { ++actions; app.newProject(); });
    safety.request([&] { actions += 100; });

    check(safety.resolve(FileSafety::Decision::Discard), "discard failed");
    check(actions == 1, "discard did not execute exactly one action");
    check(!app.getProject().modified, "discard did not execute exactly one action");
}

int main() {
    auto directory = std::filesystem::temp_directory_path() /
                     ("midi-safety-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(directory);
    try {
        cleanProjectContinuesImmediately();
        dirtyProjectCancelKeepsWork();
        untitledSaveDoesNotContinue();
        failedSaveKeepsPendingAction(directory);
        successfulSaveClearsDirtyAndContinues(directory);
        discardExecutesExactlyOnePendingAction();

        std::filesystem::remove_all(directory);
        std::cout << "All file-safety workflow tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
