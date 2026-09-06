#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ArchitectureDefinition.h"
#include "BlackIceRegistry.h"
#include "DemonRegistry.h"
#include "EnemyNetrunnerRegistry.h"
#include "game/EnemyNetrunner.h"

enum class ScenarioImportError : uint8_t
{
    None,
    SdUnavailable,
    DirectoryMissing,
    FileOpenFailed,
    JsonParseFailed,
    WrongFormat,
    UnsupportedVersion,
    MissingField,
    TooManyFloors,
    DuplicateFloorId,
    TooManyBlackIce,
    UnknownFloorType,
    InvalidSecurityTier,
    UnknownBlackIce,
    InvalidValue,
    StringTooLong,
    DuplicateScenarioId,
    UnknownConnection,
    DuplicateConnection,
    SelfConnection,
    InvalidGraph,
    TooManyEnemies,
    DuplicateEnemyId,
    UnknownEnemy,
    UnknownEnemyProgram,
    InvalidEnemyDefinition,
    InvalidEnemyBehavior,
    EnemyIdConflict
    , DuplicateBlackIceId
    , BlackIceIdConflict
    , UnknownBlackIceContent
    , InvalidBlackIceDefinition
    , DuplicateDemonId
    , DemonIdConflict
    , UnknownDemon
    , InvalidDemonDefinition
    , TooManyDemons
};

struct ScenarioImportResult
{
    bool success = false;
    ScenarioImportError error = ScenarioImportError::None;
    int floorIndex = -1;
    int floorId = -1;
    const char* message = "OK";
};

const char* scenarioErrorMessage(ScenarioImportError error);

class LoadedScenario
{
public:
    static constexpr size_t ID_LENGTH = 32;
    static constexpr size_t NAME_LENGTH = 40;
    static constexpr size_t DESCRIPTION_LENGTH = 80;
    static constexpr size_t CONTENT_ID_LENGTH = 32;
    static constexpr size_t FILE_NAME_LENGTH = 40;
    static constexpr size_t FILE_TYPE_LENGTH = 24;
    static constexpr size_t CONTROL_NAME_LENGTH = 40;
    static constexpr size_t ENEMY_ID_LENGTH = 32;
    static constexpr size_t ENEMY_HANDLE_LENGTH = 16;
    static constexpr size_t MAX_SCENARIO_ENEMIES = MAX_ENEMY_NETRUNNERS;
    static constexpr size_t MAX_LOCAL_BLACK_ICE = BlackIceRegistry::MAX_CUSTOM_DEFINITIONS;
    static constexpr size_t MAX_LOCAL_DEMONS = DemonRegistry::MAX_CUSTOM_DEFINITIONS;

    LoadedScenario() { reset(); }
    LoadedScenario(const LoadedScenario&) = delete;
    LoadedScenario& operator=(const LoadedScenario&) = delete;

    // Imports parse into static staging then commit through this rebinding copy.
    // Every pointer that targets scenario-owned storage remains valid afterward.
    void commitFrom(const LoadedScenario& source)
    {
        memcpy(this, &source, sizeof(*this));
        rebase(source, definition_.id); rebase(source, definition_.name);
        rebase(source, definition_.description); rebase(source, definition_.floors);
        rebase(source, definition_.demonDefinition);
        for (size_t index = 0; index < MAX_ARCHITECTURE_FLOORS; ++index) {
            FloorDefinition& floor = floors_[index];
            rebase(source, floor.contentId); rebase(source, floor.fileName); rebase(source, floor.fileType);
            rebase(source, floor.description); rebase(source, floor.controlName); rebase(source, floor.controlDescription);
            rebase(source, floor.enemyNetrunner); rebase(source, floor.blackIceDefinition);
            for (size_t ice = 0; ice < MAX_BLACK_ICE_PER_FLOOR; ++ice) rebase(source, floor.blackIceDefinitions[ice]);
        }
        for (size_t index = 0; index < MAX_SCENARIO_ENEMIES; ++index) {
            rebase(source, enemyDefinitions_[index].id); rebase(source, enemyDefinitions_[index].name);
        }
        for (size_t index = 0; index < MAX_LOCAL_BLACK_ICE; ++index) {
            rebase(source, localBlackIce_[index].definition.stableId);
            rebase(source, localBlackIce_[index].definition.displayName);
        }
        for (size_t index = 0; index < MAX_LOCAL_DEMONS; ++index) {
            rebase(source, localDemons_[index].definition.stableId);
            rebase(source, localDemons_[index].definition.displayName);
        }
    }

    void reset()
    {
        memset(id_, 0, sizeof(id_)); memset(name_, 0, sizeof(name_));
        memset(description_, 0, sizeof(description_)); memset(floors_, 0, sizeof(floors_));
        memset(strings_, 0, sizeof(strings_));
        memset(enemyDefinitions_, 0, sizeof(enemyDefinitions_));
        memset(enemyStrings_, 0, sizeof(enemyStrings_));
        memset(localBlackIce_, 0, sizeof(localBlackIce_));
        memset(localDemons_, 0, sizeof(localDemons_));
        localBlackIceCount_ = 0;
        enemyDefinitionCount_ = 0;
        definition_.id = id_; definition_.name = name_; definition_.description = nullptr;
        definition_.floors = floors_; definition_.floorCount = 0;
        definition_.demon = DemonType::None; definition_.demonDefinition = nullptr;
        schemaVersion_ = 0;
    }

    ArchitectureDefinition& mutableDefinition() { return definition_; }
    const ArchitectureDefinition& definition() const { return definition_; }
    FloorDefinition* floors() { return floors_; }
    char* idBuffer() { return id_; }
    char* nameBuffer() { return name_; }
    char* descriptionBuffer() { return description_; }
    void setSchemaVersion(uint8_t version) { schemaVersion_ = version; }
    uint8_t schemaVersion() const { return schemaVersion_; }

    struct FloorStrings
    {
        char contentId[CONTENT_ID_LENGTH];
        char description[DESCRIPTION_LENGTH];
        char fileName[FILE_NAME_LENGTH];
        char fileType[FILE_TYPE_LENGTH];
        char controlName[CONTROL_NAME_LENGTH];
        char controlDescription[DESCRIPTION_LENGTH];
    };
    FloorStrings& strings(size_t index) { return strings_[index]; }
    EnemyNetrunnerDefinition* enemyDefinitions() { return enemyDefinitions_; }
    const EnemyNetrunnerDefinition* enemyDefinitions() const { return enemyDefinitions_; }
    size_t enemyDefinitionCount() const { return enemyDefinitionCount_; }
    void setEnemyDefinitionCount(size_t count) { enemyDefinitionCount_ = count; }

    struct EnemyStrings
    {
        char id[ENEMY_ID_LENGTH];
        char handle[ENEMY_HANDLE_LENGTH];
    };
    EnemyStrings& enemyStrings(size_t index) { return enemyStrings_[index]; }
    BlackIceStoredDefinition* localBlackIce() { return localBlackIce_; }
    const BlackIceStoredDefinition* localBlackIce() const { return localBlackIce_; }
    size_t localBlackIceCount() const { return localBlackIceCount_; }
    void setLocalBlackIceCount(size_t count) { localBlackIceCount_ = count; }
    DemonStoredDefinition* localDemons() { return localDemons_; }
    const DemonStoredDefinition* localDemons() const { return localDemons_; }
    size_t localDemonCount() const { return localDemonCount_; }
    void setLocalDemonCount(size_t count) { localDemonCount_ = count; }
    const DemonDefinition* findLocalDemon(const char* id) const
    {
        if (id == nullptr) return nullptr;
        for (size_t index = 0; index < localDemonCount_; ++index)
            if (!strcmp(localDemons_[index].stableId, id)) return &localDemons_[index].definition;
        return nullptr;
    }
    const BlackIceDefinition* findLocalBlackIce(const char* id) const
    {
        if (id == nullptr) return nullptr;
        for (size_t index = 0; index < localBlackIceCount_; ++index)
            if (!strcmp(localBlackIce_[index].id, id)) return &localBlackIce_[index].definition;
        return nullptr;
    }

private:
    ArchitectureDefinition definition_;
    FloorDefinition floors_[MAX_ARCHITECTURE_FLOORS];
    FloorStrings strings_[MAX_ARCHITECTURE_FLOORS];
    char id_[ID_LENGTH];
    char name_[NAME_LENGTH];
    char description_[DESCRIPTION_LENGTH];
    EnemyNetrunnerDefinition enemyDefinitions_[MAX_SCENARIO_ENEMIES];
    EnemyStrings enemyStrings_[MAX_SCENARIO_ENEMIES];
    BlackIceStoredDefinition localBlackIce_[MAX_LOCAL_BLACK_ICE];
    DemonStoredDefinition localDemons_[MAX_LOCAL_DEMONS];
    size_t localBlackIceCount_ = 0;
    size_t localDemonCount_ = 0;
    size_t enemyDefinitionCount_ = 0;
    uint8_t schemaVersion_ = 0;

    template <typename T>
    void rebase(const LoadedScenario& source, const T*& pointer)
    {
        if (pointer == nullptr) return;
        const uintptr_t sourceStart = reinterpret_cast<uintptr_t>(&source);
        const uintptr_t sourceEnd = sourceStart + sizeof(source);
        const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
        if (address >= sourceStart && address < sourceEnd)
            pointer = reinterpret_cast<const T*>(reinterpret_cast<uintptr_t>(this) + (address - sourceStart));
    }
};
