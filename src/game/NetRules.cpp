#include "NetRules.h"

#include <string.h>

NetCheckResult NetRules::backdoor(Netrunner& runner, Floor& floor)
{
    return performFloorCheck(runner, floor, FloorType::Password);
}

NetCheckResult NetRules::backdoor(Netrunner& runner, Cyberdeck& deck, Floor& floor)
{
    // Worm is the only currently implemented Booster whose ability exists in
    // this rules slice. Its modifier stays in Rules, never in the UI.
    const int modifier = deck.findUsableProgram(ProgramId::Worm) != nullptr ? 2 : 0;
    return performFloorCheck(runner, floor, FloorType::Password, modifier);
}

NetCheckResult NetRules::eyeDee(Netrunner& runner, Floor& floor)
{
    NetCheckResult result = performFloorCheck(runner, floor, FloorType::File);
    if (result.success)
    {
        floor.identified = true;
    }
    return result;
}

NetCheckResult NetRules::control(Netrunner& runner, Floor& floor)
{
    NetCheckResult result = performFloorCheck(runner, floor, FloorType::ControlNode);
    if (result.success)
    {
        floor.controlled = true;
        floor.controlOwner = ControlOwner::Runner;
    }
    return result;
}

int NetRules::activeBoosterBonus(const Cyberdeck& deck, ProgramId id)
{
    int bonus = 0;
    for (size_t index = 0; index < deck.programCount(); ++index)
    {
        const Program* program = deck.programAt(index);
        if (program != nullptr && program->id() == id && program->usable()) bonus += 2;
    }
    return bonus;
}

NetCheckResult NetRules::pathfinder(Netrunner& runner, const Cyberdeck& deck)
{
    NetCheckResult result;
    result.base = runner.interfaceRank();
    result.programModifier = activeBoosterBonus(deck, ProgramId::SeeYa);
    if (runner.remainingNetActions() == 0) return result;
    result.attempted = runner.spendNetAction();
    if (!result.attempted) return result;
    result.roll = dice_.rollD10();
    result.total = result.base + result.programModifier + result.roll;
    result.success = true;
    return result;
}

NetCheckResult NetRules::cloak(Netrunner& runner, const Cyberdeck& deck)
{
    NetCheckResult result;
    result.base = runner.interfaceRank();
    result.programModifier = activeBoosterBonus(deck, ProgramId::Eraser);
    if (runner.remainingNetActions() == 0) return result;
    result.attempted = runner.spendNetAction();
    if (!result.attempted) return result;
    result.roll = dice_.rollD10();
    result.total = result.base + result.programModifier + result.roll;
    result.success = true;
    return result;
}

bool NetRules::beatsDifficulty(int total, int target)
{
    return total > target;
}

ProgramActionResult NetRules::activateProgram(Netrunner& runner, Program& program)
{
    ProgramActionResult result;
    result.resultingStatus = program.status();
    result.remainingNetActions = runner.remainingNetActions();
    if (runner.remainingNetActions() == 0 || program.status() != ProgramStatus::Inactive ||
        program.activatedThisRound() || (program.oncePerRun() && program.usedThisRun()))
        return result;

    runner.spendNetAction();
    program.restoreRez();
    program.setStatus(ProgramStatus::Rezzed);
    program.setActivatedThisRound(true);
    if (program.oncePerRun()) program.markUsedThisRun();
    result.executed = true;
    result.resultingStatus = program.status();
    result.remainingNetActions = runner.remainingNetActions();
    return result;
}

ProgramActionResult NetRules::activateProgram(Netrunner& runner, Cyberdeck& deck, Program& program)
{
    ProgramActionResult result;
    result.resultingStatus = program.status();
    result.remainingNetActions = runner.remainingNetActions();
    if (program.exclusive() && hasRezzedCopy(deck, program)) return result;
    return activateProgram(runner, program);
}

ProgramActionResult NetRules::deactivateProgram(Netrunner& runner, Program& program)
{
    ProgramActionResult result;
    result.resultingStatus = program.status();
    result.remainingNetActions = runner.remainingNetActions();
    const bool canDeactivate = program.status() == ProgramStatus::Rezzed ||
        program.status() == ProgramStatus::Derezzed;
    if (runner.remainingNetActions() == 0 || !canDeactivate)
        return result;

    runner.spendNetAction();
    program.setStatus(ProgramStatus::Inactive);
    result.executed = true;
    result.resultingStatus = program.status();
    result.remainingNetActions = runner.remainingNetActions();
    return result;
}

bool NetRules::downloadFile(Netrunner& runner, Floor& floor)
{
    if (runner.remainingNetActions() == 0 || floor.type != FloorType::File ||
        !floor.identified || floor.downloaded)
        return false;
    if (!runner.spendNetAction()) return false;
    floor.downloaded = true;
    return true;
}

OpposedCheckResult NetRules::encounterSpeedCheck(
    const Netrunner& runner, const BlackIceInstance& ice, int runnerSpeedBonus)
{
    OpposedCheckResult result;
    if (!ice.active()) return result;
    result.runnerBase = runner.interfaceRank();
    result.runnerProgramModifier = runnerSpeedBonus;
    result.iceBase = ice.definition()->speed;
    result.runnerRoll = dice_.rollD10();
    result.iceRoll = dice_.rollD10();
    result.runnerTotal = result.runnerBase + result.runnerProgramModifier +
        result.runnerHardwareModifier + result.runnerRoll;
    result.iceTotal = result.iceBase + result.iceRoll;
    result.runnerWins = result.runnerTotal > result.iceTotal;
    result.iceWins = result.iceTotal > result.runnerTotal;
    result.tie = result.runnerTotal == result.iceTotal;
    return result;
}

CombatResult NetRules::swordAttack(
    Netrunner& runner, Cyberdeck& deck, BlackIceInstance& target)
{
    return programAttack(runner, deck, target, ProgramId::Sword, 3);
}

CombatResult NetRules::banhammerAttack(
    Netrunner& runner, Cyberdeck& deck, BlackIceInstance& target)
{
    return programAttack(runner, deck, target, ProgramId::Banhammer, 2);
}

CombatResult NetRules::programAttack(Netrunner& runner, Cyberdeck& deck,
                                     BlackIceInstance& target, ProgramId id,
                                     uint8_t damageDice)
{
    CombatResult result;
    result.targetRemainingRez = target.currentRez();
    Program* program = deck.findUsableProgram(id);
    if (!target.active() || !target.pursuing() || program == nullptr ||
        runner.remainingNetActions() == 0)
        return result;

    runner.spendNetAction();
    result.executed = true;
    result.attackerRoll = dice_.rollD10();
    result.defenderRoll = dice_.rollD10();
    result.attackerTotal = runner.interfaceRank() + program->attack() + result.attackerRoll;
    result.defenderTotal = target.definition()->defense + result.defenderRoll;
    result.success = beatsDifficulty(result.attackerTotal, result.defenderTotal);
    if (result.success)
    {
        result.damage = dice_.rollD6(damageDice);
        target.takeRezDamage(result.damage);
    }
    result.targetRemainingRez = target.currentRez();
    return result;
}

CombatResult NetRules::swordAttackProgram(Netrunner& attacker, Cyberdeck& attackerDeck,
                                          Program& program, Program& target)
{
    CombatResult result;
    result.attackerProgram = program.id();
    result.targetType = CombatResult::TargetType::Program;
    result.targetInitialRez = target.rez();
    result.targetRemainingRez = target.rez();
    if ((program.id() != ProgramId::Sword && program.id() != ProgramId::Banhammer) ||
        !program.usable() || !target.usable() || attacker.remainingNetActions() == 0)
        return result;
    attacker.spendNetAction();
    result.executed = true;
    result.attackerRoll = dice_.rollD10();
    result.defenderRoll = dice_.rollD10();
    result.attackerTotal = attacker.interfaceRank() + program.attack() + result.attackerRoll;
    result.defenderTotal = target.defense() + result.defenderRoll;
    result.success = beatsDifficulty(result.attackerTotal, result.defenderTotal);
    if (result.success)
    {
        result.damage = dice_.rollD6(program.id() == ProgramId::Sword ? 2 : 3);
        target.takeRezDamage(result.damage, ProgramDamageMode::DerezzAtZero);
        result.resultingProgramStatus = target.status();
    }
    result.targetRemainingRez = target.rez();
    return result;
}

void NetRules::applyNonBlackIceDamage(Netrunner& target, Cyberdeck& targetDeck,
                                      int rawDamage, CombatResult& result)
{
    result.rawDamage = rawDamage;
    result.damage = rawDamage;
    Program* shield = targetDeck.findUsableProgram(ProgramId::Shield);
    if (shield != nullptr)
    {
        shield->setStatus(ProgramStatus::Derezzed);
        result.shieldBlocked = true;
        result.damage = 0;
        return;
    }
    if (targetDeck.findUsableProgram(ProgramId::Armor) != nullptr)
    {
        result.damageReduction = result.damage > 4 ? 4 : result.damage;
        result.damage -= result.damageReduction;
    }
    target.takeDamage(result.damage);
}

CombatResult NetRules::programAttackNetrunner(Netrunner& attacker, Cyberdeck& attackerDeck,
                                              Program& program, Netrunner& target,
                                              Cyberdeck& targetDeck)
{
    CombatResult result;
    result.attackerProgram = program.id();
    result.targetRemainingHp = target.hp();
    if (!program.usable() || attacker.remainingNetActions() == 0 ||
        program.type() != ProgramType::Attacker || program.id() == ProgramId::Sword ||
        program.id() == ProgramId::Banhammer)
        return result;
    attacker.spendNetAction();
    result.executed = true;
    result.attackerRoll = dice_.rollD10();
    result.defenderRoll = dice_.rollD10();
    result.attackerTotal = attacker.interfaceRank() + program.attack() + result.attackerRoll;
    result.defenderTotal = target.interfaceRank() + result.defenderRoll;
    result.success = beatsDifficulty(result.attackerTotal, result.defenderTotal);
    if (!result.success) return result;

    switch (program.id())
    {
        case ProgramId::DeckKRASH:
            result.krashBarrierBlocked = targetDeck.hasHardware(HardwareId::KrashBarrier);
            result.forcedUnsafeJackOut = !result.krashBarrierBlocked;
            break;
        case ProgramId::Hellbolt:
            applyNonBlackIceDamage(target, targetDeck, dice_.rollD6(2), result);
            if (!result.shieldBlocked)
            {
                result.fireBlocked = targetDeck.hasHardware(HardwareId::InsulatedWiring);
                result.fireApplied = !result.fireBlocked;
            }
            break;
        case ProgramId::Nervescrub:
            result.intReduction = static_cast<uint8_t>(dice_.rollD6(1));
            result.refReduction = static_cast<uint8_t>(dice_.rollD6(1));
            result.dexReduction = static_cast<uint8_t>(dice_.rollD6(1));
            target.reduceInt(result.intReduction);
            target.reduceRef(result.refReduction);
            target.reduceDex(result.dexReduction);
            break;
        case ProgramId::PoisonFlatline:
        {
            result.targetType = CombatResult::TargetType::Program;
            result.affectedProgramSlot = static_cast<int8_t>(selectRandomProgram(
                targetDeck, ProgramTargetFilter::InstalledNotDestroyed));
            if (result.affectedProgramSlot < 0) { result.noProgramTarget = true; break; }
            Program* victim = targetDeck.programAt(static_cast<size_t>(result.affectedProgramSlot));
            result.targetInitialRez = victim->rez();
            strncpy(result.affectedProgramName, victim->name(), sizeof(result.affectedProgramName) - 1);
            result.programSavedByBackup = targetDeck.destroyProgram(static_cast<size_t>(result.affectedProgramSlot));
            result.targetDestroyed = !result.programSavedByBackup;
            result.programEffectApplied = true;
            result.resultingProgramStatus = result.programSavedByBackup ? ProgramStatus::Inactive : ProgramStatus::Destroyed;
            break;
        }
        case ProgramId::Superglue:
            result.superglueRounds = static_cast<uint8_t>(dice_.rollD6(1));
            break;
        case ProgramId::Vrizzbolt:
            applyNonBlackIceDamage(target, targetDeck, dice_.rollD6(1), result);
            if (!result.shieldBlocked) result.nextTurnNetActionPenalty = 1;
            break;
        default: break;
    }
    Program* flak = targetDeck.findUsableProgram(ProgramId::Flak);
    if (flak != nullptr && program.usable())
        program.takeRezDamage(dice_.rollD6(2), ProgramDamageMode::DerezzAtZero);
    result.targetRemainingHp = target.hp();
    return result;
}

bool NetRules::hasRezzedCopy(const Cyberdeck& deck, const Program& program)
{
    for (size_t index = 0; index < deck.programCount(); ++index)
    {
        const Program* candidate = deck.programAt(index);
        if (candidate != nullptr && candidate != &program && candidate->id() == program.id() &&
            candidate->usable()) return true;
    }
    return false;
}

CombatResult NetRules::zap(Netrunner& runner, BlackIceInstance& target)
{
    CombatResult result;
    result.targetRemainingRez = target.currentRez();
    if (!target.active() || !target.pursuing() || runner.remainingNetActions() == 0)
        return result;

    runner.spendNetAction();
    result.executed = true;
    result.attackerRoll = dice_.rollD10();
    result.defenderRoll = dice_.rollD10();
    result.attackerTotal = runner.interfaceRank() + result.attackerRoll;
    result.defenderTotal = target.definition()->defense + result.defenderRoll;
    result.success = beatsDifficulty(result.attackerTotal, result.defenderTotal);
    if (result.success)
    {
        result.damage = dice_.rollD6(1);
        target.takeRezDamage(result.damage);
    }
    result.targetRemainingRez = target.currentRez();
    return result;
}

CombatResult NetRules::zap(Netrunner& runner, Netrunner& target)
{
    CombatResult result;
    result.targetRemainingHp = target.hp();
    if (runner.remainingNetActions() == 0) return result;

    runner.spendNetAction();
    result.executed = true;
    result.attackerRoll = dice_.rollD10();
    result.defenderRoll = dice_.rollD10();
    result.attackerTotal = runner.interfaceRank() + result.attackerRoll;
    result.defenderTotal = target.interfaceRank() + result.defenderRoll;
    result.success = beatsDifficulty(result.attackerTotal, result.defenderTotal);
    if (result.success)
    {
        result.rawDamage = dice_.rollD6(1);
        result.damage = result.rawDamage;
        target.takeDamage(result.damage);
    }
    result.targetRemainingHp = target.hp();
    return result;
}

CombatResult NetRules::zapDemon(Netrunner& runner, DemonInstance& target)
{
    CombatResult result; result.targetRemainingRez = target.currentRez;
    if (!target.active || runner.remainingNetActions() == 0) return result;
    runner.spendNetAction(); result.executed = true;
    result.attackerRoll = dice_.rollD10(); result.defenderRoll = dice_.rollD10();
    result.attackerTotal = runner.interfaceRank() + result.attackerRoll;
    result.defenderTotal = target.definition->combatNumber + result.defenderRoll;
    result.success = beatsDifficulty(result.attackerTotal, result.defenderTotal);
    if (result.success) { result.damage = dice_.rollD6(1); target.takeRezDamage(result.damage); }
    result.targetRemainingRez = target.currentRez; return result;
}

CombatResult NetRules::swordAttackDemon(Netrunner& runner, Cyberdeck& deck, DemonInstance& target)
{
    CombatResult result; result.targetRemainingRez = target.currentRez;
    Program* sword = deck.findUsableProgram(ProgramId::Sword);
    if (!target.active || sword == nullptr || runner.remainingNetActions() == 0) return result;
    runner.spendNetAction(); result.executed = true;
    result.attackerRoll = dice_.rollD10(); result.defenderRoll = dice_.rollD10();
    result.attackerTotal = runner.interfaceRank() + sword->attack() + result.attackerRoll;
    result.defenderTotal = target.definition->combatNumber + result.defenderRoll;
    result.success = beatsDifficulty(result.attackerTotal, result.defenderTotal);
    if (result.success) { result.damage = dice_.rollD6(3); target.takeRezDamage(result.damage); }
    result.targetRemainingRez = target.currentRez; return result;
}

CombatResult NetRules::demonZap(DemonInstance& attacker, Netrunner& target, Cyberdeck& targetDeck)
{
    CombatResult result; result.targetRemainingHp = target.hp();
    if (!attacker.active) return result;
    result.executed = true; result.attackerRoll = dice_.rollD10(); result.defenderRoll = dice_.rollD10();
    result.attackerTotal = attacker.definition->interfaceRank + result.attackerRoll;
    result.defenderTotal = target.interfaceRank() + result.defenderRoll;
    result.success = beatsDifficulty(result.attackerTotal, result.defenderTotal);
    if (result.success) { result.rawDamage = dice_.rollD6(1); result.damage = result.rawDamage;
        if (targetDeck.findUsableProgram(ProgramId::Armor) != nullptr) { result.damageReduction = result.damage > 4 ? 4 : result.damage; result.damage -= result.damageReduction; }
        target.takeDamage(result.damage); }
    result.targetRemainingHp = target.hp(); return result;
}

CombatResult NetRules::slide(Netrunner& runner, BlackIceInstance& target, int slideModifier)
{
    CombatResult result;
    result.targetRemainingRez = target.currentRez();
    if (!target.active() || !target.pursuing() || runner.remainingNetActions() == 0 ||
        runner.slideUsedThisTurn())
        return result;

    runner.useSlideThisTurn();
    runner.spendNetAction();
    result.executed = true;
    result.attackerRoll = dice_.rollD10();
    result.defenderRoll = dice_.rollD10();
    result.attackerTotal = runner.interfaceRank() + slideModifier + result.attackerRoll;
    result.defenderTotal = target.definition()->perception + result.defenderRoll;
    result.success = beatsDifficulty(result.attackerTotal, result.defenderTotal);
    if (result.success)
        target.stopPursuing();
    return result;
}

CombatResult NetRules::blackIceAttack(
    BlackIceInstance& attacker, Netrunner& target, Cyberdeck* targetDeck)
{
    CombatResult result;
    result.targetRemainingHp = target.hp();
    if (!attacker.active() || !attacker.pursuing())
        return result;

    result.executed = true;
    result.attackerDefinition = attacker.definition();
    if (attacker.definition()->category == BlackIceClass::AntiProgram)
    {
        result.targetType = CombatResult::TargetType::Program;
        if (targetDeck == nullptr)
        {
            result.noValidTarget = true;
            result.noProgramTarget = true;
            return result;
        }

        result.affectedProgramSlot = static_cast<int8_t>(
            selectRandomProgram(*targetDeck, ProgramTargetFilter::RezzedProgram));
        if (result.affectedProgramSlot < 0)
        {
            result.noValidTarget = true;
            result.noProgramTarget = true;
            return result;
        }

        Program* program = targetDeck->programAt(
            static_cast<size_t>(result.affectedProgramSlot));
        result.targetInitialRez = program->rez();
        result.targetRemainingRez = program->rez();
        strncpy(result.affectedProgramName, program->name(), sizeof(result.affectedProgramName) - 1);
        result.attackerRoll = dice_.rollD10();
        result.defenderRoll = dice_.rollD10();
        result.attackerTotal = attacker.definition()->attack + result.attackerRoll;
        result.defenderTotal = program->defense() + result.defenderRoll;
        result.success = beatsDifficulty(result.attackerTotal, result.defenderTotal);
        if (result.success && attacker.definition()->effect.type ==
            BlackIceEffectType::ProgramDamageDestroyAtZero)
        {
            result.damage = dice_.rollD6(attacker.definition()->effect.damageDice);
            applyProgramDamage(*targetDeck, static_cast<size_t>(result.affectedProgramSlot), result.damage, result);
        }
        return result;
    }

    result.attackerRoll = dice_.rollD10();
    result.defenderRoll = dice_.rollD10();
    result.attackerTotal = attacker.definition()->attack + result.attackerRoll;
    result.defenderTotal = target.interfaceRank() + result.defenderRoll;
    result.success = beatsDifficulty(result.attackerTotal, result.defenderTotal);
    if (result.success)
        applyBlackIceEffect(*attacker.definition(), target, targetDeck, result);
    result.targetRemainingHp = target.hp();
    return result;
}

CombatResult NetRules::resolveBlackIceEffect(
    BlackIceInstance& attacker, Netrunner& target, Cyberdeck* targetDeck)
{
    CombatResult result;
    result.executed = attacker.active();
    result.success = result.executed;
    result.attackerDefinition = attacker.definition();
    result.targetRemainingHp = target.hp();
    if (!result.executed || result.attackerDefinition == nullptr) return result;

    if (result.attackerDefinition->category == BlackIceClass::AntiProgram)
    {
        result.targetType = CombatResult::TargetType::Program;
        if (targetDeck == nullptr)
        {
            result.noValidTarget = true; result.noProgramTarget = true; return result;
        }
        result.affectedProgramSlot = static_cast<int8_t>(
            selectRandomProgram(*targetDeck, ProgramTargetFilter::RezzedProgram));
        if (result.affectedProgramSlot < 0)
        {
            result.noValidTarget = true; result.noProgramTarget = true; return result;
        }
        Program* program = targetDeck->programAt(static_cast<size_t>(result.affectedProgramSlot));
        result.targetInitialRez = program->rez();
        result.targetRemainingRez = program->rez();
        strncpy(result.affectedProgramName, program->name(), sizeof(result.affectedProgramName) - 1);
        if (result.attackerDefinition->effect.type == BlackIceEffectType::ProgramDamageDestroyAtZero)
        {
            result.damage = dice_.rollD6(result.attackerDefinition->effect.damageDice);
            applyProgramDamage(*targetDeck, static_cast<size_t>(result.affectedProgramSlot), result.damage, result);
        }
        return result;
    }

    applyBlackIceEffect(*result.attackerDefinition, target, targetDeck, result);
    result.targetRemainingHp = target.hp();
    return result;
}

bool NetRules::programEligible(const Program& program, ProgramTargetFilter filter)
{
    switch (filter)
    {
        case ProgramTargetFilter::RezzedDefender:
            return program.type() == ProgramType::Defender && program.usable();
        case ProgramTargetFilter::InstalledNotDestroyed:
            return program.status() != ProgramStatus::Destroyed;
        case ProgramTargetFilter::RezzedProgram:
            return program.usable();
    }
    return false;
}

int NetRules::selectRandomProgram(Cyberdeck& deck, ProgramTargetFilter filter)
{
    uint8_t eligibleCount = 0;
    for (size_t index = 0; index < deck.programCount(); ++index)
    {
        const Program* program = deck.programAt(index);
        if (program == nullptr) continue;
        if (programEligible(*program, filter)) ++eligibleCount;
    }
    if (eligibleCount == 0) return -1;

    uint8_t selected = eligibleCount == 1 ? 0 : dice_.rollIndex(eligibleCount);
    for (size_t index = 0; index < deck.programCount(); ++index)
    {
        const Program* program = deck.programAt(index);
        if (program == nullptr) continue;
        if (!programEligible(*program, filter)) continue;
        if (selected-- == 0) return static_cast<int>(index);
    }
    return -1;
}

void NetRules::applyBlackIceEffect(const BlackIceDefinition& definition, Netrunner& target,
                                   Cyberdeck* targetDeck, CombatResult& result)
{
    switch (definition.effect.type)
    {
        case BlackIceEffectType::DirectDamage:
            // Architecture Hellhound shares the existing Runner fire result
            // consumed by GameState; insulation blocks only the fire, not the
            // direct damage handled below.
            result.fireBlocked = targetDeck != nullptr &&
                targetDeck->hasHardware(HardwareId::InsulatedWiring);
            result.fireApplied = !result.fireBlocked;
            break;
        case BlackIceEffectType::DerezzRandomDefenderAndDamage:
            if (targetDeck == nullptr) break;
            result.affectedProgramSlot = static_cast<int8_t>(selectRandomProgram(
                *targetDeck, ProgramTargetFilter::RezzedDefender));
            if (result.affectedProgramSlot < 0) break;
            {
            Program* program = targetDeck->programAt(static_cast<size_t>(result.affectedProgramSlot));
            result.targetInitialRez = program->rez();
            result.targetRemainingRez = program->rez();
            strncpy(result.affectedProgramName, program->name(), sizeof(result.affectedProgramName) - 1);
            program
                ->setStatus(ProgramStatus::Derezzed);
            result.programEffectApplied = true;
            result.resultingProgramStatus = ProgramStatus::Derezzed;
            result.targetRemainingRez = program->rez();
            }
            break;
        case BlackIceEffectType::DestroyRandomInstalledProgram:
            if (targetDeck == nullptr) break;
            result.affectedProgramSlot = static_cast<int8_t>(selectRandomProgram(
                *targetDeck, ProgramTargetFilter::InstalledNotDestroyed));
            if (result.affectedProgramSlot < 0) { result.noProgramTarget = true; break; }
            {
            Program* program = targetDeck->programAt(static_cast<size_t>(result.affectedProgramSlot));
            result.targetInitialRez = program->rez();
            result.targetRemainingRez = program->rez();
            strncpy(result.affectedProgramName, program->name(), sizeof(result.affectedProgramName) - 1);
            result.programSavedByBackup = targetDeck->destroyProgram(
                static_cast<size_t>(result.affectedProgramSlot));
            result.programEffectApplied = true;
            result.resultingProgramStatus = result.programSavedByBackup
                ? ProgramStatus::Inactive : ProgramStatus::Destroyed;
            result.targetRemainingRez = result.programSavedByBackup ? 0 : program->rez();
            result.targetDestroyed = !result.programSavedByBackup;
            }
            break;
        case BlackIceEffectType::DamageAndNavigationLock:
            result.temporaryRunEffect = TemporaryRunEffect::NavigationAndSafeJackOutLock;
            break;
        case BlackIceEffectType::DamageAndNextTurnNetActionPenalty:
            result.temporaryRunEffect = TemporaryRunEffect::NextTurnNetActionPenalty;
            break;
        case BlackIceEffectType::ProgramDamageDestroyAtZero:
            break;
        case BlackIceEffectType::ReduceRunnerMove:
            result.moveBefore = target.move();
            target.reduceMove(dice_.rollD6(definition.effect.statusDice));
            result.moveAfter = target.move();
            break;
        case BlackIceEffectType::SlidePenaltyWhileActive:
            result.temporaryRunEffect = TemporaryRunEffect::SlidePenaltyWhileActive;
            break;
        case BlackIceEffectType::ReduceRunnerStats:
            result.intBefore = target.currentInt();
            result.refBefore = target.currentRef();
            result.dexBefore = target.currentDex();
            result.intReduction = static_cast<uint8_t>(dice_.rollD6(1));
            result.refReduction = static_cast<uint8_t>(dice_.rollD6(1));
            result.dexReduction = static_cast<uint8_t>(dice_.rollD6(1));
            target.reduceInt(result.intReduction);
            target.reduceRef(result.refReduction);
            target.reduceDex(result.dexReduction);
            result.intAfter = target.currentInt();
            result.refAfter = target.currentRef();
            result.dexAfter = target.currentDex();
            break;
        case BlackIceEffectType::DamageAndUnsafeJackOut:
            result.forcedUnsafeJackOut = true;
            break;
        case BlackIceEffectType::DamageOnly:
            break;
        case BlackIceEffectType::ApplyFire:
            result.fireBlocked = targetDeck != nullptr &&
                targetDeck->hasHardware(HardwareId::InsulatedWiring);
            result.fireApplied = !result.fireBlocked;
            break;
        case BlackIceEffectType::DamageAndReduceRunnerMove:
            result.moveBefore = target.move();
            target.reduceMove(definition.effect.statusDice);
            result.moveAfter = target.move();
            break;
        case BlackIceEffectType::ReduceRunnerStatsByAmount:
            result.intBefore = target.currentInt();
            result.refBefore = target.currentRef();
            result.dexBefore = target.currentDex();
            result.intReduction = definition.effect.statusDice;
            result.refReduction = definition.effect.statusDice;
            result.dexReduction = definition.effect.statusDice;
            target.reduceInt(result.intReduction);
            target.reduceRef(result.refReduction);
            target.reduceDex(result.dexReduction);
            result.intAfter = target.currentInt();
            result.refAfter = target.currentRef();
            result.dexAfter = target.currentDex();
            break;
    }

    if (definition.effect.damageDice > 0)
    {
        result.rawDamage = dice_.rollD6(definition.effect.damageDice);
        result.damage = result.rawDamage;
        // The current runtime models Black ICE direct damage as the available
        // brain-damage path. Armor therefore reduces it while Rezzed.
        if (targetDeck != nullptr && targetDeck->findUsableProgram(ProgramId::Armor) != nullptr)
        {
            result.damageReduction = result.damage > 4 ? 4 : result.damage;
            result.damage -= result.damageReduction;
        }
        target.takeDamage(result.damage);
    }
}

void NetRules::applyProgramDamage(Cyberdeck& deck, size_t slot, int damage, CombatResult& result)
{
    Program* program = deck.programAt(slot);
    if (program == nullptr) return;
    const bool destroyed = damage >= program->rez();
    if (destroyed)
    {
        result.programSavedByBackup = deck.destroyProgram(slot);
        result.targetDestroyed = !result.programSavedByBackup;
        result.resultingProgramStatus = result.programSavedByBackup
            ? ProgramStatus::Inactive : ProgramStatus::Destroyed;
        result.targetRemainingRez = 0;
    }
    else
    {
        program->takeRezDamage(damage, ProgramDamageMode::DestroyAtZero);
        result.resultingProgramStatus = program->status();
        result.targetRemainingRez = program->rez();
        result.targetDestroyed = false;
    }
    result.programEffectApplied = true;
}

NetCheckResult NetRules::performFloorCheck(
    Netrunner& runner, Floor& floor, FloorType requiredType, int programModifier,
    int hardwareModifier)
{
    NetCheckResult result;
    result.target = floor.dv;

    if (floor.type != requiredType || runner.remainingNetActions() == 0)
    {
        return result;
    }

    if (!runner.spendNetAction())
    {
        return result;
    }

    result.attempted = true;
    result.base = runner.interfaceRank();
    result.programModifier = programModifier;
    result.hardwareModifier = hardwareModifier;
    result.roll = dice_.rollD10();
    result.total = result.base + result.programModifier + result.hardwareModifier + result.roll;
    result.success = beatsDifficulty(result.total, result.target);

    if (result.success)
    {
        floor.resolved = true;
    }

    return result;
}
