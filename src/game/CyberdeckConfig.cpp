#include "CyberdeckConfig.h"

#include "ProgramCatalog.h"
#include "HardwareCatalog.h"
#include "BlackIce.h"

uint8_t cyberdeckSlotCapacity(CyberdeckQuality quality)
{
    switch (quality)
    {
        case CyberdeckQuality::Poor: return 5;
        case CyberdeckQuality::Standard: return 7;
        case CyberdeckQuality::Excellent: return 9;
    }
    return 0;
}

uint8_t configuredProgramSlotCost(ProgramId id)
{
    const ProgramDefinition* definition = programDefinition(id);
    return definition != nullptr ? definition->slotCost : 0;
}

uint8_t configuredHardwareSlotCost(HardwareId id)
{
    const HardwareDefinition* definition = hardwareDefinition(id);
    return definition != nullptr ? definition->slotCost : 0;
}

uint8_t configuredPlayerBlackIceSlotCost(BlackIceType type)
{
    return playerBlackIceSupported(type) ? 2 : 0;
}

uint8_t cyberdeckUsedSlots(const CyberdeckConfig& config)
{
    uint8_t used = 0;
    const uint8_t count = config.programCount > MAX_CYBERDECK_PROGRAMS
        ? MAX_CYBERDECK_PROGRAMS : config.programCount;
    for (uint8_t index = 0; index < count; ++index)
    {
        const uint8_t cost = configuredProgramSlotCost(config.programs[index]);
        if (cost == 0 || used > 255 - cost) return 255;
        used = static_cast<uint8_t>(used + cost);
    }
    const uint8_t hardwareCount = config.hardwareCount > MAX_CYBERDECK_HARDWARE
        ? MAX_CYBERDECK_HARDWARE : config.hardwareCount;
    for (uint8_t index = 0; index < hardwareCount; ++index)
    {
        const uint8_t cost = configuredHardwareSlotCost(config.hardware[index]);
        if (cost == 0 || used > 255 - cost) return 255;
        used = static_cast<uint8_t>(used + cost);
    }
    const uint8_t iceCount = config.playerBlackIceCount > MAX_PLAYER_BLACK_ICE
        ? MAX_PLAYER_BLACK_ICE : config.playerBlackIceCount;
    for (uint8_t index = 0; index < iceCount; ++index)
    {
        const uint8_t cost = configuredPlayerBlackIceSlotCost(config.playerBlackIce[index]);
        if (cost == 0 || used > 255 - cost) return 255;
        used = static_cast<uint8_t>(used + cost);
    }
    return used;
}

bool cyberdeckConfigValid(const CyberdeckConfig& config)
{
    if (config.programCount > MAX_CYBERDECK_PROGRAMS || config.hardwareCount > MAX_CYBERDECK_HARDWARE ||
        config.playerBlackIceCount > MAX_PLAYER_BLACK_ICE ||
        cyberdeckSlotCapacity(config.quality) == 0)
        return false;
    for (uint8_t index = 0; index < config.programCount; ++index)
        if (programDefinition(config.programs[index]) == nullptr) return false;
    for (uint8_t index = 0; index < config.hardwareCount; ++index)
        if (hardwareDefinition(config.hardware[index]) == nullptr) return false;
    for (uint8_t index = 0; index < config.playerBlackIceCount; ++index)
        if (!playerBlackIceSupported(config.playerBlackIce[index])) return false;
    return cyberdeckUsedSlots(config) <= cyberdeckSlotCapacity(config.quality);
}

CyberdeckConfig defaultCyberdeckConfig()
{
    return CyberdeckConfig();
}
