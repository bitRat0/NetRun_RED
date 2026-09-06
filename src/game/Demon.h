#pragma once

#include "NetTypes.h"
#include "VisualIds.h"

struct DemonDefinition
{
    const char* stableId;
    const char* displayName;
    DemonType type;
    DemonVisualId visualId;
    HostileAttackStyle animationStyle;
    int maxRez;
    uint8_t interfaceRank;
    uint8_t netActions;
    uint8_t combatNumber;
};

struct DemonInstance
{
    static constexpr size_t MAX_STABLE_ID_LENGTH = 31;
    static constexpr size_t MAX_DISPLAY_NAME_LENGTH = 31;
    static constexpr size_t MAX_ACTIONS = 5;

    // The runtime owns the definition metadata. This keeps a later
    // scenario-owned definition safe even when its source was parser-local.
    const DemonDefinition* definition = nullptr;
    int currentRez = 0;
    bool active = false;

    DemonInstance() = default;
    DemonInstance(const DemonInstance& source) { copyFrom(source); }
    DemonInstance& operator=(const DemonInstance& source)
    { if (this != &source) copyFrom(source); return *this; }

    void reset(const DemonDefinition* source)
    {
        if (source == nullptr) { definition = nullptr; currentRez = 0; active = false; return; }
        definitionStorage_ = *source;
        copyText(stableId_, sizeof(stableId_), source->stableId);
        copyText(displayName_, sizeof(displayName_), source->displayName);
        definitionStorage_.stableId = stableId_;
        definitionStorage_.displayName = displayName_;
        definition = &definitionStorage_;
        currentRez = definition->maxRez;
        active = true;
    }

    void reset(const DemonDefinition& source) { reset(&source); }
    int maxRez() const { return definition != nullptr ? definition->maxRez : 0; }
    void takeRezDamage(int damage) { if (active && damage > 0) { currentRez -= damage; if (currentRez <= 0) { currentRez = 0; active = false; } } }

private:
    DemonDefinition definitionStorage_ = {};
    char stableId_[MAX_STABLE_ID_LENGTH + 1] = {};
    char displayName_[MAX_DISPLAY_NAME_LENGTH + 1] = {};

    static void copyText(char* destination, size_t capacity, const char* source)
    {
        if (capacity == 0) return;
        if (source == nullptr) source = "";
        size_t index = 0;
        for (; index + 1 < capacity && source[index] != '\0'; ++index)
            destination[index] = source[index];
        destination[index] = '\0';
    }

    void copyFrom(const DemonInstance& source)
    {
        if (source.definition == nullptr) { reset(static_cast<const DemonDefinition*>(nullptr)); return; }
        reset(*source.definition);
        currentRez = source.currentRez;
        active = source.active;
    }
};

struct DemonStoredDefinition
{
    char stableId[DemonInstance::MAX_STABLE_ID_LENGTH + 1] = {};
    char displayName[DemonInstance::MAX_DISPLAY_NAME_LENGTH + 1] = {};
    DemonDefinition definition = {};
};

const DemonDefinition* demonDefinition(DemonType type);
const DemonDefinition* demonDefinitionByStableId(const char* stableId);
const char* demonVisualIdName(DemonVisualId id);
const char* demonVisualIdLabel(DemonVisualId id);
bool parseDemonVisualId(const char* value, DemonVisualId& result);
uint8_t demonRendererType(DemonVisualId id);
