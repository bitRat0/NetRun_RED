#include "EnemyNetrunner.h"

#include "ProgramCatalog.h"

#include <string.h>

namespace
{
CyberdeckConfig makeNullbyteDeck()
{
    CyberdeckConfig deck;
    deck.quality = CyberdeckQuality::Standard;
    deck.programs[0] = ProgramId::Hellbolt;
    deck.programs[1] = ProgramId::Vrizzbolt;
    deck.programs[2] = ProgramId::Nervescrub;
    deck.programs[3] = ProgramId::Armor;
    deck.programs[4] = ProgramId::Sword;
    deck.programCount = 5;
    deck.hardwareCount = 0;
    return deck;
}

const EnemyNetrunnerDefinition kNullbyte = {
    "nullbyte", "NULLBYTE", 4, 30, 3,
    makeNullbyteDeck(),
    EnemyAiArchetype::AntiPersonnel, false, EnemyBehaviorId::Baseline
};

const EnemyNetrunnerDefinition kZer = {
    "zer=0", "Zer=0", 4, 30, 3,
    makeNullbyteDeck(),
    EnemyAiArchetype::AntiPersonnel, true, EnemyBehaviorId::Baseline
};
}

bool EnemyNetrunnerRuntime::copyText(const char* source, char* target, size_t capacity)
{
    if (source == nullptr || capacity == 0 || strlen(source) >= capacity) return false;
    strcpy(target, source);
    return true;
}

bool EnemyNetrunnerRuntime::reset(const EnemyNetrunnerDefinition& source, uint16_t id, uint8_t floor)
{
    if (source.id == nullptr || source.id[0] == '\0' || source.name == nullptr || source.name[0] == '\0' ||
        source.maxHp == 0 || source.netActions == 0 || !cyberdeckConfigValid(source.deckConfig) ||
        !enemyBehaviorIdValid(source.behavior))
        return false;

    // A debug/runtime reset may legally use this runtime's current owned
    // definition as source. Copy all source data before clearing the slot.
    EnemyNetrunnerDefinition sourceCopy = source;
    char stableIdCopy[MAX_STABLE_ID_LENGTH + 1] = {};
    char displayNameCopy[MAX_DISPLAY_NAME_LENGTH + 1] = {};
    if (!copyText(source.id, stableIdCopy, sizeof(stableIdCopy)) ||
        !copyText(source.name, displayNameCopy, sizeof(displayNameCopy)))
        return false;
    *this = EnemyNetrunnerRuntime();
    strcpy(stableId_, stableIdCopy);
    strcpy(displayName_, displayNameCopy);
    definitionStorage_ = sourceCopy;
    definitionStorage_.id = stableId_;
    definitionStorage_.name = displayName_;
    definition = &definitionStorage_;
    runtimeId = id;
    currentFloor = floor;
    runner = Netrunner(definition->name, definition->interfaceRank, definition->maxHp, definition->netActions);
    cyberdeck.clear(definition->deckConfig.quality);
    for (uint8_t index = 0; index < definition->deckConfig.programCount; ++index)
        if (!cyberdeck.addProgram(makeProgram(definition->deckConfig.programs[index])))
        {
            *this = EnemyNetrunnerRuntime();
            return false;
        }
    for (uint8_t index = 0; index < definition->deckConfig.hardwareCount; ++index)
        if (!cyberdeck.addHardware(definition->deckConfig.hardware[index]))
        {
            *this = EnemyNetrunnerRuntime();
            return false;
        }
    active = true;
    jackedOut = false;
    runnerDown = false;
    onFire = false;
    superglueRoundsRemaining = 0;
    nextTurnNetActionPenalty = 0;
    return true;
}

const EnemyNetrunnerDefinition& nullbyteDefinition()
{
    return kNullbyte;
}

const EnemyNetrunnerDefinition& zerDefinition()
{
    return kZer;
}

const EnemyNetrunnerDefinition* enemyNetrunnerDefinitionById(const char* id)
{
    if (id == nullptr) return nullptr;
    if (strcmp(id, kNullbyte.id) == 0) return &kNullbyte;
    if (strcmp(id, kZer.id) == 0) return &kZer;
    return nullptr;
}

namespace
{
bool canActivateDefender(const Cyberdeck& deck, const Program& program)
{
    if (program.type() != ProgramType::Defender || program.status() != ProgramStatus::Inactive ||
        program.activatedThisRound() || (program.oncePerRun() && program.usedThisRun()))
        return false;
    if (!program.exclusive()) return true;
    for (size_t slot = 0; slot < deck.programCount(); ++slot)
    {
        const Program* other = deck.programAt(slot);
        if (other != nullptr && other != &program && other->id() == program.id() &&
            other->status() == ProgramStatus::Rezzed)
            return false;
    }
    return true;
}

EnemyBehaviorDecision chooseBaselineDecision(const EnemyNetrunnerRuntime& enemy,
                                             ProgramId forcedProgram,
                                             bool slideAvailable)
{
    EnemyBehaviorDecision decision;
    if (slideAvailable)
    {
        decision.type = EnemyDecisionType::SlidePlayerBlackIce;
        decision.reason = EnemyDecisionReason::PlayerIceLock;
        return decision;
    }
    if (forcedProgram != ProgramId::None)
    {
        for (size_t slot = 0; slot < enemy.cyberdeck.programCount(); ++slot)
        {
            const Program* candidate = enemy.cyberdeck.programAt(slot);
            if (candidate != nullptr && candidate->id() == forcedProgram)
            {
                decision.type = EnemyDecisionType::UseProgram;
                decision.program = forcedProgram;
                decision.reason = EnemyDecisionReason::ForcedProgram;
                return decision;
            }
        }
    }
    const ProgramId priorities[] = {ProgramId::Hellbolt, ProgramId::Vrizzbolt, ProgramId::Nervescrub};
    for (ProgramId priority : priorities)
    {
        if (enemy.cyberdeck.findUsableProgram(priority) != nullptr)
        {
            decision.type = EnemyDecisionType::UseProgram;
            decision.program = priority;
            decision.reason = EnemyDecisionReason::PriorityProgram;
            return decision;
        }
    }
    for (ProgramId priority : priorities)
    {
        for (size_t slot = 0; slot < enemy.cyberdeck.programCount(); ++slot)
        {
            const Program* candidate = enemy.cyberdeck.programAt(slot);
            if (candidate != nullptr && candidate->id() == priority &&
                candidate->status() == ProgramStatus::Inactive)
            {
                decision.type = EnemyDecisionType::ActivateProgram;
                decision.program = priority;
                decision.reason = EnemyDecisionReason::InactivePriorityProgram;
                return decision;
            }
        }
    }
    return decision;
}
}

EnemyBehaviorDecision chooseEnemyBehaviorDecision(const EnemyNetrunnerRuntime& enemy,
                                                  ProgramId forcedProgram,
                                                  bool slideAvailable,
                                                  bool coLocated)
{
    EnemyBehaviorDecision decision;
    if (enemy.definition == nullptr || !enemyBehaviorIdValid(enemy.definition->behavior)) return decision;
    const EnemyBehaviorId behavior = enemy.definition->behavior;
    if (!coLocated)
    {
        if (behavior == EnemyBehaviorId::Sentry) return decision;
        decision.type = EnemyDecisionType::MoveTowardRunner;
        decision.reason = EnemyDecisionReason::ChaseMovement;
        return decision;
    }
    // Sentry uses this exact Baseline combat path once it shares the runner's floor.
    if (behavior == EnemyBehaviorId::Defensive)
    {
        // An existing Player-ICE lock remains the first valid response.
        if (slideAvailable) return chooseBaselineDecision(enemy, forcedProgram, true);
        for (size_t slot = 0; slot < enemy.cyberdeck.programCount(); ++slot)
        {
            const Program* candidate = enemy.cyberdeck.programAt(slot);
            if (candidate != nullptr && canActivateDefender(enemy.cyberdeck, *candidate))
            {
                decision.type = EnemyDecisionType::ActivateProgram;
                decision.program = candidate->id();
                decision.reason = EnemyDecisionReason::DefensiveProgram;
                return decision;
            }
        }
    }
    return chooseBaselineDecision(enemy, forcedProgram, slideAvailable);
}
