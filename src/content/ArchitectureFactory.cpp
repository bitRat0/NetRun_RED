#include "ArchitectureFactory.h"

Architecture ArchitectureFactory::create(const ArchitectureDefinition& definition)
{
    Architecture architecture;
    createInto(definition, architecture);
    return architecture;
}

bool ArchitectureFactory::createInto(const ArchitectureDefinition& definition, Architecture& architecture)
{
    architecture.initialize(definition.id, definition.name, definition.description);
    if (!definition.valid()) return false;

    for (size_t index = 0; index < definition.floorCount; ++index)
    {
        const FloorDefinition& source = definition.floors[index];
        Floor floor(source.id, source.type, source.difficulty, source.contentId);
        floor.securityTier = source.securityTier;
        floor.fileName = source.fileName;
        floor.fileType = source.fileType;
        floor.fileValue = source.fileValue;
        floor.blackIceType = source.blackIceType;
        floor.blackIceCount = source.blackIceCount;
        floor.enemyNetrunnerDefinition = source.enemyNetrunner;
        for (size_t iceIndex = 0; iceIndex < MAX_BLACK_ICE_PER_FLOOR; ++iceIndex)
        {
            floor.blackIceTypes[iceIndex] = source.blackIceTypes[iceIndex];
            floor.blackIceDefinitions[iceIndex] = source.blackIceDefinitions[iceIndex];
        }
        floor.blackIceDefinition = source.blackIceDefinition;
        if (floor.type == FloorType::BlackICE && floor.blackIceCount == 0)
        {
            floor.blackIceCount = 1;
            floor.blackIceTypes[0] = source.blackIceType;
        }
        floor.description = source.description;
        floor.controlName = source.controlName;
        floor.controlDescription = source.controlDescription;
        architecture.addFloor(floor);
    }
    bool explicitGraph = false;
    for (size_t index = 0; index < definition.floorCount; ++index)
        if (definition.floors[index].connectionCount > 0) { explicitGraph = true; break; }
    if (!explicitGraph)
    {
        for (size_t index = 1; index < architecture.floorCount(); ++index)
        {
            if (!architecture.addConnection(static_cast<uint8_t>(index - 1), static_cast<uint8_t>(index)) ||
                !architecture.addConnection(static_cast<uint8_t>(index), static_cast<uint8_t>(index - 1))) return false;
        }
        return true;
    }
    for (size_t index = 0; index < definition.floorCount; ++index)
        for (size_t edge = 0; edge < definition.floors[index].connectionCount; ++edge)
            for (size_t target = 0; target < definition.floorCount; ++target)
                if (definition.floors[target].id == definition.floors[index].connections[edge])
                {
                    if (!architecture.addConnection(static_cast<uint8_t>(index), static_cast<uint8_t>(target)) ||
                        !architecture.addConnection(static_cast<uint8_t>(target), static_cast<uint8_t>(index))) return false;
                    break;
                }
    return true;
}
