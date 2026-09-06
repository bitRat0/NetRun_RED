#pragma once

#include <stdint.h>
#include <stddef.h>
#include "VisualIds.h"
#include "NetTypes.h"

enum class BlackIceClass : uint8_t
{
    AntiPersonnel,
    AntiProgram
};

enum class BlackIceEffectType : uint8_t
{
    DirectDamage,
    DerezzRandomDefenderAndDamage,
    DestroyRandomInstalledProgram,
    DamageAndNavigationLock,
    DamageAndNextTurnNetActionPenalty,
    ProgramDamageDestroyAtZero,
    ReduceRunnerMove,
    SlidePenaltyWhileActive,
    ReduceRunnerStats
    , DamageAndUnsafeJackOut
    , DamageOnly
    , ApplyFire
    , DamageAndReduceRunnerMove
    , ReduceRunnerStatsByAmount
};

enum class TemporaryRunEffect : uint8_t
{
    None,
    NavigationAndSafeJackOutLock,
    NextTurnNetActionPenalty,
    SlidePenaltyWhileActive
};

struct BlackIceEffect
{
    BlackIceEffectType type;
    uint8_t damageDice;
    uint8_t netActionPenalty;
    uint8_t minimumNetActions;
    uint8_t statusDice;
    uint8_t slidePenalty;
};

struct BlackIceDefinition
{
    const char* stableId;
    const char* displayName;
    int maxRez;
    BlackIceType type;
    BlackIceClass category;
    uint8_t perception;
    uint8_t speed;
    uint8_t attack;
    uint8_t defense;
    BlackIceEffect effect;
    // Compact, static UI copy shared by the deck editor and runtime menu.
    const char* shortRole;
    const char* shortEffect1;
    const char* shortEffect2;
    IceVisualId visualId;
    HostileAttackStyle animationStyle;
    // Install eligibility is content capability, not a built-in enum whitelist.
    bool playerUsable;
};

class BlackIceInstance
{
public:
    BlackIceInstance() = default;
    explicit BlackIceInstance(const BlackIceDefinition& definition);

    const BlackIceDefinition* definition() const { return definition_; }
    int currentRez() const { return currentRez_; }
    int maxRez() const { return definition_ != nullptr ? definition_->maxRez : 0; }
    bool active() const { return active_; }
    bool pursuing() const { return pursuing_; }
    bool encounteredThisRun() const { return encounteredThisRun_; }
    bool valid() const { return definition_ != nullptr; }

    void takeRezDamage(int damage);
    void stopPursuing() { pursuing_ = false; }
    void setPursuing(bool pursuing) { pursuing_ = active_ && pursuing; }
    void setRezzed(bool rezzed) { active_ = rezzed && currentRez_ > 0; if (!active_) pursuing_ = false; }
    void markEncountered() { encounteredThisRun_ = true; }

private:
    const BlackIceDefinition* definition_ = nullptr;
    int currentRez_ = 0;
    bool active_ = false;
    bool pursuing_ = false;
    bool encounteredThisRun_ = false;
};

const BlackIceDefinition& ice01Definition();
const BlackIceDefinition& ice02Definition();
const BlackIceDefinition& ice03Definition();
const BlackIceDefinition& ice04Definition();
const BlackIceDefinition& ice05Definition();
const BlackIceDefinition& ice06Definition();
const BlackIceDefinition& ice07Definition();
const BlackIceDefinition& ice08Definition();
const BlackIceDefinition& ice09Definition();
const BlackIceDefinition& ice10Definition();
const BlackIceDefinition& ice11Definition();
const BlackIceDefinition& ice12Definition();
const BlackIceDefinition* blackIceDefinition(BlackIceType type);
const BlackIceDefinition* blackIceDefinitionByStableId(const char* stableId);
size_t blackIceDefinitionCount();
const BlackIceDefinition* blackIceDefinitionAt(size_t index);
IceVisualId iceVisualId(BlackIceType type);
bool playerBlackIceSupported(BlackIceType type);
bool playerBlackIceSupported(const BlackIceDefinition* definition);
