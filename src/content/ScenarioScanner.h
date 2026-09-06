#pragma once

#include <FS.h>

#include "ScenarioLoader.h"

struct ScenarioEntry
{
    char fileName[40] = {};
    char path[72] = {};
    char name[LoadedScenario::NAME_LENGTH] = {};
    char id[LoadedScenario::ID_LENGTH] = {};
    uint8_t schemaVersion = 0;
    bool valid = false;
    ScenarioImportResult result;
};

class ScenarioScanner
{
public:
    // This is a metadata cache/window size, not a limit on SD files.
    static constexpr size_t WINDOW_SIZE = 8;
    static constexpr const char* DIRECTORY = "/scenarios";

    bool begin();
    void scan();
    size_t count() const { return count_; }
    bool sdAvailable() const { return sdAvailable_; }
    bool directoryAvailable() const { return directoryAvailable_; }
    size_t validCount() const { return validCount_; }
    bool loadWindow(size_t first, size_t requested = WINDOW_SIZE);
    const ScenarioEntry* entry(size_t index) const;
    ScenarioImportResult load(size_t index, LoadedScenario& output);

private:
    bool readEntry(File& directory, size_t targetIndex, ScenarioEntry& output);
    ScenarioEntry entries_[WINDOW_SIZE];
    size_t count_ = 0;
    size_t windowStart_ = 0;
    size_t windowCount_ = 0;
    size_t validCount_ = 0;
    bool sdAvailable_ = false;
    bool directoryAvailable_ = false;
    ScenarioLoader loader_;
};
