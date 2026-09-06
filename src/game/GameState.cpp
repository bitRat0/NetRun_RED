#include "GameState.h"
#include "content/ArchitectureFactory.h"
#include "content/BuiltInArchitectures.h"
#include "ProgramCatalog.h"
#include "app/DebugConfig.h"
#include <Arduino.h>
#include <new>
#include <string.h>

namespace
{
void resetEncounterResultInPlace(EncounterResult& result)
{
    result.~EncounterResult();
    new (&result) EncounterResult();
}

void resetIcePhaseResultInPlace(IcePhaseResult& result)
{
    result.~IcePhaseResult();
    new (&result) IcePhaseResult();
}

void resetEnemyPhaseResultInPlace(EnemyPhaseResult& result)
{
    result.~EnemyPhaseResult();
    new (&result) EnemyPhaseResult();
}

bool tracesCombatFloor2(const Architecture& architecture, bool targetFloor = false)
{
    return NETRUN_DEBUG_VERBOSE && strcmp(architecture.id(), "netrunner_combat_test") == 0 &&
        architecture.currentPosition() == (targetFloor ? 0U : 1U);
}

void logFloor2Memory(unsigned step)
{
    Serial.printf("[F2MEM] step=%u heap=%u minHeap=%u stack=%u\n", step,
        static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}
}

GameState::GameState() : GameState(BuiltInArchitectures::militechTestNet())
{
}

GameState::GameState(const ArchitectureDefinition& definition)
    : architecture_(ArchitectureFactory::create(definition)),
      architectureDefinition_(&definition)
{
    runner_ = Netrunner(runnerProfile_.handle, runnerProfile_.interfaceRank, runnerProfile_.maxHp);
    initializeRuntimeDeck();
    initializePlayerBlackIce();
    initializeEnemyNetrunners();
    initializeDemon();
}

bool GameState::setArchitectureDefinition(const ArchitectureDefinition& definition)
{
    if (!definition.valid() || runState_ == RunState::JackedIn) return false;
    architectureDefinition_ = &definition;
    architecture_ = ArchitectureFactory::create(definition);
    initializeEnemyNetrunners();
    initializeDemon();
    runState_ = RunState::Idle;
    return true;
}

bool GameState::setRunnerProfile(const RunnerProfile& profile)
{
    if (runState_ == RunState::JackedIn || !runnerProfileValid(profile)) return false;
    runnerProfile_ = profile;
    runner_ = Netrunner(runnerProfile_.handle, runnerProfile_.interfaceRank, runnerProfile_.maxHp);
    return true;
}

bool GameState::setCyberdeckConfig(const CyberdeckConfig& config)
{
    if (runState_ == RunState::JackedIn || !cyberdeckConfigValid(config)) return false;
    cyberdeckConfig_ = config;
    initializeRuntimeDeck();
    initializePlayerBlackIce();
    return true;
}

void GameState::initializeRuntimeDeck()
{
    cyberdeck_.clear(cyberdeckConfig_.quality);
    for (uint8_t index = 0; index < cyberdeckConfig_.programCount; ++index)
    {
        const Program program = makeProgram(cyberdeckConfig_.programs[index]);
        if (!cyberdeck_.addProgram(program)) break;
    }
    for (uint8_t index = 0; index < cyberdeckConfig_.hardwareCount; ++index)
        if (!cyberdeck_.addHardware(cyberdeckConfig_.hardware[index])) break;
}

void GameState::startRun()
{
    if (architectureDefinition_ != nullptr)
        architecture_ = ArchitectureFactory::create(*architectureDefinition_);
    else
        architecture_.reset();
    // The entry floor is known as soon as a run is prepared; this is the
    // persistent visit baseline for the later Architecture Map.
    architecture_.discoverCurrentFloor();
    initializeEnemyNetrunners();
    initializePlayerBlackIce();
    initializeDemon();
    // A profile is configuration, never run state. Every run starts with a
    // newly initialized runner and freshly instantiated program runtime state.
    runner_ = Netrunner(runnerProfile_.handle, runnerProfile_.interfaceRank, runnerProfile_.maxHp);
    initializeRuntimeDeck();
    turnState_ = TurnState::Inactive;
    runState_ = RunState::Ready;
    activeBlackIceCount_ = 0;
    nextRuntimeIceId_ = 1;
    forcedNextEnemyProgram_ = ProgramId::None;
    forcedEnemyTestPhase_ = false;
    for (size_t index = 0; index < MAX_ACTIVE_BLACK_ICE; ++index)
        activeBlackIce_[index] = ActiveBlackIceSlot();
    turnPhase_ = TurnPhase::Player;
    turnNumber_ = 1;
    encounterJustTriggered_ = false;
    resolvingUnsafeJackOut_ = false;
    awaitingEndDecision_ = false;
    virusPlaced_ = false;
    architectureCompleted_ = false;
    cloakUsed_ = false;
    cloakValue_ = 0;
    temporaryRunnerStatus_ = TemporaryRunnerStatus();
    runnerOnFire_ = false;
    runnerFireTickDamage_ = 0;
    superglueRoundsRemaining_ = 0;
}

void GameState::initializeEnemyNetrunners()
{
    enemyNetrunnerCount_ = 0;
    nextEnemyRuntimeId_ = 1;
    for (size_t index = 0; index < MAX_ENEMY_NETRUNNERS; ++index)
        enemyNetrunners_[index] = EnemyNetrunnerRuntime();
    for (size_t floorIndex = 0; floorIndex < architecture_.floorCount() &&
         enemyNetrunnerCount_ < MAX_ENEMY_NETRUNNERS; ++floorIndex)
    {
        const Floor* floor = architecture_.floorAt(floorIndex);
        if (floor == nullptr || floor->enemyNetrunnerDefinition == nullptr) continue;
        EnemyNetrunnerRuntime& slot = enemyNetrunners_[enemyNetrunnerCount_];
        if (!slot.reset(*floor->enemyNetrunnerDefinition, nextEnemyRuntimeId_,
                        static_cast<uint8_t>(floorIndex)))
        {
            Serial.printf("[EnemySpawn] reject floor=%u slot=%u\n",
                static_cast<unsigned>(floorIndex + 1), static_cast<unsigned>(enemyNetrunnerCount_));
            continue;
        }
        NETRUN_VERBOSE_PRINTF("[EnemySpawn] slot=%u runtimeId=%u floor=%u active=%u hp=%u deckCount=%u\n",
            static_cast<unsigned>(enemyNetrunnerCount_), static_cast<unsigned>(slot.runtimeId),
            static_cast<unsigned>(slot.currentFloor + 1), slot.active ? 1U : 0U,
            static_cast<unsigned>(slot.runner.hp()), static_cast<unsigned>(slot.cyberdeck.programCount()));
        ++enemyNetrunnerCount_;
        ++nextEnemyRuntimeId_;
    }
}

void GameState::initializeDemon()
{
    demon_.reset(architectureDefinition_ != nullptr
        ? (architectureDefinition_->demonDefinition != nullptr ? architectureDefinition_->demonDefinition :
           demonDefinition(architectureDefinition_->demon))
        : nullptr);
}

void GameState::initializePlayerBlackIce()
{
    playerBlackIceCount_ = 0;
    nextPlayerBlackIceRuntimeId_ = 1;
    for (size_t index = 0; index < MAX_PLAYER_BLACK_ICE; ++index)
        playerBlackIce_[index] = PlayerBlackIceRuntime();
    for (uint8_t index = 0; index < cyberdeckConfig_.playerBlackIceCount; ++index)
    {
        const BlackIceDefinition* definition = blackIceDefinition(cyberdeckConfig_.playerBlackIce[index]);
        if (!playerBlackIceSupported(definition) ||
            playerBlackIceCount_ >= MAX_PLAYER_BLACK_ICE) continue;
        PlayerBlackIceRuntime& runtime = playerBlackIce_[playerBlackIceCount_++];
        runtime.definition = definition;
        runtime.instance = BlackIceInstance(*definition);
        runtime.instance.setRezzed(false);
        runtime.runtimeId = nextPlayerBlackIceRuntimeId_++;
        runtime.occupied = true;
    }
}

EnemyNetrunnerRuntime* GameState::floorEnemyNetrunner()
{
    const uint8_t floor = static_cast<uint8_t>(architecture_.currentPosition());
    for (size_t index = 0; index < enemyNetrunnerCount_; ++index)
        if (enemyNetrunners_[index].available() && enemyNetrunners_[index].currentFloor == floor)
            return &enemyNetrunners_[index];
    return nullptr;
}

const EnemyNetrunnerRuntime* GameState::floorEnemyNetrunner() const
{
    const uint8_t floor = static_cast<uint8_t>(architecture_.currentPosition());
    for (size_t index = 0; index < enemyNetrunnerCount_; ++index)
        if (enemyNetrunners_[index].available() && enemyNetrunners_[index].currentFloor == floor)
            return &enemyNetrunners_[index];
    return nullptr;
}

EnemyNetrunnerRuntime* GameState::enemyNetrunnerByRuntimeId(uint16_t runtimeId)
{
    for (size_t index = 0; index < enemyNetrunnerCount_; ++index)
        if (enemyNetrunners_[index].runtimeId == runtimeId && enemyNetrunners_[index].available())
            return &enemyNetrunners_[index];
    return nullptr;
}

const EnemyNetrunnerRuntime* GameState::enemyNetrunnerByRuntimeId(uint16_t runtimeId) const
{
    for (size_t index = 0; index < enemyNetrunnerCount_; ++index)
        if (enemyNetrunners_[index].runtimeId == runtimeId && enemyNetrunners_[index].available())
            return &enemyNetrunners_[index];
    return nullptr;
}

const EnemyNetrunnerRuntime* GameState::findEligiblePlayerBlackIceTarget(
    const PlayerBlackIceRuntime& ice) const
{
    if (!ice.occupied || !ice.instance.active()) return nullptr;
    return findPlayerBlackIcePotentialTarget(ice);
}

const EnemyNetrunnerRuntime* GameState::findPlayerBlackIcePotentialTarget(
    const PlayerBlackIceRuntime& ice) const
{
    if (!ice.occupied) return nullptr;
    const EnemyNetrunnerRuntime* locked = enemyNetrunnerByRuntimeId(ice.targetEnemyRuntimeId);
    if (locked != nullptr && locked->currentFloor == ice.chasePosition)
        return locked;

    for (size_t index = 0; index < enemyNetrunnerCount_; ++index)
    {
        const EnemyNetrunnerRuntime& candidate = enemyNetrunners_[index];
        if (candidate.available() && candidate.currentFloor == ice.chasePosition)
            return &candidate;
    }
    return nullptr;
}

const char* GameState::playerBlackIceTargetDisplayName(const PlayerBlackIceRuntime& ice) const
{
    const EnemyNetrunnerRuntime* target = findPlayerBlackIcePotentialTarget(ice);
    return target != nullptr && target->definition != nullptr ? target->definition->name : nullptr;
}

const char* GameState::enemyNetrunnerNameByRuntimeId(uint16_t runtimeId) const
{
    for (size_t index = 0; index < enemyNetrunnerCount_; ++index)
    {
        const EnemyNetrunnerRuntime& enemy = enemyNetrunners_[index];
        if (enemy.runtimeId == runtimeId && enemy.definition != nullptr)
            return enemy.definition->name;
    }
    return nullptr;
}

bool GameState::consumeEnemyPresence(uint16_t& runtimeId)
{
    for (size_t index = 0; index < enemyNetrunnerCount_; ++index)
    {
        EnemyNetrunnerRuntime& enemy = enemyNetrunners_[index];
        if (enemy.available() && enemy.presencePending)
        {
            enemy.presencePending = false;
            runtimeId = enemy.runtimeId;
            return true;
        }
    }
    runtimeId = 0;
    return false;
}

ProgramActionResult GameState::activatePlayerBlackIce(size_t index, uint16_t enemyRuntimeId)
{
    ProgramActionResult result;
    PlayerBlackIceRuntime* ice = playerBlackIceAt(index);
    EnemyNetrunnerRuntime* enemy = enemyNetrunnerByRuntimeId(enemyRuntimeId);
    if (runState_ != RunState::JackedIn || ice == nullptr || ice->instance.active() ||
        enemy == nullptr || enemy->currentFloor != architecture_.currentPosition() ||
        !runner_.spendNetAction()) return result;
    ice->instance.setRezzed(true);
    ice->instance.setPursuing(true);
    ice->targetEnemyRuntimeId = enemyRuntimeId;
    // Deployment is spatial: this ICE stays where the runner placed it.
    ice->chasePosition = static_cast<uint8_t>(architecture_.currentPosition());
    result.executed = true;
    result.remainingNetActions = runner_.remainingNetActions();
    return result;
}

ProgramActionResult GameState::deactivatePlayerBlackIce(size_t index)
{
    ProgramActionResult result;
    PlayerBlackIceRuntime* ice = playerBlackIceAt(index);
    if (runState_ != RunState::JackedIn || ice == nullptr || !ice->instance.active() ||
        !runner_.spendNetAction()) return result;
    ice->instance.setRezzed(false);
    ice->targetEnemyRuntimeId = 0;
    result.executed = true;
    result.remainingNetActions = runner_.remainingNetActions();
    return result;
}

CombatResult GameState::attackFloorEnemy(NetRules& rules, Program& program)
{
    CombatResult result;
    EnemyNetrunnerRuntime* enemy = floorEnemyNetrunner();
    if (runState_ != RunState::JackedIn || enemy == nullptr) return result;
    result = rules.programAttackNetrunner(runner_, cyberdeck_, program, enemy->runner, enemy->cyberdeck);
    applyProgramEffectToEnemy(*enemy, result);
    return result;
}

CombatResult GameState::zapFloorEnemy(NetRules& rules, uint16_t enemyRuntimeId)
{
    CombatResult result;
    EnemyNetrunnerRuntime* enemy = enemyNetrunnerByRuntimeId(enemyRuntimeId);
    if (runState_ != RunState::JackedIn || turnPhase_ != TurnPhase::Player || enemy == nullptr ||
        enemy->currentFloor != architecture_.currentPosition() || !enemy->available())
        return result;
    result = rules.zap(runner_, enemy->runner);
    applyProgramEffectToEnemy(*enemy, result);
    return result;
}

CombatResult GameState::attackFloorEnemyProgram(NetRules& rules, Program& program, size_t targetSlot)
{
    CombatResult result;
    EnemyNetrunnerRuntime* enemy = floorEnemyNetrunner();
    Program* target = enemy != nullptr ? enemy->cyberdeck.programAt(targetSlot) : nullptr;
    if (runState_ != RunState::JackedIn || target == nullptr) return result;
    return rules.swordAttackProgram(runner_, cyberdeck_, program, *target);
}

bool GameState::extinguishRunnerFire()
{
    if (!runnerOnFire_ || runState_ != RunState::JackedIn || !runner_.spendNetAction()) return false;
    runnerOnFire_ = false;
    return true;
}

uint8_t GameState::consumeRunnerFireTickDamage()
{
    const uint8_t damage = runnerFireTickDamage_;
    runnerFireTickDamage_ = 0;
    return damage;
}

bool GameState::triggerCurrentBlackIce()
{
    if (runState_ != RunState::JackedIn) return false;
    Floor* floor = architecture_.currentFloor();
    if (floor == nullptr || floor->blackIceCount == 0 || floor->blackIceTriggered ||
        activeBlackIceCount_ >= MAX_ACTIVE_BLACK_ICE)
        return false;

    const size_t count = floor->blackIceCount > 0 ? floor->blackIceCount : 1;
    if (count > MAX_BLACK_ICE_PER_FLOOR || activeBlackIceCount_ + count > MAX_ACTIVE_BLACK_ICE)
        return false;
    for (size_t iceIndex = 0; iceIndex < count; ++iceIndex)
    {
        const BlackIceType type = floor->blackIceCount > 0 ? floor->blackIceTypes[iceIndex] : floor->blackIceType;
        const BlackIceDefinition* definition = floor->blackIceCount > 0
            ? floor->blackIceDefinitions[iceIndex] : floor->blackIceDefinition;
        if (definition == nullptr) definition = blackIceDefinition(type);
        if (definition == nullptr) return false;
    }
    const uint8_t floorIndex = static_cast<uint8_t>(architecture_.currentPosition());
    for (size_t iceIndex = 0; iceIndex < count; ++iceIndex)
    {
        const BlackIceType type = floor->blackIceCount > 0 ? floor->blackIceTypes[iceIndex] : floor->blackIceType;
        const BlackIceDefinition* definition = floor->blackIceCount > 0
            ? floor->blackIceDefinitions[iceIndex] : floor->blackIceDefinition;
        if (definition == nullptr) definition = blackIceDefinition(type);
        ActiveBlackIceSlot& slot = activeBlackIce_[activeBlackIceCount_++];
        slot.instance = BlackIceInstance(*definition);
        slot.runtimeId = nextRuntimeIceId_++;
        slot.sourceFloorIndex = floorIndex;
        slot.chasePosition = floorIndex;
        slot.occupied = true;
    }
    floor->blackIceTriggered = true;
    return true;
}

PathfinderResult GameState::pathfinder(NetRules& rules)
{
    PathfinderResult result;
    if (runState_ != RunState::JackedIn || turnPhase_ != TurnPhase::Player) return result;
    result.check = rules.pathfinder(runner_, cyberdeck_);
    if (!result.check.attempted) return result;
    result.scanDepth = runner_.interfaceRank();
    uint8_t queue[MAX_ARCHITECTURE_FLOORS] = {};
    uint8_t depths[MAX_ARCHITECTURE_FLOORS] = {};
    bool queued[MAX_ARCHITECTURE_FLOORS] = {};
    const size_t floorCount = architecture_.floorCount();
    const uint8_t start = static_cast<uint8_t>(architecture_.currentPosition());
    size_t read = 0, write = 0;
    if (start >= floorCount) return result;
    queue[write++] = start; queued[start] = true;
    while (read < write)
    {
        const uint8_t current = queue[read];
        const uint8_t depth = depths[read++];
        Floor* floor = architecture_.floorAt(current);
        if (floor == nullptr) continue;
        if (!floor->discovered) { floor->discovered = true; ++result.discoveredCount; }
        const bool blocks = floor->type == FloorType::Password && !floor->resolved && floor->dv > result.check.total;
        if (blocks) { result.obstructionDetected = true; continue; }
        if (depth >= result.scanDepth) continue;
        for (size_t edge = 0; edge < architecture_.connectionCount(current); ++edge)
        {
            const uint8_t next = architecture_.connectionAt(current, edge);
            if (next >= floorCount || queued[next] || write >= MAX_ARCHITECTURE_FLOORS) continue;
            queued[next] = true; queue[write] = next; depths[write++] = static_cast<uint8_t>(depth + 1);
        }
    }
    return result;
}

NetCheckResult GameState::cloak(NetRules& rules)
{
    NetCheckResult result;
    if (runState_ != RunState::JackedIn || turnPhase_ != TurnPhase::Player) return result;
    result = rules.cloak(runner_, cyberdeck_);
    if (result.attempted) { cloakUsed_ = true; cloakValue_ = result.total; }
    return result;
}

void GameState::enterCurrentFloor(NetRules& rules, bool moved, EncounterResult& result)
{
    resetEncounterResultInPlace(result);
    result.moved = moved;
    encounterJustTriggered_ = false;
    if (!moved) return;

    const bool traceFloor2 = tracesCombatFloor2(architecture_);
    if (traceFloor2) Serial.println("[F2] 07 before enemy lookup");
    const EnemyNetrunnerRuntime* floorEnemy = floorEnemyNetrunner();
    if (traceFloor2)
    {
        Serial.printf("[F2] 08 after enemy lookup present=%u\n", floorEnemy != nullptr ? 1U : 0U);
        logFloor2Memory(8);
        Serial.println("[F2] 09 before encounter");
    }

    const unsigned floor = static_cast<unsigned>(architecture_.currentPosition() + 1);
    NETRUN_VERBOSE_PRINTF("[FloorEnter] begin target=%u\n", floor);

    const size_t activeBeforeSpawn = activeBlackIceCount_;
    const bool spawned = triggerCurrentBlackIce();
    NETRUN_VERBOSE_PRINTF("[FloorEnter] runtime-ready ice=%u enemy=%u\n",
        static_cast<unsigned>(activeBlackIceCount_), floorEnemy != nullptr ? 1U : 0U);
    if (spawned)
    {
        NETRUN_VERBOSE_PRINT("[FloorEnter] spawn-ice");
        for (size_t index = activeBeforeSpawn; index < activeBlackIceCount_; ++index)
        {
            resolveEncounterForSlot(rules, activeBlackIce_[index], result);
            if (runState_ != RunState::JackedIn) break;
        }
    }

    if (floorEnemy != nullptr) NETRUN_VERBOSE_PRINT("[FloorEnter] spawn-enemy");

    const uint8_t currentPosition = static_cast<uint8_t>(architecture_.currentPosition());
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (!slot.occupied || !slot.instance.active() || slot.instance.pursuing() ||
            slot.chasePosition != currentPosition)
            continue;
        resolveEncounterForSlot(rules, slot, result);
        if (runState_ != RunState::JackedIn) break;
    }

    if (result.iceCount > 0)
    {
        encounterJustTriggered_ = true;
        result.triggered = true;
        result.speedCheck = result.ice[0].speedCheck;
        result.immediateAttack = result.ice[0].immediateAttack;
    }
    if (traceFloor2) Serial.println("[F2] 10 after encounter");
    updateEnemyPresenceStates();
    if (traceFloor2) Serial.println("[F2] 11 floor entry end");
    NETRUN_VERBOSE_PRINT("[FloorEnter] end");
}

bool GameState::hasReachedArchitectureEnd() const
{
    const Floor* floor = architecture_.currentFloor();
    if (floor == nullptr || architecture_.floorCount() == 0 ||
        (floor->type == FloorType::Password && !floor->resolved)) return false;
    const uint8_t currentDepth = architecture_.canonicalDepth(
        static_cast<uint8_t>(architecture_.currentPosition()));
    const uint8_t maximumDepth = architecture_.maximumCanonicalDepth();
    return currentDepth != Architecture::NO_FLOOR && currentDepth == maximumDepth;
}

bool GameState::completeIfArchitectureEnd()
{
    if (runState_ != RunState::JackedIn || architectureCompleted_ || !hasReachedArchitectureEnd()) return false;
    awaitingEndDecision_ = true;
    turnState_ = TurnState::Ended;
    turnPhase_ = TurnPhase::Player;
    temporaryRunnerStatus_ = TemporaryRunnerStatus();
    return true;
}

bool GameState::finishArchitectureEnd(bool virusPlaced)
{
    if (runState_ != RunState::JackedIn || !awaitingEndDecision_ || architectureCompleted_ || !virusPlaced) return false;
    virusPlaced_ = true;
    architectureCompleted_ = true;
    awaitingEndDecision_ = false;
    // Completion is an Architecture state, not an automatic disconnect. Begin
    // a normal Player turn so the runner can continue exploring or Jack Out.
    startTurn();
    return true;
}

bool GameState::markRunnerDown()
{
    if (runner_.hp() != 0 || runState_ != RunState::JackedIn) return false;
    runState_ = RunState::RunnerDown;
    turnState_ = TurnState::Ended;
    turnPhase_ = TurnPhase::Player;
    awaitingEndDecision_ = false;
    temporaryRunnerStatus_ = TemporaryRunnerStatus();
    return true;
}

void GameState::resolveEncounterForSlot(NetRules& rules, ActiveBlackIceSlot& slot,
                                        EncounterResult& result)
{
    if (result.iceCount >= MAX_ACTIVE_BLACK_ICE) return;
    EncounterResult::IceEncounter& encounter = result.ice[result.iceCount++];
    encounter.runtimeId = slot.runtimeId;
    encounter.newlyEncountered = !slot.instance.encounteredThisRun();
    slot.instance.markEncountered();
    const int speedBonus = cyberdeck_.findUsableProgram(ProgramId::SpeedyGonzalvez) != nullptr ? 2 : 0;
    encounter.speedCheck = rules.encounterSpeedCheck(runner_, slot.instance, speedBonus);
    // A re-encounter re-engages the existing instance; no definition data is copied.
    slot.instance.setPursuing(true);
    slot.chasePosition = static_cast<uint8_t>(architecture_.currentPosition());
    if (encounter.speedCheck.iceWins)
    {
        encounter.immediateAttack = rules.blackIceAttack(slot.instance, runner_, &cyberdeck_);
        encounter.immediateAttack.attackerRuntimeId = slot.runtimeId;
        applyTemporaryRunEffect(encounter.immediateAttack, &slot);
        if (markRunnerDown())
        {
            result.runTerminated = true;
        }
        else if (encounter.immediateAttack.forcedUnsafeJackOut)
        {
            resolveForcedUnsafeJackOut(rules, slot.runtimeId, nullptr, &result);
        }
    }
}

EncounterResult GameState::moveForward(NetRules& rules)
{
    EncounterResult result;
    moveForward(rules, result);
    return result;
}

void GameState::moveForward(NetRules& rules, EncounterResult& result)
{
    moveToConnectedFloor(rules, static_cast<uint8_t>(architecture_.currentPosition() + 1), result);
}

EncounterResult GameState::moveBackward(NetRules& rules)
{
    EncounterResult result;
    moveBackward(rules, result);
    return result;
}

void GameState::moveBackward(NetRules& rules, EncounterResult& result)
{
    if (architecture_.currentPosition() == 0) { resetEncounterResultInPlace(result); return; }
    moveToConnectedFloor(rules, static_cast<uint8_t>(architecture_.currentPosition() - 1), result);
}

EncounterResult GameState::moveToConnectedFloor(NetRules& rules, uint8_t destination)
{
    EncounterResult result;
    moveToConnectedFloor(rules, destination, result);
    return result;
}

void GameState::moveToConnectedFloor(NetRules& rules, uint8_t destination, EncounterResult& result)
{
    resetEncounterResultInPlace(result);
    if (runState_ != RunState::JackedIn) return;
    // Preserve existing deeper-navigation locks while allowing a connected
    // return route. Branches themselves do not add a new movement rule.
    if (destination > architecture_.currentPosition() &&
        (movementBlocked() || !canProgressDeeper())) return;
    const bool moved = architecture_.moveTo(destination);
    for (size_t index = 0; moved && index < activeBlackIceCount_; ++index)
        if (activeBlackIce_[index].occupied && activeBlackIce_[index].instance.pursuing())
            activeBlackIce_[index].chasePosition = static_cast<uint8_t>(architecture_.currentPosition());
    enterCurrentFloor(rules, moved, result);
}

bool GameState::movementBlocked() const
{
    const Floor* floor = architecture_.currentFloor();
    return floor != nullptr && floor->type == FloorType::Password && !floor->resolved;
}

bool GameState::jackIn()
{
    if (runState_ != RunState::Ready) return false;
    runState_ = RunState::JackedIn;
    architecture_.discoverCurrentFloor();
    // The initial floor is an entry as well; spawn its Architecture ICE so
    // single-floor and Floor-ID-0 scenarios cannot bypass hostile content.
    triggerCurrentBlackIce();
    startTurn();
    return true;
}

bool GameState::jackOut()
{
    if (!canSafeJackOut() || !runner_.spendNetAction()) return false;
    endTurn();
    runState_ = RunState::JackedOut;
    temporaryRunnerStatus_ = TemporaryRunnerStatus();
    return true;
}

bool GameState::unsafeJackOut(NetRules& rules, EncounterResult& result)
{
    if (runState_ != RunState::JackedIn || canSafeJackOut()) return false;
    result = EncounterResult();
    // Runtime id 0 is reserved here as the voluntary trigger: the existing
    // central resolver then applies all encountered Architecture-ICE exit
    // effects exactly as it does for Giant/DeckKRASH forced exits.
    resolveForcedUnsafeJackOut(rules, 0, nullptr, &result);
    // Lethal exit effects complete the unsafe-exit resolution as Runner Down;
    // treating this as cancellation incorrectly returned the UI to Floor.
    return runState_ == RunState::JackedOut || runState_ == RunState::RunnerDown;
}

bool GameState::startTurn()
{
    if (runState_ != RunState::JackedIn) return false;
    runner_.resetTurn(netActionsForCurrentTurn());
    cyberdeck_.resetRoundFlags();
    turnState_ = TurnState::Active;
    turnPhase_ = TurnPhase::Player;
    return true;
}

void GameState::endTurn()
{
    if (turnState_ == TurnState::Active) turnState_ = TurnState::Ended;
}

void GameState::endPlayerTurn()
{
    if (runState_ != RunState::JackedIn || turnPhase_ != TurnPhase::Player) return;
    turnState_ = TurnState::Ended;
    if (runnerOnFire_)
    {
        runnerFireTickDamage_ = 2;
        runner_.takeDamage(runnerFireTickDamage_);
        if (markRunnerDown()) return;
    }
    if (superglueRoundsRemaining_ > 0) --superglueRoundsRemaining_;
    expireTemporaryRunnerStatuses();
    // Enemies on other floors still receive a phase to move one step towards
    // the player. This does not consume a NET Action.
    turnPhase_ = demon_.active ? TurnPhase::Demon : hasPursuingPlayerBlackIce() ? TurnPhase::PlayerIce :
        (enemyNetrunnerCount_ > 0 ? TurnPhase::Enemy : TurnPhase::Ice);
}

bool GameState::updatePlayerTurn()
{
    if (turnPhase_ == TurnPhase::Player && runner_.remainingNetActions() == 0)
    {
        endPlayerTurn();
        return true;
    }
    return false;
}

IcePhaseResult GameState::runIcePhase(NetRules& rules)
{
    IcePhaseResult result;
    runIcePhase(rules, result);
    return result;
}

EnemyPhaseResult GameState::runEnemyPhase(NetRules& rules)
{
    EnemyPhaseResult result;
    runEnemyPhase(rules, result);
    return result;
}

void GameState::applyProgramEffectToRunner(const CombatResult& result)
{
    if (!result.success) return;
    if (result.fireApplied) runnerOnFire_ = true;
    if (result.superglueRounds > superglueRoundsRemaining_)
        superglueRoundsRemaining_ = result.superglueRounds;
    if (result.nextTurnNetActionPenalty > 0)
    {
        temporaryRunnerStatus_.netActionPenaltyTurn = turnNumber_ + 1;
        temporaryRunnerStatus_.netActionPenaltyAmount = result.nextTurnNetActionPenalty;
        temporaryRunnerStatus_.netActionMinimum = 2;
    }
    if (result.forcedUnsafeJackOut && !result.krashBarrierBlocked)
        runState_ = RunState::JackedOut;
}

void GameState::applyProgramEffectToEnemy(EnemyNetrunnerRuntime& enemy, const CombatResult& result)
{
    if (!result.success) return;
    if (result.fireApplied) enemy.onFire = true;
    if (result.superglueRounds > enemy.superglueRoundsRemaining)
        enemy.superglueRoundsRemaining = result.superglueRounds;
    if (result.nextTurnNetActionPenalty > 0) enemy.nextTurnNetActionPenalty = result.nextTurnNetActionPenalty;
    // Black ICE uses the shared temporary-effect descriptor. Mirror Wisp's
    // existing next-turn action penalty into the Enemy runtime instead of
    // leaving a Player-only result flag behind.
    if (result.temporaryRunEffect == TemporaryRunEffect::NextTurnNetActionPenalty &&
        result.attackerDefinition != nullptr)
        enemy.nextTurnNetActionPenalty = result.attackerDefinition->effect.netActionPenalty;
    if (result.forcedUnsafeJackOut && !result.krashBarrierBlocked) enemy.unsafeJackOut();
    if (enemy.runner.hp() == 0) enemy.markRunnerDown();
    if (!enemy.available()) clearPlayerBlackIceTargets(enemy.runtimeId);
}

bool GameState::hasPursuingPlayerBlackIce() const
{
    for (size_t index = 0; index < playerBlackIceCount_; ++index)
    {
        const PlayerBlackIceRuntime& ice = playerBlackIce_[index];
        if (!ice.occupied || !ice.instance.active()) continue;
        if (findEligiblePlayerBlackIceTarget(ice) != nullptr) return true;
    }
    return false;
}

void GameState::clearPlayerBlackIceTargets(uint16_t enemyRuntimeId)
{
    for (size_t index = 0; index < playerBlackIceCount_; ++index)
    {
        PlayerBlackIceRuntime& ice = playerBlackIce_[index];
        if (ice.occupied && ice.targetEnemyRuntimeId == enemyRuntimeId)
        {
            ice.targetEnemyRuntimeId = 0;
            ice.instance.stopPursuing();
        }
    }
}

void GameState::runPlayerBlackIcePhase(NetRules& rules, IcePhaseResult& result)
{
    resetIcePhaseResultInPlace(result);
    bool tracesWisp = false;
    for (size_t index = 0; index < playerBlackIceCount_; ++index)
    {
        const PlayerBlackIceRuntime& ice = playerBlackIce_[index];
        if (ice.occupied && ice.definition != nullptr &&
            ice.definition->effect.type == BlackIceEffectType::DamageAndNextTurnNetActionPenalty)
        { tracesWisp = true; break; }
    }
    if (tracesWisp && NETRUN_DEBUG_VERBOSE)
        Serial.printf("[Diag][PlayerIceWisp][ENTRY] phase=%u expected=%u this=%p\n",
            static_cast<unsigned>(turnPhase_), static_cast<unsigned>(TurnPhase::PlayerIce), this);
    if (runState_ != RunState::JackedIn || turnPhase_ != TurnPhase::PlayerIce) return;
    result.executed = true;
    for (size_t index = 0; index < playerBlackIceCount_ && result.attackCount < MAX_ACTIVE_BLACK_ICE; ++index)
    {
        PlayerBlackIceRuntime& ice = playerBlackIce_[index];
        if (!ice.occupied || !ice.instance.active()) continue;
        // An Enemy can successfully slide a Player ICE during its phase,
        // which intentionally clears that ICE's lock and pursuit state. A
        // rezzed ICE may reacquire a live co-located Enemy on its own floor
        // next round; it never reacquires a remote or terminal target.
        const EnemyNetrunnerRuntime* eligibleTarget = findEligiblePlayerBlackIceTarget(ice);
        if (eligibleTarget == nullptr) continue;
        if (ice.targetEnemyRuntimeId != eligibleTarget->runtimeId || !ice.instance.pursuing())
        {
            ice.targetEnemyRuntimeId = eligibleTarget->runtimeId;
            ice.instance.setPursuing(true);
        }
        EnemyNetrunnerRuntime* target = enemyNetrunnerByRuntimeId(ice.targetEnemyRuntimeId);
        if (target == nullptr) { clearPlayerBlackIceTargets(ice.targetEnemyRuntimeId); continue; }
        // A deployed Player ICE only attacks actors on its own floor. It does
        // not follow a remote enemy through the Architecture.
        if (target->currentFloor != ice.chasePosition) continue;
        CombatResult attack = rules.blackIceAttack(ice.instance, target->runner, &target->cyberdeck);
        attack.attackerRuntimeId = ice.runtimeId;
        result.attacks[result.attackCount].targetEnemyRuntimeId = target->runtimeId;
        applyProgramEffectToEnemy(*target, attack);
        result.attacks[result.attackCount].runtimeId = ice.runtimeId;
        result.attacks[result.attackCount++].result = attack;
        result.attack = attack;
        if (attack.executed) ++result.actionCount;
    }
    turnPhase_ = enemyNetrunnerCount_ > 0 ? TurnPhase::Enemy : TurnPhase::Ice;
}

void GameState::runDemonPhase(NetRules& rules, DemonPhaseResult& result)
{
    result = DemonPhaseResult();
    if (runState_ != RunState::JackedIn || turnPhase_ != TurnPhase::Demon || !demon_.active) return;
    result.executed = true;
    // Demon control is deterministic and Architecture-wide: the first node not
    // owned by the runner is defended before combat is considered.
    for (size_t index = 0; index < architecture_.floorCount(); ++index)
    {
        Floor* floor = architecture_.floorAt(index);
        if (floor != nullptr && floor->type == FloorType::ControlNode && floor->controlOwner == ControlOwner::Runner)
        {
            floor->controlOwner = ControlOwner::Demon;
            floor->controlled = false;
            result.controlledNode = true;
            result.controlledNodeFloor = static_cast<uint8_t>(index);
            result.actions[result.actionCount].type = DemonPhaseResult::ActionType::ControlReclaim;
            result.actions[result.actionCount].controlledNodeFloor = static_cast<uint8_t>(index);
            ++result.actionCount;
            break;
        }
    }
    while (result.actionCount < demon_.definition->netActions && runState_ == RunState::JackedIn)
    {
        result.zap = rules.demonZap(demon_, runner_, cyberdeck_);
        result.actions[result.actionCount].type = DemonPhaseResult::ActionType::Zap;
        result.actions[result.actionCount].attack = result.zap;
        ++result.actionCount;
        if (runner_.hp() == 0) markRunnerDown();
    }
    if (runState_ == RunState::JackedIn)
        turnPhase_ = hasPursuingPlayerBlackIce() ? TurnPhase::PlayerIce :
            (enemyNetrunnerCount_ > 0 ? TurnPhase::Enemy : TurnPhase::Ice);
}

bool GameState::enemyCanSlidePlayerBlackIce(const EnemyNetrunnerRuntime& enemy) const
{
    for (size_t index = 0; index < playerBlackIceCount_; ++index)
    {
        const PlayerBlackIceRuntime& ice = playerBlackIce_[index];
        if (ice.occupied && ice.instance.active() && ice.instance.pursuing() &&
            ice.targetEnemyRuntimeId == enemy.runtimeId && ice.chasePosition == enemy.currentFloor)
            return true;
    }
    return false;
}

void GameState::runEnemyAi(NetRules& rules, EnemyNetrunnerRuntime& enemy, EnemyPhaseResult& result)
{
    // Enemy AI has no remote-combat mode: every action must begin with a
    // live, co-located runner. This guards all callers, not only the normal
    // Enemy-phase movement path.
    if (!enemy.available() || runState_ != RunState::JackedIn ||
        enemy.currentFloor != architecture_.currentPosition()) return;
    const bool traceFloor2 = tracesCombatFloor2(architecture_);
    const EnemyBehaviorDecision slideDecision = chooseEnemyBehaviorDecision(enemy, ProgramId::None,
        enemyCanSlidePlayerBlackIce(enemy), true);
    CombatResult slide;
    if (slideDecision.type == EnemyDecisionType::SlidePlayerBlackIce &&
        enemySlidePlayerBlackIce(rules, enemy, slide) &&
        result.actionCount < MAX_ENEMY_NETRUNNERS * 3)
    {
        result.actions[result.actionCount].runtimeId = enemy.runtimeId;
        result.actions[result.actionCount++].result = slide;
    }
    while (enemy.available() && enemy.runner.remainingNetActions() > 0 &&
           result.actionCount < MAX_ENEMY_NETRUNNERS * 3 && runState_ == RunState::JackedIn)
    {
        if (traceFloor2)
            Serial.printf("[F2AI] 04 action loop remaining=%u results=%u\n",
                static_cast<unsigned>(enemy.runner.remainingNetActions()),
                static_cast<unsigned>(result.actionCount));
        const ProgramId forcedProgram = forcedNextEnemyProgram_;
        forcedNextEnemyProgram_ = ProgramId::None;
        const EnemyBehaviorDecision decision = chooseEnemyBehaviorDecision(enemy, forcedProgram, false, true);
        if (decision.type == EnemyDecisionType::None) break;
        Program* selected = nullptr;
        for (size_t slot = 0; slot < enemy.cyberdeck.programCount(); ++slot)
        {
            Program* candidate = enemy.cyberdeck.programAt(slot);
            if (candidate != nullptr && candidate->id() == decision.program)
            { selected = candidate; break; }
        }
        if (selected == nullptr) break;
        if (decision.type == EnemyDecisionType::ActivateProgram)
        {
            if (traceFloor2) Serial.println("[F2AI] 05 no rezzed candidate; activation lookup");
            if (!rules.activateProgram(enemy.runner, enemy.cyberdeck, *selected).executed) break;
            continue;
        }
        if (decision.type != EnemyDecisionType::UseProgram) break;
        if (traceFloor2)
            Serial.printf("[F2AI] 06 before program attack id=%u\n",
                static_cast<unsigned>(selected->id()));
        CombatResult attack = rules.programAttackNetrunner(enemy.runner, enemy.cyberdeck,
            *selected, runner_, cyberdeck_);
        if (traceFloor2)
            Serial.printf("[F2AI] 07 after program attack executed=%u hit=%u\n",
                attack.executed ? 1U : 0U, attack.success ? 1U : 0U);
        if (attack.executed)
        {
            result.actions[result.actionCount].runtimeId = enemy.runtimeId;
            result.actions[result.actionCount++].result = attack;
        }
        applyProgramEffectToRunner(attack);
        if (markRunnerDown() || runState_ != RunState::JackedIn) { result.runTerminated = true; break; }
    }
}

bool GameState::enemySlidePlayerBlackIce(NetRules& rules, EnemyNetrunnerRuntime& enemy,
                                         CombatResult& result)
{
    for (size_t index = 0; index < playerBlackIceCount_; ++index)
    {
        PlayerBlackIceRuntime& ice = playerBlackIce_[index];
        if (!ice.occupied || !ice.instance.active() || !ice.instance.pursuing() ||
            ice.targetEnemyRuntimeId != enemy.runtimeId || ice.chasePosition != enemy.currentFloor) continue;
        result = rules.slide(enemy.runner, ice.instance);
        // A successful Enemy slide breaks this stationary Player-ICE lock.
        // Keep the ICE rezzed on its floor, but clear both its target and
        // pursuit state through the shared target-lifecycle path.
        if (result.executed && result.success) clearPlayerBlackIceTargets(enemy.runtimeId);
        return result.executed;
    }
    return false;
}

void GameState::runEnemyPhase(NetRules& rules, EnemyPhaseResult& result)
{
    resetEnemyPhaseResultInPlace(result);
    if (hasPursuingPlayerBlackIce() && NETRUN_DEBUG_VERBOSE)
        Serial.printf("[Diag][PlayerIceSlide][ENTRY] phase=%u expected=%u this=%p\n",
            static_cast<unsigned>(turnPhase_), static_cast<unsigned>(TurnPhase::Enemy), this);
    if (runState_ != RunState::JackedIn || turnPhase_ != TurnPhase::Enemy) return;
    const bool traceFloor2 = tracesCombatFloor2(architecture_);
    if (traceFloor2) Serial.println("[F2AI] 02 phase accepted; before runtime lookup");
    result.executed = true;
    if (traceFloor2)
        Serial.printf("[F2AI] 03 after runtime lookup present=%u\n", enemyNetrunnerCount_ > 0 ? 1U : 0U);
    for (size_t index = 0; index < enemyNetrunnerCount_; ++index)
    {
        EnemyNetrunnerRuntime& enemy = enemyNetrunners_[index];
        // A prior terminal transition (for example unsafe Jack Out) may have
        // happened outside this phase. Keep Player-ICE target lifecycle
        // consistent before skipping that inactive runtime.
        if (!enemy.available()) { clearPlayerBlackIceTargets(enemy.runtimeId); continue; }
        if (enemy.definition != nullptr && enemy.definition->stationaryUntilDiscovered &&
            !enemy.discoveredByPlayer) continue;
        const bool coLocated = enemy.currentFloor == architecture_.currentPosition();
        const uint8_t pendingBeforeConsume = enemy.nextTurnNetActionPenalty;
        const uint8_t actions = forcedEnemyTestPhase_ ? 1 : Netrunner::netActionsAfterPenalty(
            enemy.definition->netActions, pendingBeforeConsume, 2);
        enemy.nextTurnNetActionPenalty = 0;
        enemy.runner.resetTurn(actions);
        bool tracesWisp = false;
        for (size_t iceIndex = 0; iceIndex < playerBlackIceCount_; ++iceIndex)
        {
            const PlayerBlackIceRuntime& ice = playerBlackIce_[iceIndex];
            if (ice.occupied && ice.definition != nullptr &&
                ice.definition->effect.type == BlackIceEffectType::DamageAndNextTurnNetActionPenalty &&
                ice.targetEnemyRuntimeId == enemy.runtimeId)
            { tracesWisp = true; break; }
        }
        if (tracesWisp && NETRUN_DEBUG_VERBOSE)
            Serial.printf("[Diag][PlayerBlackIceCombat][Wisp][EnemyPhaseStart] pendingBeforeConsume=%u baseActions=%u actionsAfterPenalty=%u pendingAfterConsume=%u\n",
                static_cast<unsigned>(pendingBeforeConsume), static_cast<unsigned>(enemy.definition->netActions),
                static_cast<unsigned>(actions), static_cast<unsigned>(enemy.nextTurnNetActionPenalty));
        enemy.cyberdeck.resetRoundFlags();
        if (coLocated) runEnemyAi(rules, enemy, result);
        if (enemy.available() && enemy.onFire)
        {
            enemy.runner.takeDamage(2);
            if (enemy.runner.hp() == 0) enemy.markRunnerDown();
        }
        if (enemy.superglueRoundsRemaining > 0) --enemy.superglueRoundsRemaining;
        if (enemy.available() && !coLocated &&
            chooseEnemyBehaviorDecision(enemy, ProgramId::None, false, false).type ==
                EnemyDecisionType::MoveTowardRunner)
        {
            moveEnemyTowardRunner(rules, enemy, result);
            // Contact generated by this one-step movement is still local and
            // uses the established Enemy-Netrunner behaviour.
            if (enemy.available() && enemy.currentFloor == architecture_.currentPosition() &&
                result.actionCount < MAX_ENEMY_NETRUNNERS * 3)
            {
                // Mark the contact transition before producing the local
                // combat result. The UI presents this pending signal before
                // it displays the queue returned by this Enemy phase.
                updateEnemyPresenceStates();
                runEnemyAi(rules, enemy, result);
            }
        }
    }
    updateEnemyPresenceStates();
    if (!result.runTerminated && runState_ == RunState::JackedIn) turnPhase_ = TurnPhase::Ice;
}

bool GameState::runEnemyProgramTest(NetRules& rules, ProgramId id, EnemyPhaseResult& result)
{
    if (runState_ != RunState::JackedIn || turnPhase_ != TurnPhase::Player ||
        id == ProgramId::None || id == ProgramId::Count)
    {
        NETRUN_VERBOSE_PRINTF("[EnemyAnimTest] FAIL state=%u phase=%u id=%u\n",
            static_cast<unsigned>(runState_), static_cast<unsigned>(turnPhase_), static_cast<unsigned>(id));
        return false;
    }
    EnemyNetrunnerRuntime* enemy = floorEnemyNetrunner();
    // The harness owns this disposable state. Make co-location and discovery
    // explicit instead of relying on a scenario navigation side effect.
    if (enemy == nullptr)
    {
        const uint8_t currentFloor = static_cast<uint8_t>(architecture_.currentPosition());
        for (size_t index = 0; index < enemyNetrunnerCount_; ++index)
        {
            EnemyNetrunnerRuntime& candidate = enemyNetrunners_[index];
            if (!candidate.available()) continue;
            candidate.currentFloor = currentFloor;
            candidate.discoveredByPlayer = true;
            enemy = &candidate;
            break;
        }
    }
    if (enemy == nullptr)
    {
        NETRUN_VERBOSE_PRINTF("[EnemyAnimTest] FAIL no active enemy count=%u floor=%u\n",
            static_cast<unsigned>(enemyNetrunnerCount_), static_cast<unsigned>(architecture_.currentPosition()));
        return false;
    }
    enemy->currentFloor = static_cast<uint8_t>(architecture_.currentPosition());
    enemy->discoveredByPlayer = true;
    NETRUN_VERBOSE_PRINTF("[EnemyAnimTest] SANDBOX enemy=%u floor=%u program=%u\n",
        static_cast<unsigned>(enemy->runtimeId), static_cast<unsigned>(enemy->currentFloor),
        static_cast<unsigned>(id));
    enemy->cyberdeck.clear(CyberdeckQuality::Standard);
    if (!enemy->cyberdeck.addProgram(makeProgram(id)))
    {
        NETRUN_VERBOSE_PRINTF("[EnemyAnimTest] FAIL program install id=%u\n", static_cast<unsigned>(id));
        return false;
    }
    Program* testProgram = enemy->cyberdeck.programAt(0);
    if (testProgram == nullptr) return false;
    testProgram->setStatus(ProgramStatus::Rezzed);
    if (id == ProgramId::DeckKRASH)
    {
        cyberdeck_.clear(CyberdeckQuality::Standard);
        cyberdeck_.addHardware(HardwareId::KrashBarrier);
    }
    enemy->runner.resetTurn(1);
    forcedNextEnemyProgram_ = id;
    forcedEnemyTestPhase_ = true;
    turnPhase_ = TurnPhase::Enemy;
    runEnemyPhase(rules, result);
    forcedEnemyTestPhase_ = false;
    NETRUN_VERBOSE_PRINTF("[EnemyAnimTest] RESULT executed=%u actions=%u success=%u source=%u\n",
        result.actionCount > 0 ? 1U : 0U, static_cast<unsigned>(result.actionCount),
        result.actionCount > 0 ? (result.actions[0].result.success ? 1U : 0U) : 0U,
        result.actionCount > 0 ? static_cast<unsigned>(result.actions[0].result.attackerProgram) : 0U);
    return result.actionCount > 0;
}

bool GameState::enemyMovementBlocked(uint8_t from, uint8_t to) const
{
    const Floor* source = architecture_.floorAt(from);
    const Floor* destination = architecture_.floorAt(to);
    return (source != nullptr && source->type == FloorType::Password && !source->resolved) ||
           (destination != nullptr && destination->type == FloorType::Password && !destination->resolved);
}

void GameState::moveEnemyTowardRunner(NetRules& rules, EnemyNetrunnerRuntime& enemy,
                                      EnemyPhaseResult& result)
{
    const uint8_t runnerFloor = static_cast<uint8_t>(architecture_.currentPosition());
    if (enemy.currentFloor == runnerFloor) return;
    const uint8_t destination = architecture_.nextStepToward(enemy.currentFloor, runnerFloor);
    // An invalid or disconnected graph is safe at runtime: the Enemy simply
    // cannot advance this phase.
    if (destination == Architecture::NO_FLOOR) return;
    // Superglue's existing movement restriction is directionally deeper.
    if (destination > enemy.currentFloor && enemy.superglueRoundsRemaining > 0) return;
    if (enemyMovementBlocked(enemy.currentFloor, destination)) return;
    // Guttermesh uses the same bounded Netrunner move status as its hostile
    // form. A reduced Enemy move prevents this phase's one-floor chase.
    if (enemy.runner.move() < enemy.runner.baseMove()) return;

    enemy.currentFloor = destination;
    // Runtime-array order is the existing stable Player-ICE order. Resolve
    // the first deployed ICE on the entered floor, then stop this movement.
    for (size_t index = 0; index < playerBlackIceCount_; ++index)
    {
        PlayerBlackIceRuntime& ice = playerBlackIce_[index];
        if (!ice.occupied || !ice.instance.active() || !ice.instance.pursuing() ||
            ice.chasePosition != destination) continue;
        CombatResult encounter = rules.blackIceAttack(ice.instance, enemy.runner, &enemy.cyberdeck);
        encounter.attackerRuntimeId = ice.runtimeId;
        applyProgramEffectToEnemy(enemy, encounter);
        if (result.actionCount < MAX_ENEMY_NETRUNNERS * 3)
        {
            result.actions[result.actionCount].runtimeId = ice.runtimeId;
            result.actions[result.actionCount].targetEnemyRuntimeId = enemy.runtimeId;
            result.actions[result.actionCount++].result = encounter;
            result.actions[result.actionCount - 1].fromPlayerBlackIce = true;
        }
        if (!enemy.available()) clearPlayerBlackIceTargets(enemy.runtimeId);
        return;
    }
}

void GameState::updateEnemyPresenceStates()
{
    const uint8_t runnerFloor = static_cast<uint8_t>(architecture_.currentPosition());
    for (size_t index = 0; index < enemyNetrunnerCount_; ++index)
    {
        EnemyNetrunnerRuntime& enemy = enemyNetrunners_[index];
        const bool coLocated = enemy.available() && enemy.currentFloor == runnerFloor;
        if (coLocated)
        {
            enemy.discoveredByPlayer = true;
            if (!enemy.coLocatedWithPlayer) enemy.presencePending = true;
        }
        enemy.coLocatedWithPlayer = coLocated;
    }
}

void GameState::runIcePhase(NetRules& rules, IcePhaseResult& result)
{
    resetIcePhaseResultInPlace(result);
    if (runState_ != RunState::JackedIn || turnPhase_ != TurnPhase::Ice) return;

    result.executed = true;
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (!slot.occupied || !slot.instance.active() || !slot.instance.pursuing()) continue;
        CombatResult attack = rules.blackIceAttack(slot.instance, runner_, &cyberdeck_);
        attack.attackerRuntimeId = slot.runtimeId;
        applyTemporaryRunEffect(attack, &slot);
        result.attacks[result.attackCount].runtimeId = slot.runtimeId;
        result.attacks[result.attackCount].result = attack;
        ++result.attackCount;
        result.attack = attack;
        if (attack.executed) ++result.actionCount;
        if (markRunnerDown())
        {
            result.runTerminated = true;
            break;
        }
        if (attack.forcedUnsafeJackOut)
        {
            resolveForcedUnsafeJackOut(rules, slot.runtimeId, &result, nullptr);
            result.runTerminated = true;
            break;
        }
        if (runner_.hp() == 0) break;
    }

    if (!result.runTerminated)
    {
        ++turnNumber_;
        startTurn();
    }
}

void GameState::resolveForcedUnsafeJackOut(
    NetRules& rules, uint16_t triggeringRuntimeId, IcePhaseResult* phaseResult,
    EncounterResult* encounterResult)
{
    if (runState_ == RunState::RunnerDown) return;
    if (resolvingUnsafeJackOut_) return;
    resolvingUnsafeJackOut_ = true;
    bool runnerDown = false;
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (!slot.occupied || slot.runtimeId == triggeringRuntimeId ||
            !slot.instance.active() || !slot.instance.encounteredThisRun()) continue;
        CombatResult effect = rules.resolveBlackIceEffect(slot.instance, runner_, &cyberdeck_);
        effect.attackerRuntimeId = slot.runtimeId;
        if (phaseResult != nullptr && phaseResult->attackCount < MAX_ACTIVE_BLACK_ICE)
        {
            phaseResult->attacks[phaseResult->attackCount].runtimeId = slot.runtimeId;
            phaseResult->attacks[phaseResult->attackCount].unsafeExitEffect = true;
            phaseResult->attacks[phaseResult->attackCount].result = effect;
            ++phaseResult->attackCount;
        }
        if (encounterResult != nullptr && encounterResult->exitEffectCount < MAX_ACTIVE_BLACK_ICE)
            encounterResult->exitEffects[encounterResult->exitEffectCount++] = effect;
        if (markRunnerDown())
        {
            runnerDown = true;
            break;
        }
    }
    if (!runnerDown)
    {
        runState_ = RunState::JackedOut;
        turnState_ = TurnState::Ended;
        turnPhase_ = TurnPhase::Player;
        temporaryRunnerStatus_ = TemporaryRunnerStatus();
    }
    encounterJustTriggered_ = false;
    resolvingUnsafeJackOut_ = false;
    if (encounterResult != nullptr) encounterResult->runTerminated = true;
    if (phaseResult != nullptr) phaseResult->runTerminated = true;
}

size_t GameState::engagedBlackIceCount() const
{
    size_t count = 0;
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
        if (activeBlackIce_[index].occupied && activeBlackIce_[index].instance.active() &&
            activeBlackIce_[index].instance.pursuing()) ++count;
    return count;
}

BlackIceInstance* GameState::engagedBlackIceAt(size_t engagedIndex)
{
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (!slot.occupied || !slot.instance.active() || !slot.instance.pursuing()) continue;
        if (engagedIndex-- == 0) return &slot.instance;
    }
    return nullptr;
}

const BlackIceInstance* GameState::engagedBlackIceAt(size_t engagedIndex) const
{
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        const ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (!slot.occupied || !slot.instance.active() || !slot.instance.pursuing()) continue;
        if (engagedIndex-- == 0) return &slot.instance;
    }
    return nullptr;
}

uint16_t GameState::engagedBlackIceRuntimeId(size_t engagedIndex) const
{
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        const ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (!slot.occupied || !slot.instance.active() || !slot.instance.pursuing()) continue;
        if (engagedIndex-- == 0) return slot.runtimeId;
    }
    return 0;
}

BlackIceInstance* GameState::engagedBlackIceByRuntimeId(uint16_t runtimeId)
{
    if (runtimeId == 0) return nullptr;
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (slot.occupied && slot.runtimeId == runtimeId && slot.instance.active() &&
            slot.instance.pursuing()) return &slot.instance;
    }
    return nullptr;
}

const BlackIceInstance* GameState::engagedBlackIceByRuntimeId(uint16_t runtimeId) const
{
    if (runtimeId == 0) return nullptr;
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        const ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (slot.occupied && slot.runtimeId == runtimeId && slot.instance.active() &&
            slot.instance.pursuing()) return &slot.instance;
    }
    return nullptr;
}

uint16_t GameState::nextEngagedBlackIceRuntimeId(uint16_t currentRuntimeId, int direction) const
{
    const size_t count = engagedBlackIceCount();
    if (count == 0) return 0;
    size_t current = 0;
    for (; current < count; ++current)
        if (engagedBlackIceRuntimeId(current) == currentRuntimeId) break;
    if (current == count) return engagedBlackIceRuntimeId(0);
    const int next = (static_cast<int>(current) + static_cast<int>(count) + direction) %
        static_cast<int>(count);
    return engagedBlackIceRuntimeId(static_cast<size_t>(next));
}

size_t GameState::floorBlackIceCount() const
{
    const uint8_t currentPosition = static_cast<uint8_t>(architecture_.currentPosition());
    size_t count = 0;
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
        if (activeBlackIce_[index].occupied && activeBlackIce_[index].chasePosition == currentPosition)
            ++count;
    return count;
}

BlackIceInstance* GameState::floorBlackIceAt(size_t rosterIndex)
{
    const uint8_t currentPosition = static_cast<uint8_t>(architecture_.currentPosition());
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (!slot.occupied || slot.chasePosition != currentPosition) continue;
        if (rosterIndex-- == 0) return &slot.instance;
    }
    return nullptr;
}

const BlackIceInstance* GameState::floorBlackIceAt(size_t rosterIndex) const
{
    const uint8_t currentPosition = static_cast<uint8_t>(architecture_.currentPosition());
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        const ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (!slot.occupied || slot.chasePosition != currentPosition) continue;
        if (rosterIndex-- == 0) return &slot.instance;
    }
    return nullptr;
}

uint16_t GameState::floorBlackIceRuntimeId(size_t rosterIndex) const
{
    const uint8_t currentPosition = static_cast<uint8_t>(architecture_.currentPosition());
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        const ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (!slot.occupied || slot.chasePosition != currentPosition) continue;
        if (rosterIndex-- == 0) return slot.runtimeId;
    }
    return 0;
}

BlackIceInstance* GameState::floorBlackIceByRuntimeId(uint16_t runtimeId)
{
    if (runtimeId == 0) return nullptr;
    const uint8_t currentPosition = static_cast<uint8_t>(architecture_.currentPosition());
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (slot.occupied && slot.runtimeId == runtimeId && slot.chasePosition == currentPosition)
            return &slot.instance;
    }
    return nullptr;
}

const BlackIceInstance* GameState::floorBlackIceByRuntimeId(uint16_t runtimeId) const
{
    if (runtimeId == 0) return nullptr;
    const uint8_t currentPosition = static_cast<uint8_t>(architecture_.currentPosition());
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        const ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (slot.occupied && slot.runtimeId == runtimeId && slot.chasePosition == currentPosition)
            return &slot.instance;
    }
    return nullptr;
}

uint16_t GameState::nextFloorBlackIceRuntimeId(uint16_t currentRuntimeId, int direction) const
{
    const size_t count = floorBlackIceCount();
    if (count == 0) return 0;
    size_t current = 0;
    for (; current < count; ++current)
        if (floorBlackIceRuntimeId(current) == currentRuntimeId) break;
    if (current == count) return floorBlackIceRuntimeId(0);
    const int next = (static_cast<int>(current) + static_cast<int>(count) + direction) %
        static_cast<int>(count);
    return floorBlackIceRuntimeId(static_cast<size_t>(next));
}

void GameState::recordBlackIcePosition(const BlackIceInstance& ice)
{
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
        if (activeBlackIce_[index].occupied && &activeBlackIce_[index].instance == &ice)
            activeBlackIce_[index].chasePosition = static_cast<uint8_t>(architecture_.currentPosition());
}

int GameState::activeSlidePenalty() const
{
    int modifier = 0;
    for (size_t index = 0; index < activeBlackIceCount_; ++index)
    {
        const ActiveBlackIceSlot& slot = activeBlackIce_[index];
        if (!slot.occupied || !slot.instance.active() || !slot.slidePenaltyApplied ||
            slot.instance.definition() == nullptr) continue;
        if (slot.instance.definition()->effect.type == BlackIceEffectType::SlidePenaltyWhileActive)
            modifier -= slot.instance.definition()->effect.slidePenalty;
    }
    return modifier;
}

void GameState::applyTemporaryRunEffect(CombatResult& result, ActiveBlackIceSlot* source)
{
    if (!result.success) return;

    if (result.fireApplied) runnerOnFire_ = true;

    switch (result.temporaryRunEffect)
    {
        case TemporaryRunEffect::NavigationAndSafeJackOutLock:
        {
            const bool lockWasActive = navigationLockActive();
            const uint32_t throughTurn = turnNumber_ + 1;
            if (throughTurn > temporaryRunnerStatus_.navigationLockThroughTurn)
                temporaryRunnerStatus_.navigationLockThroughTurn = throughTurn;
            const uint8_t hitPosition = static_cast<uint8_t>(architecture_.currentPosition());
            if (!lockWasActive || hitPosition >= temporaryRunnerStatus_.navigationLockMaxPosition)
                temporaryRunnerStatus_.navigationLockMaxPosition = hitPosition;
            result.temporaryRunEffectApplied = true;
            result.depthLockApplied = true;
            result.safeJackOutLockApplied = true;
            break;
        }
        case TemporaryRunEffect::NextTurnNetActionPenalty:
        {
            if (result.attackerDefinition == nullptr) break;
            const uint32_t targetTurn = turnNumber_ + 1;
            const uint8_t penalty = result.attackerDefinition->effect.netActionPenalty;
            const uint8_t minimum = result.attackerDefinition->effect.minimumNetActions;
            if (penalty == 0) break;
            // Wisp's status is a one-turn refresh, not a stack.  A second
            // hit before that turn refreshes the same pending penalty.
            temporaryRunnerStatus_.netActionPenaltyTurn = targetTurn;
            temporaryRunnerStatus_.netActionPenaltyAmount = penalty;
            temporaryRunnerStatus_.netActionMinimum = minimum;
            // Preserve the applied value on the combat result as well as in
            // the persistent runtime state so generic result feedback can
            // report Wisp/Vrizzbolt without identifying the attacker by name.
            result.nextTurnNetActionPenalty = penalty;
            result.temporaryRunEffectApplied = true;
            break;
        }
        case TemporaryRunEffect::SlidePenaltyWhileActive:
            if (source != nullptr && source->instance.active() && !source->slidePenaltyApplied)
            {
                source->slidePenaltyApplied = true;
                result.temporaryRunEffectApplied = true;
            }
            result.slideModifier = static_cast<int8_t>(activeSlidePenalty());
            break;
        case TemporaryRunEffect::None:
            break;
    }
}

void GameState::expireTemporaryRunnerStatuses()
{
    if (temporaryRunnerStatus_.navigationLockThroughTurn != 0 &&
        turnNumber_ >= temporaryRunnerStatus_.navigationLockThroughTurn)
    {
        temporaryRunnerStatus_.navigationLockThroughTurn = 0;
        temporaryRunnerStatus_.navigationLockMaxPosition = 0;
    }
}

uint8_t GameState::netActionsForCurrentTurn()
{
    const uint8_t baseActions = runner_.maxNetActions();
    temporaryRunnerStatus_.netActionPenaltyAppliedThisTurn = 0;
    if (temporaryRunnerStatus_.netActionPenaltyTurn != turnNumber_)
        return baseActions;

    temporaryRunnerStatus_.netActionPenaltyAppliedThisTurn =
        temporaryRunnerStatus_.netActionPenaltyAmount;
    const uint8_t effectiveActions = Netrunner::netActionsAfterPenalty(
        baseActions, temporaryRunnerStatus_.netActionPenaltyAmount,
        temporaryRunnerStatus_.netActionMinimum);
    temporaryRunnerStatus_.netActionPenaltyTurn = 0;
    temporaryRunnerStatus_.netActionPenaltyAmount = 0;
    temporaryRunnerStatus_.netActionMinimum = 0;
    return effectiveActions;
}
