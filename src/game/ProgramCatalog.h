#pragma once

#include <stddef.h>

#include "Program.h"

struct ProgramDefinition
{
    ProgramId id;
    const char* name;
    ProgramType type;
    uint8_t attack;
    uint8_t defense;
    uint8_t maxRez;
    uint8_t slotCost;
    bool oncePerRun;
    bool exclusive;
    bool attacksBlackIce;
    bool currentlyUnsupported;
    // Short static UI metadata.  Keep rules text out of the renderer.
    const char* shortRole;
    const char* shortEffect1;
    const char* shortEffect2;
};

const ProgramDefinition* programDefinition(ProgramId id);
const char* programClassName(const ProgramDefinition& definition);
Program makeProgram(ProgramId id);
