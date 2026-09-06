#pragma once

#include <stddef.h>
#include <FS.h>
#include <ArduinoJson.h>
#include "game/Demon.h"

enum class DemonCatalogError : uint8_t
{
    None, Missing, FileOpen, Json, Format, Version, Field, Duplicate, Conflict,
    Bounds, Visual, Animation, TooMany
};

struct DemonCatalogResult
{
    bool success = false;
    DemonCatalogError error = DemonCatalogError::None;
    const char* message = "NOT LOADED";
};

class DemonRegistry
{
public:
    static constexpr size_t MAX_CUSTOM_DEFINITIONS = 8;
    static constexpr size_t JSON_CAPACITY = 4096;
    static constexpr const char* CATALOG_PATH = "/catalog/demons.json";

    DemonCatalogResult loadFile(fs::FS& fs, const char* path = CATALOG_PATH);
    DemonCatalogResult loadJson(const char* json);
    void clear();
    bool add(const DemonDefinition& definition);
    const DemonDefinition* findByStableId(const char* stableId) const;
    size_t count() const { return count_; }

private:
    DemonCatalogResult parse(ArduinoJson::JsonDocument& document);
    DemonStoredDefinition custom_[MAX_CUSTOM_DEFINITIONS] = {};
    size_t count_ = 0;
};

DemonRegistry& demonRegistry();
