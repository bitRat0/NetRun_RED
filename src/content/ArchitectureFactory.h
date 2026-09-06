#pragma once

#include "ArchitectureDefinition.h"
#include "game/Architecture.h"

class ArchitectureFactory
{
public:
    static Architecture create(const ArchitectureDefinition& definition);
    // In-place construction is for constrained diagnostic paths that cannot
    // afford a full Architecture return temporary on loopTask's stack.
    static bool createInto(const ArchitectureDefinition& definition, Architecture& output);
};
