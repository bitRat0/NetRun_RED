#pragma once

#include "NetTypes.h"

struct HardwareDefinition
{
    HardwareId id;
    const char* name;
    uint8_t slotCost;
    bool currentlyUnsupported;
};

const HardwareDefinition* hardwareDefinition(HardwareId id);
