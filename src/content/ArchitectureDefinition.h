#pragma once

#include <stddef.h>
#include <stdint.h>

#include "game/NetTypes.h"

struct EnemyNetrunnerDefinition;
struct BlackIceDefinition;
struct DemonDefinition;

struct FloorDefinition
{
    uint8_t id;
    FloorType type;
    uint8_t difficulty;
    SecurityTier securityTier;
    const char* contentId;
    const char* fileName;
    const char* fileType;
    uint32_t fileValue;
    BlackIceType blackIceType;
    const char* description;
    const char* controlName;
    const char* controlDescription;
    BlackIceType blackIceTypes[MAX_BLACK_ICE_PER_FLOOR];
    uint8_t blackIceCount;
    const EnemyNetrunnerDefinition* enemyNetrunner;
    // Optional Schema 1 graph links, expressed as stable FloorDefinition IDs. If
    // every floor leaves this empty, the Factory creates the legacy line.
    uint8_t connections[MAX_CONNECTIONS_PER_FLOOR];
    uint8_t connectionCount;
    // Schema 1 content IDs resolve to definitions retained by LoadedScenario.
    // Legacy definitions continue to use the enum fields above.
    const BlackIceDefinition* blackIceDefinition;
    const BlackIceDefinition* blackIceDefinitions[MAX_BLACK_ICE_PER_FLOOR];
};

struct ArchitectureDefinition
{
    constexpr ArchitectureDefinition(const char* architectureId = nullptr, const char* architectureName = nullptr,
                                     const char* architectureDescription = nullptr,
                                     const FloorDefinition* architectureFloors = nullptr,
                                     size_t architectureFloorCount = 0,
                                     DemonType architectureDemon = DemonType::None,
                                     const DemonDefinition* customDemon = nullptr)
        : id(architectureId), name(architectureName), description(architectureDescription),
          floors(architectureFloors), floorCount(architectureFloorCount), demon(architectureDemon),
          demonDefinition(customDemon) {}
    const char* id;
    const char* name;
    const char* description;
    const FloorDefinition* floors;
    size_t floorCount;
    DemonType demon;
    const DemonDefinition* demonDefinition;

    bool valid() const
    {
        return id != nullptr && name != nullptr && floors != nullptr && floorCount > 0 &&
               floorCount <= MAX_ARCHITECTURE_FLOORS && graphDataValid();
    }

private:
    bool graphDataValid() const
    {
        bool explicitGraph = false;
        for (size_t index = 0; index < floorCount; ++index)
        {
            const FloorDefinition& floor = floors[index];
            if (floor.connectionCount > MAX_CONNECTIONS_PER_FLOOR) return false;
            if (floor.connectionCount > 0) explicitGraph = true;
            for (size_t other = index + 1; other < floorCount; ++other)
                if (floor.id == floors[other].id) return false;
        }
        if (!explicitGraph) return true;
        for (size_t index = 0; index < floorCount; ++index)
        {
            const FloorDefinition& floor = floors[index];
            for (size_t connection = 0; connection < floor.connectionCount; ++connection)
            {
                const uint8_t targetId = floor.connections[connection];
                if (targetId == floor.id) return false;
                bool found = false;
                for (size_t candidate = 0; candidate < floorCount; ++candidate)
                    if (floors[candidate].id == targetId) { found = true; break; }
                if (!found) return false;
                for (size_t previous = 0; previous < connection; ++previous)
                    if (floor.connections[previous] == targetId) return false;
            }
        }
        // The factory normalizes every declared edge to both directions.
        // Validate the resulting undirected degree before construction so an
        // incoming fan-in cannot overflow Architecture's fixed adjacency.
        uint8_t normalizedDegrees[MAX_ARCHITECTURE_FLOORS] = {};
        for (size_t source = 0; source < floorCount; ++source)
        {
            for (size_t target = source + 1; target < floorCount; ++target)
            {
                bool related = false;
                for (size_t edge = 0; edge < floors[source].connectionCount; ++edge)
                    if (floors[source].connections[edge] == floors[target].id) { related = true; break; }
                if (!related)
                    for (size_t edge = 0; edge < floors[target].connectionCount; ++edge)
                        if (floors[target].connections[edge] == floors[source].id) { related = true; break; }
                if (!related) continue;
                if (++normalizedDegrees[source] > MAX_CONNECTIONS_PER_FLOOR ||
                    ++normalizedDegrees[target] > MAX_CONNECTIONS_PER_FLOOR) return false;
            }
        }
        return true;
    }
};
