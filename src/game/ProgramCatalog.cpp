#include "ProgramCatalog.h"

namespace
{
constexpr ProgramDefinition kPrograms[] = {
    {ProgramId::Sword, "Sword", ProgramType::Attacker, 1, 0, 5, 1, false, false, true, false, "ATK vs PROGRAM", "3d6 ICE", "2d6 PROG"},
    {ProgramId::Banhammer, "Banhammer", ProgramType::Attacker, 1, 0, 7, 1, false, false, true, false, "ATK vs PROGRAM", "2d6 ICE", "3d6 PROG"},
    {ProgramId::Armor, "Armor", ProgramType::Defender, 0, 0, 7, 1, true, true, false, false, "DEFENDER", "-4 BRAIN DMG", ""},
    {ProgramId::Flak, "Flak", ProgramType::Defender, 0, 0, 7, 1, true, true, false, false, "DEFENDER", "RETALIATE 2d6", "vs PROGRAM"},
    {ProgramId::Shield, "Shield", ProgramType::Defender, 0, 0, 7, 1, true, true, false, false, "DEFENDER", "BLOCK 1 HIT", "THEN DEREZ"},
    {ProgramId::Eraser, "Eraser", ProgramType::Booster, 0, 0, 7, 1, false, false, false, false, "BOOSTER", "+2 CLOAK", ""},
    {ProgramId::SeeYa, "See Ya", ProgramType::Booster, 0, 0, 7, 1, false, false, false, false, "BOOSTER", "+2 PATHFINDER", ""},
    {ProgramId::SpeedyGonzalvez, "Speedy Gonzalvez", ProgramType::Booster, 0, 0, 7, 1, false, false, false, false, "BOOSTER", "+2 SPEED", ""},
    {ProgramId::Worm, "Worm", ProgramType::Booster, 0, 0, 7, 1, false, false, false, false, "BOOSTER", "+2 BACKDOOR", ""},
    {ProgramId::DeckKRASH, "DeckKRASH", ProgramType::Attacker, 0, 0, 7, 1, false, false, false, false, "ATK vs PERSON", "FORCE JACK OUT", ""},
    {ProgramId::Hellbolt, "Hellbolt", ProgramType::Attacker, 2, 0, 7, 1, false, false, false, false, "ATK vs PERSON", "2d6 BRAIN", "FIRE"},
    {ProgramId::Nervescrub, "Nervescrub", ProgramType::Attacker, 0, 0, 7, 1, false, false, false, false, "ATK vs PERSON", "-INT/REF/DEX", ""},
    {ProgramId::PoisonFlatline, "Poison Flatline", ProgramType::Attacker, 0, 0, 7, 1, false, false, false, false, "ATK vs PERSON", "DESTROY PROG", ""},
    {ProgramId::Superglue, "Superglue", ProgramType::Attacker, 2, 0, 7, 1, true, false, false, false, "ATK vs PERSON", "BLOCK MOVE/JACK", ""},
    {ProgramId::Vrizzbolt, "Vrizzbolt", ProgramType::Attacker, 1, 0, 7, 1, false, false, false, false, "ATK vs PERSON", "1d6 BRAIN", "-1 NEXT ACT"},
};
}

const ProgramDefinition* programDefinition(ProgramId id)
{
    for (const ProgramDefinition& definition : kPrograms)
        if (definition.id == id) return &definition;
    return nullptr;
}

const char* programClassName(const ProgramDefinition& definition)
{
    switch (definition.type)
    {
        case ProgramType::Attacker: return "ATK";
        case ProgramType::Defender: return "DEF";
        case ProgramType::Booster: return "BOOST";
    }
    return "?";
}

Program makeProgram(ProgramId id)
{
    const ProgramDefinition* definition = programDefinition(id);
    return definition == nullptr ? Program() : Program(definition->id, definition->type,
        definition->name, definition->attack, definition->defense, definition->maxRez,
        definition->slotCost, definition->oncePerRun, definition->exclusive);
}
