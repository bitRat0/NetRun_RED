#pragma once

#include <FS.h>
#include <ArduinoJson.h>

#include "game/BlackIce.h"

enum class BlackIceCatalogError : uint8_t
{
    None, Missing, FileOpen, Json, Format, Version, Field, Id, Duplicate, Conflict,
    Behavior, Visual, Animation, Bounds, TooMany
};

struct BlackIceCatalogResult
{
    bool success = false;
    BlackIceCatalogError error = BlackIceCatalogError::None;
    const char* message = "NOT LOADED";
};

struct BlackIceStoredDefinition
{
    char id[32] = {};
    char name[32] = {};
    BlackIceDefinition definition = {};
};

class BlackIceRegistry
{
public:
    static constexpr size_t MAX_CUSTOM_DEFINITIONS = 8;
    static constexpr size_t ID_LENGTH = 32;
    static constexpr size_t NAME_LENGTH = 32;
    static constexpr size_t JSON_CAPACITY = 6144;
    static constexpr const char* CATALOG_PATH = "/catalog/black_ice.json";

    BlackIceCatalogResult loadFile(fs::FS& fs, const char* path = CATALOG_PATH);
    BlackIceCatalogResult loadJson(const char* json);
    void clear();
    const BlackIceDefinition* findByContentId(const char* id) const;
    const BlackIceDefinition* findCustomByContentId(const char* id) const;
    const BlackIceDefinition* definitionAt(size_t index) const;
    size_t count() const { return blackIceDefinitionCount() + customCount_; }
    size_t customCount() const { return customCount_; }

private:
    BlackIceCatalogResult parse(ArduinoJson::JsonDocument& document);
    BlackIceStoredDefinition custom_[MAX_CUSTOM_DEFINITIONS] = {};
    size_t customCount_ = 0;
};

BlackIceRegistry& blackIceRegistry();
const BlackIceDefinition* findBlackIceByContentId(const char* id);
const char* blackIceCatalogErrorMessage(BlackIceCatalogError error);
// Shared by the catalog and Scenario Schema 1 parsing. It validates a definition's
// intrinsic data only; each owner enforces its own ID conflict rules.
BlackIceCatalogResult parseBlackIceDefinition(ArduinoJson::JsonObjectConst source,
                                              BlackIceStoredDefinition& target);
