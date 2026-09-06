#pragma once
#include "Architecture.h"
#include "Cyberdeck.h"
#include "Netrunner.h"
#include "BlackIce.h"
#include "NetRules.h"
#include "RunnerProfile.h"
#include "CyberdeckConfig.h"
#include "EnemyNetrunner.h"
#include "Demon.h"

struct ArchitectureDefinition;

struct EncounterResult
{
    bool moved = false;
    bool triggered = false;
    OpposedCheckResult speedCheck;
    CombatResult immediateAttack;
    struct IceEncounter
    {
        uint16_t runtimeId = 0;
        bool newlyEncountered = false;
        OpposedCheckResult speedCheck;
        CombatResult immediateAttack;
    } ice[MAX_ACTIVE_BLACK_ICE];
    size_t iceCount = 0;
    CombatResult exitEffects[MAX_ACTIVE_BLACK_ICE] = {};
    size_t exitEffectCount = 0;
    bool runTerminated = false;
};

struct IcePhaseResult
{
    bool executed = false;
    uint8_t actionCount = 0;
    CombatResult attack;
    struct IceAttack
    {
        uint16_t runtimeId = 0;
        uint16_t targetEnemyRuntimeId = 0;
        bool unsafeExitEffect = false;
        CombatResult result;
    } attacks[MAX_ACTIVE_BLACK_ICE];
    size_t attackCount = 0;
    bool runTerminated = false;
};

struct EnemyPhaseResult
{
    struct EnemyAction {
        uint16_t runtimeId = 0;
        uint16_t targetEnemyRuntimeId = 0;
        CombatResult result;
        bool fromPlayerBlackIce = false;
    } actions[MAX_ENEMY_NETRUNNERS * 3];
    size_t actionCount = 0;
    bool executed = false;
    bool runTerminated = false;
};

struct DemonPhaseResult
{
    enum class ActionType : uint8_t { None, ControlReclaim, Zap };
    struct ActionResult
    {
        ActionType type = ActionType::None;
        uint8_t controlledNodeFloor = Architecture::NO_FLOOR;
        CombatResult attack;
    };
    static constexpr size_t MAX_ACTIONS = 5;
    bool executed = false;
    bool controlledNode = false;
    uint8_t controlledNodeFloor = Architecture::NO_FLOOR;
    CombatResult zap;
    uint8_t actionCount = 0;
    ActionResult actions[MAX_ACTIONS] = {};
};

struct ActiveBlackIceSlot
{
    BlackIceInstance instance;
    uint16_t runtimeId = 0;
    uint8_t sourceFloorIndex = 0;
    uint8_t chasePosition = 0;
    bool occupied = false;
    bool slidePenaltyApplied = false;
};

struct PlayerBlackIceRuntime
{
    const BlackIceDefinition* definition = nullptr;
    BlackIceInstance instance;
    uint16_t runtimeId = 0;
    uint16_t targetEnemyRuntimeId = 0;
    uint8_t chasePosition = 0;
    bool occupied = false;
};

struct PathfinderResult
{
    NetCheckResult check;
    uint8_t discoveredCount = 0;
    uint8_t scanDepth = 0;
    bool obstructionDetected = false;
};

class GameState
{
public:
    GameState();
    explicit GameState(const ArchitectureDefinition& definition);
    bool setArchitectureDefinition(const ArchitectureDefinition& definition);
    bool setRunnerProfile(const RunnerProfile& profile);
    bool setCyberdeckConfig(const CyberdeckConfig& config);
    const RunnerProfile& runnerProfile() const { return runnerProfile_; }
    const CyberdeckConfig& cyberdeckConfig() const { return cyberdeckConfig_; }
    Netrunner& runner() { return runner_; }
    const Netrunner& runner() const { return runner_; }
    Cyberdeck& cyberdeck() { return cyberdeck_; }
    const Cyberdeck& cyberdeck() const { return cyberdeck_; }
    Architecture& architecture() { return architecture_; }
    const Architecture& architecture() const { return architecture_; }
    RunState runState() const { return runState_; }
    TurnState turnState() const { return turnState_; }
    TurnPhase turnPhase() const { return turnPhase_; }
    uint32_t turnNumber() const { return turnNumber_; }
    bool navigationLockActive() const { return temporaryRunnerStatus_.navigationLockThroughTurn != 0; }
    bool canProgressDeeper() const
    {
        return superglueRoundsRemaining_ == 0 && (!navigationLockActive() ||
            architecture_.currentPosition() + 1 <= temporaryRunnerStatus_.navigationLockMaxPosition);
    }
    bool canSafeJackOut() const { return runState_ == RunState::JackedIn && !navigationLockActive() && superglueRoundsRemaining_ == 0; }
    bool hasReachedArchitectureEnd() const;
    bool completeIfArchitectureEnd();
    bool isAwaitingEndDecision() const { return awaitingEndDecision_; }
    bool finishArchitectureEnd(bool virusPlaced);
    bool architectureCompleted() const { return architectureCompleted_; }
    bool virusPlaced() const { return virusPlaced_; }
    uint8_t scheduledNetActionPenalty(uint32_t turn) const
    {
        return temporaryRunnerStatus_.netActionPenaltyTurn == turn
            ? temporaryRunnerStatus_.netActionPenaltyAmount : 0;
    }
    uint8_t netActionPenaltyThisTurn() const
    {
        return temporaryRunnerStatus_.netActionPenaltyAppliedThisTurn;
    }
    bool moveLocked() const { return superglueRoundsRemaining_ > 0; }
    int activeSlidePenalty() const;
    bool encounterJustTriggered() const { return encounterJustTriggered_; }
    size_t activeBlackIceCount() const { return activeBlackIceCount_; }
    BlackIceInstance* activeBlackIceAt(size_t index)
    {
        return index < activeBlackIceCount_ && activeBlackIce_[index].occupied
            ? &activeBlackIce_[index].instance : nullptr;
    }
    const BlackIceInstance* activeBlackIceAt(size_t index) const
    {
        return index < activeBlackIceCount_ && activeBlackIce_[index].occupied
            ? &activeBlackIce_[index].instance : nullptr;
    }
    uint16_t activeBlackIceRuntimeId(size_t index) const
    {
        return index < activeBlackIceCount_ ? activeBlackIce_[index].runtimeId : 0;
    }
    uint8_t activeBlackIceChasePosition(size_t index) const
    {
        return index < activeBlackIceCount_ ? activeBlackIce_[index].chasePosition : 0;
    }
    uint8_t activeBlackIceSourceFloor(size_t index) const
    {
        return index < activeBlackIceCount_ ? activeBlackIce_[index].sourceFloorIndex : 0;
    }
    size_t engagedBlackIceCount() const;
    BlackIceInstance* engagedBlackIceAt(size_t engagedIndex);
    const BlackIceInstance* engagedBlackIceAt(size_t engagedIndex) const;
    uint16_t engagedBlackIceRuntimeId(size_t engagedIndex) const;
    BlackIceInstance* engagedBlackIceByRuntimeId(uint16_t runtimeId);
    const BlackIceInstance* engagedBlackIceByRuntimeId(uint16_t runtimeId) const;
    uint16_t nextEngagedBlackIceRuntimeId(uint16_t currentRuntimeId, int direction) const;
    // Runtime ICE physically present at the runner's current Architecture position.
    // This includes pursuing, slid, and derezzed instances without changing combat validity.
    size_t floorBlackIceCount() const;
    BlackIceInstance* floorBlackIceAt(size_t rosterIndex);
    const BlackIceInstance* floorBlackIceAt(size_t rosterIndex) const;
    uint16_t floorBlackIceRuntimeId(size_t rosterIndex) const;
    BlackIceInstance* floorBlackIceByRuntimeId(uint16_t runtimeId);
    const BlackIceInstance* floorBlackIceByRuntimeId(uint16_t runtimeId) const;
    uint16_t nextFloorBlackIceRuntimeId(uint16_t currentRuntimeId, int direction) const;
    void recordBlackIcePosition(const BlackIceInstance& ice);
    BlackIceInstance* engagedBlackIceTarget()
    {
        return engagedBlackIceAt(0);
    }
    bool triggerCurrentBlackIce();
    PathfinderResult pathfinder(NetRules& rules);
    NetCheckResult cloak(NetRules& rules);
    bool cloakUsed() const { return cloakUsed_; }
    int cloakValue() const { return cloakValue_; }
    EncounterResult moveForward(NetRules& rules);
    EncounterResult moveBackward(NetRules& rules);
    EncounterResult moveToConnectedFloor(NetRules& rules, uint8_t destination);
    void moveForward(NetRules& rules, EncounterResult& result);
    void moveBackward(NetRules& rules, EncounterResult& result);
    void moveToConnectedFloor(NetRules& rules, uint8_t destination, EncounterResult& result);
    void endPlayerTurn();
    bool updatePlayerTurn();
    IcePhaseResult runIcePhase(NetRules& rules);
    void runIcePhase(NetRules& rules, IcePhaseResult& result);
    void runPlayerBlackIcePhase(NetRules& rules, IcePhaseResult& result);
    void runDemonPhase(NetRules& rules, DemonPhaseResult& result);
    const DemonInstance& demon() const { return demon_; }
    DemonInstance& demon() { return demon_; }
    EnemyPhaseResult runEnemyPhase(NetRules& rules);
    void runEnemyPhase(NetRules& rules, EnemyPhaseResult& result);
    // Dev-only presentation harness hook. Normal callers never set this.
    void setForcedNextEnemyProgram(ProgramId id) { forcedNextEnemyProgram_ = id; }
    bool runEnemyProgramTest(NetRules& rules, ProgramId id, EnemyPhaseResult& result);
    size_t enemyNetrunnerCount() const { return enemyNetrunnerCount_; }
    EnemyNetrunnerRuntime* enemyNetrunnerAt(size_t index)
    { return index < enemyNetrunnerCount_ ? &enemyNetrunners_[index] : nullptr; }
    const EnemyNetrunnerRuntime* enemyNetrunnerAt(size_t index) const
    { return index < enemyNetrunnerCount_ ? &enemyNetrunners_[index] : nullptr; }
    EnemyNetrunnerRuntime* enemyNetrunnerByRuntimeId(uint16_t runtimeId);
    const EnemyNetrunnerRuntime* enemyNetrunnerByRuntimeId(uint16_t runtimeId) const;
    // Read-only target resolution shared by Player ICE gameplay and its UI preview.
    // A valid lock wins; otherwise the first eligible same-floor Enemy in runtime order wins.
    const EnemyNetrunnerRuntime* findEligiblePlayerBlackIceTarget(
        const PlayerBlackIceRuntime& ice) const;
    const EnemyNetrunnerRuntime* findPlayerBlackIcePotentialTarget(
        const PlayerBlackIceRuntime& ice) const;
    const char* playerBlackIceTargetDisplayName(const PlayerBlackIceRuntime& ice) const;
    const char* enemyNetrunnerNameByRuntimeId(uint16_t runtimeId) const;
    bool consumeEnemyPresence(uint16_t& runtimeId);
    size_t playerBlackIceCount() const { return playerBlackIceCount_; }
    PlayerBlackIceRuntime* playerBlackIceAt(size_t index)
    { return index < playerBlackIceCount_ && playerBlackIce_[index].occupied ? &playerBlackIce_[index] : nullptr; }
    const PlayerBlackIceRuntime* playerBlackIceAt(size_t index) const
    { return index < playerBlackIceCount_ && playerBlackIce_[index].occupied ? &playerBlackIce_[index] : nullptr; }
    ProgramActionResult activatePlayerBlackIce(size_t index, uint16_t enemyRuntimeId);
    ProgramActionResult deactivatePlayerBlackIce(size_t index);
    CombatResult attackFloorEnemy(NetRules& rules, Program& program);
    CombatResult attackFloorEnemyProgram(NetRules& rules, Program& program, size_t targetSlot);
    CombatResult zapFloorEnemy(NetRules& rules, uint16_t enemyRuntimeId);
    EnemyNetrunnerRuntime* floorEnemyNetrunner();
    const EnemyNetrunnerRuntime* floorEnemyNetrunner() const;
    bool extinguishRunnerFire();
    bool runnerOnFire() const { return runnerOnFire_; }
    uint8_t consumeRunnerFireTickDamage();
    void startRun();
    bool jackIn();
    bool jackOut();
    bool unsafeJackOut(NetRules& rules, EncounterResult& result);
    bool startTurn();
    void endTurn();

private:
    Netrunner runner_;
    Cyberdeck cyberdeck_;
    Architecture architecture_;
    const ArchitectureDefinition* architectureDefinition_ = nullptr;
    RunState runState_ = RunState::Idle;
    TurnState turnState_ = TurnState::Inactive;
    TurnPhase turnPhase_ = TurnPhase::Player;
    uint32_t turnNumber_ = 1;
    bool encounterJustTriggered_ = false;
    bool resolvingUnsafeJackOut_ = false;
    bool awaitingEndDecision_ = false;
    bool virusPlaced_ = false;
    bool architectureCompleted_ = false;
    bool cloakUsed_ = false;
    int cloakValue_ = 0;
    ActiveBlackIceSlot activeBlackIce_[MAX_ACTIVE_BLACK_ICE];
    size_t activeBlackIceCount_ = 0;
    uint16_t nextRuntimeIceId_ = 1;
    PlayerBlackIceRuntime playerBlackIce_[MAX_PLAYER_BLACK_ICE];
    size_t playerBlackIceCount_ = 0;
    uint16_t nextPlayerBlackIceRuntimeId_ = 1;
    EnemyNetrunnerRuntime enemyNetrunners_[MAX_ENEMY_NETRUNNERS];
    DemonInstance demon_;
    size_t enemyNetrunnerCount_ = 0;
    uint16_t nextEnemyRuntimeId_ = 1;
    ProgramId forcedNextEnemyProgram_ = ProgramId::None;
    bool forcedEnemyTestPhase_ = false;
    bool runnerOnFire_ = false;
    uint8_t runnerFireTickDamage_ = 0;
    uint8_t superglueRoundsRemaining_ = 0;
    RunnerProfile runnerProfile_ = defaultRunnerProfile();
    CyberdeckConfig cyberdeckConfig_ = defaultCyberdeckConfig();
    struct TemporaryRunnerStatus
    {
        uint32_t navigationLockThroughTurn = 0;
        uint32_t netActionPenaltyTurn = 0;
        uint8_t navigationLockMaxPosition = 0;
        uint8_t netActionPenaltyAmount = 0;
        uint8_t netActionMinimum = 0;
        uint8_t netActionPenaltyAppliedThisTurn = 0;
    } temporaryRunnerStatus_;
    void enterCurrentFloor(NetRules& rules, bool moved, EncounterResult& result);
    void resolveEncounterForSlot(NetRules& rules, ActiveBlackIceSlot& slot,
                                 EncounterResult& result);
    void resolveForcedUnsafeJackOut(NetRules& rules, uint16_t triggeringRuntimeId,
                                    IcePhaseResult* phaseResult, EncounterResult* encounterResult);
    bool movementBlocked() const;
    void applyTemporaryRunEffect(CombatResult& result, ActiveBlackIceSlot* source = nullptr);
    void expireTemporaryRunnerStatuses();
    uint8_t netActionsForCurrentTurn();
    void initializeEnemyNetrunners();
    void initializeDemon();
    void initializePlayerBlackIce();
    bool hasPursuingPlayerBlackIce() const;
    void clearPlayerBlackIceTargets(uint16_t enemyRuntimeId);
    bool enemySlidePlayerBlackIce(NetRules& rules, EnemyNetrunnerRuntime& enemy,
                                  CombatResult& result);
    bool enemyCanSlidePlayerBlackIce(const EnemyNetrunnerRuntime& enemy) const;
    void applyProgramEffectToRunner(const CombatResult& result);
    void applyProgramEffectToEnemy(EnemyNetrunnerRuntime& enemy, const CombatResult& result);
    void runEnemyAi(NetRules& rules, EnemyNetrunnerRuntime& enemy, EnemyPhaseResult& result);
    // Player ICE is a deployed actor. An enemy that enters its floor is
    // intercepted before it can continue toward the runner.
    void moveEnemyTowardRunner(NetRules& rules, EnemyNetrunnerRuntime& enemy,
                               EnemyPhaseResult& result);
    void updateEnemyPresenceStates();
    bool enemyMovementBlocked(uint8_t from, uint8_t to) const;
    bool markRunnerDown();
    void initializeRuntimeDeck();
};
