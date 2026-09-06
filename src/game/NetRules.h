#pragma once

#include "Dice.h"
#include "Netrunner.h"
#include "NetTypes.h"
#include "BlackIce.h"
#include "Demon.h"
#include "Cyberdeck.h"

struct NetCheckResult
{
    int base = 0;
    int programModifier = 0;
    int hardwareModifier = 0;
    int roll = 0;
    int total = 0;
    int target = 0;
    bool success = false;
    bool attempted = false;
};

struct CombatResult
{
    bool executed = false;
    int attackerRoll = 0;
    int attackerTotal = 0;
    int defenderRoll = 0;
    int defenderTotal = 0;
    bool success = false;
    int damage = 0;
    int rawDamage = 0;
    int damageReduction = 0;
    int targetRemainingRez = 0;
    int targetRemainingHp = 0;
    int targetInitialRez = 0;
    uint16_t attackerRuntimeId = 0;
    uint8_t intBefore = 0;
    uint8_t intAfter = 0;
    uint8_t refBefore = 0;
    uint8_t refAfter = 0;
    uint8_t dexBefore = 0;
    uint8_t dexAfter = 0;
    uint8_t intReduction = 0;
    uint8_t refReduction = 0;
    uint8_t dexReduction = 0;
    uint8_t moveBefore = 0;
    uint8_t moveAfter = 0;
    int8_t slideModifier = 0;
    const BlackIceDefinition* attackerDefinition = nullptr;
    bool programEffectApplied = false;
    ProgramStatus resultingProgramStatus = ProgramStatus::Inactive;
    TemporaryRunEffect temporaryRunEffect = TemporaryRunEffect::None;
    bool temporaryRunEffectApplied = false;
    // Explicit presentation/test flags for Kraken's two simultaneous locks.
    // The underlying gameplay state remains owned by GameState.
    bool depthLockApplied = false;
    bool safeJackOutLockApplied = false;
    bool forcedUnsafeJackOut = false;
    bool runTerminated = false;
    int8_t affectedProgramSlot = -1;
    char affectedProgramName[16] = {};
    enum class TargetType : uint8_t { Netrunner, Program } targetType = TargetType::Netrunner;
    bool targetDestroyed = false;
    bool programSavedByBackup = false;
    bool noValidTarget = false;
    bool noProgramTarget = false;
    ProgramId attackerProgram = ProgramId::None;
    bool fireApplied = false;
    bool fireBlocked = false;
    bool krashBarrierBlocked = false;
    bool shieldBlocked = false;
    uint8_t superglueRounds = 0;
    uint8_t nextTurnNetActionPenalty = 0;
};

struct ProgramActionResult
{
    bool executed = false;
    ProgramStatus resultingStatus = ProgramStatus::Inactive;
    uint8_t remainingNetActions = 0;
};

struct OpposedCheckResult
{
    int runnerBase = 0;
    int runnerProgramModifier = 0;
    int runnerHardwareModifier = 0;
    int iceBase = 0;
    int runnerRoll = 0;
    int runnerTotal = 0;
    int iceRoll = 0;
    int iceTotal = 0;
    bool runnerWins = false;
    bool iceWins = false;
    bool tie = false;
};

class NetRules
{
public:
    explicit NetRules(Dice& dice) : dice_(dice) {}

    NetCheckResult backdoor(Netrunner& runner, Floor& floor);
    NetCheckResult backdoor(Netrunner& runner, Cyberdeck& deck, Floor& floor);
    NetCheckResult eyeDee(Netrunner& runner, Floor& floor);
    NetCheckResult control(Netrunner& runner, Floor& floor);
    NetCheckResult pathfinder(Netrunner& runner, const Cyberdeck& deck);
    NetCheckResult cloak(Netrunner& runner, const Cyberdeck& deck);
    static int activeBoosterBonus(const Cyberdeck& deck, ProgramId id);
    CombatResult swordAttack(Netrunner& runner, Cyberdeck& deck, BlackIceInstance& target);
    CombatResult banhammerAttack(Netrunner& runner, Cyberdeck& deck, BlackIceInstance& target);
    CombatResult zap(Netrunner& runner, BlackIceInstance& target);
    CombatResult zap(Netrunner& runner, Netrunner& target);
    CombatResult zapDemon(Netrunner& runner, DemonInstance& target);
    CombatResult swordAttackDemon(Netrunner& runner, Cyberdeck& deck, DemonInstance& target);
    CombatResult demonZap(DemonInstance& attacker, Netrunner& target, Cyberdeck& targetDeck);
    CombatResult slide(Netrunner& runner, BlackIceInstance& target, int slideModifier = 0);
    CombatResult blackIceAttack(
        BlackIceInstance& attacker, Netrunner& target, Cyberdeck* targetDeck = nullptr);
    CombatResult resolveBlackIceEffect(BlackIceInstance& attacker, Netrunner& target,
                                       Cyberdeck* targetDeck = nullptr);
    CombatResult programAttackNetrunner(Netrunner& attacker, Cyberdeck& attackerDeck,
                                        Program& program, Netrunner& target, Cyberdeck& targetDeck);
    CombatResult swordAttackProgram(Netrunner& attacker, Cyberdeck& attackerDeck,
                                    Program& program, Program& target);
    ProgramActionResult activateProgram(Netrunner& runner, Program& program);
    ProgramActionResult activateProgram(Netrunner& runner, Cyberdeck& deck, Program& program);
    ProgramActionResult deactivateProgram(Netrunner& runner, Program& program);
    bool downloadFile(Netrunner& runner, Floor& floor);
    OpposedCheckResult encounterSpeedCheck(
        const Netrunner& runner, const BlackIceInstance& ice, int runnerSpeedBonus = 0);

private:
    enum class ProgramTargetFilter : uint8_t
    {
        RezzedDefender,
        InstalledNotDestroyed,
        RezzedProgram
    };
    static bool beatsDifficulty(int total, int target);
    NetCheckResult performFloorCheck(
        Netrunner& runner, Floor& floor, FloorType requiredType, int programModifier = 0,
        int hardwareModifier = 0);
    CombatResult programAttack(Netrunner& runner, Cyberdeck& deck, BlackIceInstance& target,
                               ProgramId id, uint8_t damageDice);
    static bool hasRezzedCopy(const Cyberdeck& deck, const Program& program);
    static bool programEligible(const Program& program, ProgramTargetFilter filter);
    int selectRandomProgram(Cyberdeck& deck, ProgramTargetFilter filter);
    void applyBlackIceEffect(const BlackIceDefinition& definition, Netrunner& target,
                             Cyberdeck* targetDeck, CombatResult& result);
    void applyProgramDamage(Cyberdeck& deck, size_t slot, int damage, CombatResult& result);
    void applyNonBlackIceDamage(Netrunner& target, Cyberdeck& targetDeck, int rawDamage,
                                CombatResult& result);

    Dice& dice_;
};
