#pragma once

#include <stddef.h>
#include <FS.h>
#include <ArduinoJson.h>

#include "game/EnemyNetrunner.h"

enum class EnemyCatalogError : uint8_t
{
    None, Missing, FileOpen, Json, Format, Version, Field, Duplicate, Conflict,
    Program, Behavior, Bounds, TooMany
};

struct EnemyCatalogResult
{
    bool success = false;
    EnemyCatalogError error = EnemyCatalogError::None;
    const char* message = "NOT LOADED";
};

class EnemyNetrunnerRegistry
{
public:
    static constexpr size_t MAX_CUSTOM_DEFINITIONS = 8;
    static constexpr size_t ID_LENGTH = 32;
    static constexpr size_t NAME_LENGTH = 16;
    static constexpr size_t JSON_CAPACITY = 6144;
    static constexpr const char* CATALOG_PATH = "/catalog/enemies.json";

    EnemyCatalogResult loadFile(fs::FS& fs, const char* path = CATALOG_PATH);
    EnemyCatalogResult loadJson(const char* json);
    void clear();
    bool add(const EnemyNetrunnerDefinition& definition);
    const EnemyNetrunnerDefinition* findByStableId(const char* id) const;
    size_t count() const { return count_; }

private:
    EnemyCatalogResult parse(ArduinoJson::JsonDocument& document);
    struct StoredDefinition
    {
        char id[ID_LENGTH] = {};
        char name[NAME_LENGTH] = {};
        EnemyNetrunnerDefinition definition = {};
    };

    StoredDefinition custom_[MAX_CUSTOM_DEFINITIONS] = {};
    size_t count_ = 0;
};

EnemyNetrunnerRegistry& enemyNetrunnerRegistry();
