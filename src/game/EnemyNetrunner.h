#pragma once

#include "Cyberdeck.h"
#include "Netrunner.h"

constexpr size_t MAX_ENEMY_NETRUNNERS = 2;

enum class EnemyAiArchetype : uint8_t { AntiPersonnel };
// R3.5a exposes the policy identity without changing the established
// AntiPersonnel execution rules. Future profiles extend this enum.
enum class EnemyBehaviorId : uint8_t { Baseline, Defensive, Sentry };
constexpr bool enemyBehaviorIdValid(EnemyBehaviorId behavior)
{
    return behavior == EnemyBehaviorId::Baseline || behavior == EnemyBehaviorId::Defensive ||
        behavior == EnemyBehaviorId::Sentry;
}
enum class EnemyDecisionType : uint8_t { None, SlidePlayerBlackIce, ActivateProgram, UseProgram, MoveTowardRunner };
enum class EnemyDecisionReason : uint8_t {
    None, PlayerIceLock, ForcedProgram, PriorityProgram, InactivePriorityProgram,
    DefensiveProgram, ChaseMovement
};

struct EnemyBehaviorDecision
{
    EnemyDecisionType type = EnemyDecisionType::None;
    ProgramId program = ProgramId::None;
    EnemyDecisionReason reason = EnemyDecisionReason::None;
};

struct EnemyNetrunnerDefinition
{
    const char* id;
    const char* name;
    uint8_t interfaceRank;
    uint8_t maxHp;
    uint8_t netActions;
    CyberdeckConfig deckConfig;
    EnemyAiArchetype ai;
    bool stationaryUntilDiscovered;
    // All existing definitions and R3.4 JSON content value-initialize this
    // trailing field to Baseline until a later schema block opts in.
    EnemyBehaviorId behavior;
};

struct EnemyNetrunnerRuntime
{
    static constexpr size_t MAX_STABLE_ID_LENGTH = 31;
    static constexpr size_t MAX_DISPLAY_NAME_LENGTH = 31;

    // Runtime-owned metadata keeps future scenario definitions independent of
    // temporary parser or stack storage. Deck configuration is already a
    // value member of EnemyNetrunnerDefinition and is copied with it.
    const EnemyNetrunnerDefinition* definition = nullptr;
    uint16_t runtimeId = 0;
    uint8_t currentFloor = 0;
    Netrunner runner;
    Cyberdeck cyberdeck;
    bool active = false;
    bool jackedOut = false;
    bool runnerDown = false;
    bool onFire = false;
    uint8_t superglueRoundsRemaining = 0;
    uint8_t nextTurnNetActionPenalty = 0;
    // Transition state only: rendering and focus changes never alter it.
    bool coLocatedWithPlayer = false;
    bool presencePending = false;
    bool discoveredByPlayer = false;

    // Reject malformed content instead of leaving a partially initialized deck
    // in a runtime slot.
    bool reset(const EnemyNetrunnerDefinition& source, uint16_t id, uint8_t floor);
    bool available() const { return active && !jackedOut && !runnerDown; }
    void markRunnerDown() { active = false; runnerDown = true; }
    void unsafeJackOut() { active = false; jackedOut = true; }

private:
    EnemyNetrunnerDefinition definitionStorage_ = {};
    char stableId_[MAX_STABLE_ID_LENGTH + 1] = {};
    char displayName_[MAX_DISPLAY_NAME_LENGTH + 1] = {};
    static bool copyText(const char* source, char* target, size_t capacity);
};

const EnemyNetrunnerDefinition& nullbyteDefinition();
const EnemyNetrunnerDefinition& zerDefinition();
const EnemyNetrunnerDefinition* enemyNetrunnerDefinitionById(const char* id);

// Pure policy selection. Execution remains owned by GameState and NetRules.
EnemyBehaviorDecision chooseEnemyBehaviorDecision(const EnemyNetrunnerRuntime& enemy,
                                                  ProgramId forcedProgram,
                                                  bool slideAvailable,
                                                  bool coLocated);
