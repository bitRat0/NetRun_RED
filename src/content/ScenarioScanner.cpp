#include "ScenarioScanner.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <string.h>

#include "BlackIceRegistry.h"
#include "DemonRegistry.h"
#include "EnemyNetrunnerRegistry.h"
#include "system/SharedSpiBus.h"

namespace
{
constexpr int SD_SPI_CS_PIN = 12;
constexpr uint32_t SD_FREQUENCY = 25000000;

bool hasJsonExtension(const char* name)
{
    if (name == nullptr) return false;
    const size_t length = strlen(name);
    return length >= 5 && strcasecmp(name + length - 5, ".json") == 0;
}

void copyBounded(char* destination, size_t capacity, const char* source)
{
    if (capacity == 0) return;
    snprintf(destination, capacity, "%s", source != nullptr ? source : "");
}
}

constexpr const char* ScenarioScanner::DIRECTORY;

bool ScenarioScanner::begin()
{
    SharedSpiBus::prepareForSd();
    sdAvailable_ = SD.begin(SD_SPI_CS_PIN, SPI, SD_FREQUENCY) && SD.cardType() != CARD_NONE;
    if (sdAvailable_)
    {
        const BlackIceCatalogResult catalog = blackIceRegistry().loadFile(SD);
        Serial.printf("[BlackIceCatalog] %s | %s | Custom=%u\n", catalog.success ? "PASS" : "FAIL",
            catalog.message, static_cast<unsigned>(blackIceRegistry().customCount()));
        const DemonCatalogResult demons = demonRegistry().loadFile(SD);
        Serial.printf("[DemonCatalog] %s | %s | Custom=%u\n", demons.success ? "PASS" : "FAIL",
            demons.message, static_cast<unsigned>(demonRegistry().count()));
        const EnemyCatalogResult enemies = enemyNetrunnerRegistry().loadFile(SD);
        Serial.printf("[EnemyCatalog] %s | %s | Custom=%u\n", enemies.success ? "PASS" : "FAIL",
            enemies.message, static_cast<unsigned>(enemyNetrunnerRegistry().count()));
    }
    scan();
    Serial.printf("[SD] begin: %s\n", sdAvailable_ ? "OK" : "FAIL");
    Serial.printf("[SD] /scenarios: %s\n", directoryAvailable_ ? "OK" : "MISSING");
    Serial.printf("[SD] json files: %u\n", static_cast<unsigned>(count_));
    Serial.printf("[Scenario] loaded: %u\n", static_cast<unsigned>(validCount_));
    return sdAvailable_;
}

void ScenarioScanner::scan()
{
    count_ = 0;
    validCount_ = 0;
    directoryAvailable_ = false;
    memset(entries_, 0, sizeof(entries_));
    windowStart_ = 0;
    windowCount_ = 0;
    if (!sdAvailable_) return;
    SharedSpiBus::prepareForSd();
    File directory = SD.open(DIRECTORY);
    if (!directory || !directory.isDirectory()) { if (directory) directory.close(); return; }
    directoryAvailable_ = true;

    File file = directory.openNextFile();
    while (file)
    {
        if (!file.isDirectory() && hasJsonExtension(file.name()))
            ++count_;
        file.close();
        file = directory.openNextFile();
    }
    if (file) file.close();
    directory.close();
    // Read only lightweight headers for the validity count. Full scenario
    // validation remains deferred until the user presses ENTER.
    for (size_t index = 0; index < count_; ++index)
    {
        if (!loadWindow(index, 1)) continue;
        const ScenarioEntry* current = entry(index);
        if (current != nullptr && current->valid) ++validCount_;
    }
    loadWindow(0, WINDOW_SIZE);
}

bool ScenarioScanner::readEntry(File& directory, size_t targetIndex, ScenarioEntry& output)
{
    memset(&output, 0, sizeof(output));
    File file = directory.openNextFile();
    size_t jsonIndex = 0;
    while (file)
    {
        if (!file.isDirectory() && hasJsonExtension(file.name()))
        {
            if (jsonIndex++ == targetIndex)
            {
                const char* path = file.path();
                copyBounded(output.fileName, sizeof(output.fileName), file.name());
                copyBounded(output.path, sizeof(output.path), path);
                if (strlen(file.name()) >= sizeof(output.fileName) || strlen(path) >= sizeof(output.path))
                {
                    output.result.error = ScenarioImportError::StringTooLong;
                    output.result.message = "FILE NAME TOO LONG";
                    file.close(); return true;
                }
                file.close();
                file = SD.open(output.path, FILE_READ);
                if (!file)
                {
                    output.result.error = ScenarioImportError::FileOpenFailed;
                    output.result.message = "FILE OPEN FAILED";
                    copyBounded(output.name, sizeof(output.name), output.fileName);
                    return true;
                }
                DynamicJsonDocument document(ScenarioLoader::JSON_CAPACITY);
                const DeserializationError jsonError = deserializeJson(document, file);
                file.close();
                if (jsonError)
                {
                    output.result.error = ScenarioImportError::JsonParseFailed;
                    output.result.message = "JSON PARSE FAILED";
                    copyBounded(output.name, sizeof(output.name), output.fileName);
                    return true;
                }
                if (!document.is<JsonObject>())
                {
                    output.result.error = ScenarioImportError::MissingField;
                    output.result.message = "ROOT OBJECT";
                    copyBounded(output.name, sizeof(output.name), output.fileName);
                    return true;
                }
                JsonObjectConst root = document.as<JsonObjectConst>();
                const char* id = root["id"].is<const char*>() ? root["id"].as<const char*>() : nullptr;
                const char* name = root["name"].is<const char*>() ? root["name"].as<const char*>() : nullptr;
                if (id != nullptr && strlen(id) < sizeof(output.id)) copyBounded(output.id, sizeof(output.id), id);
                if (name != nullptr && strlen(name) < sizeof(output.name)) copyBounded(output.name, sizeof(output.name), name);
                if (output.name[0] == '\0') copyBounded(output.name, sizeof(output.name), output.fileName);
                output.schemaVersion = root["schemaVersion"].is<int>() ? root["schemaVersion"].as<uint8_t>() : 0;
                if (!root["format"].is<const char*>() || strcmp(root["format"].as<const char*>(), "netrun-architecture"))
                {
                    output.result.error = !root["format"].is<const char*>() ? ScenarioImportError::MissingField : ScenarioImportError::WrongFormat;
                    output.result.message = output.result.error == ScenarioImportError::MissingField ? "MISSING FORMAT" : "WRONG FORMAT";
                    return true;
                }
                if (!root["schemaVersion"].is<int>())
                {
                    output.result.error = ScenarioImportError::MissingField;
                    output.result.message = "MISSING SCHEMA VERSION";
                    return true;
                }
                if (output.schemaVersion != 1)
                {
                    output.result.error = ScenarioImportError::UnsupportedVersion;
                    output.result.message = "SCHEMA VERSION NOT SUPPORTED";
                    return true;
                }
                output.result.success = true;
                output.result.error = ScenarioImportError::None;
                output.result.message = "METADATA OK";
                output.valid = true;
                return true;
            }
        }
        file.close();
        file = directory.openNextFile();
    }
    if (file) file.close();
    return false;
}

bool ScenarioScanner::loadWindow(size_t first, size_t requested)
{
    windowStart_ = first < count_ ? first : count_;
    windowCount_ = 0;
    memset(entries_, 0, sizeof(entries_));
    if (!sdAvailable_ || !directoryAvailable_ || windowStart_ >= count_ || requested == 0) return true;
    if (requested > WINDOW_SIZE) requested = WINDOW_SIZE;
    SharedSpiBus::prepareForSd();
    for (size_t offset = 0; offset < requested; ++offset)
    {
        // Each lookup starts from the directory head. Filesystem iteration has
        // no portable seek-to-entry operation, so this keeps logical indices
        // stable across page changes without retaining a full filename list.
        File directory = SD.open(DIRECTORY);
        if (!directory || !directory.isDirectory()) { if (directory) directory.close(); break; }
        const bool found = readEntry(directory, windowStart_ + offset, entries_[offset]);
        directory.close();
        if (!found) break;
        ++windowCount_;
    }
    return windowCount_ == requested;
}

const ScenarioEntry* ScenarioScanner::entry(size_t index) const
{
    if (index < windowStart_ || index >= windowStart_ + windowCount_) return nullptr;
    return &entries_[index - windowStart_];
}

ScenarioImportResult ScenarioScanner::load(size_t index, LoadedScenario& output)
{
    if (!sdAvailable_) {
        ScenarioImportResult result; result.error = ScenarioImportError::SdUnavailable;
        result.message = "SD UNAVAILABLE"; return result;
    }
    SharedSpiBus::prepareForSd();
    if (index >= count_) {
        ScenarioImportResult result; result.error = ScenarioImportError::FileOpenFailed;
        result.message = "SCENARIO INDEX INVALID"; return result;
    }
    if (!loadWindow(index, 1)) {
        ScenarioImportResult result; result.error = ScenarioImportError::FileOpenFailed;
        result.message = "SCENARIO FILE CHANGED"; return result;
    }
    const ScenarioEntry* selected = entry(index);
    if (selected == nullptr || !selected->valid) {
        if (selected != nullptr) return selected->result;
        ScenarioImportResult result; result.error = ScenarioImportError::FileOpenFailed;
        result.message = "SCENARIO FILE CHANGED"; return result;
    }
    return loader_.loadFile(SD, selected->path, output);
}
