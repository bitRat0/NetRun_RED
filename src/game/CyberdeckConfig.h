#pragma once

#include <stddef.h>

#include "NetTypes.h"

constexpr size_t MAX_CYBERDECK_PROGRAMS = 9;
constexpr size_t MAX_CYBERDECK_HARDWARE = 9;
constexpr size_t MAX_PLAYER_BLACK_ICE = 4;

struct CyberdeckConfig
{
    CyberdeckQuality quality = CyberdeckQuality::Poor;
    ProgramId programs[MAX_CYBERDECK_PROGRAMS] = {ProgramId::Sword, ProgramId::Banhammer,
        ProgramId::Armor, ProgramId::Worm, ProgramId::SpeedyGonzalvez};
    uint8_t programCount = 5;
    HardwareId hardware[MAX_CYBERDECK_HARDWARE] = {};
    uint8_t hardwareCount = 0;
    BlackIceType playerBlackIce[MAX_PLAYER_BLACK_ICE] = {};
    uint8_t playerBlackIceCount = 0;
};

uint8_t cyberdeckSlotCapacity(CyberdeckQuality quality);
uint8_t configuredProgramSlotCost(ProgramId id);
uint8_t configuredHardwareSlotCost(HardwareId id);
uint8_t configuredPlayerBlackIceSlotCost(BlackIceType type);
uint8_t cyberdeckUsedSlots(const CyberdeckConfig& config);
bool cyberdeckConfigValid(const CyberdeckConfig& config);
CyberdeckConfig defaultCyberdeckConfig();
