#include "BlackIce.h"

#include <string.h>

namespace
{
constexpr BlackIceDefinition DEFINITIONS[] = {
    {"ice_01", "TRACEJACKAL", 18, BlackIceType::Ice01, BlackIceClass::AntiPersonnel,
     5, 7, 5, 3, {BlackIceEffectType::DamageOnly, 2, 0, 0}, "BLACK ICE", "ANTI-PERSON", "2D6 BRAIN", IceVisualId::Hound01, HostileAttackStyle::Lunge, true},
    {"ice_02", "CARRIONBYTE", 16, BlackIceType::Ice02, BlackIceClass::AntiPersonnel,
     7, 5, 4, 3, {BlackIceEffectType::DestroyRandomInstalledProgram, 0, 0, 0}, "BLACK ICE", "ANTI-PROG", "DESTROY PROG", IceVisualId::Bird01, HostileAttackStyle::Pulse, true},
    {"ice_03", "NEEDLECOIL", 16, BlackIceType::Ice03, BlackIceClass::AntiPersonnel,
     5, 7, 3, 3, {BlackIceEffectType::DerezzRandomDefenderAndDamage, 1, 0, 0}, "BLACK ICE", "ANTI-PERSON", "DEREZ + 1D6", IceVisualId::Serpent01, HostileAttackStyle::Slash, true},
    {"ice_04", "DEADLOCK", 26, BlackIceType::Ice04, BlackIceClass::AntiPersonnel,
     5, 3, 7, 5, {BlackIceEffectType::DamageAndNavigationLock, 2, 0, 0}, "BLACK ICE", "ANTI-PERSON", "2D6 / DEPTH LOCK", IceVisualId::Octopus01, HostileAttackStyle::Lunge, true},
    {"ice_05", "GHOSTPULSE", 14, BlackIceType::Ice05, BlackIceClass::AntiPersonnel,
     5, 5, 4, 3, {BlackIceEffectType::DamageAndNextTurnNetActionPenalty, 1, 1, 2}, "BLACK ICE", "ANTI-PERSON", "1D6 BRAIN -1 ACT", IceVisualId::Wraith01, HostileAttackStyle::Burst, true},
    {"ice_06", "PACKETSAW", 19, BlackIceType::Ice06, BlackIceClass::AntiProgram,
     5, 7, 5, 3, {BlackIceEffectType::ProgramDamageDestroyAtZero, 3, 0, 0}, "BLACK ICE", "ANTI-PROG", "3D6 PROG DMG", IceVisualId::Hunter01, HostileAttackStyle::Slash, true},
    {"ice_07", "STINGWIRE", 14, BlackIceType::Ice07, BlackIceClass::AntiPersonnel,
     3, 7, 3, 3, {BlackIceEffectType::ApplyFire, 0, 0, 0, 0, 0}, "BLACK ICE", "ANTI-PERSON", "APPLY FIRE", IceVisualId::Scorp01, HostileAttackStyle::Burst, true},
    {"ice_08", "GUTTERMESH", 12, BlackIceType::Ice08, BlackIceClass::AntiPersonnel,
     3, 5, 5, 3, {BlackIceEffectType::DamageAndReduceRunnerMove, 1, 0, 0, 1, 0}, "BLACK ICE", "ANTI-PERSON", "1D6 + MOVE -1", IceVisualId::Rat01, HostileAttackStyle::Pulse, true},
    {"ice_09", "STORMRAZOR", 27, BlackIceType::Ice09, BlackIceClass::AntiProgram,
     7, 5, 6, 5, {BlackIceEffectType::ProgramDamageDestroyAtZero, 5, 0, 0, 0, 0}, "BLACK ICE", "ANTI-PROG", "5D6 PROG DMG", IceVisualId::Winged01, HostileAttackStyle::Slash, true},
    {"ice_10", "GLASSCAT", 23, BlackIceType::Ice10, BlackIceClass::AntiProgram,
     8, 7, 5, 3, {BlackIceEffectType::ProgramDamageDestroyAtZero, 4, 0, 0, 0, 0}, "BLACK ICE", "ANTI-PROG", "4D6 PROG DMG", IceVisualId::Feline01, HostileAttackStyle::Lunge, true},
    {"ice_11", "GREYMARK", 22, BlackIceType::Ice11, BlackIceClass::AntiPersonnel,
     7, 3, 5, 3, {BlackIceEffectType::ReduceRunnerStatsByAmount, 0, 0, 0, 1, 0}, "BLACK ICE", "ANTI-PERSON", "STAT -1", IceVisualId::Skull01, HostileAttackStyle::Pulse, true},
    {"ice_12", "BREACHER", 24, BlackIceType::Ice12, BlackIceClass::AntiPersonnel,
     3, 3, 7, 5, {BlackIceEffectType::DamageAndUnsafeJackOut, 2, 0, 0, 0, 0}, "BLACK ICE", "ANTI-PERSON", "2D6 / FORCE JACK", IceVisualId::BigGuy01, HostileAttackStyle::Lunge, true}
};

static_assert(sizeof(DEFINITIONS) / sizeof(DEFINITIONS[0]) ==
    static_cast<size_t>(BlackIceType::Count), "Black ICE definition table incomplete");
}

BlackIceInstance::BlackIceInstance(const BlackIceDefinition& definition)
    : definition_(&definition), currentRez_(definition.maxRez),
      active_(true), pursuing_(true)
{
}

void BlackIceInstance::takeRezDamage(int damage)
{
    if (!active_ || damage <= 0)
    {
        return;
    }

    currentRez_ -= damage;
    if (currentRez_ <= 0)
    {
        currentRez_ = 0;
        active_ = false;
        pursuing_ = false;
    }
}

const BlackIceDefinition& ice01Definition()
{
    return DEFINITIONS[static_cast<size_t>(BlackIceType::Ice01)];
}

const BlackIceDefinition& ice02Definition()
{
    return DEFINITIONS[static_cast<size_t>(BlackIceType::Ice02)];
}

const BlackIceDefinition& ice03Definition()
{
    return DEFINITIONS[static_cast<size_t>(BlackIceType::Ice03)];
}

const BlackIceDefinition& ice04Definition()
{
    return DEFINITIONS[static_cast<size_t>(BlackIceType::Ice04)];
}

const BlackIceDefinition& ice05Definition()
{
    return DEFINITIONS[static_cast<size_t>(BlackIceType::Ice05)];
}

const BlackIceDefinition& ice06Definition()
{
    return DEFINITIONS[static_cast<size_t>(BlackIceType::Ice06)];
}

const BlackIceDefinition& ice07Definition()
{
    return DEFINITIONS[static_cast<size_t>(BlackIceType::Ice07)];
}

const BlackIceDefinition& ice08Definition()
{
    return DEFINITIONS[static_cast<size_t>(BlackIceType::Ice08)];
}

const BlackIceDefinition& ice09Definition()
{
    return DEFINITIONS[static_cast<size_t>(BlackIceType::Ice09)];
}

const BlackIceDefinition& ice10Definition()
{
    return DEFINITIONS[static_cast<size_t>(BlackIceType::Ice10)];
}

const BlackIceDefinition& ice11Definition()
{
    return DEFINITIONS[static_cast<size_t>(BlackIceType::Ice11)];
}

const BlackIceDefinition& ice12Definition()
{
    return DEFINITIONS[static_cast<size_t>(BlackIceType::Ice12)];
}

const BlackIceDefinition* blackIceDefinition(BlackIceType type)
{
    const size_t index = static_cast<size_t>(type);
    return index < static_cast<size_t>(BlackIceType::Count) ? &DEFINITIONS[index] : nullptr;
}

const BlackIceDefinition* blackIceDefinitionByStableId(const char* stableId)
{
    if (stableId == nullptr || stableId[0] == '\0') return nullptr;
    for (const BlackIceDefinition& definition : DEFINITIONS)
        if (strcmp(definition.stableId, stableId) == 0) return &definition;
    return nullptr;
}

size_t blackIceDefinitionCount()
{
    return sizeof(DEFINITIONS) / sizeof(DEFINITIONS[0]);
}

const BlackIceDefinition* blackIceDefinitionAt(size_t index)
{
    return index < blackIceDefinitionCount() ? &DEFINITIONS[index] : nullptr;
}

IceVisualId iceVisualId(BlackIceType type)
{
    const BlackIceDefinition* definition = blackIceDefinition(type);
    return definition != nullptr ? definition->visualId : IceVisualId::Hound01;
}

bool playerBlackIceSupported(BlackIceType type)
{
    return playerBlackIceSupported(blackIceDefinition(type));
}

bool playerBlackIceSupported(const BlackIceDefinition* definition)
{
    return definition != nullptr && definition->playerUsable;
}
