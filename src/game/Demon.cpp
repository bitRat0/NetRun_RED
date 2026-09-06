#include "Demon.h"

#include <string.h>

namespace
{
constexpr DemonDefinition kDefinitions[] = {
    {"demon_01", "LATCH", DemonType::Demon01, DemonVisualId::Orb01, HostileAttackStyle::Pulse, 18, 3, 2, 12},
    {"demon_02", "WARDEN", DemonType::Demon02, DemonVisualId::Sentinel01, HostileAttackStyle::Pulse, 24, 5, 3, 13},
    {"demon_03", "CROWN", DemonType::Demon03, DemonVisualId::Crown01, HostileAttackStyle::Pulse, 28, 6, 4, 14}
};
}

const DemonDefinition* demonDefinition(DemonType type)
{
    const size_t index = static_cast<size_t>(type);
    return type != DemonType::None && index - 1 < sizeof(kDefinitions) / sizeof(kDefinitions[0])
        ? &kDefinitions[index - 1] : nullptr;
}

const DemonDefinition* demonDefinitionByStableId(const char* stableId)
{
    if (stableId == nullptr || stableId[0] == '\0') return nullptr;
    for (const DemonDefinition& definition : kDefinitions)
        if (strcmp(definition.stableId, stableId) == 0) return &definition;
    return nullptr;
}

const char* demonVisualIdName(DemonVisualId id)
{
    static constexpr const char* names[] = {"orb", "sentinel", "crown"};
    const uint8_t index = static_cast<uint8_t>(id);
    return index < static_cast<uint8_t>(DemonVisualId::Count) ? names[index] : nullptr;
}

const char* demonVisualIdLabel(DemonVisualId id)
{
    static constexpr const char* labels[] = {"Orb", "Sentinel", "Crown"};
    const uint8_t index = static_cast<uint8_t>(id);
    return index < static_cast<uint8_t>(DemonVisualId::Count) ? labels[index] : nullptr;
}

bool parseDemonVisualId(const char* value, DemonVisualId& result)
{
    if (value == nullptr) return false;
    for (uint8_t index = 0; index < static_cast<uint8_t>(DemonVisualId::Count); ++index)
        if (!strcmp(value, demonVisualIdName(static_cast<DemonVisualId>(index))))
        { result = static_cast<DemonVisualId>(index); return true; }
    return false;
}

uint8_t demonRendererType(DemonVisualId id)
{
    return isValidDemonVisualId(id) ? static_cast<uint8_t>(id) + 1 : 1;
}