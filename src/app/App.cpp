#include "App.h"
#include "system/SharedSpiBus.h"

#include <Arduino.h>
#include <M5Cardputer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <new>
#include <string.h>

#include "game/NetRules.h"
#include "game/ProgramCatalog.h"
#include "game/HardwareCatalog.h"
#include "content/ArchitectureFactory.h"
#include "content/BuiltInArchitectures.h"
#include "content/BlackIceRegistry.h"
#include "content/EnemyNetrunnerRegistry.h"
#include "app/DebugConfig.h"
#include "ui/DigitalRollAnimation.h"
#include "ui/ListViewState.h"

namespace
{
alignas(GameUIController) static uint8_t debugUiStorage[sizeof(GameUIController)];
struct PlayerIceAnimationDiagnosticSnapshot
{
    bool executed = false;
    bool dispatch = false;
    const BlackIceDefinition* attackerDefinition = nullptr;
    IceVisualId visualId = IceVisualId::Count;
    HostileAttackStyle animationStyle = HostileAttackStyle::Pulse;
};

struct PlayerIceRepeatRoundDiagnosticSnapshot
{
    bool config = false;
    bool run = false;
    bool enemy = false;
    bool iceCount = false;
    bool rezzed = false;
    bool eligible = false;
    bool ids = false;
    bool targetState = false;
    bool playerIcePhase = false;
    bool round1 = false;
    bool duplicateGuard = false;
    bool cleared = false;
    bool round2 = false;
    bool ordered = false;
    uint8_t r1count = 0;
    uint8_t duplicateCount = 0;
    uint8_t r2count = 0;
    uint16_t expectedId0 = 0;
    uint16_t expectedId1 = 0;
    uint16_t expectedId2 = 0;
    uint16_t r2id0 = 0;
    uint16_t r2id1 = 0;
    uint16_t r2id2 = 0;
};

struct PlayerIceTargetPreviewDiagnosticSnapshot
{
    // stage 1 is the initial runtime lookup; reason 1 means a required
    // Player ICE or Enemy runtime was missing.
    uint8_t stage = 0;
    uint8_t reason = 0;
    bool validStored = false;
    bool lockedReadOnly = false;
    bool clearedSameFloor = false;
    bool deterministicOrder = false;
    bool terminalInactive = false;
    bool terminalInactivePreview = false;
    bool otherFloor = false;
    bool otherFloorGameplay = false;
    bool unrezzedPreviewOk = false;
    bool unrezzedGameplayRejected = false;
    bool unrezzedReadOnly = false;
    uint8_t iceFloor = 0;
    uint8_t enemy0Floor = 0;
    uint8_t enemy1Floor = 0;
    uint16_t potentialTargetId = 0;
    uint16_t strictTargetId = 0;
};

PlayerIceRepeatRoundDiagnosticSnapshot playerIceRepeatRoundDiagnostic;
PlayerIceTargetPreviewDiagnosticSnapshot playerIceTargetPreviewDiagnostic;

IcePhaseResult& debugPlayerIceRepeatRoundPhaseResult()
{
    static IcePhaseResult fixture;
    fixture.~IcePhaseResult();
    new (&fixture) IcePhaseResult();
    return fixture;
}

EnemyPhaseResult& debugPlayerIceRepeatRoundEnemyResult()
{
    static EnemyPhaseResult fixture;
    fixture.~EnemyPhaseResult();
    new (&fixture) EnemyPhaseResult();
    return fixture;
}

void logStackHighWaterMark(const char* label)
{
    // This is the minimum reserve observed since loopTask started, not its
    // currently free stack space.
    const UBaseType_t minimumStackReserveBytes = uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("[Stack] %s = %u bytes (minimum reserve)\n", label,
        static_cast<unsigned int>(minimumStackReserveBytes));
}

// Boot diagnostics run in Arduino's loopTask. Keep the large GameState fixture
// out of that task's stack and fully reset it before every independent case.
GameState& debugTestState(const ArchitectureDefinition& definition)
{
    static GameState fixture;
    // GameState::startRun intentionally preserves runner HP. Boot-test cases
    // need stronger isolation, so reconstruct the fixture in place instead.
    fixture.~GameState();
    new (&fixture) GameState(definition);
    // Boot tests intentionally retain the original, small deterministic test
    // loadout. Product defaults are verified separately by DeckConfig.
    RunnerProfile testProfile;
    snprintf(testProfile.handle, sizeof(testProfile.handle), "REDSHIFT");
    testProfile.interfaceRank = 4;
    testProfile.maxHp = 40;
    fixture.setRunnerProfile(testProfile);
    CyberdeckConfig testDeck;
    testDeck.quality = CyberdeckQuality::Standard;
    testDeck.programs[0] = ProgramId::Sword;
    testDeck.programs[1] = ProgramId::Armor;
    testDeck.programCount = 2;
    fixture.setCyberdeckConfig(testDeck);
    return fixture;
}

GameState& debugTestState()
{
    return debugTestState(BuiltInArchitectures::militechTestNet());
}

const ArchitectureDefinition& playerTargetContextArchitecture()
{
    static FloorDefinition floors[2];
    static const ArchitectureDefinition definition = {
        "player_target_context", "PLAYER TARGET CONTEXT", "Mixed focus regression fixture.", floors, 2
    };
    static bool initialized = false;
    if (!initialized)
    {
        floors[0] = FloorDefinition();
        floors[0].id = 1;
        floors[0].type = FloorType::BlackICE;
        floors[0].blackIceType = BlackIceType::Ice01;
        floors[0].contentId = "CONTEXT ICE";
        floors[1] = FloorDefinition();
        floors[1].id = 2;
        floors[1].type = FloorType::File;
        floors[1].contentId = "CONTEXT FILE";
        floors[1].fileName = "CONTEXT.DAT";
        floors[1].fileType = "TEST DATA";
        floors[1].enemyNetrunner = &nullbyteDefinition();
        initialized = true;
    }
    return definition;
}

GameState& debugPlayerTargetContextState()
{
    GameState& state = debugTestState(playerTargetContextArchitecture());
    state.startRun();
    if (!state.jackIn()) return state;
    BlackIceInstance* ice = state.floorBlackIceAt(0);
    state.architecture().moveForward();
    if (ice != nullptr) state.recordBlackIcePosition(*ice);
    return state;
}

// Scenario suites run sequentially during boot. Sharing one static fixture
// avoids permanently reserving a second LoadedScenario-sized BSS block.
LoadedScenario& debugScenarioFixture()
{
    static LoadedScenario fixture;
    return fixture;
}

EncounterResult& debugEncounterResult()
{
    static EncounterResult fixture;
    fixture.~EncounterResult();
    new (&fixture) EncounterResult();
    return fixture;
}

IcePhaseResult& debugIcePhaseResult()
{
    static IcePhaseResult fixture;
    fixture.~IcePhaseResult();
    new (&fixture) IcePhaseResult();
    return fixture;
}

DemonPhaseResult& debugDemonPhaseResult(uint8_t slot = 0)
{
    static DemonPhaseResult fixtures[2];
    if (slot > 1) slot = 0;
    fixtures[slot] = DemonPhaseResult();
    return fixtures[slot];
}

EnemyPhaseResult& debugEnemyPhaseResult()
{
    static EnemyPhaseResult fixture;
    fixture.~EnemyPhaseResult();
    new (&fixture) EnemyPhaseResult();
    return fixture;
}

CombatResult& debugCombatResult(uint8_t slot = 0)
{
    static CombatResult fixtures[3];
    if (slot > 2) slot = 0;
    fixtures[slot].~CombatResult();
    new (&fixtures[slot]) CombatResult();
    return fixtures[slot];
}

bool debugMoveForward(GameState& state, NetRules& rules)
{
    EncounterResult& result = debugEncounterResult();
    state.moveForward(rules, result);
    return result.moved;
}

bool debugMoveBackward(GameState& state, NetRules& rules)
{
    EncounterResult& result = debugEncounterResult();
    state.moveBackward(rules, result);
    return result.moved;
}

void debugRunIcePhase(GameState& state, NetRules& rules)
{
    state.runIcePhase(rules, debugIcePhaseResult());
}

class FixedDice final : public Dice
{
public:
    explicit FixedDice(int fixedRoll) : fixedRoll_(fixedRoll) {}

    int rollD10() override { return fixedRoll_; }

private:
    int fixedRoll_;
};

class CombatDice final : public Dice
{
public:
    CombatDice(int attackerRoll, int defenderRoll, int damage, uint8_t selectedIndex = 0)
        : rolls_{attackerRoll, defenderRoll}, damage_(damage), selectedIndex_(selectedIndex) {}
    int rollD10() override
    {
        const uint8_t index = rollIndex_ < 2 ? rollIndex_ : 1;
        ++rollIndex_;
        return rolls_[index];
    }
    int rollD6(uint8_t count) override
    {
        lastD6Count_ = count;
        return damage_;
    }
    uint8_t rollIndex(uint8_t count) override
    {
        lastIndexCount_ = count;
        return count == 0 ? 0 : selectedIndex_ % count;
    }
    uint8_t lastD6Count() const { return lastD6Count_; }
    uint8_t lastIndexCount() const { return lastIndexCount_; }

private:
    int rolls_[2];
    int damage_;
    uint8_t selectedIndex_ = 0;
    uint8_t rollIndex_ = 0;
    uint8_t lastD6Count_ = 0;
    uint8_t lastIndexCount_ = 0;
};

class SequenceDice final : public Dice
{
public:
    SequenceDice(int first, int second, int third = 1, int fourth = 1, int damage = 0)
        : rolls_{first, second, third, fourth}, damage_(damage) {}
    int rollD10() override
    {
        const uint8_t index = rollIndex_ < 4 ? rollIndex_ : 3;
        ++rollIndex_;
        return rolls_[index];
    }
    int rollD6(uint8_t) override { return damage_; }

private:
    int rolls_[4];
    int damage_;
    uint8_t rollIndex_ = 0;
};

class MultiDamageDice final : public Dice
{
public:
    MultiDamageDice(int firstAttackRoll, int firstDefenseRoll,
                    int secondAttackRoll, int secondDefenseRoll,
                    int firstDamage, int secondDamage, uint8_t selectedIndex = 0)
        : rolls_{firstAttackRoll, firstDefenseRoll, secondAttackRoll, secondDefenseRoll},
          damages_{firstDamage, secondDamage}, selectedIndex_(selectedIndex) {}
    int rollD10() override
    {
        const uint8_t index = rollIndex_ < 4 ? rollIndex_ : 3;
        ++rollIndex_;
        return rolls_[index];
    }
    int rollD6(uint8_t) override
    {
        const uint8_t index = damageIndex_ < 2 ? damageIndex_ : 1;
        ++damageIndex_;
        return damages_[index];
    }
    uint8_t rollIndex(uint8_t count) override
    {
        return count == 0 ? 0 : selectedIndex_ % count;
    }
private:
    int rolls_[4];
    int damages_[2];
    uint8_t selectedIndex_ = 0;
    uint8_t rollIndex_ = 0;
    uint8_t damageIndex_ = 0;
};

class GreymarkDice final : public Dice
{
public:
    GreymarkDice(int firstAttackRoll, int firstDefenseRoll,
              int intReduction, int refReduction, int dexReduction,
              int secondAttackRoll = 1, int secondDefenseRoll = 1,
              int secondEffectRoll = 1)
        : d10_{firstAttackRoll, firstDefenseRoll, secondAttackRoll, secondDefenseRoll},
          d6_{intReduction, refReduction, dexReduction, secondEffectRoll} {}
    int rollD10() override
    {
        const uint8_t index = d10Index_ < 4 ? d10Index_ : 3;
        ++d10Index_;
        return d10_[index];
    }
    int rollD6(uint8_t) override
    {
        const uint8_t index = d6Index_ < 4 ? d6Index_ : 3;
        ++d6Index_;
        return d6_[index];
    }
private:
    int d10_[4];
    int d6_[4];
    uint8_t d10Index_ = 0;
    uint8_t d6Index_ = 0;
};

class EncounterDice final : public Dice
{
public:
    EncounterDice(int a, int b, int c, int d, int e, int f)
        : rolls_{a, b, c, d, e, f} {}
    int rollD10() override
    {
        const uint8_t index = rollIndex_ < 6 ? rollIndex_ : 5;
        ++rollIndex_;
        return rolls_[index];
    }
    int rollD6(uint8_t) override { return 0; }

private:
    int rolls_[6];
    uint8_t rollIndex_ = 0;
};

Architecture& branchingArchitectureFixture()
{
    static Architecture fixture;
    return fixture;
}

bool branchingLegacyTest()
{
    Architecture& architecture = branchingArchitectureFixture();
    ArchitectureFactory::createInto(BuiltInArchitectures::militechTestNet(), architecture);
    return architecture.connectionCount(0) == 1 && architecture.connectionAt(0, 0) == 1 &&
        architecture.connectionCount(1) == 2 && architecture.moveTo(1) && architecture.currentPosition() == 1 &&
        architecture.currentFloor() != nullptr && architecture.currentFloor()->visited && !architecture.moveTo(3);
}

void branchingGraphTest(bool& explicitGraph, bool& noPath)
{
    Architecture& architecture = branchingArchitectureFixture();
    ArchitectureFactory::createInto(BuiltInArchitectures::branchingTestNet(), architecture);
    explicitGraph = architecture.connectionCount(0) == 1 && architecture.connectionAt(0, 0) == 1 &&
        architecture.connectionCount(1) == 4 && architecture.connectionCount(4) == 3 &&
        architecture.nextStepToward(4, 0) == 2 && architecture.nextStepToward(6, 0) == 4;
    architecture.initialize("disconnected", "disconnected", nullptr);
    architecture.addFloor(Floor(1, FloorType::Empty, 0, "A"));
    architecture.addFloor(Floor(2, FloorType::Empty, 0, "B"));
    noPath = architecture.nextStepToward(0, 1) == Architecture::NO_FLOOR;
}

bool branchingMovementVisitTest()
{
    GameState& state = debugTestState(BuiltInArchitectures::branchingTestNet());
    FixedDice dice(10); NetRules rules(dice);
    state.startRun(); state.jackIn();
    const uint8_t actions = state.runner().remainingNetActions();
    Serial.printf("[Diag][Branching][Move][0] start=%u current=%u actions=%u\n",
        state.architecture().currentFloor() != nullptr ? 1U : 0U,
        static_cast<unsigned>(state.architecture().currentPosition()), static_cast<unsigned>(actions));
    EncounterResult& move = debugEncounterResult(); state.moveToConnectedFloor(rules, 1, move);
    const bool movedForward = move.moved;
    const Floor* firstPath = state.architecture().currentFloor();
    const bool previousAfterForward = state.architecture().previousPosition() == 0;
    Serial.printf("[Diag][Branching][Move][1] moved=%u current=%u previous=%u visited=%u discovered=%u\n",
        movedForward ? 1U : 0U, static_cast<unsigned>(state.architecture().currentPosition()),
        static_cast<unsigned>(state.architecture().previousPosition()),
        firstPath != nullptr && firstPath->visited ? 1U : 0U,
        firstPath != nullptr && firstPath->discovered ? 1U : 0U);
    // Floor 3 is now a valid Branch-B neighbor of the Hub; use the actual
    // disconnected EXIT node to retain the invalid-destination assertion.
    EncounterResult& invalid = debugEncounterResult(); state.moveToConnectedFloor(rules, 6, invalid);
    const bool invalidRejected = !invalid.moved;
    Serial.printf("[Diag][Branching][Move][2] invalidTarget=6 rejected=%u current=%u connectedExpected=0\n",
        invalidRejected ? 1U : 0U, static_cast<unsigned>(state.architecture().currentPosition()));
    EncounterResult& back = debugEncounterResult(); state.moveToConnectedFloor(rules, 0, back);
    const bool returned = back.moved;
    const bool previousAfterBack = state.architecture().previousPosition() == 1;
    Serial.printf("[Diag][Branching][Move][3] returned=%u current=%u previous=%u actions=%u\n",
        returned ? 1U : 0U, static_cast<unsigned>(state.architecture().currentPosition()),
        static_cast<unsigned>(state.architecture().previousPosition()),
        static_cast<unsigned>(state.runner().remainingNetActions()));
    const bool movement = movedForward && firstPath != nullptr && firstPath->visited && firstPath->discovered && invalidRejected && returned &&
        previousAfterForward && previousAfterBack &&
        state.runner().remainingNetActions() == actions;
    if (!movement) Serial.printf("[Diag][Branching][Move] moved=%u invalid=%u returned=%u current=%u prev=%u actions=%u/%u\n",
        movedForward ? 1U : 0U, invalidRejected ? 1U : 0U, returned ? 1U : 0U,
        static_cast<unsigned>(state.architecture().currentPosition()), static_cast<unsigned>(state.architecture().previousPosition()),
        static_cast<unsigned>(state.runner().remainingNetActions()), static_cast<unsigned>(actions));
    return movement;
}

bool branchingVisitsTest()
{
    GameState& state = debugTestState(BuiltInArchitectures::branchingTestNet());
    FixedDice dice(10); NetRules rules(dice);
    state.startRun(); state.jackIn();
    const Floor* start = state.architecture().currentFloor();
    const Floor* path = state.architecture().floorAt(1);
    const bool initial = start != nullptr && start->visited && start->discovered && path != nullptr &&
        !path->visited && !path->discovered;
    EncounterResult& forward = debugEncounterResult(); state.moveToConnectedFloor(rules, 1, forward);
    const Floor* hub = state.architecture().currentFloor();
    const bool afterForward = forward.moved && hub != nullptr && hub->visited && hub->discovered &&
        state.architecture().floorAt(0)->visited && state.architecture().floorAt(0)->discovered;
    EncounterResult& back = debugEncounterResult(); state.moveToConnectedFloor(rules, 0, back);
    const bool afterBack = back.moved && state.architecture().floorAt(0)->visited && state.architecture().floorAt(0)->discovered &&
        state.architecture().floorAt(1)->visited && state.architecture().floorAt(1)->discovered;
    const bool visits = initial && afterForward && afterBack;
    if (!visits) Serial.printf("[Diag][Branching][Visits] initial=%u forward=%u back=%u\n",
        initial ? 1U : 0U, afterForward ? 1U : 0U, afterBack ? 1U : 0U);
    return visits;
}

bool branchingBfsTest()
{
    GameState& state = debugTestState(BuiltInArchitectures::branchingTestNet());
    FixedDice dice(1); NetRules rules(dice);
    state.startRun(); state.jackIn(); state.endPlayerTurn();
    EnemyPhaseResult& phase = debugEnemyPhaseResult(); state.runEnemyPhase(rules, phase);
    const EnemyNetrunnerRuntime* enemy = state.enemyNetrunnerAt(0);
    return phase.executed && enemy != nullptr && enemy->currentFloor == 2;
}

bool branchingIceInterceptTest()
{
    GameState& state = debugTestState(BuiltInArchitectures::branchingTestNet());
    CyberdeckConfig config; config.quality = CyberdeckQuality::Excellent; config.programCount = 0;
    config.playerBlackIce[0] = BlackIceType::Ice01; config.playerBlackIceCount = 1;
    state.setCyberdeckConfig(config); state.startRun(); state.jackIn();
    EnemyNetrunnerRuntime* enemy = state.enemyNetrunnerAt(0);
    PlayerBlackIceRuntime* ice = state.playerBlackIceAt(0);
    if (enemy != nullptr && ice != nullptr)
    {
        ice->instance.setRezzed(true); ice->instance.setPursuing(true);
        ice->targetEnemyRuntimeId = enemy->runtimeId; ice->chasePosition = 2;
    }
    FixedDice dice(10); NetRules rules(dice);
    state.endPlayerTurn(); state.runPlayerBlackIcePhase(rules, debugIcePhaseResult());
    EnemyPhaseResult& phase = debugEnemyPhaseResult(); state.runEnemyPhase(rules, phase);
    return enemy != nullptr && ice != nullptr && enemy->currentFloor == 2 && phase.actionCount == 1 &&
        phase.actions[0].runtimeId == ice->runtimeId;
}

bool branchingJsonValidationTest()
{
    static LoadedScenario loaded;
    static ScenarioLoader loader;
    const char* validJson = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"branch\",\"name\":\"BRANCH\",\"floors\":[{\"id\":1,\"type\":\"password\",\"dv\":6,\"security\":\"low\",\"next\":[2,3]},{\"id\":2,\"type\":\"file\",\"dv\":6,\"file\":{\"name\":\"A\",\"type\":\"LOG\",\"value\":1},\"next\":[1,4]},{\"id\":3,\"type\":\"file\",\"dv\":6,\"file\":{\"name\":\"B\",\"type\":\"LOG\",\"value\":1},\"next\":[1,4]},{\"id\":4,\"type\":\"control\",\"dv\":6,\"control\":{\"name\":\"NODE\"},\"next\":[2,3]}]}";
    const char* unknownJson = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"bad\",\"name\":\"BAD\",\"floors\":[{\"id\":1,\"type\":\"password\",\"dv\":6,\"security\":\"low\",\"next\":[2]},{\"id\":2,\"type\":\"control\",\"dv\":6,\"control\":{\"name\":\"N\"},\"next\":[9]}]}";
    const char* duplicateJson = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"dup\",\"name\":\"DUP\",\"floors\":[{\"id\":1,\"type\":\"password\",\"dv\":6,\"security\":\"low\",\"next\":[2,2]},{\"id\":2,\"type\":\"control\",\"dv\":6,\"control\":{\"name\":\"N\"},\"next\":[1]}]}";
    const char* selfJson = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"self\",\"name\":\"SELF\",\"floors\":[{\"id\":1,\"type\":\"password\",\"dv\":6,\"security\":\"low\",\"next\":[1]}]}";
    return loader.loadJson(validJson, loaded).success && loaded.definition().valid() &&
        loader.loadJson(unknownJson, loaded).error == ScenarioImportError::UnknownConnection &&
        loader.loadJson(duplicateJson, loaded).error == ScenarioImportError::DuplicateConnection &&
        loader.loadJson(selfJson, loaded).error == ScenarioImportError::SelfConnection;
}

void logBranchingBoundary(const char* section, bool starting)
{
    Serial.printf("[Branching] %s %s\n", section, starting ? "START" : "END");
    char stackLabel[32];
    snprintf(stackLabel, sizeof(stackLabel), "%s %s", starting ? "before" : "after", section);
    logStackHighWaterMark(stackLabel);
}

const char* resetReasonName(esp_reset_reason_t reason)
{
    switch (reason)
    {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_EXT: return "external";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic/exception";
        case ESP_RST_INT_WDT: return "interrupt-watchdog";
        case ESP_RST_TASK_WDT: return "task-watchdog";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_DEEPSLEEP: return "deep-sleep";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_SDIO: return "sdio";
        default: return "unknown/other";
    }
}
}

static void runExpandedPlayerIceEffectsDebugTest();
static void runBlackIceCatalogDebugTest();
static void runEnemyCatalogDebugTest();
static void runDemonCatalogDebugTest();
static void runScenarioCustomIceDebugTest();

App::App() : rules_(dice_), gameUi_(display_, gameState_, rules_, scenarioScanner_, loadedScenario_,
                                    playerConfigStore_, runnerProfile_, cyberdeckConfig_) {}

void App::begin()
{
    const unsigned long serialWaitStartedMs = millis();
    while (!Serial && millis() - serialWaitStartedMs < SERIAL_WAIT_TIMEOUT_MS)
    {
        delay(10);
    }

    auto config = M5.config();
    M5Cardputer.begin(config, true);

    SharedSpiBus::begin();
    keyboard_.begin();
    display_.begin();
    playerConfigStore_.load(runnerProfile_, cyberdeckConfig_);
    gameState_.setRunnerProfile(runnerProfile_);
    gameState_.setCyberdeckConfig(cyberdeckConfig_);
    scenarioScanner_.begin();
    gameUi_.begin();

    Serial.println("Cardputer ADV started");
    Serial.printf("[Memory] LoadedScenario=%u bytes | Architecture=%u bytes\n",
        static_cast<unsigned int>(sizeof(LoadedScenario)),
        static_cast<unsigned int>(sizeof(Architecture)));
    Serial.printf("[Memory] EnemyBehaviorId=%u | EnemyDecision=%u | EnemyDefinition=%u | EnemyRegistry=%u\n",
        static_cast<unsigned int>(sizeof(EnemyBehaviorId)),
        static_cast<unsigned int>(sizeof(EnemyBehaviorDecision)),
        static_cast<unsigned int>(sizeof(EnemyNetrunnerDefinition)),
        static_cast<unsigned int>(sizeof(EnemyNetrunnerRegistry)));
    Serial.printf("[Memory] FloorDefinition=%u | Floor=%u | EnemyRuntime=%u | PlayerIceRuntime=%u | GameState=%u | UI=%u | ActiveICE=%u\n",
        static_cast<unsigned int>(sizeof(FloorDefinition)),
        static_cast<unsigned int>(sizeof(Floor)),
        static_cast<unsigned int>(sizeof(EnemyNetrunnerRuntime)),
        static_cast<unsigned int>(sizeof(PlayerBlackIceRuntime)),
        static_cast<unsigned int>(sizeof(GameState)),
        static_cast<unsigned int>(sizeof(GameUIController)),
        static_cast<unsigned int>(MAX_ACTIVE_BLACK_ICE));
    Serial.printf("[Memory] EncounterResult=%u | IcePhaseResult=%u | EnemyPhaseResult=%u\n",
        static_cast<unsigned int>(sizeof(EncounterResult)),
        static_cast<unsigned int>(sizeof(IcePhaseResult)),
        static_cast<unsigned int>(sizeof(EnemyPhaseResult)));
#if NETRUN_RUN_BOOT_TESTS
    display_.setPresentationSuppressed(true);
    const esp_reset_reason_t resetReason = esp_reset_reason();
    Serial.printf("[BootTest] ResetReason=%s (%d)\n", resetReasonName(resetReason),
        static_cast<int>(resetReason));
    Serial.println("[BootTest] Content START");
    logStackHighWaterMark("before Content");
    runContentDebugTest();
    Serial.println("[BootTest] Content END");
    logStackHighWaterMark("after Content");
    Serial.println("[BootTest] ContentIdentity START");
    logStackHighWaterMark("before ContentIdentity");
    runContentIdentityDebugTest();
    Serial.println("[BootTest] ContentIdentity END");
    logStackHighWaterMark("after ContentIdentity");
    Serial.println("[BootTest] VisualId START");
    logStackHighWaterMark("before VisualId");
    runVisualIdDebugTest();
    Serial.println("[BootTest] VisualId END");
    logStackHighWaterMark("after VisualId");
    Serial.println("[BootTest] ScenarioImport START");
    logStackHighWaterMark("before ScenarioImport");
    runScenarioImportDebugTest();
    Serial.println("[BootTest] ScenarioImport END");
    logStackHighWaterMark("after ScenarioImport");
    Serial.println("[BootTest] ScenarioCustomEnemy START");
    logStackHighWaterMark("before ScenarioCustomEnemy");
    runScenarioCustomEnemyDebugTest();
    Serial.println("[BootTest] ScenarioCustomEnemy END");
    logStackHighWaterMark("after ScenarioCustomEnemy");
    Serial.println("[BootTest] ScenarioEnemyBehavior START");
    logStackHighWaterMark("before ScenarioEnemyBehavior");
    runScenarioEnemyBehaviorDebugTest();
    Serial.println("[BootTest] ScenarioEnemyBehavior END");
    logStackHighWaterMark("after ScenarioEnemyBehavior");
    Serial.println("[BootTest] R3.4Audit START");
    logStackHighWaterMark("before R3.4Audit");
    runR34AuditDebugTest();
    Serial.println("[BootTest] R3.4Audit END");
    logStackHighWaterMark("after R3.4Audit");
    Serial.println("[BootTest] R3.5Audit START");
    logStackHighWaterMark("before R3.5Audit");
    runR35AuditDebugTest();
    Serial.println("[BootTest] R3.5Audit END");
    logStackHighWaterMark("after R3.5Audit");

    Serial.println("[BootTest] GameModel START");
    logStackHighWaterMark("before GameModel");
    runGameModelDebugTest();
    Serial.println("[BootTest] GameModel END");
    logStackHighWaterMark("after GameModel");
    Serial.println("[BootTest] PlayerProfile START");
    logStackHighWaterMark("before PlayerProfile");
    runPlayerProfileDebugTest();
    Serial.println("[BootTest] PlayerProfile END");
    logStackHighWaterMark("after PlayerProfile");
    Serial.println("[BootTest] DeckConfig START");
    logStackHighWaterMark("before DeckConfig");
    runDeckConfigDebugTest();
    Serial.println("[BootTest] DeckConfig END");
    logStackHighWaterMark("after DeckConfig");
    Serial.println("[BootTest] DeckHardware START");
    logStackHighWaterMark("before DeckHardware");
    runDeckHardwareDebugTest();
    Serial.println("[BootTest] DeckHardware END");
    logStackHighWaterMark("after DeckHardware");
    Serial.println("[BootTest] NetrunnerStats START");
    logStackHighWaterMark("before NetrunnerStats");
    runNetrunnerStatsDebugTest();
    Serial.println("[BootTest] NetrunnerStats END");
    logStackHighWaterMark("after NetrunnerStats");
    Serial.println("[BootTest] Greymark START");
    logStackHighWaterMark("before Greymark");
    runGreymarkDebugTest();
    Serial.println("[BootTest] Greymark END");
    logStackHighWaterMark("after Greymark");

    Serial.println("[BootTest] NetRules START");
    logStackHighWaterMark("before NetRules");
    runNetRulesDebugTest();
    Serial.println("[BootTest] NetRules END");
    logStackHighWaterMark("after NetRules");

    Serial.println("[BootTest] NetCombat START");
    logStackHighWaterMark("before NetCombat");
    runNetCombatDebugTest();
    Serial.println("[BootTest] NetCombat END");
    logStackHighWaterMark("after NetCombat");

    Serial.println("[BootTest] PlayerZapEnemy START");
    logStackHighWaterMark("before PlayerZapEnemy");
    runPlayerZapEnemyDebugTest();
    Serial.println("[BootTest] PlayerZapEnemy END");
    logStackHighWaterMark("after PlayerZapEnemy");

    Serial.println("[BootTest] PlayerSlideTargeting START");
    logStackHighWaterMark("before PlayerSlideTargeting");
    runPlayerSlideTargetingDebugTest();
    Serial.println("[BootTest] PlayerSlideTargeting END");
    logStackHighWaterMark("after PlayerSlideTargeting");

    Serial.println("[BootTest] PlayerTargetContext START");
    logStackHighWaterMark("before PlayerTargetContext");
    runPlayerTargetContextDebugTest();
    Serial.println("[BootTest] PlayerTargetContext END");
    logStackHighWaterMark("after PlayerTargetContext");

    Serial.println("[BootTest] BlackIce START");
    logStackHighWaterMark("before BlackIce");
    runBlackIceDebugTest();
    Serial.println("[BootTest] BlackIce END");
    logStackHighWaterMark("after BlackIce");
    Serial.println("[BootTest] BlackIceEffects START");
    logStackHighWaterMark("before BlackIceEffects");
    runBlackIceEffectsDebugTest();
    Serial.println("[BootTest] BlackIceEffects END");
    logStackHighWaterMark("after BlackIceEffects");
    Serial.println("[BootTest] BehaviorId START");
    logStackHighWaterMark("before BehaviorId");
    runBehaviorIdDebugTest();
    Serial.println("[BootTest] BehaviorId END");
    logStackHighWaterMark("after BehaviorId");
    Serial.println("[BootTest] AnimationId START");
    logStackHighWaterMark("before AnimationId");
    runAnimationIdDebugTest();
    Serial.println("[BootTest] AnimationId END");
    logStackHighWaterMark("after AnimationId");

    Serial.println("[BootTest] AntiProgram START");
    logStackHighWaterMark("before AntiProgram");
    runAntiProgramDebugTest();
    Serial.println("[BootTest] AntiProgram END");
    logStackHighWaterMark("after AntiProgram");
    Serial.println("[BootTest] AntiProgramExpansion START");
    logStackHighWaterMark("before AntiProgramExpansion");
    runAntiProgramExpansionDebugTest();
    Serial.println("[BootTest] AntiProgramExpansion END");
    logStackHighWaterMark("after AntiProgramExpansion");
    Serial.println("[BootTest] DigitalRoll START");
    logStackHighWaterMark("before DigitalRoll");
    runDigitalRollAnimationDebugTest();
    Serial.println("[BootTest] DigitalRoll END");
    logStackHighWaterMark("after DigitalRoll");
    Serial.println("[BootTest] MultiIce START");
    logStackHighWaterMark("before MultiIce");
    runMultiIceDebugTest();
    Serial.println("[BootTest] MultiIce END");
    logStackHighWaterMark("after MultiIce");
    Serial.println("[BootTest] IceRoster START");
    logStackHighWaterMark("before IceRoster");
    runIceRosterDebugTest();
    Serial.println("[BootTest] IceRoster END");
    logStackHighWaterMark("after IceRoster");

    Serial.println("[BootTest] DeadlockStatus START");
    logStackHighWaterMark("before DeadlockStatus");
    runDeadlockStatusDebugTest();
    Serial.println("[BootTest] DeadlockStatus END");
    logStackHighWaterMark("after DeadlockStatus");

    Serial.println("[BootTest] Breacher START");
    logStackHighWaterMark("before Breacher");
    runBreacherDebugTest();
    Serial.println("[BootTest] Breacher END");
    logStackHighWaterMark("after Breacher");

    Serial.println("[BootTest] GhostpulseStatus START");
    logStackHighWaterMark("before GhostpulseStatus");
    runGhostpulseStatusDebugTest();
    Serial.println("[BootTest] GhostpulseStatus END");
    logStackHighWaterMark("after GhostpulseStatus");

    Serial.println("[BootTest] Programs START");
    logStackHighWaterMark("before Programs");
    runProgramDebugTest();
    Serial.println("[BootTest] Programs END");
    logStackHighWaterMark("after Programs");

    Serial.println("[BootTest] TurnFlow START");
    logStackHighWaterMark("before TurnFlow");
    runTurnOrchestrationDebugTest();
    Serial.println("[BootTest] TurnFlow END");
    logStackHighWaterMark("after TurnFlow");
    Serial.println("[BootTest] AutomaticEnd START");
    runAutomaticEndDebugTest();
    Serial.println("[BootTest] AutomaticEnd END");
    Serial.println("[BootTest] EnemyNetrunner START");
    logStackHighWaterMark("before EnemyNetrunner");
    runEnemyNetrunnerDebugTest();
    Serial.println("[BootTest] EnemyNetrunner END");
    logStackHighWaterMark("after EnemyNetrunner");
    Serial.println("[BootTest] EnemyBehaviorBaseline START");
    runEnemyBehaviorBaselineDebugTest();
    Serial.println("[BootTest] EnemyBehaviorBaseline END");
    Serial.println("[BootTest] EnemyBehaviorProfiles START");
    logStackHighWaterMark("before EnemyBehaviorProfiles");
    runEnemyBehaviorProfilesDebugTest();
    Serial.println("[BootTest] EnemyBehaviorProfiles END");
    logStackHighWaterMark("after EnemyBehaviorProfiles");
    Serial.println("[BootTest] EnemyCustomDefinition START");
    runEnemyNetrunnerCustomDefinitionDebugTest();
    Serial.println("[BootTest] EnemyCustomDefinition END");
    Serial.println("[BootTest] EnemyResultTone START");
    runEnemyResultToneDebugTest();
    Serial.println("[BootTest] EnemyResultTone END");
    Serial.println("[BootTest] MilitechDemoEnemyDormant START");
    runMilitechDemoEnemyDormantDebugTest();
    Serial.println("[BootTest] MilitechDemoEnemyDormant END");
    Serial.println("[BootTest] SpriteReviewNullbyteDormant START");
    runSpriteReviewNullbyteDormantDebugTest();
    Serial.println("[BootTest] SpriteReviewNullbyteDormant END");
    Serial.println("[BootTest] MilitechDemoEnemySpatial START");
    runMilitechDemoEnemySpatialDebugTest();
    Serial.println("[BootTest] MilitechDemoEnemySpatial END");
    Serial.println("[BootTest] EnemyFloorEntry START");
    logStackHighWaterMark("before EnemyFloorEntry");
    runEnemyFloorEntryDebugTest();
    Serial.println("[BootTest] EnemyFloorEntry END");
    logStackHighWaterMark("after EnemyFloorEntry");
    Serial.println("[BootTest] EnemyFloorUIFlow START");
    logStackHighWaterMark("before EnemyFloorUIFlow");
    runEnemyFloorUiFlowDebugTest();
    Serial.println("[BootTest] EnemyFloorUIFlow END");
    logStackHighWaterMark("after EnemyFloorUIFlow");
    Serial.println("[BootTest] Branching START");
    runBranchingDebugTest();
    Serial.println("[BootTest] Branching END");
    Serial.println("[BootTest] ArchitectureMap START");
    runArchitectureMapDebugTest();
    Serial.println("[BootTest] ArchitectureMap END");
    Serial.println("[BootTest] Pathfinder START");
    runPathfinderDebugTest();
    Serial.println("[BootTest] Pathfinder END");
    Serial.println("[BootTest] Cloak START");
    runCloakDebugTest();
    Serial.println("[BootTest] Cloak END");
    Serial.println("[BootTest] UiListState START");
    logStackHighWaterMark("before UiListState");
    runUiListStateDebugTest();
    Serial.println("[BootTest] UiListState END");
    logStackHighWaterMark("after UiListState");
    Serial.println("[BootTest] PlayerIceIdentity START");
    runPlayerIceIdentityDebugTest();
    Serial.println("[BootTest] PlayerIceIdentity END");
    Serial.println("[BootTest] BlackIceCatalog START");
    runBlackIceCatalogDebugTest();
    Serial.println("[BootTest] BlackIceCatalog END");
    Serial.println("[BootTest] EnemyCatalog START");
    runEnemyCatalogDebugTest();
    Serial.println("[BootTest] EnemyCatalog END");
    Serial.println("[BootTest] DemonCatalog START");
    runDemonCatalogDebugTest();
    Serial.println("[BootTest] DemonCatalog END");
    Serial.println("[BootTest] ScenarioCustomIce START");
    runScenarioCustomIceDebugTest();
    Serial.println("[BootTest] ScenarioCustomIce END");
    Serial.println("[BootTest] PlayerIceExpanded START");
    runExpandedPlayerIceEffectsDebugTest();
    Serial.println("[BootTest] PlayerIceExpanded END");
    Serial.println("[BootTest] PlayerBlackIce START");
    runPlayerBlackIceDebugTest();
    Serial.println("[BootTest] PlayerBlackIce END");
    Serial.println("[BootTest] PlayerBlackIceCombat START");
    runPlayerBlackIceCombatDebugTest();
    Serial.println("[BootTest] PlayerBlackIceCombat END");
    Serial.println("[BootTest] PlayerBlackIceSlide START");
    runPlayerBlackIceSlideDebugTest();
    Serial.println("[BootTest] PlayerBlackIceSlide END");
    Serial.println("[BootTest] PlayerBlackIceChase START");
    runPlayerBlackIceChaseDebugTest();
    Serial.println("[BootTest] PlayerBlackIceChase END");
    Serial.println("[BootTest] ArchitectureCompletion START");
    runArchitectureCompletionDebugTest();
    Serial.println("[BootTest] ArchitectureCompletion END");
    Serial.println("[BootTest] DemonIdentity START");
    logStackHighWaterMark("before DemonIdentity");
    runDemonIdentityDebugTest();
    Serial.println("[BootTest] DemonIdentity END");
    logStackHighWaterMark("after DemonIdentity");
    Serial.println("[BootTest] DemonCustomDefinition START");
    logStackHighWaterMark("before DemonCustomDefinition");
    runDemonCustomDefinitionDebugTest();
    Serial.println("[BootTest] DemonCustomDefinition END");
    logStackHighWaterMark("after DemonCustomDefinition");
    Serial.println("[BootTest] ScenarioCustomDemon START");
    logStackHighWaterMark("before ScenarioCustomDemon");
    runScenarioCustomDemonDebugTest();
    Serial.println("[BootTest] ScenarioCustomDemon END");
    logStackHighWaterMark("after ScenarioCustomDemon");
    Serial.println("[BootTest] Demon START");
    runDemonDebugTest();
    Serial.println("[BootTest] Demon END");
    Serial.println("[BootTest] Integration START");
    logStackHighWaterMark("before Integration");
    runIntegrationDebugTest();
    Serial.println("[BootTest] Integration END");
    logStackHighWaterMark("after Integration");
    display_.setPresentationSuppressed(false);
    gameUi_.begin();
    Serial.printf("[BootPresentationIsolation] %s\n",
        !display_.presentationSuppressed() ? "PASS" : "FAIL");
#endif
}

void App::runDemonIdentityDebugTest()
{
    const DemonType types[] = {DemonType::Demon01, DemonType::Demon02, DemonType::Demon03};
    const char* stableIds[] = {"demon_01", "demon_02", "demon_03"};
    const int expectedRez[] = {18, 24, 28};
    const uint8_t expectedInterface[] = {3, 5, 6};
    const uint8_t expectedActions[] = {2, 3, 4};
    const uint8_t expectedCombat[] = {12, 13, 14};
    const char* expectedNames[] = {"LATCH", "WARDEN", "CROWN"};
    bool definitions = true;
    bool ids = true;
    bool unique = true;
    bool names = true;
    bool typeLookup = true;
    bool stableLookup = true;
    bool stats = true;
    const DemonDefinition* definitionsByType[3] = {};

    for (size_t index = 0; index < 3; ++index)
    {
        const DemonDefinition* definition = demonDefinition(types[index]);
        definitionsByType[index] = definition;
        definitions = definitions && definition != nullptr;
        ids = ids && definition != nullptr && definition->stableId != nullptr &&
            definition->stableId[0] != '\0' && strcmp(definition->stableId, stableIds[index]) == 0;
        names = names && definition != nullptr && definition->displayName != nullptr &&
            strcmp(definition->displayName, expectedNames[index]) == 0;
        typeLookup = typeLookup && definition != nullptr && definition->type == types[index];
        stableLookup = stableLookup && definition != nullptr &&
            demonDefinitionByStableId(stableIds[index]) == definition;
        stats = stats && definition != nullptr && definition->maxRez == expectedRez[index] &&
            definition->interfaceRank == expectedInterface[index] &&
            definition->netActions == expectedActions[index] &&
            definition->combatNumber == expectedCombat[index];
        for (size_t previous = 0; previous < index; ++previous)
            unique = unique && definition != nullptr && definitionsByType[previous] != nullptr &&
                strcmp(definition->stableId, definitionsByType[previous]->stableId) != 0;
    }

    const bool unknownSafe = demonDefinition(DemonType::None) == nullptr &&
        demonDefinition(static_cast<DemonType>(255)) == nullptr &&
        demonDefinitionByStableId(nullptr) == nullptr &&
        demonDefinitionByStableId("") == nullptr &&
        demonDefinitionByStableId("unknown") == nullptr;
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    state.startRun();
    const bool runtime = state.demon().active && state.demon().definition != nullptr &&
        strcmp(state.demon().definition->stableId, stableIds[0]) == 0 &&
        strcmp(state.demon().definition->displayName, expectedNames[0]) == 0 &&
        state.demon().definition->type == types[0] &&
        state.demon().definition->interfaceRank == expectedInterface[0] &&
        state.demon().definition->netActions == expectedActions[0] &&
        state.demon().maxRez() == expectedRez[0] && state.demon().currentRez == expectedRez[0];
    const DemonDefinition* retained = state.demon().definition;
    (void)demonDefinitionByStableId(stableIds[1]);
    const bool pointerLifetime = retained != nullptr && state.demon().definition == retained;
    const bool pass = definitions && ids && unique && names && typeLookup && stableLookup &&
        stats && unknownSafe && runtime && pointerLifetime;
    Serial.printf("[DemonIdentity] %s | Definitions=%s | StableIds=%s | Unique=%s | DisplayNames=%s | TypeLookup=%s | StableLookup=%s | Stats=%s | Runtime=%s | PointerLifetime=%s | UnknownSafe=%s\n",
        pass ? "PASS" : "FAIL", definitions ? "PASS" : "FAIL", ids ? "PASS" : "FAIL",
        unique ? "PASS" : "FAIL", names ? "PASS" : "FAIL", typeLookup ? "PASS" : "FAIL",
        stableLookup ? "PASS" : "FAIL", stats ? "PASS" : "FAIL", runtime ? "PASS" : "FAIL",
        pointerLifetime ? "PASS" : "FAIL", unknownSafe ? "PASS" : "FAIL");
}

void App::runDemonCustomDefinitionDebugTest()
{
    // Deliberately use mutable local text to prove the runtime does not retain
    // parser/stack-owned name storage.
    char stableId[] = "test_daemon";
    char displayName[] = "TEST_DAEMON";
    const DemonDefinition custom = {stableId, displayName, DemonType::None, DemonVisualId::Orb01, HostileAttackStyle::Pulse, 21, 5, 3, 17};
    DemonInstance runtime;
    runtime.reset(custom);
    stableId[0] = 'X';
    displayName[0] = 'X';

    const bool pass = runtime.active && runtime.definition != nullptr &&
        strcmp(runtime.definition->stableId, "test_daemon") == 0 &&
        strcmp(runtime.definition->displayName, "TEST_DAEMON") == 0 &&
        runtime.definition->type == DemonType::None &&
        runtime.definition->interfaceRank == 5 && runtime.definition->maxRez == 21 &&
        runtime.currentRez == 21 && runtime.definition->netActions == 3;
    Serial.printf("[DemonCustomDefinition] %s | Name=%s | MaxREZ=%d | CurrentREZ=%d | Interface=%u | Actions=%u | IdentityIndependent=%s | LifetimeOwned=%s\n",
        pass ? "PASS" : "FAIL", runtime.definition != nullptr ? runtime.definition->displayName : "(null)",
        runtime.maxRez(), runtime.currentRez,
        runtime.definition != nullptr ? runtime.definition->interfaceRank : 0U,
        runtime.definition != nullptr ? runtime.definition->netActions : 0U,
        runtime.definition != nullptr && runtime.definition->type == DemonType::None ? "PASS" : "FAIL",
        runtime.definition != nullptr && strcmp(runtime.definition->displayName, "TEST_DAEMON") == 0 ? "PASS" : "FAIL");
}

void App::runScenarioCustomDemonDebugTest()
{
    static LoadedScenario loaded;
    static DemonRegistry globals;
    static const DemonDefinition globalDefinition = {"global_probe", "GLOBAL PROBE", DemonType::None, DemonVisualId::Orb01, HostileAttackStyle::Pulse, 19, 4, 2, 0};
    globals.clear();
    const bool globalRegistered = globals.add(globalDefinition);
    ScenarioLoader loader(blackIceRegistry(), globals);

    const char* base = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"d\",\"name\":\"D\",\"demon\":\"%s\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    char json[256] = {};
    snprintf(json, sizeof(json), base, "local_probe_demon");
    const char* localJson = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"local\",\"name\":\"LOCAL\",\"demons\":[{\"id\":\"local_probe_demon\",\"name\":\"LOCAL PROBE\",\"interface\":5,\"rez\":21,\"actions\":3,\"visual\":\"crown\",\"animation\":\"slash\"}],\"demon\":\"local_probe_demon\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    const ScenarioImportResult localResult = loader.loadJson(localJson, loaded);
    logStackHighWaterMark("ScenarioCustomDemon Local");
    GameState& localState = debugTestState(loaded.definition());
    localState.startRun();
    const bool local = localResult.success && localState.demon().active && localState.demon().definition != nullptr &&
        !strcmp(localState.demon().definition->displayName, "LOCAL PROBE") &&
        localState.demon().definition->interfaceRank == 5 && localState.demon().currentRez == 21 &&
        localState.demon().definition->netActions == 3 && localState.demon().definition->visualId == DemonVisualId::Crown01 && localState.demon().definition->animationStyle == HostileAttackStyle::Slash;
    const bool presentation = local &&
        GameUIController::demonPresentationRendererType(localState.demon().definition) == demonRendererType(DemonVisualId::Crown01);
    const bool invalidLocalPresentation = !loader.loadJson("{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"bad_visual\",\"name\":\"BAD\",\"demons\":[{\"id\":\"bad\",\"name\":\"BAD\",\"interface\":1,\"rez\":1,\"actions\":1,\"visual\":\"unknown\"}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}", loaded).success;
    const bool failedImportAtomic = !loader.loadJson("{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"bad\",\"name\":\"BAD\",\"demon\":\"missing_demon\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}", loaded).success &&
        loaded.definition().demonDefinition != nullptr &&
        !strcmp(loaded.definition().demonDefinition->displayName, "LOCAL PROBE");
    loaded.reset();
    logStackHighWaterMark("ScenarioCustomDemon Lifetime");
    const bool lifetime = localState.demon().definition != nullptr &&
        !strcmp(localState.demon().definition->displayName, "LOCAL PROBE") && localState.demon().currentRez == 21;

    snprintf(json, sizeof(json), base, "global_probe");
    const bool global = loader.loadJson(json, loaded).success && loaded.definition().demonDefinition == globals.findByStableId("global_probe");
    snprintf(json, sizeof(json), base, "demon_02");
    const bool builtin = loader.loadJson(json, loaded).success && loaded.definition().demonDefinition == demonDefinition(DemonType::Demon02);
    const DemonDefinition* builtinCrown = demonDefinition(DemonType::Demon03);
    static const DemonDefinition legacyPresentation = {"legacy_presentation", "LEGACY", DemonType::None, DemonVisualId::Orb01, HostileAttackStyle::Pulse, 1, 1, 1, 0};
    const bool builtinPresentation = builtinCrown != nullptr &&
        GameUIController::demonPresentationRendererType(builtinCrown) == demonRendererType(DemonVisualId::Crown01);
    const bool legacyPresentationOk = GameUIController::demonPresentationRendererType(&legacyPresentation) ==
        demonRendererType(DemonVisualId::Orb01) && GameUIController::demonPresentationRendererType(nullptr) == demonRendererType(DemonVisualId::Orb01);
    snprintf(json, sizeof(json), base, "missing_demon");
    const bool unknown = !loader.loadJson(json, loaded).success && loader.loadJson(json, loaded).error == ScenarioImportError::UnknownDemon;
    const char* duplicate = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"x\",\"name\":\"X\",\"demons\":[{\"id\":\"dup\",\"name\":\"A\",\"interface\":1,\"rez\":1,\"actions\":1},{\"id\":\"dup\",\"name\":\"B\",\"interface\":1,\"rez\":1,\"actions\":1}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    const bool duplicateLocal = !loader.loadJson(duplicate, loaded).success && loader.loadJson(duplicate, loaded).error == ScenarioImportError::DuplicateDemonId;
    const char* localBuiltinConflict = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"x\",\"name\":\"X\",\"demons\":[{\"id\":\"demon_01\",\"name\":\"X\",\"interface\":1,\"rez\":1,\"actions\":1}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    const bool builtinConflict = !loader.loadJson(localBuiltinConflict, loaded).success && loader.loadJson(localBuiltinConflict, loaded).error == ScenarioImportError::DemonIdConflict;
    const char* localGlobalConflict = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"x\",\"name\":\"X\",\"demons\":[{\"id\":\"global_probe\",\"name\":\"X\",\"interface\":1,\"rez\":1,\"actions\":1}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    const bool globalConflict = !loader.loadJson(localGlobalConflict, loaded).success && loader.loadJson(localGlobalConflict, loaded).error == ScenarioImportError::DemonIdConflict;
    const bool crossConflict = !globals.add(*demonDefinition(DemonType::Demon01));
    const char* scenarioA = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"a\",\"name\":\"A\",\"demons\":[{\"id\":\"custom_a\",\"name\":\"CUSTOM A\",\"interface\":3,\"rez\":11,\"actions\":2}],\"demon\":\"custom_a\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    const char* scenarioB = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"b\",\"name\":\"B\",\"demons\":[{\"id\":\"custom_b\",\"name\":\"CUSTOM B\",\"interface\":6,\"rez\":17,\"actions\":4}],\"demon\":\"custom_b\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    const bool scenarioAOk = loader.loadJson(scenarioA, loaded).success;
    logStackHighWaterMark("ScenarioCustomDemon Fresh");
    GameState& stateA = debugTestState(loaded.definition()); stateA.startRun();
    stateA.demon().takeRezDamage(99);
    const bool freshA = stateA.demon().currentRez == 0 && !stateA.demon().active;
    stateA.startRun();
    const bool fresh = freshA && stateA.demon().active && stateA.demon().currentRez == 11 &&
        stateA.demon().definition->interfaceRank == 3 && stateA.demon().definition->netActions == 2;
    const bool scenarioBOk = loader.loadJson(scenarioB, loaded).success;
    logStackHighWaterMark("ScenarioCustomDemon ScenarioSwitch");
    GameState& stateB = debugTestState(loaded.definition()); stateB.startRun();
    const bool scenarioSwitch = scenarioAOk && scenarioBOk && stateB.demon().definition != nullptr &&
        !strcmp(stateB.demon().definition->displayName, "CUSTOM B") && stateB.demon().currentRez == 17;
    const bool gameplay = scenarioSwitch && stateB.demon().definition->netActions == 4;
    logStackHighWaterMark("ScenarioCustomDemon Gameplay");
    const char* v1 = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"v1\",\"name\":\"SCHEMA 1\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    const char* v2 = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"v2\",\"name\":\"SCHEMA 1\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    const bool schemaOneImportA = loader.loadJson(v1, loaded).success;
    const bool schemaOneImportB = loader.loadJson(v2, loaded).success;
    const bool schemaOneImportC = loader.loadJson("{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"v3\",\"name\":\"SCHEMA 1\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}", loaded).success;
    const bool future = !loader.loadJson("{\"format\":\"netrun-architecture\",\"schemaVersion\":4,\"id\":\"f\",\"name\":\"F\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}", loaded).success;
    const bool pass = globalRegistered && local && global && builtin && unknown && duplicateLocal && builtinConflict && globalConflict &&
        crossConflict && lifetime && presentation && builtinPresentation && legacyPresentationOk && invalidLocalPresentation && failedImportAtomic && fresh && scenarioSwitch && gameplay && schemaOneImportA && schemaOneImportB && schemaOneImportC && future;
    Serial.printf("[ScenarioCustomDemon] %s | Local=%s | Global=%s | Builtin=%s | UnknownRef=%s | Duplicate=%s | BuiltinConflict=%s | GlobalConflict=%s | CrossConflict=%s | Lifetime=%s | Presentation=%s | BuiltinPresentation=%s | LegacyPresentation=%s | InvalidPresentation=%s | Atomicity=%s | Fresh=%s | ScenarioSwitch=%s | Gameplay=%s | Schema1A=%s | Schema1B=%s | Schema1C=%s | FutureSchemaVersion=%s\n",
        pass ? "PASS" : "FAIL", local ? "PASS" : "FAIL", global ? "PASS" : "FAIL", builtin ? "PASS" : "FAIL",
        unknown ? "PASS" : "FAIL", duplicateLocal ? "PASS" : "FAIL", builtinConflict ? "PASS" : "FAIL",
        globalConflict ? "PASS" : "FAIL", crossConflict ? "PASS" : "FAIL", lifetime ? "PASS" : "FAIL",
        presentation ? "PASS" : "FAIL", builtinPresentation ? "PASS" : "FAIL", legacyPresentationOk ? "PASS" : "FAIL",
        invalidLocalPresentation ? "PASS" : "FAIL", failedImportAtomic ? "PASS" : "FAIL", fresh ? "PASS" : "FAIL", scenarioSwitch ? "PASS" : "FAIL",
        gameplay ? "PASS" : "FAIL", schemaOneImportA ? "PASS" : "FAIL", schemaOneImportB ? "PASS" : "FAIL",
        schemaOneImportC ? "PASS" : "FAIL", future ? "PASS" : "FAIL");
}

void App::runIntegrationDebugTest()
{
    const ArchitectureDefinition& definition = BuiltInArchitectures::v1IntegrationTestNet();
    const bool scenarioLoads = definition.valid() && definition.floorCount == 8 &&
        strcmp(definition.id, "v1_integration_test_net") == 0;
    const bool contentShape = definition.floors[0].type == FloorType::Password &&
        definition.floors[3].type == FloorType::File &&
        definition.floors[4].type == FloorType::ControlNode &&
        definition.floors[6].type == FloorType::ControlNode;
    const bool multiIceContent = definition.floors[2].type == FloorType::BlackICE &&
        definition.floors[2].blackIceCount == 3;
    Architecture& integrationArchitecture = branchingArchitectureFixture();
    ArchitectureFactory::createInto(definition, integrationArchitecture);
    const bool branchMergeReachable = integrationArchitecture.nextStepToward(0, 5) != Architecture::NO_FLOOR &&
        integrationArchitecture.nextStepToward(2, 5) != Architecture::NO_FLOOR &&
        integrationArchitecture.nextStepToward(4, 5) != Architecture::NO_FLOOR;
    const bool deadEndReachable = integrationArchitecture.nextStepToward(0, 7) != Architecture::NO_FLOOR;

    GameState& state = debugTestState(definition);
    state.startRun();
    const bool runtimes = state.enemyNetrunnerCount() == 1 && state.demon().active &&
        state.demon().definition != nullptr && state.demon().definition->type == DemonType::Demon03;
    const bool freshBaseline = state.architecture().currentPosition() == 0 && state.activeBlackIceCount() == 0;
    state.demon().takeRezDamage(30);
    state.startRun();
    const bool freshRunState = state.runState() == RunState::Ready;
    const bool freshPosition = state.architecture().currentPosition() == 0 &&
        state.architecture().previousPosition() == Architecture::NO_FLOOR;
    const bool freshTurn = state.turnNumber() == 1 && state.turnPhase() == TurnPhase::Player;
    const bool freshRunner = state.runner().hp() == state.runner().maxHp() &&
        state.runner().remainingNetActions() == state.runner().maxNetActions();
    const bool freshStatuses = state.netActionPenaltyThisTurn() == 0 && !state.navigationLockActive() &&
        !state.runnerOnFire() && state.activeSlidePenalty() == 0;
    const bool freshRuntimes = state.playerBlackIceCount() == 0 && state.enemyNetrunnerCount() == 1 &&
        state.demon().active && state.demon().currentRez == state.demon().maxRez() &&
        state.activeBlackIceCount() == 0;
    const bool freshCompletion = !state.virusPlaced() && !state.architectureCompleted();
    const bool freshReset = freshRunState && freshPosition && freshTurn && freshRunner && freshStatuses &&
        freshRuntimes && freshCompletion;
    const bool pass = scenarioLoads && contentShape && multiIceContent && branchMergeReachable &&
        deadEndReachable && runtimes && freshBaseline && freshReset;
    Serial.printf("[Integration] %s | Scenario=%s | Graph=%s | Enemy=%s | Demon=%s | MultiICE=%s | BranchMerge=%s | Fresh=%s\n",
        pass ? "PASS" : "FAIL", scenarioLoads && contentShape ? "PASS" : "FAIL",
        definition.valid() ? "PASS" : "FAIL", state.enemyNetrunnerCount() == 1 ? "PASS" : "FAIL",
        state.demon().active ? "PASS" : "FAIL", multiIceContent ? "PASS" : "FAIL",
        branchMergeReachable && deadEndReachable ? "PASS" : "FAIL", freshReset ? "PASS" : "FAIL");
    if (!pass) Serial.printf("[Diag][Integration] floors=%u enemy=%u demon=%u multi=%u branch=%u deadEnd=%u fresh=%u\n",
        static_cast<unsigned>(definition.floorCount), static_cast<unsigned>(state.enemyNetrunnerCount()),
        state.demon().active ? 1U : 0U, multiIceContent ? 1U : 0U,
        branchMergeReachable ? 1U : 0U, deadEndReachable ? 1U : 0U, freshReset ? 1U : 0U);
    if (!freshReset) Serial.printf("[Diag][Integration][Fresh] run=%u position=%u previous=%u turn=%u phase=%u hp=%d/%d actions=%u/%u pending=%u lock=%u fire=%u slide=%d playerIce=%u enemy=%u demon=%u rez=%d/%d activeIce=%u virus=%u complete=%u | runOk=%u positionOk=%u turnOk=%u runnerOk=%u statusesOk=%u runtimesOk=%u completionOk=%u\n",
        static_cast<unsigned>(state.runState()), static_cast<unsigned>(state.architecture().currentPosition()),
        static_cast<unsigned>(state.architecture().previousPosition()), static_cast<unsigned>(state.turnNumber()),
        static_cast<unsigned>(state.turnPhase()), state.runner().hp(), state.runner().maxHp(),
        state.runner().remainingNetActions(), state.runner().maxNetActions(), state.netActionPenaltyThisTurn(),
        state.navigationLockActive() ? 1U : 0U, state.runnerOnFire() ? 1U : 0U, state.activeSlidePenalty(),
        static_cast<unsigned>(state.playerBlackIceCount()), static_cast<unsigned>(state.enemyNetrunnerCount()),
        state.demon().active ? 1U : 0U, state.demon().currentRez, state.demon().maxRez(),
        static_cast<unsigned>(state.activeBlackIceCount()), state.virusPlaced() ? 1U : 0U,
        state.architectureCompleted() ? 1U : 0U, freshRunState ? 1U : 0U, freshPosition ? 1U : 0U,
        freshTurn ? 1U : 0U, freshRunner ? 1U : 0U, freshStatuses ? 1U : 0U,
        freshRuntimes ? 1U : 0U, freshCompletion ? 1U : 0U);
}

void App::runContentIdentityDebugTest()
{
    static constexpr BlackIceType types[] = {
        BlackIceType::Ice01, BlackIceType::Ice02, BlackIceType::Ice03,
        BlackIceType::Ice04, BlackIceType::Ice05, BlackIceType::Ice06,
        BlackIceType::Ice07, BlackIceType::Ice08, BlackIceType::Ice09,
        BlackIceType::Ice10, BlackIceType::Ice11, BlackIceType::Ice12};
    static constexpr const char* stableIds[] = {
        "ice_01", "ice_02", "ice_03", "ice_04", "ice_05", "ice_06",
        "ice_07", "ice_08", "ice_09", "ice_10", "ice_11", "ice_12"};
    static constexpr const char* expectedNames[] = {
        "TRACEJACKAL", "CARRIONBYTE", "NEEDLECOIL", "DEADLOCK", "GHOSTPULSE", "PACKETSAW",
        "STINGWIRE", "GUTTERMESH", "STORMRAZOR", "GLASSCAT", "GREYMARK", "BREACHER"};

    bool definitionsValid = true;
    bool stableIdsValid = true;
    bool namesValid = true;
    bool lookupsValid = true;
    for (size_t index = 0; index < sizeof(types) / sizeof(types[0]); ++index)
    {
        const BlackIceDefinition* definition = blackIceDefinition(types[index]);
        definitionsValid = definitionsValid && definition != nullptr;
        stableIdsValid = stableIdsValid && definition != nullptr &&
            definition->stableId != nullptr && definition->stableId[0] != '\0' &&
            definition->displayName != nullptr &&
            strcmp(definition->stableId, stableIds[index]) == 0 &&
            strcmp(definition->stableId, definition->displayName) != 0;
        namesValid = namesValid && definition != nullptr && definition->displayName != nullptr &&
            strcmp(definition->displayName, expectedNames[index]) == 0;
        lookupsValid = lookupsValid && definition != nullptr &&
            blackIceDefinitionByStableId(stableIds[index]) == definition;
    }

    bool unique = true;
    for (size_t left = 0; left < sizeof(stableIds) / sizeof(stableIds[0]); ++left)
        for (size_t right = left + 1; right < sizeof(stableIds) / sizeof(stableIds[0]); ++right)
            if (strcmp(stableIds[left], stableIds[right]) == 0) unique = false;

    const bool unknownSafe = blackIceDefinitionByStableId(nullptr) == nullptr &&
        blackIceDefinitionByStableId("") == nullptr &&
        blackIceDefinitionByStableId("ice_99") == nullptr;
    const bool pass = definitionsValid && stableIdsValid && namesValid && unique &&
        lookupsValid && unknownSafe;
    Serial.printf("[ContentIdentity] %s | Definitions=%s | StableIds=%s | Names=%s | Unique=%s | Lookup=%s | Unknown=%s\n",
        pass ? "PASS" : "FAIL", definitionsValid ? "PASS" : "FAIL",
        stableIdsValid ? "PASS" : "FAIL", namesValid ? "PASS" : "FAIL",
        unique ? "PASS" : "FAIL", lookupsValid ? "PASS" : "FAIL",
        unknownSafe ? "PASS" : "FAIL");
}

void App::runBehaviorIdDebugTest()
{
    struct ExpectedBehavior
    {
        BlackIceType type;
        BlackIceEffectType effectType;
        uint8_t damageDice;
        uint8_t netActionPenalty;
        uint8_t minimumNetActions;
        uint8_t statusDice;
        uint8_t slidePenalty;
    };
    static constexpr ExpectedBehavior expected[] = {
        {BlackIceType::Ice01, BlackIceEffectType::DamageOnly, 2, 0, 0, 0, 0},
        {BlackIceType::Ice02, BlackIceEffectType::DestroyRandomInstalledProgram, 0, 0, 0, 0, 0},
        {BlackIceType::Ice03, BlackIceEffectType::DerezzRandomDefenderAndDamage, 1, 0, 0, 0, 0},
        {BlackIceType::Ice04, BlackIceEffectType::DamageAndNavigationLock, 2, 0, 0, 0, 0},
        {BlackIceType::Ice05, BlackIceEffectType::DamageAndNextTurnNetActionPenalty, 1, 1, 2, 0, 0},
        {BlackIceType::Ice06, BlackIceEffectType::ProgramDamageDestroyAtZero, 3, 0, 0, 0, 0},
        {BlackIceType::Ice07, BlackIceEffectType::ApplyFire, 0, 0, 0, 0, 0},
        {BlackIceType::Ice08, BlackIceEffectType::DamageAndReduceRunnerMove, 1, 0, 0, 1, 0},
        {BlackIceType::Ice09, BlackIceEffectType::ProgramDamageDestroyAtZero, 5, 0, 0, 0, 0},
        {BlackIceType::Ice10, BlackIceEffectType::ProgramDamageDestroyAtZero, 4, 0, 0, 0, 0},
        {BlackIceType::Ice11, BlackIceEffectType::ReduceRunnerStatsByAmount, 0, 0, 0, 1, 0},
        {BlackIceType::Ice12, BlackIceEffectType::DamageAndUnsafeJackOut, 2, 0, 0, 0, 0}};

    bool definitionsValid = true;
    bool mappingPreserved = true;
    uint8_t navigationLockCount = 0;
    for (const ExpectedBehavior& item : expected)
    {
        const BlackIceDefinition* definition = blackIceDefinition(item.type);
        definitionsValid = definitionsValid && definition != nullptr &&
            definition->effect.type <= BlackIceEffectType::ReduceRunnerStatsByAmount;
        mappingPreserved = mappingPreserved && definition != nullptr &&
            definition->effect.type == item.effectType &&
            definition->effect.damageDice == item.damageDice &&
            definition->effect.netActionPenalty == item.netActionPenalty &&
            definition->effect.minimumNetActions == item.minimumNetActions &&
            definition->effect.statusDice == item.statusDice &&
            definition->effect.slidePenalty == item.slidePenalty;
        if (definition != nullptr && definition->effect.type ==
            BlackIceEffectType::DamageAndNavigationLock) ++navigationLockCount;
    }

    const BlackIceDefinition* ghostpulse = blackIceDefinition(BlackIceType::Ice05);
    const BlackIceDefinition* stingwire = blackIceDefinition(BlackIceType::Ice07);
    const BlackIceDefinition* guttermesh = blackIceDefinition(BlackIceType::Ice08);
    const BlackIceDefinition* greymark = blackIceDefinition(BlackIceType::Ice11);
    const BlackIceDefinition* breacher = blackIceDefinition(BlackIceType::Ice12);
    const BlackIceDefinition* carrionbyte = blackIceDefinition(BlackIceType::Ice02);
    const BlackIceDefinition* needlecoil = blackIceDefinition(BlackIceType::Ice03);
    const bool specialBehavior = navigationLockCount == 1 && ghostpulse != nullptr &&
        ghostpulse->effect.type == BlackIceEffectType::DamageAndNextTurnNetActionPenalty &&
        ghostpulse->effect.netActionPenalty == 1 && ghostpulse->effect.minimumNetActions == 2 &&
        stingwire != nullptr && stingwire->effect.type == BlackIceEffectType::ApplyFire &&
        guttermesh != nullptr && guttermesh->effect.type == BlackIceEffectType::DamageAndReduceRunnerMove &&
        guttermesh->effect.damageDice == 1 && guttermesh->effect.statusDice == 1 &&
        greymark != nullptr && greymark->effect.type == BlackIceEffectType::ReduceRunnerStatsByAmount &&
        greymark->effect.statusDice == 1 && breacher != nullptr &&
        breacher->effect.type == BlackIceEffectType::DamageAndUnsafeJackOut && carrionbyte != nullptr &&
        carrionbyte->effect.type == BlackIceEffectType::DestroyRandomInstalledProgram && needlecoil != nullptr &&
        needlecoil->effect.type == BlackIceEffectType::DerezzRandomDefenderAndDamage;
    const bool pass = definitionsValid && mappingPreserved && specialBehavior;
    Serial.printf("[BehaviorId] %s | Definitions=%s | Mapping=%s | Specials=%s | NavigationLock=%u\n",
        pass ? "PASS" : "FAIL", definitionsValid ? "PASS" : "FAIL",
        mappingPreserved ? "PASS" : "FAIL", specialBehavior ? "PASS" : "FAIL",
        static_cast<unsigned>(navigationLockCount));
}

void App::runAnimationIdDebugTest()
{
    static constexpr BlackIceType types[] = {
        BlackIceType::Ice01, BlackIceType::Ice02, BlackIceType::Ice03,
        BlackIceType::Ice04, BlackIceType::Ice05, BlackIceType::Ice06,
        BlackIceType::Ice07, BlackIceType::Ice08, BlackIceType::Ice09,
        BlackIceType::Ice10, BlackIceType::Ice11, BlackIceType::Ice12};
    static constexpr HostileAttackStyle expected[] = {
        HostileAttackStyle::Lunge, HostileAttackStyle::Pulse, HostileAttackStyle::Slash,
        HostileAttackStyle::Lunge, HostileAttackStyle::Burst, HostileAttackStyle::Slash,
        HostileAttackStyle::Burst, HostileAttackStyle::Pulse, HostileAttackStyle::Slash,
        HostileAttackStyle::Lunge, HostileAttackStyle::Pulse, HostileAttackStyle::Lunge};

    bool definitionsValid = true;
    bool mappingPreserved = true;
    bool renderersValid = true;
    for (size_t index = 0; index < sizeof(types) / sizeof(types[0]); ++index)
    {
        const BlackIceDefinition* definition = blackIceDefinition(types[index]);
        definitionsValid = definitionsValid && definition != nullptr &&
            isValidHostileAttackStyle(definition->animationStyle);
        mappingPreserved = mappingPreserved && definition != nullptr &&
            definition->animationStyle == expected[index];
        renderersValid = renderersValid && definition != nullptr &&
            DisplayManager::hasHostileAttackStyle(definition->animationStyle);
    }

    bool allStylesUsed = true;
    for (uint8_t style = 0; style < static_cast<uint8_t>(HostileAttackStyle::Count); ++style)
    {
        bool found = false;
        for (const HostileAttackStyle assigned : expected)
            if (static_cast<uint8_t>(assigned) == style) { found = true; break; }
        allStylesUsed = allStylesUsed && found;
    }
    const bool unknownSafe = !isValidHostileAttackStyle(HostileAttackStyle::Count) &&
        !DisplayManager::hasHostileAttackStyle(HostileAttackStyle::Count);
    const bool pass = definitionsValid && mappingPreserved && renderersValid && allStylesUsed && unknownSafe;
    Serial.printf("[AnimationId] %s | Definitions=%s | Mapping=%s | Renderers=%s | Styles=%s | UnknownSafe=%s\n",
        pass ? "PASS" : "FAIL", definitionsValid ? "PASS" : "FAIL",
        mappingPreserved ? "PASS" : "FAIL", renderersValid ? "PASS" : "FAIL",
        allStylesUsed ? "PASS" : "FAIL", unknownSafe ? "PASS" : "FAIL");
}

void App::runVisualIdDebugTest()
{
    static constexpr BlackIceType types[] = {
        BlackIceType::Ice01, BlackIceType::Ice02, BlackIceType::Ice03,
        BlackIceType::Ice04, BlackIceType::Ice05, BlackIceType::Ice06,
        BlackIceType::Ice07, BlackIceType::Ice08, BlackIceType::Ice09,
        BlackIceType::Ice10, BlackIceType::Ice11, BlackIceType::Ice12};
    static constexpr IceVisualId expected[] = {
        IceVisualId::Hound01, IceVisualId::Bird01, IceVisualId::Serpent01,
        IceVisualId::Octopus01, IceVisualId::Wraith01, IceVisualId::Hunter01,
        IceVisualId::Scorp01, IceVisualId::Rat01, IceVisualId::Winged01,
        IceVisualId::Feline01, IceVisualId::Skull01, IceVisualId::BigGuy01};

    bool definitionsValid = true;
    bool mappingStable = true;
    bool renderersValid = true;
    for (size_t index = 0; index < sizeof(types) / sizeof(types[0]); ++index)
    {
        const BlackIceDefinition* definition = blackIceDefinition(types[index]);
        definitionsValid = definitionsValid && definition != nullptr &&
            isValidIceVisualId(definition->visualId);
        mappingStable = mappingStable && definition != nullptr &&
            definition->visualId == expected[index] && iceVisualId(types[index]) == expected[index];
        renderersValid = renderersValid && definition != nullptr &&
            DisplayManager::hasIceVisualRenderer(definition->visualId);
    }

    bool unique = true;
    for (size_t left = 0; left < sizeof(expected) / sizeof(expected[0]); ++left)
        for (size_t right = left + 1; right < sizeof(expected) / sizeof(expected[0]); ++right)
            if (expected[left] == expected[right]) unique = false;

    const bool unknownSafe = !DisplayManager::hasIceVisualRenderer(IceVisualId::Count) &&
        blackIceDefinition(BlackIceType::Count) == nullptr;
    const bool pass = definitionsValid && mappingStable && unique && renderersValid && unknownSafe;
    Serial.printf("[VisualId] %s | Definitions=%s | Mapping=%s | Unique=%s | Renderers=%s | UnknownSafe=%s\n",
        pass ? "PASS" : "FAIL", definitionsValid ? "PASS" : "FAIL",
        mappingStable ? "PASS" : "FAIL", unique ? "PASS" : "FAIL",
        renderersValid ? "PASS" : "FAIL", unknownSafe ? "PASS" : "FAIL");
}

static __attribute__((noinline)) void countDemonActions(const DemonPhaseResult& result, size_t& reclaimCount, size_t& zapCount)
{
    reclaimCount = 0;
    zapCount = 0;
    for (size_t index = 0; index < result.actionCount; ++index)
    {
        if (result.actions[index].type == DemonPhaseResult::ActionType::ControlReclaim) ++reclaimCount;
        if (result.actions[index].type == DemonPhaseResult::ActionType::Zap) ++zapCount;
    }
}

static __attribute__((noinline)) bool runDemonControlSubtest()
{
    Serial.println("[Demon] Control START");
    logStackHighWaterMark("Demon Control START");
    const DemonDefinition* latchDef = demonDefinition(DemonType::Demon01);
    const DemonDefinition* wardenDef = demonDefinition(DemonType::Demon02);
    const DemonDefinition* crownDef = demonDefinition(DemonType::Demon03);
    const bool definitions = latchDef != nullptr && latchDef->maxRez == 18 && latchDef->interfaceRank == 3 && latchDef->netActions == 2 &&
        wardenDef != nullptr && wardenDef->maxRez == 24 && wardenDef->interfaceRank == 5 && wardenDef->netActions == 3 &&
        crownDef != nullptr && crownDef->maxRez == 28 && crownDef->interfaceRank == 6 && crownDef->netActions == 4;

    GameState& noDemon = debugTestState();
    const bool noDemonOk = !noDemon.demon().active;
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    state.startRun();
    state.jackIn();
    const bool runtime = state.demon().active && state.demon().currentRez == 18 &&
        state.demon().definition->interfaceRank == 3 && state.demon().definition->netActions == 2;
    CombatDice phaseDice(10, 1, 4);
    NetRules phaseRules(phaseDice);
    Floor* control = state.architecture().floorAt(3);
    control->controlOwner = ControlOwner::Runner;
    control->controlled = true;
    state.endPlayerTurn();
    DemonPhaseResult& phase = debugDemonPhaseResult();
    state.runDemonPhase(phaseRules, phase);
    const bool priority = phase.executed && phase.actionCount == 2 && phase.controlledNode &&
        phase.controlledNodeFloor == 3 && phase.zap.executed &&
        phase.actions[0].type == DemonPhaseResult::ActionType::ControlReclaim &&
        phase.actions[0].controlledNodeFloor == 3 &&
        phase.actions[1].type == DemonPhaseResult::ActionType::Zap &&
        phase.actions[1].attack.executed && control != nullptr &&
        control->controlOwner == ControlOwner::Demon && state.turnPhase() == TurnPhase::Enemy;

    control->controlOwner = ControlOwner::Runner;
    control->controlled = true;
    state.startTurn();
    state.endPlayerTurn();
    DemonPhaseResult& reclaim = debugDemonPhaseResult();
    state.runDemonPhase(phaseRules, reclaim);
    const bool reclaimOk = reclaim.controlledNode && reclaim.controlledNodeFloor == 3 &&
        control->controlOwner == ControlOwner::Demon;

    GameState& freshDemonState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    freshDemonState.startRun();
    freshDemonState.jackIn();
    Floor* freshDemonControl = freshDemonState.architecture().floorAt(3);
    freshDemonControl->controlOwner = ControlOwner::Demon;
    freshDemonControl->controlled = false;
    freshDemonState.endPlayerTurn();
    DemonPhaseResult& freshDemonPhase = debugDemonPhaseResult();
    freshDemonState.runDemonPhase(phaseRules, freshDemonPhase);
    size_t freshReclaimCount = 0;
    size_t freshZapCount = 0;
    countDemonActions(freshDemonPhase, freshReclaimCount, freshZapCount);
    const bool freshDemonOk = freshReclaimCount == 0 && freshZapCount == 2 &&
        freshDemonControl->controlOwner == ControlOwner::Demon;

    GameState& followupState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    followupState.startRun();
    followupState.jackIn();
    Floor* followupControl = followupState.architecture().floorAt(3);
    followupControl->controlOwner = ControlOwner::Runner;
    followupControl->controlled = true;
    followupState.endPlayerTurn();
    DemonPhaseResult& firstReclaim = debugDemonPhaseResult(0);
    followupState.runDemonPhase(phaseRules, firstReclaim);
    followupState.startTurn();
    followupState.endPlayerTurn();
    DemonPhaseResult& followup = debugDemonPhaseResult(1);
    followupState.runDemonPhase(phaseRules, followup);
    size_t followupReclaimCount = 0;
    size_t followupZapCount = 0;
    countDemonActions(followup, followupReclaimCount, followupZapCount);
    const bool followupOk = firstReclaim.controlledNode && followupReclaimCount == 0 &&
        followupZapCount == 2 && followupControl->controlOwner == ControlOwner::Demon;

    static const FloorDefinition noControlFloors[] = {
        {1, FloorType::Empty, 0, SecurityTier::Low, "EMPTY", nullptr, nullptr, 0, BlackIceType::Ice01, nullptr, nullptr, nullptr}
    };
    static const ArchitectureDefinition noControlLatch("latch_zap_test", "LATCH ZAP", nullptr, noControlFloors, 1, DemonType::Demon01);
    GameState& fallbackState = debugTestState(noControlLatch);
    fallbackState.startRun();
    fallbackState.jackIn();
    fallbackState.endPlayerTurn();
    DemonPhaseResult& fallback = debugDemonPhaseResult();
    fallbackState.runDemonPhase(phaseRules, fallback);
    const bool fallbackZap = !fallback.controlledNode && fallback.controlledNodeFloor == Architecture::NO_FLOOR &&
        fallback.actionCount == 2 && fallback.zap.executed &&
        fallback.actions[0].type == DemonPhaseResult::ActionType::Zap &&
        fallback.actions[1].type == DemonPhaseResult::ActionType::Zap;

    const bool pass = definitions && noDemonOk && runtime && priority && reclaimOk &&
        freshDemonOk && followupOk && fallbackZap;
    Serial.printf("[Demon] Control %s | Definitions=%s | Latch=%s | Priority=%s | Reclaim=%s | Fresh=%s | Followup=%s | LATCH=%s\n",
        pass ? "PASS" : "FAIL", definitions ? "PASS" : "FAIL", runtime ? "PASS" : "FAIL",
        priority ? "PASS" : "FAIL", reclaimOk ? "PASS" : "FAIL", freshDemonOk ? "PASS" : "FAIL",
        followupOk ? "PASS" : "FAIL", fallbackZap ? "PASS" : "FAIL");
    logStackHighWaterMark("Demon Control END");
    Serial.println("[Demon] Control END");
    return pass;
}

static __attribute__((noinline)) bool runDemonStateSubtest()
{
    Serial.println("[Demon] State START");
    logStackHighWaterMark("Demon State START");
    static const FloorDefinition noControlFloors[] = {
        {1, FloorType::Empty, 0, SecurityTier::Low, "EMPTY", nullptr, nullptr, 0, BlackIceType::Ice01, nullptr, nullptr, nullptr}
    };
    static const ArchitectureDefinition wardenTest("warden_test", "WARDEN TEST", nullptr, noControlFloors, 1, DemonType::Demon02);
    static const ArchitectureDefinition crownTest("crown_test", "CROWN TEST", nullptr, noControlFloors, 1, DemonType::Demon03);
    CombatDice phaseDice(10, 1, 4);
    NetRules phaseRules(phaseDice);

    GameState& wardenState = debugTestState(wardenTest);
    wardenState.startRun();
    wardenState.jackIn();
    wardenState.endPlayerTurn();
    DemonPhaseResult& wardenPhase = debugDemonPhaseResult();
    wardenState.runDemonPhase(phaseRules, wardenPhase);
    const bool wardenRuntime = wardenState.demon().active && wardenState.demon().currentRez == 24 &&
        wardenPhase.actionCount == 3 && wardenPhase.actions[0].type == DemonPhaseResult::ActionType::Zap &&
        wardenPhase.actions[1].type == DemonPhaseResult::ActionType::Zap &&
        wardenPhase.actions[2].type == DemonPhaseResult::ActionType::Zap;

    GameState& crownState = debugTestState(crownTest);
    crownState.startRun();
    crownState.jackIn();
    crownState.endPlayerTurn();
    DemonPhaseResult& crownPhase = debugDemonPhaseResult();
    crownState.runDemonPhase(phaseRules, crownPhase);
    const bool crownRuntime = crownState.demon().active && crownState.demon().currentRez == 28 &&
        crownPhase.actionCount == 4 && crownPhase.actions[0].type == DemonPhaseResult::ActionType::Zap &&
        crownPhase.actions[1].type == DemonPhaseResult::ActionType::Zap &&
        crownPhase.actions[2].type == DemonPhaseResult::ActionType::Zap &&
        crownPhase.actions[3].type == DemonPhaseResult::ActionType::Zap;

    GameState& resetState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    resetState.startRun();
    resetState.jackIn();
    resetState.demon().takeRezDamage(18);
    resetState.startTurn();
    resetState.endPlayerTurn();
    DemonPhaseResult& inactive = debugDemonPhaseResult();
    resetState.runDemonPhase(phaseRules, inactive);
    const bool derezz = !resetState.demon().active && !inactive.executed;
    resetState.startRun();
    const bool fresh = resetState.demon().active && resetState.demon().currentRez == 18;

    const Floor* password = resetState.architecture().floorAt(0);
    const Floor* file = resetState.architecture().floorAt(1);
    const Floor* node = resetState.architecture().floorAt(3);
    const bool floorSemantics = password != nullptr && password->type == FloorType::Password &&
        file != nullptr && file->type == FloorType::File &&
        node != nullptr && node->type == FloorType::ControlNode;

    const bool pass = wardenRuntime && crownRuntime && derezz && fresh && floorSemantics;
    Serial.printf("[Demon] State %s | WARDEN=%s | CROWN=%s | Down=%s | Fresh=%s | Floors=%s\n",
        pass ? "PASS" : "FAIL", wardenRuntime ? "PASS" : "FAIL", crownRuntime ? "PASS" : "FAIL",
        derezz ? "PASS" : "FAIL", fresh ? "PASS" : "FAIL", floorSemantics ? "PASS" : "FAIL");
    logStackHighWaterMark("Demon State END");
    Serial.println("[Demon] State END");
    return pass;
}

static __attribute__((noinline)) bool runDemonRezSubtest()
{
    Serial.println("[Demon] REZ START");
    logStackHighWaterMark("Demon REZ START");
    GameState& damageState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    RunnerProfile rezProfile = damageState.runnerProfile();
    rezProfile.interfaceRank = 5;
    damageState.setRunnerProfile(rezProfile);
    damageState.startRun();
    damageState.jackIn();
    Program* sword = damageState.cyberdeck().programAt(0);
    if (sword != nullptr) sword->setStatus(ProgramStatus::Rezzed);

    CombatDice firstDamageDice(10, 1, 5);
    CombatDice secondDamageDice(10, 1, 5);
    CombatDice thirdDamageDice(10, 1, 5);
    NetRules firstDamageRules(firstDamageDice);
    NetRules secondDamageRules(secondDamageDice);
    NetRules thirdDamageRules(thirdDamageDice);
    CombatResult& firstHit = debugCombatResult(0);
    CombatResult& secondHit = debugCombatResult(1);
    CombatResult& thirdHit = debugCombatResult(2);
    if (damageState.demon().active)
    {
        firstHit = firstDamageRules.swordAttackDemon(
            damageState.runner(), damageState.cyberdeck(), damageState.demon());
        secondHit = secondDamageRules.swordAttackDemon(
            damageState.runner(), damageState.cyberdeck(), damageState.demon());
        thirdHit = thirdDamageRules.swordAttackDemon(
            damageState.runner(), damageState.cyberdeck(), damageState.demon());
    }
    const bool authoritativeRez = firstHit.success && secondHit.success && thirdHit.success &&
        thirdHit.damage == 5 && firstHit.targetRemainingRez == 13 &&
        secondHit.targetRemainingRez == 8 && thirdHit.targetRemainingRez == 3 &&
        damageState.demon().active && damageState.demon().currentRez == 3;
    if (!authoritativeRez)
    {
        Serial.printf("[Diag][Demon][REZ] hits=%u/%u/%u rez=%d/%d/%d active=%u\n",
            firstHit.success, secondHit.success, thirdHit.success,
            firstHit.targetRemainingRez, secondHit.targetRemainingRez,
            thirdHit.targetRemainingRez, damageState.demon().active);
    }
    Serial.printf("[Demon] REZ %s\n", authoritativeRez ? "PASS" : "FAIL");
    logStackHighWaterMark("Demon REZ END");
    Serial.println("[Demon] REZ END");
    return authoritativeRez;
}

static __attribute__((noinline)) bool runDemonTargetSubtest()
{
    Serial.println("[Demon] Targets START");
    logStackHighWaterMark("Demon Targets START");
    GameState& targetState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    targetState.startRun();
    targetState.jackIn();
    targetState.endPlayerTurn();
    CombatDice targetPhaseDice(10, 1, 4);
    NetRules targetPhaseRules(targetPhaseDice);
    DemonPhaseResult& targetPhase = debugDemonPhaseResult();
    targetState.runDemonPhase(targetPhaseRules, targetPhase);
    size_t candidateCount = 0;
    int candidate0Type = -1;
    int candidate0Floor = -1;
    int candidate1Type = -1;
    int candidate1Floor = -1;
    for (size_t floorIndex = 0; floorIndex < targetState.architecture().floorCount(); ++floorIndex)
    {
        const Floor* candidate = targetState.architecture().floorAt(floorIndex);
        if (candidate == nullptr || candidate->type != FloorType::ControlNode) continue;
        if (candidateCount == 0) { candidate0Type = static_cast<int>(candidate->type); candidate0Floor = static_cast<int>(floorIndex); }
        if (candidateCount == 1) { candidate1Type = static_cast<int>(candidate->type); candidate1Floor = static_cast<int>(floorIndex); }
        ++candidateCount;
    }
    targetState.demon().takeRezDamage(18);
    CombatResult& filteredTarget = debugCombatResult(0);
    filteredTarget = targetPhaseRules.zapDemon(targetState.runner(), targetState.demon());
    const bool targetFiltering = !targetState.demon().active && !filteredTarget.executed;
    if (!targetFiltering)
        Serial.printf("[Diag][Demon][Targets] demonActive=%u demonRezzed=%u candidateCount=%u candidate0Type=%d candidate0Floor=%d candidate1Type=%d candidate1Floor=%d chosenAction=%u chosenTargetType=%d chosenTargetFloor=%d targetFiltered=%u executed=%u\n",
            targetState.demon().active, targetState.demon().currentRez > 0, static_cast<unsigned>(candidateCount),
            candidate0Type, candidate0Floor, candidate1Type, candidate1Floor, targetPhase.actionCount,
            targetPhase.controlledNode ? static_cast<int>(FloorType::ControlNode) : -1,
            targetPhase.controlledNode ? candidate0Floor : -1, targetFiltering, filteredTarget.executed);
    Serial.printf("[Demon] Targets %s\n", targetFiltering ? "PASS" : "FAIL");
    logStackHighWaterMark("Demon Targets END");
    Serial.println("[Demon] Targets END");
    return targetFiltering;
}

void App::runDemonDebugTest()
{
    Serial.println("[Demon] START");
    logStackHighWaterMark("Demon overall START");
    const bool control = runDemonControlSubtest();
    const bool state = runDemonStateSubtest();
    const bool rez = runDemonRezSubtest();
    const bool targets = runDemonTargetSubtest();
    const bool pass = control && state && rez && targets;
    Serial.printf("[Demon] %s | Control=%s | State=%s | REZ=%s | Targets=%s\n",
        pass ? "PASS" : "FAIL", control ? "PASS" : "FAIL", state ? "PASS" : "FAIL",
        rez ? "PASS" : "FAIL", targets ? "PASS" : "FAIL");
    logStackHighWaterMark("Demon overall END");
    Serial.println("[Demon] END");
}

void App::runArchitectureCompletionDebugTest()
{
    static FloorDefinition nonFinalControlFloors[] = {
        {1, FloorType::Password, 6, SecurityTier::Low, "ENTRY", nullptr, nullptr, 0, BlackIceType::Ice01, "ENTRY", nullptr, nullptr},
        {2, FloorType::ControlNode, 6, SecurityTier::Low, "CONTROL", nullptr, nullptr, 0, BlackIceType::Ice01, "CONTROL", "NODE", nullptr},
        {3, FloorType::File, 0, SecurityTier::Low, "DATA", nullptr, nullptr, 0, BlackIceType::Ice01, "DATA", nullptr, nullptr},
        {4, FloorType::File, 0, SecurityTier::Low, "END", nullptr, nullptr, 0, BlackIceType::Ice01, "END", nullptr, nullptr}
    };
    static bool nonFinalControlGraphInitialized = false;
    if (!nonFinalControlGraphInitialized)
    {
        nonFinalControlFloors[0].connections[0] = 2; nonFinalControlFloors[0].connectionCount = 1;
        nonFinalControlFloors[1].connections[0] = 3; nonFinalControlFloors[1].connectionCount = 1;
        nonFinalControlFloors[2].connections[0] = 4; nonFinalControlFloors[2].connectionCount = 1;
        nonFinalControlGraphInitialized = true;
    }
    static const ArchitectureDefinition nonFinalControlArchitecture = {
        "completion_control_nonfinal", "CONTROL NONFINAL", nullptr, nonFinalControlFloors, 4};
    GameState& nonFinalControlState = debugTestState(nonFinalControlArchitecture);
    nonFinalControlState.startRun(); nonFinalControlState.jackIn();
    nonFinalControlState.architecture().currentFloor()->resolved = true;
    FixedDice controlDice(10); NetRules controlRules(controlDice);
    EncounterResult& controlMove = debugEncounterResult();
    nonFinalControlState.moveToConnectedFloor(controlRules, 1, controlMove);
    NetCheckResult controlResult = controlRules.control(nonFinalControlState.runner(),
        *nonFinalControlState.architecture().currentFloor());
    const bool nonFinalControl = controlMove.moved && controlResult.success &&
        nonFinalControlState.architecture().currentFloor()->controlled &&
        !nonFinalControlState.completeIfArchitectureEnd() &&
        !nonFinalControlState.isAwaitingEndDecision() &&
        nonFinalControlState.moveToConnectedFloor(controlRules, 2).moved;

    static FloorDefinition branchControlFloors[] = {
        {1, FloorType::Password, 6, SecurityTier::Low, "ENTRY", nullptr, nullptr, 0, BlackIceType::Ice01, "ENTRY", nullptr, nullptr},
        {2, FloorType::ControlNode, 6, SecurityTier::Low, "BRANCH_CONTROL", nullptr, nullptr, 0, BlackIceType::Ice01, "CONTROL", "NODE", nullptr},
        {3, FloorType::File, 0, SecurityTier::Low, "DEAD_END", nullptr, nullptr, 0, BlackIceType::Ice01, "DEAD END", nullptr, nullptr},
        {4, FloorType::File, 0, SecurityTier::Low, "CONTROL_EXIT", nullptr, nullptr, 0, BlackIceType::Ice01, "EXIT", nullptr, nullptr},
        {5, FloorType::File, 0, SecurityTier::Low, "LONG_BRANCH", nullptr, nullptr, 0, BlackIceType::Ice01, "LONG", nullptr, nullptr},
        {6, FloorType::File, 0, SecurityTier::Low, "FINAL", nullptr, nullptr, 0, BlackIceType::Ice01, "FINAL", nullptr, nullptr}
    };
    static bool branchControlGraphInitialized = false;
    if (!branchControlGraphInitialized)
    {
        branchControlFloors[0].connections[0] = 2; branchControlFloors[0].connections[1] = 3; branchControlFloors[0].connections[2] = 5; branchControlFloors[0].connectionCount = 3;
        branchControlFloors[1].connections[0] = 4; branchControlFloors[1].connectionCount = 1;
        branchControlFloors[4].connections[0] = 6; branchControlFloors[4].connectionCount = 1;
        branchControlGraphInitialized = true;
    }
    static const ArchitectureDefinition branchControlArchitecture = {
        "completion_control_branch", "CONTROL BRANCH", nullptr, branchControlFloors, 6};
    GameState& branchControlState = debugTestState(branchControlArchitecture);
    branchControlState.startRun(); branchControlState.jackIn();
    branchControlState.architecture().currentFloor()->resolved = true;
    EncounterResult& branchMove = debugEncounterResult();
    branchControlState.moveToConnectedFloor(controlRules, 1, branchMove);
    NetCheckResult branchResult = controlRules.control(branchControlState.runner(),
        *branchControlState.architecture().currentFloor());
    const bool branchControl = branchMove.moved && branchResult.success &&
        branchControlState.architecture().currentFloor()->controlled &&
        branchControlState.architecture().canonicalDepth(1) < branchControlState.architecture().maximumCanonicalDepth() &&
        !branchControlState.completeIfArchitectureEnd() &&
        !branchControlState.isAwaitingEndDecision();
    GameState& shallowDeadEndState = debugTestState(branchControlArchitecture);
    shallowDeadEndState.startRun(); shallowDeadEndState.jackIn();
    shallowDeadEndState.architecture().currentFloor()->resolved = true;
    EncounterResult& deadEndMove = debugEncounterResult();
    shallowDeadEndState.moveToConnectedFloor(controlRules, 2, deadEndMove);
    const bool shallowDeadEnd = deadEndMove.moved &&
        shallowDeadEndState.architecture().canonicalDepth(2) < shallowDeadEndState.architecture().maximumCanonicalDepth() &&
        !shallowDeadEndState.completeIfArchitectureEnd() &&
        !shallowDeadEndState.isAwaitingEndDecision();

    GameState& state = debugTestState();
    state.startRun(); state.jackIn();
    while (state.architecture().moveForward()) {}
    const size_t completionFloor = state.architecture().currentPosition();
    const bool prompt = state.completeIfArchitectureEnd() && state.isAwaitingEndDecision();
    const bool placed = state.finishArchitectureEnd(true);
    const bool completed = placed && state.architectureCompleted() && state.virusPlaced() &&
        state.runState() == RunState::JackedIn && state.architecture().currentPosition() == completionFloor &&
        !state.isAwaitingEndDecision();
    const bool repeatedRejected = !state.finishArchitectureEnd(true) && !state.completeIfArchitectureEnd();
    FixedDice movementDice(10); NetRules movementRules(movementDice);
    EncounterResult& movement = debugEncounterResult(); state.moveBackward(movementRules, movement);
    const bool moved = movement.moved && state.architecture().currentPosition() + 1 == completionFloor;
    const uint8_t actionsBeforeJackOut = state.runner().remainingNetActions();
    const bool manualJackOut = state.jackOut() && state.runState() == RunState::JackedOut;
    const bool jackOutCost = manualJackOut && actionsBeforeJackOut > 0 &&
        state.runner().remainingNetActions() + 1 == actionsBeforeJackOut;
    state.startRun();
    const bool fresh = !state.architectureCompleted() && !state.virusPlaced() && !state.isAwaitingEndDecision() &&
        state.runState() == RunState::Ready;
    const bool pass = nonFinalControl && branchControl && shallowDeadEnd && prompt && completed && repeatedRejected && moved && manualJackOut && jackOutCost && fresh;
    if (!pass) Serial.printf("[Diag][ArchitectureCompletion] prompt=%u completed=%u moved=%u run=%u fresh=%u\n",
        prompt, completed, moved, static_cast<unsigned>(state.runState()), fresh);
    Serial.printf("[ArchitectureCompletion] %s | ControlNonFinal=%s | BranchControl=%s | DeadEnd=%s | Virus=%s | ActiveRun=%s | Movement=%s | Repeat=%s | JackOut=%s | JackOutCost=%s | Fresh=%s\n",
        pass ? "PASS" : "FAIL", nonFinalControl ? "PASS" : "FAIL", branchControl ? "PASS" : "FAIL",
        shallowDeadEnd ? "PASS" : "FAIL",
        placed ? "PASS" : "FAIL", completed ? "PASS" : "FAIL", moved ? "PASS" : "FAIL",
        repeatedRejected ? "PASS" : "FAIL", manualJackOut ? "PASS" : "FAIL", jackOutCost ? "PASS" : "FAIL",
        fresh ? "PASS" : "FAIL");
}

void App::runPlayerIceIdentityDebugTest()
{
    struct ExpectedPlayerIce {
        BlackIceType type;
        const char* stableId;
        const char* displayName;
    };
    static constexpr ExpectedPlayerIce supported[] = {
        {BlackIceType::Ice01, "ice_01", "TRACEJACKAL"},
        {BlackIceType::Ice02, "ice_02", "CARRIONBYTE"},
        {BlackIceType::Ice03, "ice_03", "NEEDLECOIL"},
        {BlackIceType::Ice04, "ice_04", "DEADLOCK"},
        {BlackIceType::Ice05, "ice_05", "GHOSTPULSE"},
        {BlackIceType::Ice06, "ice_06", "PACKETSAW"},
        {BlackIceType::Ice07, "ice_07", "STINGWIRE"},
        {BlackIceType::Ice08, "ice_08", "GUTTERMESH"},
        {BlackIceType::Ice09, "ice_09", "STORMRAZOR"},
        {BlackIceType::Ice10, "ice_10", "GLASSCAT"},
        {BlackIceType::Ice11, "ice_11", "GREYMARK"},
        {BlackIceType::Ice12, "ice_12", "BREACHER"}};
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    CyberdeckConfig config;
    config.quality = CyberdeckQuality::Excellent;
    config.programCount = 0;
    config.hardwareCount = 0;
    config.playerBlackIceCount = 1;
    bool allResolved = true;
    bool allMatch = true;
    bool allStable = true;
    for (size_t index = 0; index < sizeof(supported) / sizeof(supported[0]); ++index)
    {
        const ExpectedPlayerIce& item = supported[index];
        config.playerBlackIce[0] = item.type;
        const BlackIceDefinition* expected = blackIceDefinition(item.type);
        const bool configured = state.setCyberdeckConfig(config);
        state.startRun();
        const PlayerBlackIceRuntime* runtime = state.playerBlackIceAt(0);
        const BlackIceDefinition* definition = runtime != nullptr ? runtime->definition : nullptr;
        allResolved = allResolved && configured && expected != nullptr && definition != nullptr;
        allMatch = allMatch && definition == expected && runtime != nullptr &&
            runtime->instance.definition() == definition && definition->type == item.type &&
            definition->displayName != nullptr && definition->stableId != nullptr &&
            strcmp(definition->displayName, item.displayName) == 0 &&
            strcmp(definition->stableId, item.stableId) == 0 &&
            isValidIceVisualId(definition->visualId) &&
            isValidHostileAttackStyle(definition->animationStyle);
        allStable = allStable && definition == blackIceDefinition(item.type) &&
            definition->maxRez > 0;
    }
    CyberdeckConfig invalid = config;
    invalid.playerBlackIce[0] = BlackIceType::Count;
    const bool invalidRejected = !state.setCyberdeckConfig(invalid);
    state.startRun();
    const PlayerBlackIceRuntime* fresh = state.playerBlackIceAt(0);
    const bool freshRuntime = fresh != nullptr && fresh->definition == blackIceDefinition(config.playerBlackIce[0]) &&
        fresh->instance.definition() == fresh->definition;
    static const BlackIceDefinition customCapable = {
        "diag_custom_player_ice", "DIAG CUSTOM", 1, BlackIceType::Ice01, BlackIceClass::AntiPersonnel,
        1, 1, 1, 1, {BlackIceEffectType::DamageOnly, 1, 0, 0, 0, 0},
        "TEST", "1D6", nullptr, IceVisualId::Hound01, HostileAttackStyle::Lunge, true};
    static const BlackIceDefinition customRejected = {
        "diag_custom_hostile_ice", "DIAG HOSTILE", 1, BlackIceType::Ice01, BlackIceClass::AntiPersonnel,
        1, 1, 1, 1, {BlackIceEffectType::DamageOnly, 1, 0, 0, 0, 0},
        "TEST", "1D6", nullptr, IceVisualId::Hound01, HostileAttackStyle::Lunge, false};
    const bool capability = playerBlackIceSupported(&customCapable) && !playerBlackIceSupported(&customRejected);
    const bool pass = allResolved && allMatch && allStable && invalidRejected && freshRuntime && capability;
    Serial.printf("[PlayerIceIdentity] %s | Resolve=%s | Match=%s | Stable=%s | Invalid=%s | Fresh=%s | Capability=%s\n",
        pass ? "PASS" : "FAIL", allResolved ? "PASS" : "FAIL", allMatch ? "PASS" : "FAIL",
        allStable ? "PASS" : "FAIL", invalidRejected ? "PASS" : "FAIL",
        freshRuntime ? "PASS" : "FAIL", capability ? "PASS" : "FAIL");
}

static __attribute__((noinline)) void runBlackIceCatalogDebugTest()
{
    static BlackIceRegistry registry;
    static const char valid[] = "{\"format\":\"netrun-black-ice-catalog\",\"version\":1,\"black_ice\":[{\"id\":\"voidhound\",\"name\":\"VOIDHOUND\",\"per\":6,\"spd\":7,\"atk\":5,\"def\":4,\"rez\":20,\"behavior\":\"direct_damage\",\"damage_dice\":2,\"visual\":\"hound\",\"animation\":\"lunge\",\"player_usable\":true}]}";
    static const char duplicate[] = "{\"format\":\"netrun-black-ice-catalog\",\"version\":1,\"black_ice\":[{\"id\":\"dup\",\"name\":\"A\",\"per\":1,\"spd\":1,\"atk\":1,\"def\":1,\"rez\":1,\"behavior\":\"direct_damage\",\"damage_dice\":1,\"visual\":\"hound\",\"animation\":\"lunge\",\"player_usable\":true},{\"id\":\"dup\",\"name\":\"B\",\"per\":1,\"spd\":1,\"atk\":1,\"def\":1,\"rez\":1,\"behavior\":\"direct_damage\",\"damage_dice\":1,\"visual\":\"hound\",\"animation\":\"lunge\",\"player_usable\":true}]}";
    static const char conflict[] = "{\"format\":\"netrun-black-ice-catalog\",\"version\":1,\"black_ice\":[{\"id\":\"ice_01\",\"name\":\"X\",\"per\":1,\"spd\":1,\"atk\":1,\"def\":1,\"rez\":1,\"behavior\":\"direct_damage\",\"damage_dice\":1,\"visual\":\"hound\",\"animation\":\"lunge\",\"player_usable\":true}]}";
    const BlackIceCatalogResult validResult = registry.loadJson(valid);
    const BlackIceDefinition* definition = registry.findByContentId("voidhound");
    const bool validOk = validResult.success && definition != nullptr && definition->playerUsable &&
        definition->effect.type == BlackIceEffectType::DamageOnly && definition->visualId == IceVisualId::Hound01 &&
        definition->animationStyle == HostileAttackStyle::Lunge && registry.definitionAt(blackIceDefinitionCount()) == definition;
    const BlackIceCatalogResult duplicateResult = registry.loadJson(duplicate);
    const bool duplicateOk = !duplicateResult.success && duplicateResult.error == BlackIceCatalogError::Duplicate &&
        registry.findByContentId("voidhound") == definition;
    const BlackIceCatalogResult conflictResult = registry.loadJson(conflict);
    const bool conflictOk = !conflictResult.success && conflictResult.error == BlackIceCatalogError::Conflict;
    const bool capability = playerBlackIceSupported(definition);
    BlackIceInstance runtime(*definition);
    const bool lifetime = runtime.definition() == definition && runtime.maxRez() == 20;
    const bool missing = registry.findByContentId("missing") == nullptr;
    const bool pass = validOk && duplicateOk && conflictOk && capability && lifetime && missing;
    Serial.printf("[BlackIceCatalog][Valid] %s\n[BlackIceCatalog][Duplicate] %s\n[BlackIceCatalog][Conflict] %s\n[BlackIceCatalog][Lifetime] %s\n[BlackIceCatalog][Capability] %s\n[BlackIceCatalog][Missing] %s\n[BlackIceCatalog] %s\n",
        validOk ? "PASS" : "FAIL", duplicateOk ? "PASS" : "FAIL", conflictOk ? "PASS" : "FAIL",
        lifetime ? "PASS" : "FAIL", capability ? "PASS" : "FAIL", missing ? "PASS" : "FAIL", pass ? "PASS" : "FAIL");
}

static __attribute__((noinline)) void runEnemyCatalogDebugTest()
{
    static EnemyNetrunnerRegistry registry;
    static LoadedScenario scenario;
    static const char valid[] =
        "{\"format\":\"netrun-enemy-catalog\",\"version\":1,\"enemies\":[{\"id\":\"sd_probe_runner\",\"handle\":\"SD_PROBE\",\"interface\":4,\"hp\":22,\"actions\":2,\"programs\":[\"Hellbolt\"],\"behavior\":\"sentry\"}]}";
    static const char duplicate[] =
        "{\"format\":\"netrun-enemy-catalog\",\"version\":1,\"enemies\":[{\"id\":\"dup\",\"handle\":\"A\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Sword\"]},{\"id\":\"dup\",\"handle\":\"B\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Sword\"]}]}";
    static const char builtinConflict[] =
        "{\"format\":\"netrun-enemy-catalog\",\"version\":1,\"enemies\":[{\"id\":\"nullbyte\",\"handle\":\"X\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Sword\"]}]}";
    static const char unknownProgram[] =
        "{\"format\":\"netrun-enemy-catalog\",\"version\":1,\"enemies\":[{\"id\":\"bad_program\",\"handle\":\"BAD\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Unknown\"]}]}";
    static const char unknownBehavior[] =
        "{\"format\":\"netrun-enemy-catalog\",\"version\":1,\"enemies\":[{\"id\":\"bad_behavior\",\"handle\":\"BAD\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Sword\"],\"behavior\":\"unknown\"}]}";
    const EnemyCatalogResult loaded = registry.loadJson(valid);
    const EnemyNetrunnerDefinition* definition = registry.findByStableId("sd_probe_runner");
    ScenarioLoader loader(blackIceRegistry(), demonRegistry(), registry);
    const bool global = loader.loadJson("{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"enemy_global\",\"name\":\"ENEMY\",\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"sd_probe_runner\"}]}", scenario).success &&
        scenario.floors()[0].enemyNetrunner == definition;
    const bool validOk = loaded.success && definition != nullptr && definition->behavior == EnemyBehaviorId::Sentry;
    const bool duplicateOk = registry.loadJson(duplicate).error == EnemyCatalogError::Duplicate;
    const bool conflictOk = registry.loadJson(builtinConflict).error == EnemyCatalogError::Conflict;
    const bool programOk = registry.loadJson(unknownProgram).error == EnemyCatalogError::Program;
    const bool behaviorOk = registry.loadJson(unknownBehavior).error == EnemyCatalogError::Behavior;
    const bool atomicity = registry.findByStableId("sd_probe_runner") == definition;
    registry.clear();
    const bool fresh = registry.count() == 0 && registry.loadJson(valid).success && registry.findByStableId("sd_probe_runner") != nullptr;
    const bool pass = validOk && global && duplicateOk && conflictOk && programOk && behaviorOk && atomicity && fresh;
    Serial.printf("[EnemyCatalog] %s | Valid=%s | Global=%s | Duplicate=%s | BuiltinConflict=%s | Program=%s | Behavior=%s | Atomicity=%s | Fresh=%s\n",
        pass ? "PASS" : "FAIL", validOk ? "PASS" : "FAIL", global ? "PASS" : "FAIL", duplicateOk ? "PASS" : "FAIL",
        conflictOk ? "PASS" : "FAIL", programOk ? "PASS" : "FAIL", behaviorOk ? "PASS" : "FAIL",
        atomicity ? "PASS" : "FAIL", fresh ? "PASS" : "FAIL");
}

static __attribute__((noinline)) void runDemonCatalogDebugTest()
{
    static DemonRegistry registry;
    static LoadedScenario scenario;
    static const char valid[] =
        "{\"format\":\"netrun-demon-catalog\",\"version\":1,\"demons\":[{\"id\":\"sd_probe_demon\",\"name\":\"SD PROBE\",\"interface\":4,\"rez\":19,\"actions\":2,\"visual\":\"sentinel\",\"animation\":\"slash\"}]}";
    static const char legacy[] =
        "{\"format\":\"netrun-demon-catalog\",\"version\":1,\"demons\":[{\"id\":\"legacy_probe\",\"name\":\"LEGACY\",\"interface\":1,\"rez\":1,\"actions\":1}]}";
    static const char duplicate[] =
        "{\"format\":\"netrun-demon-catalog\",\"version\":1,\"demons\":[{\"id\":\"dup\",\"name\":\"A\",\"interface\":1,\"rez\":1,\"actions\":1},{\"id\":\"dup\",\"name\":\"B\",\"interface\":1,\"rez\":1,\"actions\":1}]}";
    static const char builtinConflict[] =
        "{\"format\":\"netrun-demon-catalog\",\"version\":1,\"demons\":[{\"id\":\"demon_01\",\"name\":\"X\",\"interface\":1,\"rez\":1,\"actions\":1}]}";
    static const char invalidStats[] =
        "{\"format\":\"netrun-demon-catalog\",\"version\":1,\"demons\":[{\"id\":\"bad\",\"name\":\"BAD\",\"interface\":11,\"rez\":0,\"actions\":0}]}";
    static const char invalidVisual[] =
        "{\"format\":\"netrun-demon-catalog\",\"version\":1,\"demons\":[{\"id\":\"bad\",\"name\":\"BAD\",\"interface\":1,\"rez\":1,\"actions\":1,\"visual\":\"unknown\"}]}";
    static const char invalidAnimation[] =
        "{\"format\":\"netrun-demon-catalog\",\"version\":1,\"demons\":[{\"id\":\"bad\",\"name\":\"BAD\",\"interface\":1,\"rez\":1,\"actions\":1,\"animation\":\"unknown\"}]}";
    const DemonCatalogResult loaded = registry.loadJson(valid);
    const DemonDefinition* definition = registry.findByStableId("sd_probe_demon");
    ScenarioLoader loader(blackIceRegistry(), registry);
    const bool global = loader.loadJson("{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"demon_global\",\"name\":\"DEMON\",\"demon\":\"sd_probe_demon\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}", scenario).success &&
        scenario.definition().demonDefinition == definition;
    const bool validOk = loaded.success && definition != nullptr && definition->maxRez == 19 &&
        definition->visualId == DemonVisualId::Sentinel01 && definition->animationStyle == HostileAttackStyle::Slash;
    const bool duplicateOk = registry.loadJson(duplicate).error == DemonCatalogError::Duplicate;
    const bool conflictOk = registry.loadJson(builtinConflict).error == DemonCatalogError::Conflict;
    const bool statsOk = registry.loadJson(invalidStats).error == DemonCatalogError::Bounds;
    const bool visualOk = registry.loadJson(invalidVisual).error == DemonCatalogError::Visual;
    const bool animationOk = registry.loadJson(invalidAnimation).error == DemonCatalogError::Animation;
    const bool atomicity = registry.findByStableId("sd_probe_demon") == definition;
    registry.clear();
    const DemonCatalogResult legacyLoaded = registry.loadJson(legacy);
    const DemonDefinition* legacyDefinition = registry.findByStableId("legacy_probe");
    const bool legacyOk = legacyLoaded.success && legacyDefinition != nullptr &&
        legacyDefinition->visualId == DemonVisualId::Orb01 && legacyDefinition->animationStyle == HostileAttackStyle::Pulse;
    registry.clear();
    const bool fresh = registry.count() == 0 && registry.loadJson(valid).success && registry.findByStableId("sd_probe_demon") != nullptr;
    const bool pass = validOk && global && duplicateOk && conflictOk && statsOk && visualOk && animationOk && atomicity && legacyOk && fresh;
    Serial.printf("[DemonCatalog] %s | Valid=%s | Global=%s | Duplicate=%s | BuiltinConflict=%s | Stats=%s | Visual=%s | Animation=%s | Atomicity=%s | LegacyDefaults=%s | Fresh=%s\n",
        pass ? "PASS" : "FAIL", validOk ? "PASS" : "FAIL", global ? "PASS" : "FAIL", duplicateOk ? "PASS" : "FAIL",
        conflictOk ? "PASS" : "FAIL", statsOk ? "PASS" : "FAIL", visualOk ? "PASS" : "FAIL", animationOk ? "PASS" : "FAIL",
        atomicity ? "PASS" : "FAIL", legacyOk ? "PASS" : "FAIL", fresh ? "PASS" : "FAIL");
}

static __attribute__((noinline)) void runScenarioCustomIceDebugTest()
{
    static BlackIceRegistry registry;
    static LoadedScenario scenario;
    static GameState runtime;
    static CombatDice customIceDice(10, 1, 2);
    static NetRules customIceRules(customIceDice);
    static const char catalog[] = "{\"format\":\"netrun-black-ice-catalog\",\"version\":1,\"black_ice\":[{\"id\":\"sd_probe_01\",\"name\":\"SD PROBE\",\"per\":3,\"spd\":4,\"atk\":5,\"def\":2,\"rez\":9,\"behavior\":\"direct_damage\",\"damage_dice\":1,\"visual\":\"bird\",\"animation\":\"pulse\",\"player_usable\":false},{\"id\":\"custom_black_ice\",\"name\":\"SIGNAL\",\"per\":3,\"spd\":4,\"atk\":4,\"def\":3,\"rez\":12,\"behavior\":\"apply_fire\",\"damage_dice\":1,\"visual\":\"skull\",\"animation\":\"pulse\",\"player_usable\":true}]}";
    static const char local[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"local_custom\",\"name\":\"LOCAL CUSTOM\",\"black_ice\":[{\"id\":\"local_probe_01\",\"name\":\"LOCAL PROBE\",\"per\":4,\"spd\":5,\"atk\":6,\"def\":3,\"rez\":11,\"behavior\":\"direct_damage\",\"damage_dice\":2,\"visual\":\"serpent\",\"animation\":\"slash\",\"player_usable\":true}],\"floors\":[{\"id\":0,\"type\":\"black_ice\",\"ice\":[\"local_probe_01\"]}]}";
    static const char global[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"global_custom\",\"name\":\"GLOBAL CUSTOM\",\"floors\":[{\"id\":0,\"type\":\"black_ice\",\"ice\":[\"sd_probe_01\"]}]}";
    static const char mixedPassword[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"mixed_password\",\"name\":\"MIXED PASSWORD\",\"floors\":[{\"id\":0,\"type\":\"password\",\"dv\":15,\"security\":\"low\",\"ice\":[\"custom_black_ice\"]}]}";
    static const char mixed[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"mixed_custom\",\"name\":\"MIXED CUSTOM\",\"black_ice\":[{\"id\":\"local_probe_01\",\"name\":\"LOCAL PROBE\",\"per\":4,\"spd\":5,\"atk\":6,\"def\":3,\"rez\":11,\"behavior\":\"direct_damage\",\"damage_dice\":2,\"visual\":\"serpent\",\"animation\":\"slash\",\"player_usable\":false}],\"floors\":[{\"id\":0,\"type\":\"black_ice\",\"ice\":[\"ice_01\",\"sd_probe_01\",\"local_probe_01\"]}]}";
    static const char unknown[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"unknown_custom\",\"name\":\"UNKNOWN\",\"floors\":[{\"id\":0,\"type\":\"black_ice\",\"ice\":[\"not_here\"]}]}";
    static const char duplicate[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"duplicate_custom\",\"name\":\"DUPLICATE\",\"black_ice\":[{\"id\":\"dup_custom\",\"name\":\"A\",\"per\":1,\"spd\":1,\"atk\":1,\"def\":1,\"rez\":1,\"behavior\":\"direct_damage\",\"damage_dice\":1,\"visual\":\"hound\",\"animation\":\"lunge\",\"player_usable\":false},{\"id\":\"dup_custom\",\"name\":\"B\",\"per\":1,\"spd\":1,\"atk\":1,\"def\":1,\"rez\":1,\"behavior\":\"direct_damage\",\"damage_dice\":1,\"visual\":\"hound\",\"animation\":\"lunge\",\"player_usable\":false}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char builtinConflict[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"builtin_custom\",\"name\":\"BUILTIN\",\"black_ice\":[{\"id\":\"ice_01\",\"name\":\"X\",\"per\":1,\"spd\":1,\"atk\":1,\"def\":1,\"rez\":1,\"behavior\":\"direct_damage\",\"damage_dice\":1,\"visual\":\"hound\",\"animation\":\"lunge\",\"player_usable\":false}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char globalConflict[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"global_conflict_custom\",\"name\":\"GLOBAL\",\"black_ice\":[{\"id\":\"sd_probe_01\",\"name\":\"X\",\"per\":1,\"spd\":1,\"atk\":1,\"def\":1,\"rez\":1,\"behavior\":\"direct_damage\",\"damage_dice\":1,\"visual\":\"hound\",\"animation\":\"lunge\",\"player_usable\":false}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char v1[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"v1\",\"name\":\"SCHEMA 1\",\"floors\":[{\"id\":0,\"type\":\"black_ice\",\"ice\":[\"ice_01\"]}]}";
    static const char v2[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"v2\",\"name\":\"SCHEMA 1\",\"floors\":[{\"id\":0,\"type\":\"black_ice\",\"ice\":[\"ice_01\"]}]}";
    ScenarioLoader loader(registry);
    const bool catalogOk = registry.loadJson(catalog).success;
    const ScenarioImportResult localResult = loader.loadJson(local, scenario);
    const BlackIceDefinition* localDefinition = scenario.findLocalBlackIce("local_probe_01");
    const bool localOk = catalogOk && localResult.success && scenario.localBlackIceCount() == 1 &&
        scenario.floors()[0].blackIceDefinitions[0] == localDefinition && localDefinition != nullptr;
    runtime.~GameState(); new (&runtime) GameState(scenario.definition()); runtime.startRun();
    const bool runtimeStarted = runtime.jackIn();
    const BlackIceInstance* runtimeIce = runtime.activeBlackIceAt(0);
    const bool runtimeSetup = localOk && runtimeStarted && runtime.activeBlackIceCount() == 1 &&
        runtime.activeBlackIceSourceFloor(0) == 0 && runtimeIce != nullptr && runtimeIce->definition() == localDefinition &&
        !runtime.triggerCurrentBlackIce() && runtime.activeBlackIceCount() == 1;
    runtime.endPlayerTurn();
    IcePhaseResult& runtimePhase = debugIcePhaseResult();
    runtime.runIcePhase(customIceRules, runtimePhase);
    const bool runtimeOk = runtimeSetup && runtimePhase.attackCount == 1 &&
        runtimePhase.attack.executed && runtimePhase.attack.success && runtimePhase.attack.damage == 2 &&
        runtimePhase.attack.attackerDefinition == localDefinition;
    const bool globalOk = loader.loadJson(global, scenario).success &&
        scenario.floors()[0].blackIceDefinitions[0] == registry.findCustomByContentId("sd_probe_01");
    const bool mixedOk = loader.loadJson(mixed, scenario).success && scenario.floors()[0].blackIceCount == 3 &&
        scenario.floors()[0].blackIceDefinitions[0] == blackIceDefinitionByStableId("ice_01") &&
        scenario.floors()[0].blackIceDefinitions[1] == registry.findCustomByContentId("sd_probe_01") &&
        scenario.floors()[0].blackIceDefinitions[2] == scenario.findLocalBlackIce("local_probe_01");
    const BlackIceDefinition* retained = scenario.floors()[0].blackIceDefinitions[2];
    const bool unknownOk = !loader.loadJson(unknown, scenario).success && retained == scenario.floors()[0].blackIceDefinitions[2];
    const bool duplicateOk = !loader.loadJson(duplicate, scenario).success;
    const bool builtinOk = !loader.loadJson(builtinConflict, scenario).success;
    const bool globalConflictOk = !loader.loadJson(globalConflict, scenario).success;
    const bool v1Ok = loader.loadJson(v1, scenario).success && scenario.schemaVersion() == 1 &&
        scenario.floors()[0].blackIceDefinitions[0] == blackIceDefinitionByStableId("ice_01");
    const bool v2Ok = loader.loadJson(v2, scenario).success && scenario.schemaVersion() == 1 &&
        scenario.floors()[0].blackIceDefinitions[0] == blackIceDefinitionByStableId("ice_01");
    const bool mixedRuntimeSetup = loader.loadJson(mixedPassword, scenario).success &&
        scenario.floors()[0].id == 0 && scenario.floors()[0].type == FloorType::Password &&
        scenario.floors()[0].difficulty == 15 && scenario.floors()[0].blackIceCount == 1 &&
        scenario.floors()[0].blackIceDefinitions[0] == registry.findCustomByContentId("custom_black_ice");
    runtime.~GameState(); new (&runtime) GameState(scenario.definition()); runtime.startRun();
    const bool mixedStarted = runtime.jackIn();
    CombatDice mixedIceDice(10, 1, 2);
    NetRules mixedIceRules(mixedIceDice);
    const BlackIceInstance* mixedIce = runtime.activeBlackIceAt(0);
    const BlackIceDefinition* mixedDefinition = mixedIce != nullptr ? mixedIce->definition() : nullptr;
    const bool mixedRuntime = mixedRuntimeSetup && mixedStarted && runtime.activeBlackIceCount() == 1 &&
        runtime.activeBlackIceSourceFloor(0) == 0 && runtime.floorBlackIceCount() == 1 && mixedDefinition != nullptr &&
        strcmp(mixedDefinition->stableId, "custom_black_ice") == 0 && strcmp(mixedDefinition->displayName, "SIGNAL") == 0 &&
        mixedDefinition->perception == 3 && mixedDefinition->speed == 4 && mixedDefinition->attack == 4 &&
        mixedDefinition->defense == 3 && mixedDefinition->visualId == IceVisualId::Skull01 &&
        mixedDefinition->animationStyle == HostileAttackStyle::Pulse &&
        mixedDefinition->effect.type == BlackIceEffectType::ApplyFire && mixedDefinition->effect.damageDice == 1;
    runtime.endPlayerTurn();
    const bool mixedNoDuplicate = mixedRuntime && !runtime.triggerCurrentBlackIce() && runtime.activeBlackIceCount() == 1;
    IcePhaseResult& mixedPhase = debugIcePhaseResult();
    runtime.runIcePhase(mixedIceRules, mixedPhase);
    const bool mixedEncounter = mixedNoDuplicate && mixedPhase.attackCount == 1 && mixedPhase.attacks[0].result.executed &&
        mixedPhase.attacks[0].result.attackerDefinition == mixedDefinition && mixedPhase.attacks[0].result.success &&
        runtime.architecture().currentFloor()->type == FloorType::Password;
    runtime.~GameState(); new (&runtime) GameState(scenario.definition()); runtime.startRun();
    const bool mixedFreshStarted = runtime.jackIn();
    const BlackIceInstance* freshMixedIce = runtime.activeBlackIceAt(0);
    const bool mixedFresh = mixedFreshStarted && runtime.architecture().currentPosition() == 0 &&
        runtime.architecture().currentFloor()->type == FloorType::Password && runtime.activeBlackIceCount() == 1 &&
        runtime.activeBlackIceSourceFloor(0) == 0 && freshMixedIce != nullptr &&
        freshMixedIce->definition() != nullptr && freshMixedIce->definition() == mixedDefinition &&
        freshMixedIce->currentRez() == freshMixedIce->definition()->maxRez &&
        !runtime.triggerCurrentBlackIce() && runtime.activeBlackIceCount() == 1;
    const bool pass = localOk && runtimeOk && globalOk && mixedOk && unknownOk && duplicateOk && builtinOk && globalConflictOk && v1Ok && v2Ok && mixedEncounter && mixedFresh;
    Serial.printf("[ScenarioCustomIce][Local][Setup] %s\n[ScenarioCustomIce][Local] %s\n[ScenarioCustomIce][Global][Setup] %s\n[ScenarioCustomIce][Global] %s\n[ScenarioCustomIce][Mixed] %s\n[ScenarioCustomIce][UnknownRef] %s\n[ScenarioCustomIce][Duplicate] %s\n[ScenarioCustomIce][BuiltinConflict] %s\n[ScenarioCustomIce][GlobalConflict] %s\n[ScenarioCustomIce][Lifetime] %s\n[ScenarioCustomIce][StartFloor] %s\n[ScenarioCustomIce][FreshRun] %s\n[ScenarioCustomIce][Schema1A] %s\n[ScenarioCustomIce][Schema1B] %s\n[ScenarioCustomIce][MixedEncounter] %s\n[ScenarioCustomIce] %s\n",
        catalogOk ? "PASS" : "FAIL", localOk ? "PASS" : "FAIL", catalogOk ? "PASS" : "FAIL", globalOk ? "PASS" : "FAIL",
        mixedOk ? "PASS" : "FAIL", unknownOk ? "PASS" : "FAIL", duplicateOk ? "PASS" : "FAIL", builtinOk ? "PASS" : "FAIL",
        globalConflictOk ? "PASS" : "FAIL", runtimeOk ? "PASS" : "FAIL", mixedNoDuplicate ? "PASS" : "FAIL", mixedFresh ? "PASS" : "FAIL",
        v1Ok ? "PASS" : "FAIL", v2Ok ? "PASS" : "FAIL", mixedEncounter ? "PASS" : "FAIL", pass ? "PASS" : "FAIL");
}

static __attribute__((noinline)) bool runExpandedPlayerIceEffectCase(BlackIceType type, const char* label)
{
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    CyberdeckConfig config;
    config.quality = CyberdeckQuality::Excellent;
    config.programCount = 0;
    config.hardwareCount = 0;
    config.playerBlackIce[0] = type;
    config.playerBlackIceCount = 1;
    const bool configured = state.setCyberdeckConfig(config);
    state.startRun();
    state.jackIn();
    state.demon().active = false;
    state.architecture().moveForward();
    EnemyNetrunnerRuntime* enemy = state.floorEnemyNetrunner();
    PlayerBlackIceRuntime* ice = state.playerBlackIceAt(0);
    if (enemy != nullptr)
        for (size_t index = 0; index < enemy->cyberdeck.programCount(); ++index)
            if (Program* program = enemy->cyberdeck.programAt(index)) program->setStatus(ProgramStatus::Rezzed);
    const bool activated = ice != nullptr && enemy != nullptr &&
        state.activatePlayerBlackIce(0, enemy->runtimeId).executed;
    state.endPlayerTurn();
    const bool setup = configured && state.runState() == RunState::JackedIn && enemy != nullptr &&
        enemy->available() && ice != nullptr && ice->definition != nullptr && ice->definition->playerUsable &&
        ice->instance.active() && ice->instance.pursuing() && state.turnPhase() == TurnPhase::PlayerIce && activated;
    Serial.printf("[PlayerIceExpanded][%s][Setup] %s | Config=%s | Enemy=%s | Runtime=%s | Rezzed=%s | Phase=%s\n",
        label, setup ? "PASS" : "FAIL", configured ? "PASS" : "FAIL", enemy != nullptr ? "PASS" : "FAIL",
        ice != nullptr ? "PASS" : "FAIL", ice != nullptr && ice->instance.active() ? "PASS" : "FAIL",
        state.turnPhase() == TurnPhase::PlayerIce ? "PASS" : "FAIL");
    if (!setup) return false;

    CombatDice dice(10, 1, 4);
    NetRules rules(dice);
    IcePhaseResult& phase = debugIcePhaseResult();
    state.runPlayerBlackIcePhase(rules, phase);
    const CombatResult* attack = phase.attackCount == 1 ? &phase.attacks[0].result : nullptr;
    const bool identity = attack != nullptr && attack->executed && attack->success &&
        attack->attackerDefinition == ice->definition && isValidIceVisualId(ice->definition->visualId) &&
        isValidHostileAttackStyle(ice->definition->animationStyle);
    bool effect = false;
    if (type == BlackIceType::Ice02)
        effect = attack != nullptr && attack->programEffectApplied && attack->affectedProgramSlot >= 0 &&
            attack->resultingProgramStatus == ProgramStatus::Destroyed;
    else if (type == BlackIceType::Ice03)
        effect = attack != nullptr && attack->programEffectApplied && attack->affectedProgramSlot >= 0 &&
            attack->resultingProgramStatus == ProgramStatus::Derezzed && attack->damage > 0;
    else if (type == BlackIceType::Ice07)
        effect = attack != nullptr && attack->fireApplied && enemy->onFire;
    else if (type == BlackIceType::Ice08)
    {
        const uint8_t originalFloor = enemy->currentFloor;
        enemy->currentFloor = static_cast<uint8_t>(originalFloor + 1);
        EnemyPhaseResult& enemyPhase = debugEnemyPhaseResult();
        state.runEnemyPhase(rules, enemyPhase);
        effect = attack != nullptr && attack->moveAfter < attack->moveBefore &&
            enemy->runner.move() < enemy->runner.baseMove() && enemy->currentFloor == originalFloor + 1;
    }
    else if (type == BlackIceType::Ice11)
        effect = attack != nullptr && attack->intAfter < attack->intBefore && attack->refAfter < attack->refBefore &&
            attack->dexAfter < attack->dexBefore && enemy->runner.currentInt() == attack->intAfter;
    const bool pass = identity && effect;
    Serial.printf("[PlayerIceExpanded][%s] %s | Attack=%s | Effect=%s | Visual=%u | Animation=%u\n",
        label, pass ? "PASS" : "FAIL", identity ? "PASS" : "FAIL", effect ? "PASS" : "FAIL",
        static_cast<unsigned>(ice->definition->visualId), static_cast<unsigned>(ice->definition->animationStyle));
    return pass;
}

static __attribute__((noinline)) void runExpandedPlayerIceEffectsDebugTest()
{
    const bool carrionbyte = runExpandedPlayerIceEffectCase(BlackIceType::Ice02, "CARRIONBYTE");
    const bool needlecoil = runExpandedPlayerIceEffectCase(BlackIceType::Ice03, "NEEDLECOIL");
    const bool stingwire = runExpandedPlayerIceEffectCase(BlackIceType::Ice07, "STINGWIRE");
    const bool guttermesh = runExpandedPlayerIceEffectCase(BlackIceType::Ice08, "GUTTERMESH");
    const bool greymark = runExpandedPlayerIceEffectCase(BlackIceType::Ice11, "GREYMARK");
    const bool pass = carrionbyte && needlecoil && stingwire && guttermesh && greymark;
    Serial.printf("[PlayerIceExpanded] %s | Carrionbyte=%s | Needlecoil=%s | Stingwire=%s | Guttermesh=%s | Greymark=%s\n",
        pass ? "PASS" : "FAIL", carrionbyte ? "PASS" : "FAIL", needlecoil ? "PASS" : "FAIL",
        stingwire ? "PASS" : "FAIL", guttermesh ? "PASS" : "FAIL", greymark ? "PASS" : "FAIL");
}

void App::runPlayerBlackIceDebugTest()
{
    CyberdeckConfig config;
    config.quality = CyberdeckQuality::Excellent; config.programCount = 0;
    for (uint8_t i = 0; i < MAX_PLAYER_BLACK_ICE; ++i) config.playerBlackIce[i] = BlackIceType::Ice01;
    config.playerBlackIceCount = MAX_PLAYER_BLACK_ICE;
    CyberdeckConfig mixed = config; mixed.playerBlackIceCount = 1;
    mixed.programs[0] = ProgramId::Sword; mixed.programs[1] = ProgramId::Armor; mixed.programCount = 2;
    mixed.hardware[0] = HardwareId::BackupDrive; mixed.hardwareCount = 1;
    CyberdeckConfig rejected = mixed; rejected.quality = CyberdeckQuality::Poor;
    CyberdeckConfig unsupported = mixed; unsupported.playerBlackIce[0] = BlackIceType::Count;
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    const bool applied = state.setCyberdeckConfig(mixed);
    state.startRun(); state.jackIn(); state.architecture().moveForward();
    PlayerBlackIceRuntime* ice = state.playerBlackIceAt(0);
    EnemyNetrunnerRuntime* enemy = state.floorEnemyNetrunner();
    const bool runtimeIdentity = ice != nullptr && ice->definition == &ice01Definition() &&
        ice->instance.definition() == &ice01Definition();
    const uint8_t before = state.runner().remainingNetActions();
    const bool activated = ice != nullptr && enemy != nullptr && state.activatePlayerBlackIce(0, enemy->runtimeId).executed;
    ice->instance.takeRezDamage(3);
    const bool deactivated = state.deactivatePlayerBlackIce(0).executed;
    const bool lifecycle = activated && deactivated && before == state.runner().remainingNetActions() + 2 &&
        !ice->instance.active() && !ice->instance.pursuing() && ice->targetEnemyRuntimeId == 0 &&
        ice->instance.currentRez() == ice->instance.maxRez() - 3;
    state.startRun(); ice = state.playerBlackIceAt(0);
    const bool fresh = ice != nullptr && !ice->instance.active() && !ice->instance.pursuing() &&
        ice->targetEnemyRuntimeId == 0 && ice->instance.currentRez() == ice->instance.maxRez();
    const bool capacity = configuredPlayerBlackIceSlotCost(BlackIceType::Ice01) == 2 &&
        cyberdeckUsedSlots(mixed) == 6 && cyberdeckConfigValid(mixed) && !cyberdeckConfigValid(rejected) &&
        !cyberdeckConfigValid(unsupported) && cyberdeckConfigValid(config);
    const bool pass = applied && capacity && runtimeIdentity && lifecycle && fresh;
    if (!pass) Serial.printf("[Diag][PlayerBlackIce] used=%u active=%u target=%u rez=%d\n", cyberdeckUsedSlots(mixed),
        ice != nullptr && ice->instance.active(), ice != nullptr ? ice->targetEnemyRuntimeId : 0, ice != nullptr ? ice->instance.currentRez() : -1);
    Serial.printf("[PlayerBlackIce] %s | Config=%s | Bounds=%s | Runtime=%s | Lifecycle=%s | NewRun=%s\n",
        pass ? "PASS" : "FAIL", capacity ? "PASS" : "FAIL", cyberdeckConfigValid(config) ? "PASS" : "FAIL",
        applied && runtimeIdentity ? "PASS" : "FAIL", lifecycle ? "PASS" : "FAIL", fresh ? "PASS" : "FAIL");
}

static __attribute__((noinline)) GameState& setupPlayerBlackIceRepeatRoundFixture(uint16_t& enemyRuntimeId)
{
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    CyberdeckConfig config;
    config.quality = CyberdeckQuality::Excellent;
    config.programCount = 0;
    config.hardwareCount = 0;
    config.playerBlackIce[0] = BlackIceType::Ice01;
    config.playerBlackIce[1] = BlackIceType::Ice04;
    config.playerBlackIce[2] = BlackIceType::Ice05;
    config.playerBlackIceCount = 3;
    playerIceRepeatRoundDiagnostic.config = state.setCyberdeckConfig(config);

    state.startRun();
    state.jackIn();
    state.demon().active = false;
    state.architecture().moveForward();
    EnemyNetrunnerRuntime* enemy = state.floorEnemyNetrunner();
    enemyRuntimeId = enemy != nullptr ? enemy->runtimeId : 0;

    // This diagnostic's subject starts after REZ. Lifecycle and NET-action
    // economy are covered by their own tests; retain the normal runtime
    // invariant that every rezzed ICE has a live co-located target lock.
    if (enemy != nullptr)
    {
        for (size_t index = 0; index < 3; ++index)
        {
            PlayerBlackIceRuntime* ice = state.playerBlackIceAt(index);
            if (ice == nullptr) continue;
            ice->instance.setRezzed(true);
            ice->chasePosition = enemy->currentFloor;
            ice->targetEnemyRuntimeId = enemy->runtimeId;
            ice->instance.setPursuing(true);
        }
    }
    return state;
}

static __attribute__((noinline)) bool validatePlayerBlackIceRepeatRoundFixture(
    GameState& state, uint16_t enemyRuntimeId)
{
    const EnemyNetrunnerRuntime* enemy = state.enemyNetrunnerByRuntimeId(enemyRuntimeId);
    playerIceRepeatRoundDiagnostic.run = state.runState() == RunState::JackedIn;
    playerIceRepeatRoundDiagnostic.enemy = enemy != nullptr && enemy->available() &&
        enemy->currentFloor == state.architecture().currentPosition();
    playerIceRepeatRoundDiagnostic.iceCount = state.playerBlackIceCount() == 3;

    const PlayerBlackIceRuntime* ice0 = state.playerBlackIceAt(0);
    const PlayerBlackIceRuntime* ice1 = state.playerBlackIceAt(1);
    const PlayerBlackIceRuntime* ice2 = state.playerBlackIceAt(2);
    playerIceRepeatRoundDiagnostic.expectedId0 = ice0 != nullptr ? ice0->runtimeId : 0;
    playerIceRepeatRoundDiagnostic.expectedId1 = ice1 != nullptr ? ice1->runtimeId : 0;
    playerIceRepeatRoundDiagnostic.expectedId2 = ice2 != nullptr ? ice2->runtimeId : 0;
    playerIceRepeatRoundDiagnostic.ids = playerIceRepeatRoundDiagnostic.expectedId0 != 0 &&
        playerIceRepeatRoundDiagnostic.expectedId1 != 0 && playerIceRepeatRoundDiagnostic.expectedId2 != 0 &&
        playerIceRepeatRoundDiagnostic.expectedId0 != playerIceRepeatRoundDiagnostic.expectedId1 &&
        playerIceRepeatRoundDiagnostic.expectedId0 != playerIceRepeatRoundDiagnostic.expectedId2 &&
        playerIceRepeatRoundDiagnostic.expectedId1 != playerIceRepeatRoundDiagnostic.expectedId2;
    playerIceRepeatRoundDiagnostic.rezzed = ice0 != nullptr && ice1 != nullptr && ice2 != nullptr &&
        ice0->instance.active() && ice1->instance.active() && ice2->instance.active();
    playerIceRepeatRoundDiagnostic.targetState = enemy != nullptr && ice0 != nullptr && ice1 != nullptr && ice2 != nullptr &&
        ice0->chasePosition == enemy->currentFloor && ice1->chasePosition == enemy->currentFloor &&
        ice2->chasePosition == enemy->currentFloor && ice0->targetEnemyRuntimeId == enemyRuntimeId &&
        ice1->targetEnemyRuntimeId == enemyRuntimeId && ice2->targetEnemyRuntimeId == enemyRuntimeId &&
        ice0->instance.pursuing() && ice1->instance.pursuing() && ice2->instance.pursuing();
    playerIceRepeatRoundDiagnostic.eligible = ice0 != nullptr && ice1 != nullptr && ice2 != nullptr &&
        state.findEligiblePlayerBlackIceTarget(*ice0) == enemy &&
        state.findEligiblePlayerBlackIceTarget(*ice1) == enemy &&
        state.findEligiblePlayerBlackIceTarget(*ice2) == enemy;
    state.endPlayerTurn();
    playerIceRepeatRoundDiagnostic.playerIcePhase = state.turnPhase() == TurnPhase::PlayerIce;
    return playerIceRepeatRoundDiagnostic.config && playerIceRepeatRoundDiagnostic.run &&
        playerIceRepeatRoundDiagnostic.enemy && playerIceRepeatRoundDiagnostic.iceCount &&
        playerIceRepeatRoundDiagnostic.rezzed && playerIceRepeatRoundDiagnostic.eligible &&
        playerIceRepeatRoundDiagnostic.ids && playerIceRepeatRoundDiagnostic.targetState &&
        playerIceRepeatRoundDiagnostic.playerIcePhase;
}

static __attribute__((noinline)) bool clearPlayerBlackIceRepeatRoundLocks(GameState& state, uint16_t enemyRuntimeId)
{
    bool cleared = true;
    for (size_t index = 0; index < 3; ++index)
    {
        PlayerBlackIceRuntime* ice = state.playerBlackIceAt(index);
        if (ice == nullptr || ice->targetEnemyRuntimeId != enemyRuntimeId) { cleared = false; continue; }
        ice->targetEnemyRuntimeId = 0;
        ice->instance.stopPursuing();
        cleared = cleared && ice->instance.active() && ice->targetEnemyRuntimeId == 0 && !ice->instance.pursuing();
    }
    return cleared;
}

static __attribute__((noinline)) bool executePlayerBlackIceRepeatRoundScenario(
    GameState& state, uint16_t enemyRuntimeId)
{
    CombatDice playerDice(1, 10, 0);
    NetRules playerRules(playerDice);
    IcePhaseResult& first = debugPlayerIceRepeatRoundPhaseResult();
    state.runPlayerBlackIcePhase(playerRules, first);
    const uint8_t firstCount = static_cast<uint8_t>(first.attackCount);
    const uint16_t firstId0 = firstCount > 0 ? first.attacks[0].runtimeId : 0;
    const uint16_t firstId1 = firstCount > 1 ? first.attacks[1].runtimeId : 0;
    const uint16_t firstId2 = firstCount > 2 ? first.attacks[2].runtimeId : 0;
    const uint16_t firstTarget0 = firstCount > 0 ? first.attacks[0].targetEnemyRuntimeId : 0;
    const uint16_t firstTarget1 = firstCount > 1 ? first.attacks[1].targetEnemyRuntimeId : 0;
    const uint16_t firstTarget2 = firstCount > 2 ? first.attacks[2].targetEnemyRuntimeId : 0;
    playerIceRepeatRoundDiagnostic.r1count = firstCount;
    playerIceRepeatRoundDiagnostic.round1 = firstCount == 3 &&
        firstId0 == playerIceRepeatRoundDiagnostic.expectedId0 &&
        firstId1 == playerIceRepeatRoundDiagnostic.expectedId1 &&
        firstId2 == playerIceRepeatRoundDiagnostic.expectedId2 &&
        firstTarget0 == enemyRuntimeId && firstTarget1 == enemyRuntimeId && firstTarget2 == enemyRuntimeId;

    IcePhaseResult& duplicate = debugPlayerIceRepeatRoundPhaseResult();
    state.runPlayerBlackIcePhase(playerRules, duplicate);
    playerIceRepeatRoundDiagnostic.duplicateCount = static_cast<uint8_t>(duplicate.attackCount);
    playerIceRepeatRoundDiagnostic.duplicateGuard = !duplicate.executed &&
        playerIceRepeatRoundDiagnostic.duplicateCount == 0 && state.turnPhase() == TurnPhase::Enemy;

    playerIceRepeatRoundDiagnostic.cleared = clearPlayerBlackIceRepeatRoundLocks(state, enemyRuntimeId);
    FixedDice phaseDice(1);
    NetRules phaseRules(phaseDice);
    EnemyPhaseResult& enemyPhase = debugPlayerIceRepeatRoundEnemyResult();
    state.runEnemyPhase(phaseRules, enemyPhase);
    IcePhaseResult& icePhase = debugPlayerIceRepeatRoundPhaseResult();
    if (state.turnPhase() == TurnPhase::Ice) state.runIcePhase(phaseRules, icePhase);
    const EnemyNetrunnerRuntime* enemy = state.enemyNetrunnerByRuntimeId(enemyRuntimeId);
    if (state.turnPhase() == TurnPhase::Player && enemy != nullptr && enemy->available()) state.endPlayerTurn();
    const bool nextPhase = state.turnPhase() == TurnPhase::PlayerIce;

    IcePhaseResult& second = debugPlayerIceRepeatRoundPhaseResult();
    state.runPlayerBlackIcePhase(playerRules, second);
    const uint8_t secondCount = static_cast<uint8_t>(second.attackCount);
    const uint16_t secondId0 = secondCount > 0 ? second.attacks[0].runtimeId : 0;
    const uint16_t secondId1 = secondCount > 1 ? second.attacks[1].runtimeId : 0;
    const uint16_t secondId2 = secondCount > 2 ? second.attacks[2].runtimeId : 0;
    const uint16_t secondTarget0 = secondCount > 0 ? second.attacks[0].targetEnemyRuntimeId : 0;
    const uint16_t secondTarget1 = secondCount > 1 ? second.attacks[1].targetEnemyRuntimeId : 0;
    const uint16_t secondTarget2 = secondCount > 2 ? second.attacks[2].targetEnemyRuntimeId : 0;
    playerIceRepeatRoundDiagnostic.r2count = secondCount;
    playerIceRepeatRoundDiagnostic.r2id0 = secondId0;
    playerIceRepeatRoundDiagnostic.r2id1 = secondId1;
    playerIceRepeatRoundDiagnostic.r2id2 = secondId2;
    playerIceRepeatRoundDiagnostic.ordered = secondId0 == playerIceRepeatRoundDiagnostic.expectedId0 &&
        secondId1 == playerIceRepeatRoundDiagnostic.expectedId1 && secondId2 == playerIceRepeatRoundDiagnostic.expectedId2;
    playerIceRepeatRoundDiagnostic.round2 = nextPhase && secondCount == 3 &&
        playerIceRepeatRoundDiagnostic.ordered && secondTarget0 == enemyRuntimeId &&
        secondTarget1 == enemyRuntimeId && secondTarget2 == enemyRuntimeId;
    return playerIceRepeatRoundDiagnostic.round1 && playerIceRepeatRoundDiagnostic.duplicateGuard &&
        playerIceRepeatRoundDiagnostic.cleared && playerIceRepeatRoundDiagnostic.round2;
}

static __attribute__((noinline)) bool runPlayerBlackIceRepeatRoundDebugTest()
{
    playerIceRepeatRoundDiagnostic = PlayerIceRepeatRoundDiagnosticSnapshot();
    uint16_t enemyRuntimeId = 0;
    GameState& state = setupPlayerBlackIceRepeatRoundFixture(enemyRuntimeId);
    const bool setup = validatePlayerBlackIceRepeatRoundFixture(state, enemyRuntimeId);
    Serial.printf("[PlayerBlackIceRepeatRound][Setup] %s | Config=%s | Run=%s | Enemy=%s | IceCount=%s | Rezzed=%s | Eligible=%s | IDs=%s | Target=%s | PlayerIce=%s\n",
        setup ? "PASS" : "FAIL", playerIceRepeatRoundDiagnostic.config ? "PASS" : "FAIL",
        playerIceRepeatRoundDiagnostic.run ? "PASS" : "FAIL", playerIceRepeatRoundDiagnostic.enemy ? "PASS" : "FAIL",
        playerIceRepeatRoundDiagnostic.iceCount ? "PASS" : "FAIL", playerIceRepeatRoundDiagnostic.rezzed ? "PASS" : "FAIL",
        playerIceRepeatRoundDiagnostic.eligible ? "PASS" : "FAIL", playerIceRepeatRoundDiagnostic.ids ? "PASS" : "FAIL",
        playerIceRepeatRoundDiagnostic.targetState ? "PASS" : "FAIL", playerIceRepeatRoundDiagnostic.playerIcePhase ? "PASS" : "FAIL");
    if (!setup) return false;

    const bool pass = executePlayerBlackIceRepeatRoundScenario(state, enemyRuntimeId);
    Serial.printf("[PlayerBlackIceRepeatRound] %s | Round1=%s | Duplicate=%s | Cleared=%s | Round2=%s | Ordered=%s | Counts=%u/%u/%u | IDs=%u,%u,%u\n",
        pass ? "PASS" : "FAIL", playerIceRepeatRoundDiagnostic.round1 ? "PASS" : "FAIL",
        playerIceRepeatRoundDiagnostic.duplicateGuard ? "PASS" : "FAIL", playerIceRepeatRoundDiagnostic.cleared ? "PASS" : "FAIL",
        playerIceRepeatRoundDiagnostic.round2 ? "PASS" : "FAIL", playerIceRepeatRoundDiagnostic.ordered ? "PASS" : "FAIL",
        static_cast<unsigned>(playerIceRepeatRoundDiagnostic.r1count),
        static_cast<unsigned>(playerIceRepeatRoundDiagnostic.duplicateCount),
        static_cast<unsigned>(playerIceRepeatRoundDiagnostic.r2count),
        static_cast<unsigned>(playerIceRepeatRoundDiagnostic.r2id0),
        static_cast<unsigned>(playerIceRepeatRoundDiagnostic.r2id1),
        static_cast<unsigned>(playerIceRepeatRoundDiagnostic.r2id2));
    return pass;
}

static __attribute__((noinline)) bool runPlayerBlackIceTargetPreviewDebugTest()
{
    playerIceTargetPreviewDiagnostic = PlayerIceTargetPreviewDiagnosticSnapshot();
    static FloorDefinition floors[2];
    static bool initialized = false;
    if (!initialized)
    {
        floors[0] = FloorDefinition();
        floors[0].id = 1;
        floors[0].type = FloorType::Empty;
        floors[0].enemyNetrunner = &nullbyteDefinition();
        floors[1] = FloorDefinition();
        floors[1].id = 2;
        floors[1].type = FloorType::Empty;
        floors[1].enemyNetrunner = &zerDefinition();
        initialized = true;
    }
    static const ArchitectureDefinition definition = {
        "player_ice_target_preview", "PLAYER ICE TARGET PREVIEW", "Resolver regression fixture.", floors, 2};
    GameState& state = debugTestState(definition);
    CyberdeckConfig config;
    config.quality = CyberdeckQuality::Excellent;
    config.playerBlackIce[0] = BlackIceType::Ice01;
    config.playerBlackIceCount = 1;
    state.setCyberdeckConfig(config);
    state.startRun();
    state.jackIn();
    PlayerBlackIceRuntime* ice = state.playerBlackIceAt(0);
    EnemyNetrunnerRuntime* first = state.enemyNetrunnerAt(0);
    EnemyNetrunnerRuntime* second = state.enemyNetrunnerAt(1);
    if (ice == nullptr || first == nullptr || second == nullptr)
    {
        playerIceTargetPreviewDiagnostic.stage = 1;
        playerIceTargetPreviewDiagnostic.reason = 1;
        return false;
    }
    first->currentFloor = 0;
    second->currentFloor = 0;
    ice->chasePosition = 0;
    ice->targetEnemyRuntimeId = 0;
    ice->instance.stopPursuing();

    const uint16_t firstId = first->runtimeId;
    const uint16_t secondId = second->runtimeId;
    const EnemyNetrunnerRuntime* unrezzedPreview = state.findPlayerBlackIcePotentialTarget(*ice);
    const bool unrezzedName = strcmp(state.playerBlackIceTargetDisplayName(*ice), "NULLBYTE") == 0;
    const bool unrezzedPreviewOk = unrezzedPreview != nullptr && unrezzedPreview->runtimeId == firstId && unrezzedName;
    const bool unrezzedGameplayRejected = state.findEligiblePlayerBlackIceTarget(*ice) == nullptr;
    const bool unrezzedReadOnly = ice->targetEnemyRuntimeId == 0 && !ice->instance.pursuing() &&
        !ice->instance.active();

    ice->instance.setRezzed(true);
    ice->targetEnemyRuntimeId = secondId;
    ice->instance.setPursuing(true);
    const uint16_t lockedId = ice->targetEnemyRuntimeId;
    const bool lockedPursuit = ice->instance.pursuing();
    const uint8_t lockedFloor = second->currentFloor;
    const EnemyNetrunnerRuntime* locked = state.findEligiblePlayerBlackIceTarget(*ice);
    const bool validStored = locked != nullptr && locked->runtimeId == secondId &&
        strcmp(state.playerBlackIceTargetDisplayName(*ice), "Zer=0") == 0;
    const bool lockedReadOnly = ice->targetEnemyRuntimeId == lockedId &&
        ice->instance.pursuing() == lockedPursuit && second->currentFloor == lockedFloor;

    ice->targetEnemyRuntimeId = 0;
    ice->instance.stopPursuing();
    const EnemyNetrunnerRuntime* reacquired = state.findEligiblePlayerBlackIceTarget(*ice);
    const bool clearedSameFloor = reacquired != nullptr && reacquired->runtimeId == firstId &&
        strcmp(state.playerBlackIceTargetDisplayName(*ice), "NULLBYTE") == 0 &&
        ice->targetEnemyRuntimeId == 0 && !ice->instance.pursuing();

    first->currentFloor = 1;
    second->currentFloor = 0;
    const EnemyNetrunnerRuntime* ordered = state.findEligiblePlayerBlackIceTarget(*ice);
    const bool deterministicOrder = ordered != nullptr && ordered->runtimeId == secondId;
    second->currentFloor = 1;
    const bool otherFloor = state.findEligiblePlayerBlackIceTarget(*ice) == nullptr;
    const bool otherFloorGameplay = state.findEligiblePlayerBlackIceTarget(*ice) == nullptr;
    second->currentFloor = 0;
    second->unsafeJackOut();
    const bool terminalInactive = state.findEligiblePlayerBlackIceTarget(*ice) == nullptr;
    const bool terminalInactivePreview = state.findPlayerBlackIcePotentialTarget(*ice) == nullptr;
    first->currentFloor = 1;
    const bool pass = validStored && lockedReadOnly && clearedSameFloor && deterministicOrder &&
        terminalInactive && terminalInactivePreview && otherFloor && otherFloorGameplay &&
        unrezzedPreviewOk && unrezzedGameplayRejected && unrezzedReadOnly;
    playerIceTargetPreviewDiagnostic.validStored = validStored;
    playerIceTargetPreviewDiagnostic.lockedReadOnly = lockedReadOnly;
    playerIceTargetPreviewDiagnostic.clearedSameFloor = clearedSameFloor;
    playerIceTargetPreviewDiagnostic.deterministicOrder = deterministicOrder;
    playerIceTargetPreviewDiagnostic.terminalInactive = terminalInactive;
    playerIceTargetPreviewDiagnostic.terminalInactivePreview = terminalInactivePreview;
    playerIceTargetPreviewDiagnostic.otherFloor = otherFloor;
    playerIceTargetPreviewDiagnostic.otherFloorGameplay = otherFloorGameplay;
    playerIceTargetPreviewDiagnostic.unrezzedPreviewOk = unrezzedPreviewOk;
    playerIceTargetPreviewDiagnostic.unrezzedGameplayRejected = unrezzedGameplayRejected;
    playerIceTargetPreviewDiagnostic.unrezzedReadOnly = unrezzedReadOnly;
    playerIceTargetPreviewDiagnostic.iceFloor = ice->chasePosition;
    playerIceTargetPreviewDiagnostic.enemy0Floor = first->currentFloor;
    playerIceTargetPreviewDiagnostic.enemy1Floor = second->currentFloor;
    playerIceTargetPreviewDiagnostic.potentialTargetId = unrezzedPreview != nullptr ? unrezzedPreview->runtimeId : 0;
    playerIceTargetPreviewDiagnostic.strictTargetId = locked != nullptr ? locked->runtimeId : 0;
    return pass;
}

void App::runPlayerBlackIceCombatDebugTest()
{
    const BlackIceDefinition& tracejackal = ice01Definition();
    const BlackIceDefinition& deadlock = ice04Definition();
    const BlackIceDefinition& ghostpulse = ice05Definition();
    const BlackIceDefinition& packetsaw = ice06Definition();
    const BlackIceDefinition& stormrazor = ice09Definition();
    const BlackIceDefinition& glasscat = ice10Definition();
    const BlackIceDefinition& breacher = ice12Definition();
    const bool definitionsOk = tracejackal.perception == 5 && tracejackal.speed == 7 &&
        tracejackal.attack == 5 && tracejackal.defense == 3 && tracejackal.maxRez == 18 &&
        tracejackal.effect.type == BlackIceEffectType::DamageOnly && tracejackal.effect.damageDice == 2 &&
        deadlock.perception == 5 && deadlock.speed == 3 && deadlock.attack == 7 && deadlock.defense == 5 &&
        deadlock.maxRez == 26 && deadlock.effect.type == BlackIceEffectType::DamageAndNavigationLock &&
        deadlock.effect.damageDice == 2 && ghostpulse.perception == 5 && ghostpulse.speed == 5 &&
        ghostpulse.attack == 4 && ghostpulse.defense == 3 && ghostpulse.maxRez == 14 &&
        ghostpulse.effect.type == BlackIceEffectType::DamageAndNextTurnNetActionPenalty &&
        ghostpulse.effect.damageDice == 1 && ghostpulse.effect.netActionPenalty == 1 &&
        ghostpulse.effect.minimumNetActions == 2 && packetsaw.perception == 5 && packetsaw.speed == 7 &&
        packetsaw.attack == 5 && packetsaw.defense == 3 && packetsaw.maxRez == 19 &&
        packetsaw.effect.type == BlackIceEffectType::ProgramDamageDestroyAtZero && packetsaw.effect.damageDice == 3 &&
        stormrazor.perception == 7 && stormrazor.speed == 5 && stormrazor.attack == 6 && stormrazor.defense == 5 &&
        stormrazor.maxRez == 27 && stormrazor.effect.type == BlackIceEffectType::ProgramDamageDestroyAtZero &&
        stormrazor.effect.damageDice == 5 && glasscat.perception == 8 && glasscat.speed == 7 &&
        glasscat.attack == 5 && glasscat.defense == 3 && glasscat.maxRez == 23 &&
        glasscat.effect.type == BlackIceEffectType::ProgramDamageDestroyAtZero && glasscat.effect.damageDice == 4 &&
        breacher.perception == 3 && breacher.speed == 3 && breacher.attack == 7 && breacher.defense == 5 &&
        breacher.maxRez == 24 && breacher.effect.type == BlackIceEffectType::DamageAndUnsafeJackOut &&
        breacher.effect.damageDice == 2;
    Netrunner enemy("ENEMY", 4, 30, 3); Cyberdeck deck; deck.clear(CyberdeckQuality::Standard);
    BlackIceInstance Tracejackal(tracejackal);
    CombatDice hitDice(10, 1, 7); NetRules hitRules(hitDice);
    const CombatResult hit = hitRules.blackIceAttack(Tracejackal, enemy, &deck);
    BlackIceInstance missTracejackal(tracejackal); CombatDice missDice(1, 10, 7); NetRules missRules(missDice);
    const CombatResult miss = missRules.blackIceAttack(missTracejackal, enemy, &deck);
    deck.addProgram(makeProgram(ProgramId::Armor)); deck.programAt(0)->setStatus(ProgramStatus::Rezzed);
    Netrunner armored("ARMORED", 4, 30, 3); BlackIceInstance armorTracejackal(tracejackal);
    CombatDice armorDice(10, 1, 7); NetRules armorRules(armorDice);
    const CombatResult armor = armorRules.blackIceAttack(armorTracejackal, armored, &deck);
    Cyberdeck programs; programs.clear(CyberdeckQuality::Standard); programs.addProgram(makeProgram(ProgramId::Sword)); programs.programAt(0)->setStatus(ProgramStatus::Rezzed);
    Netrunner programTarget("PROGRAM", 4, 30, 3);
    BlackIceInstance Packetsaw(ice06Definition()); CombatDice PacketsawDice(10, 1, 20); NetRules PacketsawRules(PacketsawDice);
    const CombatResult PacketsawHit = PacketsawRules.blackIceAttack(Packetsaw, programTarget, &programs);
    programs.clear(CyberdeckQuality::Standard); programs.addProgram(makeProgram(ProgramId::Sword)); programs.programAt(0)->setStatus(ProgramStatus::Rezzed);
    BlackIceInstance Stormrazor(ice09Definition()); CombatDice StormrazorDice(10, 1, 20); NetRules StormrazorRules(StormrazorDice);
    const CombatResult StormrazorHit = StormrazorRules.blackIceAttack(Stormrazor, programTarget, &programs);
    programs.clear(CyberdeckQuality::Standard); programs.addProgram(makeProgram(ProgramId::Sword)); programs.programAt(0)->setStatus(ProgramStatus::Rezzed);
    BlackIceInstance Glasscat(ice10Definition()); CombatDice GlasscatDice(10, 1, 20); NetRules GlasscatRules(GlasscatDice);
    const CombatResult GlasscatHit = GlasscatRules.blackIceAttack(Glasscat, programTarget, &programs);
    Cyberdeck empty; empty.clear(CyberdeckQuality::Standard); BlackIceInstance noTarget(ice06Definition());
    CombatDice noTargetDice(10, 1, 20); NetRules noTargetRules(noTargetDice);
    const CombatResult noProgram = noTargetRules.blackIceAttack(noTarget, programTarget, &empty);
    BlackIceInstance Breacher(ice12Definition()); CombatDice BreacherDice(10, 1, 9); NetRules BreacherRules(BreacherDice);
    const CombatResult BreacherHit = BreacherRules.blackIceAttack(Breacher, programTarget, &empty);
    BlackIceInstance Deadlock(ice04Definition()); CombatDice DeadlockDice(10, 1, 9); NetRules DeadlockRules(DeadlockDice);
    const CombatResult DeadlockHit = DeadlockRules.blackIceAttack(Deadlock, programTarget, &empty);
    GameState& targetState = debugTestState(BuiltInArchitectures::militechTestNet());
    CyberdeckConfig targetConfig; targetConfig.quality = CyberdeckQuality::Excellent; targetConfig.programCount = 0;
    targetConfig.playerBlackIce[0] = BlackIceType::Ice12; targetConfig.playerBlackIceCount = 1;
    targetState.setCyberdeckConfig(targetConfig); targetState.startRun(); targetState.jackIn();
    for (uint8_t step = 0; step < 6; ++step) targetState.architecture().moveForward();
    EnemyNetrunnerRuntime* targetEnemy = targetState.floorEnemyNetrunner();
    const bool BreacherActivated = targetEnemy != nullptr &&
        targetState.activatePlayerBlackIce(0, targetEnemy->runtimeId).executed;
    targetState.endPlayerTurn();
    IcePhaseResult& BreacherPhase = debugIcePhaseResult();
    CombatDice BreacherTargetDice(10, 1, 9); NetRules BreacherTargetRules(BreacherTargetDice);
    targetState.runPlayerBlackIcePhase(BreacherTargetRules, BreacherPhase);
    static PlayerIceAnimationDiagnosticSnapshot breacherAnimation;
    breacherAnimation = PlayerIceAnimationDiagnosticSnapshot();
    if (BreacherPhase.attackCount == 1)
    {
        const CombatResult& attack = BreacherPhase.attacks[0].result;
        breacherAnimation.executed = attack.executed;
        breacherAnimation.dispatch = GameUIController::shouldAnimatePlayerBlackIceAttackForDebug(attack, true);
        breacherAnimation.attackerDefinition = attack.attackerDefinition;
        if (attack.attackerDefinition != nullptr)
        {
            breacherAnimation.visualId = attack.attackerDefinition->visualId;
            breacherAnimation.animationStyle = attack.attackerDefinition->animationStyle;
        }
    }
    const bool BreacherTargetPresentation = BreacherActivated && BreacherPhase.attackCount == 1 &&
        BreacherPhase.attacks[0].targetEnemyRuntimeId == (targetEnemy != nullptr ? targetEnemy->runtimeId : 0) &&
        targetEnemy != nullptr && targetEnemy->definition != nullptr &&
        strcmp(targetEnemy->definition->name, "Zer=0") == 0 &&
        strcmp(targetState.enemyNetrunnerNameByRuntimeId(BreacherPhase.attacks[0].targetEnemyRuntimeId), "Zer=0") == 0;
    GameState& GhostpulseState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    CyberdeckConfig GhostpulseConfig; GhostpulseConfig.quality = CyberdeckQuality::Standard; GhostpulseConfig.programCount = 0;
    GhostpulseConfig.playerBlackIce[0] = BlackIceType::Ice05; GhostpulseConfig.playerBlackIceCount = 1;
    GhostpulseState.setCyberdeckConfig(GhostpulseConfig); GhostpulseState.startRun(); GhostpulseState.jackIn(); GhostpulseState.startTurn(); GhostpulseState.architecture().moveForward();
    EnemyNetrunnerRuntime* GhostpulseEnemy = GhostpulseState.floorEnemyNetrunner();
    PlayerBlackIceRuntime* GhostpulseIce = GhostpulseState.playerBlackIceAt(0);
    const bool GhostpulseRunOk = GhostpulseState.runState() == RunState::JackedIn;
    const bool GhostpulsePhaseOk = GhostpulseState.turnPhase() == TurnPhase::Player;
    const bool GhostpulseEnemyOk = GhostpulseEnemy != nullptr;
    const bool GhostpulseEnemyActiveOk = GhostpulseEnemy != nullptr && GhostpulseEnemy->available();
    const bool GhostpulseIceOk = GhostpulseIce != nullptr;
    const bool GhostpulseIceRezzedOk = GhostpulseIce != nullptr && GhostpulseIce->instance.active();
    const bool GhostpulseIceFloorOk = GhostpulseIce != nullptr && GhostpulseEnemy != nullptr &&
        GhostpulseIce->chasePosition == GhostpulseEnemy->currentFloor;
    const bool GhostpulseIcePursuingOk = GhostpulseIce != nullptr && GhostpulseIce->instance.pursuing();
    const bool GhostpulseTargetOk = GhostpulseIce != nullptr && GhostpulseIce->targetEnemyRuntimeId != 0;
    const bool GhostpulseTargetMatchesOk = GhostpulseIce != nullptr && GhostpulseEnemy != nullptr &&
        GhostpulseIce->targetEnemyRuntimeId == GhostpulseEnemy->runtimeId;
    const bool GhostpulseActionOk = GhostpulseState.runner().remainingNetActions() > 0;
    NETRUN_VERBOSE_PRINTF("[Diag][PlayerBlackIceCombat][Ghostpulse][Pre] runActive=%u phase=%u enemyFound=%u enemyActive=%u enemyFloor=%u playerIceFound=%u playerIceRezzed=%u playerIceFloor=%u playerIcePursuing=%u targetRuntimeId=%u targetMatches=%u alreadyActed=0 currentFloor=%u\n",
        GhostpulseRunOk ? 1U : 0U, static_cast<unsigned>(GhostpulseState.turnPhase()), GhostpulseEnemyOk ? 1U : 0U,
        GhostpulseEnemyActiveOk ? 1U : 0U, GhostpulseEnemy != nullptr ? static_cast<unsigned>(GhostpulseEnemy->currentFloor) : 255U,
        GhostpulseIceOk ? 1U : 0U, GhostpulseIceRezzedOk ? 1U : 0U, GhostpulseIceFloorOk ? 1U : 0U,
        GhostpulseIcePursuingOk ? 1U : 0U, GhostpulseIce != nullptr ? static_cast<unsigned>(GhostpulseIce->targetEnemyRuntimeId) : 0U,
        GhostpulseTargetMatchesOk ? 1U : 0U, static_cast<unsigned>(GhostpulseState.architecture().currentPosition()));
    NETRUN_VERBOSE_PRINTF("[Diag][PlayerBlackIceCombat][Ghostpulse][PreFlags] runOk=%u phaseOk=%u iceOk=%u rezzedOk=%u targetOk=%u sameFloorOk=%u actionAvailableOk=%u oncePerTurnOk=1\n",
        GhostpulseRunOk ? 1U : 0U, GhostpulsePhaseOk ? 1U : 0U, GhostpulseIceOk ? 1U : 0U,
        GhostpulseIceRezzedOk ? 1U : 0U, GhostpulseTargetOk ? 1U : 0U, GhostpulseIceFloorOk ? 1U : 0U,
        GhostpulseActionOk ? 1U : 0U);
    const bool GhostpulseActivated = GhostpulseEnemy != nullptr && GhostpulseState.activatePlayerBlackIce(0, GhostpulseEnemy->runtimeId).executed;
    GhostpulseState.endPlayerTurn();
    // NETRUNNER_COMBAT_TEST contains an active LATCH: advance the real Demon
    // phase before expecting deployed Player ICE to receive PlayerIce.
    FixedDice GhostpulseDemonDice(1); NetRules GhostpulseDemonRules(GhostpulseDemonDice);
    DemonPhaseResult& GhostpulseDemonPhase = debugDemonPhaseResult();
    if (GhostpulseState.turnPhase() == TurnPhase::Demon)
        GhostpulseState.runDemonPhase(GhostpulseDemonRules, GhostpulseDemonPhase);
    const bool GhostpulseAttackPhaseOk = GhostpulseState.turnPhase() == TurnPhase::PlayerIce;
    GhostpulseIce = GhostpulseState.playerBlackIceAt(0);
    const bool GhostpulseRuntimeOk = GhostpulseIce != nullptr && GhostpulseIce->occupied;
    const bool GhostpulseAttackRezzedOk = GhostpulseRuntimeOk && GhostpulseIce->instance.active();
    const bool GhostpulseAttackPursuingOk = GhostpulseRuntimeOk && GhostpulseIce->instance.pursuing();
    const bool GhostpulseAttackTargetOk = GhostpulseRuntimeOk && GhostpulseIce->targetEnemyRuntimeId != 0;
    const bool GhostpulseAttackSameFloorOk = GhostpulseRuntimeOk && GhostpulseEnemy != nullptr &&
        GhostpulseIce->chasePosition == GhostpulseEnemy->currentFloor;
    NETRUN_VERBOSE_PRINTF("[Diag][PlayerBlackIceCombat][Ghostpulse][Guard] actualPhase=%u requiredPhase=%u phaseOk=%u runOk=%u runtimeOk=%u rezzedOk=%u targetOk=%u sameFloorOk=%u actedOk=1\n",
        static_cast<unsigned>(GhostpulseState.turnPhase()), static_cast<unsigned>(TurnPhase::PlayerIce),
        GhostpulseAttackPhaseOk ? 1U : 0U, GhostpulseState.runState() == RunState::JackedIn ? 1U : 0U,
        GhostpulseRuntimeOk ? 1U : 0U, GhostpulseAttackRezzedOk ? 1U : 0U,
        GhostpulseAttackTargetOk ? 1U : 0U, GhostpulseAttackSameFloorOk ? 1U : 0U);
    NETRUN_VERBOSE_PRINTF("[Diag][PlayerBlackIceCombat][Ghostpulse][AttackPre] runOk=%u phaseOk=%u iceOk=%u rezzedOk=%u targetOk=%u sameFloorOk=%u actionAvailableOk=%u targetRuntimeId=%u\n",
        GhostpulseState.runState() == RunState::JackedIn ? 1U : 0U,
        GhostpulseState.turnPhase() == TurnPhase::PlayerIce ? 1U : 0U,
        GhostpulseIce != nullptr ? 1U : 0U,
        GhostpulseIce != nullptr && GhostpulseIce->instance.active() ? 1U : 0U,
        GhostpulseIce != nullptr && GhostpulseIce->targetEnemyRuntimeId != 0 ? 1U : 0U,
        GhostpulseIce != nullptr && GhostpulseEnemy != nullptr && GhostpulseIce->chasePosition == GhostpulseEnemy->currentFloor ? 1U : 0U,
        GhostpulseEnemy != nullptr && GhostpulseEnemy->runner.remainingNetActions() > 0 ? 1U : 0U,
        GhostpulseIce != nullptr ? static_cast<unsigned>(GhostpulseIce->targetEnemyRuntimeId) : 0U);
    const uint8_t enemyActionsBefore = GhostpulseEnemy != nullptr ? GhostpulseEnemy->runner.remainingNetActions() : 0;
    const uint8_t enemyPendingBefore = GhostpulseEnemy != nullptr ? GhostpulseEnemy->nextTurnNetActionPenalty : 0;
    CombatDice GhostpulseDice(10, 1, 6); NetRules GhostpulseRules(GhostpulseDice);
    NETRUN_VERBOSE_PRINTF("[Diag][PlayerIceGhostpulse][CALL] phase=%u expected=%u callerState=%p\n",
        static_cast<unsigned>(GhostpulseState.turnPhase()), static_cast<unsigned>(TurnPhase::PlayerIce), &GhostpulseState);
    IcePhaseResult& GhostpulseResult = debugIcePhaseResult(); GhostpulseState.runPlayerBlackIcePhase(GhostpulseRules, GhostpulseResult);
    const CombatResult GhostpulseAttack = GhostpulseResult.attack;
    const uint8_t ghostpulseDamageDice = GhostpulseDice.lastD6Count();
    const uint8_t enemyPendingAfterApply = GhostpulseEnemy != nullptr ? GhostpulseEnemy->nextTurnNetActionPenalty : 0;
    const uint8_t enemyPhaseStartActions = GhostpulseEnemy != nullptr && GhostpulseEnemy->definition != nullptr
        ? Netrunner::netActionsAfterPenalty(GhostpulseEnemy->definition->netActions, enemyPendingAfterApply, 2) : 0;
    EnemyPhaseResult& GhostpulseEnemyPhase = debugEnemyPhaseResult();
    GhostpulseState.runEnemyPhase(GhostpulseRules, GhostpulseEnemyPhase);
    const uint8_t enemyActionsAfterAi = GhostpulseEnemy != nullptr ? GhostpulseEnemy->runner.remainingNetActions() : 0;
    const uint8_t enemyPendingAfterNextPhase = GhostpulseEnemy != nullptr ? GhostpulseEnemy->nextTurnNetActionPenalty : 0;
    const bool GhostpulseOk = GhostpulseActivated && GhostpulseResult.attackCount == 1 && GhostpulseResult.attacks[0].result.success &&
        GhostpulseIce != nullptr && GhostpulseIce->definition == &ghostpulse &&
        GhostpulseAttack.attackerDefinition == &ghostpulse && ghostpulseDamageDice == 1 &&
        GhostpulseAttackPhaseOk && enemyActionsBefore == 3 && enemyPendingBefore == 0 &&
        enemyPendingAfterApply == 1 && enemyPhaseStartActions == 2 && enemyPendingAfterNextPhase == 0;
    const bool TracejackalOk = hit.executed && hit.success && hit.rawDamage == 7 && hit.damage == 7 && enemy.hp() == 23 &&
        hitDice.lastD6Count() == 2 && !hit.fireApplied && !hit.fireBlocked && miss.executed && !miss.success &&
        armor.success && armorDice.lastD6Count() == 2 && !armor.fireApplied && !armor.fireBlocked &&
        armor.rawDamage == 7 && armor.damageReduction == 4 && armor.damage == 3 && deck.programAt(0)->usable();
    const bool PacketsawOk = PacketsawHit.success && PacketsawHit.targetDestroyed && PacketsawHit.affectedProgramSlot == 0 &&
        PacketsawDice.lastD6Count() == 3;
    const bool StormrazorOk = StormrazorHit.executed && StormrazorHit.success && StormrazorDice.lastD6Count() == 5;
    const bool GlasscatOk = GlasscatHit.executed && GlasscatHit.success && GlasscatDice.lastD6Count() == 4;
    const bool noTargetOk = noProgram.noValidTarget && noProgram.noProgramTarget && noTarget.active() && noTarget.pursuing();
    const bool BreacherOk = BreacherHit.success && BreacherHit.rawDamage == 9 && BreacherDice.lastD6Count() == 2 &&
        BreacherHit.forcedUnsafeJackOut && !BreacherHit.krashBarrierBlocked;
    const bool BreacherResultFlowOk = !GameUIController::shouldPendUnsafeExitPresentationForDebug(BreacherHit, true) &&
        GameUIController::shouldPendUnsafeExitPresentationForDebug(BreacherHit, false) &&
        !GameUIController::shouldPendUnsafeExitPresentationForDebug(miss, false) &&
        !GameUIController::shouldPendUnsafeExitPresentationForDebug(noProgram, false);
    const bool BreacherAnimationDispatchOk = breacherAnimation.executed && breacherAnimation.dispatch &&
        breacherAnimation.attackerDefinition == &breacher &&
        breacher.visualId == breacherAnimation.visualId &&
        breacher.animationStyle == breacherAnimation.animationStyle &&
        !GameUIController::shouldAnimatePlayerBlackIceAttackForDebug(noProgram, true) &&
        !GameUIController::shouldAnimatePlayerBlackIceAttackForDebug(miss, false);
    if (!BreacherAnimationDispatchOk)
        Serial.printf("[Diag][PlayerBlackIceCombat][BreacherAnim] count=%u dispatch=%u definition=%u visual=%u style=%u\n",
            static_cast<unsigned>(BreacherPhase.attackCount),
            breacherAnimation.executed && breacherAnimation.dispatch ? 1U : 0U,
            breacherAnimation.attackerDefinition == &breacher ? 1U : 0U,
            breacher.visualId == breacherAnimation.visualId ? 1U : 0U,
            breacher.animationStyle == breacherAnimation.animationStyle ? 1U : 0U);
    const bool RepeatRoundOk = runPlayerBlackIceRepeatRoundDebugTest();
    const bool TargetPreviewOk = runPlayerBlackIceTargetPreviewDebugTest();
    const bool DeadlockOk = DeadlockHit.success && DeadlockDice.lastD6Count() == 2 &&
        DeadlockHit.temporaryRunEffect == TemporaryRunEffect::NavigationAndSafeJackOutLock;
    const bool pass = definitionsOk && TracejackalOk && PacketsawOk && StormrazorOk && GlasscatOk && noTargetOk &&
        GhostpulseOk && BreacherOk && BreacherResultFlowOk && BreacherAnimationDispatchOk && RepeatRoundOk &&
        TargetPreviewOk && DeadlockOk && BreacherTargetPresentation;
    Serial.printf("[Diag][PlayerBlackIceCombat][TargetPreview][Final] stage=%u reason=%u validStored=%u lockedReadOnly=%u clearedSameFloor=%u deterministicOrder=%u terminalInactive=%u terminalInactivePreview=%u otherFloor=%u otherFloorGameplay=%u unrezzedPreviewOk=%u unrezzedGameplayRejected=%u unrezzedReadOnly=%u iceFloor=%u enemy0Floor=%u enemy1Floor=%u potentialTargetId=%u strictTargetId=%u FINAL=%u\n",
        static_cast<unsigned>(playerIceTargetPreviewDiagnostic.stage), static_cast<unsigned>(playerIceTargetPreviewDiagnostic.reason),
        playerIceTargetPreviewDiagnostic.validStored ? 1U : 0U, playerIceTargetPreviewDiagnostic.lockedReadOnly ? 1U : 0U,
        playerIceTargetPreviewDiagnostic.clearedSameFloor ? 1U : 0U, playerIceTargetPreviewDiagnostic.deterministicOrder ? 1U : 0U,
        playerIceTargetPreviewDiagnostic.terminalInactive ? 1U : 0U, playerIceTargetPreviewDiagnostic.terminalInactivePreview ? 1U : 0U,
        playerIceTargetPreviewDiagnostic.otherFloor ? 1U : 0U, playerIceTargetPreviewDiagnostic.otherFloorGameplay ? 1U : 0U,
        playerIceTargetPreviewDiagnostic.unrezzedPreviewOk ? 1U : 0U, playerIceTargetPreviewDiagnostic.unrezzedGameplayRejected ? 1U : 0U,
        playerIceTargetPreviewDiagnostic.unrezzedReadOnly ? 1U : 0U, static_cast<unsigned>(playerIceTargetPreviewDiagnostic.iceFloor),
        static_cast<unsigned>(playerIceTargetPreviewDiagnostic.enemy0Floor), static_cast<unsigned>(playerIceTargetPreviewDiagnostic.enemy1Floor),
        static_cast<unsigned>(playerIceTargetPreviewDiagnostic.potentialTargetId), static_cast<unsigned>(playerIceTargetPreviewDiagnostic.strictTargetId),
        TargetPreviewOk ? 1U : 0U);
    if (!pass) Serial.printf("[Diag][PlayerBlackIceCombat] hp=%u raw=%d reduction=%d final=%d slot=%d\n", enemy.hp(), hit.rawDamage, armor.damageReduction, armor.damage, PacketsawHit.affectedProgramSlot);
    // TurnPhase::PlayerIce is enum value 2: deployed Player ICE acts here.
    NETRUN_VERBOSE_PRINTF("[Diag][PlayerBlackIceCombat][Ghostpulse] phaseBefore=%u(PlayerIce) executed=%u hit=%u effectType=%u penalty=%u targetRuntimeId=%u enemyPendingBefore=%u enemyPendingAfterApply=%u enemyPhaseStartActions=%u enemyActionsAfterAi=%u enemyPendingAfterNextPhase=%u\n",
        static_cast<unsigned>(TurnPhase::PlayerIce),
        GhostpulseAttack.executed ? 1U : 0U, GhostpulseAttack.success ? 1U : 0U,
        static_cast<unsigned>(GhostpulseAttack.temporaryRunEffect),
        GhostpulseAttack.attackerDefinition != nullptr ? static_cast<unsigned>(GhostpulseAttack.attackerDefinition->effect.netActionPenalty) : 0U,
        GhostpulseEnemy != nullptr ? static_cast<unsigned>(GhostpulseEnemy->runtimeId) : 0U,
        static_cast<unsigned>(enemyPendingBefore), static_cast<unsigned>(enemyPendingAfterApply),
        static_cast<unsigned>(enemyPhaseStartActions), static_cast<unsigned>(enemyActionsAfterAi),
        static_cast<unsigned>(enemyPendingAfterNextPhase));
    Serial.printf("[PlayerBlackIceCombat] %s | Definitions=%s | Tracejackal=%s | Armor=%s | Ghostpulse=%s | Breacher=%s | BreacherFlow=%s | BreacherAnim=%s | RepeatRound=%s | TargetPreview=%s | BreacherTarget=%s | Deadlock=%s | Packetsaw=%s | Stormrazor=%s | Glasscat=%s | NoTarget=%s | NoFire=%s\n",
        pass ? "PASS" : "FAIL", definitionsOk ? "PASS" : "FAIL", TracejackalOk ? "PASS" : "FAIL", armor.success ? "PASS" : "FAIL",
        GhostpulseOk ? "PASS" : "FAIL", BreacherOk ? "PASS" : "FAIL", BreacherResultFlowOk ? "PASS" : "FAIL", BreacherAnimationDispatchOk ? "PASS" : "FAIL", RepeatRoundOk ? "PASS" : "FAIL", TargetPreviewOk ? "PASS" : "FAIL", BreacherTargetPresentation ? "PASS" : "FAIL",
        DeadlockOk ? "PASS" : "FAIL", PacketsawOk ? "PASS" : "FAIL",
        StormrazorOk ? "PASS" : "FAIL", GlasscatOk ? "PASS" : "FAIL", noTargetOk ? "PASS" : "FAIL",
        !hit.fireApplied && !armor.fireApplied ? "PASS" : "FAIL");
}

static __attribute__((noinline)) EnemyNetrunnerRuntime* preparePlayerBlackIceSlideSubtest(GameState& state, uint8_t count)
{
    CyberdeckConfig config;
    config.quality = CyberdeckQuality::Excellent;
    config.programCount = 0;
    for (uint8_t i = 0; i < count; ++i) config.playerBlackIce[i] = BlackIceType::Ice01;
    config.playerBlackIceCount = count;
    state.setCyberdeckConfig(config);
    state.startRun();
    state.jackIn();
    state.startTurn();
    state.architecture().moveForward();
    EnemyNetrunnerRuntime* enemy = state.floorEnemyNetrunner();
    if (enemy != nullptr)
        for (uint8_t i = 0; i < count; ++i) state.activatePlayerBlackIce(i, enemy->runtimeId);
    return enemy;
}

static __attribute__((noinline)) void advancePlayerBlackIceSlideToPlayerIce(GameState& state)
{
    if (state.turnPhase() != TurnPhase::Demon) return;
    FixedDice demonDice(1);
    NetRules demonRules(demonDice);
    state.runDemonPhase(demonRules, debugDemonPhaseResult());
}

static __attribute__((noinline)) bool runPlayerBlackIceSlideSuccessSubtest()
{
    Serial.println("[PlayerBlackIceSlide] Success START");
    logStackHighWaterMark("PlayerBlackIceSlide Success START");
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    EnemyNetrunnerRuntime* enemy = preparePlayerBlackIceSlideSubtest(state, 1);
    state.endPlayerTurn();
    advancePlayerBlackIceSlideToPlayerIce(state);
    CombatDice playerDice(1, 10, 0);
    NetRules playerRules(playerDice);
    state.runPlayerBlackIcePhase(playerRules, debugIcePhaseResult());
    const PlayerBlackIceRuntime* preIce = state.playerBlackIceAt(0);
    const bool preEnemyOk = enemy != nullptr && enemy->available();
    const bool preIceOk = preIce != nullptr;
    const bool preRezzedOk = preIceOk && preIce->instance.active();
    const bool prePursuingOk = preIceOk && preIce->instance.pursuing();
    const bool preTargetOk = preIceOk && preIce->targetEnemyRuntimeId != 0;
    const bool preSameFloorOk = preEnemyOk && preIceOk && preIce->chasePosition == enemy->currentFloor;
    const bool prePhaseOk = state.turnPhase() == TurnPhase::Enemy;
    const bool preSlideAvailableOk = preEnemyOk && enemy->runner.remainingNetActions() > 0;
    Serial.printf("[Diag][PlayerBlackIceSlide][Success][Pre] enemyFound=%u enemyActive=%u enemyFloor=%u playerIceFound=%u playerIceFloor=%u playerIceRezzed=%u playerIcePursuing=%u targetRuntimeId=%u targetMatches=%u slideUsed=%u turn=%u phase=%u\n",
        enemy != nullptr ? 1U : 0U, preEnemyOk ? 1U : 0U,
        enemy != nullptr ? static_cast<unsigned>(enemy->currentFloor) : 255U, preIceOk ? 1U : 0U,
        preIceOk ? static_cast<unsigned>(preIce->chasePosition) : 255U, preRezzedOk ? 1U : 0U,
        prePursuingOk ? 1U : 0U, preIceOk ? static_cast<unsigned>(preIce->targetEnemyRuntimeId) : 0U,
        preEnemyOk && preIceOk && preIce->targetEnemyRuntimeId == enemy->runtimeId ? 1U : 0U,
        enemy != nullptr && enemy->runner.slideUsedThisTurn() ? 1U : 0U,
        static_cast<unsigned>(state.turnNumber()), static_cast<unsigned>(state.turnPhase()));
    Serial.printf("[Diag][PlayerBlackIceSlide][Success][PreFlags] enemyOk=%u iceOk=%u rezzedOk=%u pursuingOk=%u targetOk=%u sameFloorOk=%u slideAvailableOk=%u phaseOk=%u\n",
        preEnemyOk ? 1U : 0U, preIceOk ? 1U : 0U, preRezzedOk ? 1U : 0U,
        prePursuingOk ? 1U : 0U, preTargetOk ? 1U : 0U, preSameFloorOk ? 1U : 0U,
        preSlideAvailableOk ? 1U : 0U, prePhaseOk ? 1U : 0U);
    const bool iceBeforePursuing = state.playerBlackIceAt(0) != nullptr && state.playerBlackIceAt(0)->instance.pursuing();
    const uint16_t targetBefore = state.playerBlackIceAt(0) != nullptr ? state.playerBlackIceAt(0)->targetEnemyRuntimeId : 0;
    CombatDice slideDice(10, 1, 0);
    NetRules slideRules(slideDice);
    const bool phaseOk = state.turnPhase() == TurnPhase::Enemy;
    const bool guardEnemyOk = enemy != nullptr && enemy->available();
    const bool guardIceOk = state.playerBlackIceAt(0) != nullptr;
    const bool guardRezzedOk = guardIceOk && state.playerBlackIceAt(0)->instance.active();
    const bool guardPursuingOk = guardIceOk && state.playerBlackIceAt(0)->instance.pursuing();
    const bool guardTargetOk = guardIceOk && state.playerBlackIceAt(0)->targetEnemyRuntimeId == enemy->runtimeId;
    const bool guardSameFloorOk = guardIceOk && enemy != nullptr && state.playerBlackIceAt(0)->chasePosition == enemy->currentFloor;
    Serial.printf("[Diag][PlayerBlackIceSlide][Success][Guard] actualPhase=%u requiredPhase=%u phaseOk=%u enemyOk=%u iceOk=%u rezzedOk=%u pursuingOk=%u targetOk=%u sameFloorOk=%u onceOk=%u\n",
        static_cast<unsigned>(state.turnPhase()), static_cast<unsigned>(TurnPhase::Enemy), phaseOk ? 1U : 0U,
        guardEnemyOk ? 1U : 0U, guardIceOk ? 1U : 0U, guardRezzedOk ? 1U : 0U,
        guardPursuingOk ? 1U : 0U, guardTargetOk ? 1U : 0U, guardSameFloorOk ? 1U : 0U,
        enemy != nullptr && !enemy->runner.slideUsedThisTurn() ? 1U : 0U);
    Serial.println("[PlayerBlackIceSlide] Success before execution");
    logStackHighWaterMark("PlayerBlackIceSlide Success before execution");
    EnemyPhaseResult& phase = debugEnemyPhaseResult();
    state.runEnemyPhase(slideRules, phase);
    Serial.println("[PlayerBlackIceSlide] Success after execution");
    logStackHighWaterMark("PlayerBlackIceSlide Success after execution");
    const PlayerBlackIceRuntime* ice = state.playerBlackIceAt(0);
    const CombatResult* slide = phase.actionCount > 0 ? &phase.actions[0].result : nullptr;
    const bool pass = phaseOk && slide != nullptr && slide->executed && slide->success && enemy != nullptr && ice != nullptr && ice->instance.active() && !ice->instance.pursuing() &&
        ice->targetEnemyRuntimeId == 0 && ice->instance.currentRez() == ice->instance.maxRez();
    Serial.printf("[Diag][PlayerBlackIceSlide][Success] phase=%u(Enemy) executed=%u runnerRoll=%d iceRoll=%d success=%u targetBefore=%u targetAfter=%u pursuingBefore=%u pursuingAfter=%u rezzedAfter=%u sourceFloor=%u onceUsed=%u\n",
        static_cast<unsigned>(TurnPhase::Enemy), slide != nullptr && slide->executed ? 1U : 0U,
        slide != nullptr ? slide->attackerRoll : 0, slide != nullptr ? slide->defenderRoll : 0,
        slide != nullptr && slide->success ? 1U : 0U, static_cast<unsigned>(targetBefore),
        static_cast<unsigned>(ice != nullptr ? ice->targetEnemyRuntimeId : 0), iceBeforePursuing ? 1U : 0U,
        ice != nullptr && ice->instance.pursuing() ? 1U : 0U, ice != nullptr && ice->instance.active() ? 1U : 0U,
        ice != nullptr ? static_cast<unsigned>(ice->chasePosition) + 1U : 0U,
        enemy != nullptr && enemy->runner.slideUsedThisTurn() ? 1U : 0U);
    logStackHighWaterMark("PlayerBlackIceSlide Success END");
    Serial.println("[PlayerBlackIceSlide] Success END");
    return pass;
}

static __attribute__((noinline)) bool runPlayerBlackIceSlideFailureSubtest()
{
    Serial.println("[PlayerBlackIceSlide] Failure START");
    logStackHighWaterMark("PlayerBlackIceSlide Failure START");
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    EnemyNetrunnerRuntime* enemy = preparePlayerBlackIceSlideSubtest(state, 1);
    state.endPlayerTurn();
    advancePlayerBlackIceSlideToPlayerIce(state);
    CombatDice playerDice(1, 10, 0);
    NetRules playerRules(playerDice);
    state.runPlayerBlackIcePhase(playerRules, debugIcePhaseResult());
    CombatDice slideDice(1, 10, 0);
    NetRules slideRules(slideDice);
    state.runEnemyPhase(slideRules, debugEnemyPhaseResult());
    const PlayerBlackIceRuntime* ice = state.playerBlackIceAt(0);
    const bool pass = enemy != nullptr && ice != nullptr && ice->instance.pursuing() && ice->targetEnemyRuntimeId == enemy->runtimeId;
    logStackHighWaterMark("PlayerBlackIceSlide Failure END");
    Serial.println("[PlayerBlackIceSlide] Failure END");
    return pass;
}

static __attribute__((noinline)) bool runPlayerBlackIceSlideTieSubtest()
{
    Serial.println("[PlayerBlackIceSlide] Tie START");
    logStackHighWaterMark("PlayerBlackIceSlide Tie START");
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    EnemyNetrunnerRuntime* enemy = preparePlayerBlackIceSlideSubtest(state, 2);
    state.endPlayerTurn();
    advancePlayerBlackIceSlideToPlayerIce(state);
    CombatDice playerDice(1, 10, 0);
    NetRules playerRules(playerDice);
    state.runPlayerBlackIcePhase(playerRules, debugIcePhaseResult());
    CombatDice tieDice(2, 1, 0);
    NetRules tieRules(tieDice);
    EnemyPhaseResult& tiePhase = debugEnemyPhaseResult();
    state.runEnemyPhase(tieRules, tiePhase);
    const CombatResult* tie = tiePhase.actionCount > 0 ? &tiePhase.actions[0].result : nullptr;
    const PlayerBlackIceRuntime* first = state.playerBlackIceAt(0);
    const bool pass = tie != nullptr && tie->executed && !tie->success && tie->attackerTotal == tie->defenderTotal &&
        enemy != nullptr && first != nullptr && first->instance.pursuing() && first->targetEnemyRuntimeId == enemy->runtimeId;
    Serial.printf("[Diag][PlayerBlackIceSlide][Tie] runnerRoll=%d runnerTotal=%d iceRoll=%d iceTotal=%d executed=%u success=%u\n",
        tie != nullptr ? tie->attackerRoll : 0, tie != nullptr ? tie->attackerTotal : 0,
        tie != nullptr ? tie->defenderRoll : 0, tie != nullptr ? tie->defenderTotal : 0,
        tie != nullptr && tie->executed ? 1U : 0U, tie != nullptr && tie->success ? 1U : 0U);
    logStackHighWaterMark("PlayerBlackIceSlide Tie END");
    Serial.println("[PlayerBlackIceSlide] Tie END");
    return pass;
}

static __attribute__((noinline)) bool runPlayerBlackIceSlideOnceSubtest()
{
    Serial.println("[PlayerBlackIceSlide] Once START");
    logStackHighWaterMark("PlayerBlackIceSlide Once START");
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    EnemyNetrunnerRuntime* enemy = preparePlayerBlackIceSlideSubtest(state, 1);
    state.endPlayerTurn();
    advancePlayerBlackIceSlideToPlayerIce(state);
    CombatDice playerDice(1, 10, 0);
    NetRules playerRules(playerDice);
    state.runPlayerBlackIcePhase(playerRules, debugIcePhaseResult());
    PlayerBlackIceRuntime* ice = state.playerBlackIceAt(0);
    const bool phaseOk = state.turnPhase() == TurnPhase::Enemy;
    const bool targetValid = enemy != nullptr && ice != nullptr && ice->targetEnemyRuntimeId == enemy->runtimeId;
    const bool sameFloor = targetValid && ice->chasePosition == enemy->currentFloor;
    const bool pursuing = ice != nullptr && ice->instance.active() && ice->instance.pursuing();
    if (enemy != nullptr) enemy->runner.resetTurn();
    CombatDice firstDice(2, 1, 0);
    NetRules firstRules(firstDice);
    CombatResult& first = debugCombatResult(0);
    if (enemy != nullptr && ice != nullptr) first = firstRules.slide(enemy->runner, ice->instance);
    const bool usedAfterFirst = enemy != nullptr && enemy->runner.slideUsedThisTurn();
    CombatDice secondDice(10, 1, 0);
    NetRules secondRules(secondDice);
    CombatResult& second = debugCombatResult(1);
    if (enemy != nullptr && ice != nullptr) second = secondRules.slide(enemy->runner, ice->instance);
    const bool usedAfterSecond = enemy != nullptr && enemy->runner.slideUsedThisTurn();
    const bool pass = phaseOk && targetValid && sameFloor && pursuing && first.executed && !first.success &&
        usedAfterFirst && !second.executed && usedAfterSecond && ice != nullptr && ice->instance.pursuing() &&
        ice->targetEnemyRuntimeId == enemy->runtimeId;
    Serial.printf("[Diag][PlayerBlackIceSlide][Once] firstExecuted=%u firstSuccess=%u usedAfterFirst=%u secondExecuted=%u usedAfterSecond=%u phase=%u targetValid=%u sameFloor=%u pursuing=%u\n",
        first.executed ? 1U : 0U, first.success ? 1U : 0U, usedAfterFirst ? 1U : 0U,
        second.executed ? 1U : 0U, usedAfterSecond ? 1U : 0U, static_cast<unsigned>(state.turnPhase()),
        targetValid ? 1U : 0U, sameFloor ? 1U : 0U, pursuing ? 1U : 0U);
    logStackHighWaterMark("PlayerBlackIceSlide Once END");
    Serial.println("[PlayerBlackIceSlide] Once END");
    return pass;
}

void App::runPlayerBlackIceSlideDebugTest()
{
    Serial.println("[PlayerBlackIceSlide] START");
    logStackHighWaterMark("PlayerBlackIceSlide overall START");
    const BlackIceDefinition& tracejackal = ice01Definition();
    const bool fixtureIdentity = tracejackal.type == BlackIceType::Ice01 && tracejackal.perception == 5 &&
        tracejackal.speed == 7 && tracejackal.attack == 5 && tracejackal.defense == 3 && tracejackal.maxRez == 18 &&
        tracejackal.effect.type == BlackIceEffectType::DamageOnly && tracejackal.effect.damageDice == 2;
    const bool successOk = runPlayerBlackIceSlideSuccessSubtest();
    const bool failureOk = runPlayerBlackIceSlideFailureSubtest();
    const bool tieOk = runPlayerBlackIceSlideTieSubtest();
    const bool onceOk = runPlayerBlackIceSlideOnceSubtest();
    const bool pass = fixtureIdentity && successOk && failureOk && tieOk && onceOk;
    if (!pass) Serial.printf("[Diag][PlayerBlackIceSlide] success=%u failure=%u tie=%u once=%u\n", successOk, failureOk, tieOk, onceOk);
    Serial.printf("[PlayerBlackIceSlide] %s | Tracejackal=%s | Success=%s | Failure=%s | Tie=%s | OncePerTurn=%s\n", pass ? "PASS" : "FAIL",
        fixtureIdentity ? "PASS" : "FAIL", successOk ? "PASS" : "FAIL", failureOk ? "PASS" : "FAIL", tieOk ? "PASS" : "FAIL", onceOk ? "PASS" : "FAIL");
    logStackHighWaterMark("PlayerBlackIceSlide overall END");
    Serial.println("[PlayerBlackIceSlide] END");
}

void App::runAutomaticEndDebugTest()
{
    const auto finishPhases = [](GameState& state, NetRules& rules) {
        if (!state.updatePlayerTurn()) return false;
        if (state.turnPhase() == TurnPhase::Demon) state.runDemonPhase(rules, debugDemonPhaseResult());
        if (state.turnPhase() == TurnPhase::PlayerIce) state.runPlayerBlackIcePhase(rules, debugIcePhaseResult());
        if (state.turnPhase() == TurnPhase::Enemy) state.runEnemyPhase(rules, debugEnemyPhaseResult());
        if (state.turnPhase() == TurnPhase::Ice) state.runIcePhase(rules, debugIcePhaseResult());
        return state.runState() == RunState::JackedIn && state.turnPhase() == TurnPhase::Player &&
            state.runner().remainingNetActions() > 0;
    };
    const auto leaveOneAction = [](Netrunner& runner) {
        while (runner.remainingNetActions() > 1) runner.spendNetAction();
        return runner.remainingNetActions() == 1;
    };

    CombatDice playerDice(10, 1, 1);
    CombatDice phaseDice(1, 10, 1);
    NetRules attackRules(playerDice);
    NetRules phaseRules(phaseDice);

    GameState& enemyAttackState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    CyberdeckConfig attackDeck;
    attackDeck.quality = CyberdeckQuality::Standard;
    attackDeck.programs[0] = ProgramId::Hellbolt;
    attackDeck.programCount = 1;
    enemyAttackState.setCyberdeckConfig(attackDeck);
    enemyAttackState.startRun(); enemyAttackState.jackIn(); enemyAttackState.architecture().moveForward();
    Program* attacker = enemyAttackState.cyberdeck().programAt(0);
    if (attacker != nullptr) attacker->setStatus(ProgramStatus::Rezzed);
    const CombatResult enemyAttack = attacker != nullptr && leaveOneAction(enemyAttackState.runner())
        ? enemyAttackState.attackFloorEnemy(attackRules, *attacker) : CombatResult();
    const bool enemyAttackOk = enemyAttack.executed && enemyAttackState.runner().remainingNetActions() == 0 &&
        finishPhases(enemyAttackState, phaseRules);

    GameState& programAttackState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    programAttackState.startRun(); programAttackState.jackIn(); programAttackState.architecture().moveForward();
    Program* sword = programAttackState.cyberdeck().programAt(0);
    EnemyNetrunnerRuntime* enemy = programAttackState.floorEnemyNetrunner();
    Program* target = enemy != nullptr ? enemy->cyberdeck.programAt(0) : nullptr;
    if (sword != nullptr) sword->setStatus(ProgramStatus::Rezzed);
    if (target != nullptr) target->setStatus(ProgramStatus::Rezzed);
    const CombatResult programAttack = sword != nullptr && target != nullptr && leaveOneAction(programAttackState.runner())
        ? programAttackState.attackFloorEnemyProgram(attackRules, *sword, 0) : CombatResult();
    const bool enemyProgramOk = programAttack.executed && programAttackState.runner().remainingNetActions() == 0 &&
        finishPhases(programAttackState, phaseRules);

    static FloorDefinition mixedFloor;
    static ArchitectureDefinition mixedDefinition;
    static bool mixedDefinitionInitialized = false;
    if (!mixedDefinitionInitialized)
    {
        mixedFloor = FloorDefinition();
        mixedFloor.id = 1;
        mixedFloor.type = FloorType::BlackICE;
        mixedFloor.blackIceType = BlackIceType::Ice01;
        mixedFloor.enemyNetrunner = &nullbyteDefinition();
        mixedDefinition = ArchitectureDefinition("automatic_end_mixed", "AUTOMATIC END MIXED",
            "Enemy plus Architecture ICE regression.", &mixedFloor, 1);
        mixedDefinitionInitialized = true;
    }
    GameState& mixedState = debugTestState(mixedDefinition);
    mixedState.setCyberdeckConfig(attackDeck);
    mixedState.startRun(); mixedState.jackIn(); mixedState.triggerCurrentBlackIce();
    BlackIceInstance* mixedIce = mixedState.activeBlackIceAt(0);
    if (mixedIce != nullptr) mixedIce->setPursuing(true);
    Program* mixedAttacker = mixedState.cyberdeck().programAt(0);
    if (mixedAttacker != nullptr) mixedAttacker->setStatus(ProgramStatus::Rezzed);
    const CombatResult mixedAttack = mixedAttacker != nullptr && leaveOneAction(mixedState.runner())
        ? mixedState.attackFloorEnemy(attackRules, *mixedAttacker) : CombatResult();
    const bool mixedOk = mixedAttack.executed && mixedState.runner().remainingNetActions() == 0 &&
        finishPhases(mixedState, phaseRules);

    GameState& noHostileState = debugTestState(BuiltInArchitectures::branchingTestNet());
    noHostileState.startRun(); noHostileState.jackIn();
    const bool noHostileOk = leaveOneAction(noHostileState.runner()) && noHostileState.runner().spendNetAction() &&
        finishPhases(noHostileState, phaseRules);

    GameState& playerIceRezState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    CyberdeckConfig playerIceRezDeck;
    playerIceRezDeck.quality = CyberdeckQuality::Excellent;
    playerIceRezDeck.playerBlackIce[0] = BlackIceType::Ice01;
    playerIceRezDeck.playerBlackIce[1] = BlackIceType::Ice04;
    playerIceRezDeck.playerBlackIceCount = 2;
    playerIceRezState.setCyberdeckConfig(playerIceRezDeck);
    playerIceRezState.startRun(); playerIceRezState.jackIn(); playerIceRezState.architecture().moveForward();
    EnemyNetrunnerRuntime* playerIceRezEnemy = playerIceRezState.floorEnemyNetrunner();
    const bool playerIceRezTargetOk = playerIceRezEnemy != nullptr &&
        playerIceRezState.activatePlayerBlackIce(0, playerIceRezEnemy->runtimeId).executed &&
        playerIceRezState.runner().remainingNetActions() == 2 &&
        playerIceRezState.turnPhase() == TurnPhase::Player;
    const bool playerIceRezSecondOk = playerIceRezTargetOk &&
        playerIceRezState.activatePlayerBlackIce(1, playerIceRezEnemy->runtimeId).executed &&
        playerIceRezState.runner().remainingNetActions() == 1 &&
        playerIceRezState.turnPhase() == TurnPhase::Player;
    const bool playerIceRezFinalOk = playerIceRezSecondOk &&
        GameUIController::shouldScheduleAutomaticTurnEndForDebug(
            playerIceRezState.runState(), playerIceRezState.turnPhase(),
            0, true, false) &&
        playerIceRezState.runner().spendNetAction() &&
        playerIceRezState.runner().remainingNetActions() == 0 &&
        playerIceRezState.updatePlayerTurn() &&
        playerIceRezState.turnPhase() != TurnPhase::Player;
    const bool playerIceRezSchedulerOk = playerIceRezTargetOk && playerIceRezSecondOk && playerIceRezFinalOk &&
        !GameUIController::shouldScheduleAutomaticTurnEndForDebug(
            RunState::JackedIn, TurnPhase::Player, 1, true, false) &&
        !GameUIController::shouldScheduleAutomaticTurnEndForDebug(
            RunState::JackedIn, TurnPhase::Player, 0, false, false);

    const bool pass = enemyAttackOk && enemyProgramOk && mixedOk && noHostileOk && playerIceRezSchedulerOk;
    if (!pass)
        Serial.printf("[Diag][AutomaticEnd] enemy=%u program=%u mixed=%u noHostiles=%u rezTarget=%u rezSecond=%u rezFinal=%u scheduler=%u\n",
            enemyAttackOk ? 1U : 0U, enemyProgramOk ? 1U : 0U, mixedOk ? 1U : 0U,
            noHostileOk ? 1U : 0U, playerIceRezTargetOk ? 1U : 0U,
            playerIceRezSecondOk ? 1U : 0U, playerIceRezFinalOk ? 1U : 0U,
            playerIceRezSchedulerOk ? 1U : 0U);
    Serial.printf("[AutomaticEnd] %s | EnemyAttack=%s | EnemyProgram=%s | EnemyIceMixed=%s | NoHostiles=%s | PlayerIceRez=%s\n",
        pass ? "PASS" : "FAIL", enemyAttackOk ? "PASS" : "FAIL",
        enemyProgramOk ? "PASS" : "FAIL", mixedOk ? "PASS" : "FAIL",
        noHostileOk ? "PASS" : "FAIL", playerIceRezSchedulerOk ? "PASS" : "FAIL");
}

void App::runEnemyResultToneDebugTest()
{
    CombatResult hit; hit.executed = true; hit.success = true; hit.damage = 8;
    CombatResult miss; miss.executed = true;
    CombatResult status; status.executed = true; status.success = true; status.intReduction = 2;
    CombatResult programDamage; programDamage.executed = true; programDamage.success = true;
    programDamage.targetType = CombatResult::TargetType::Program; programDamage.damage = 4;
    CombatResult blocked; blocked.executed = true; blocked.success = true; blocked.krashBarrierBlocked = true;
    CombatResult mitigated; mitigated.executed = true; mitigated.success = true;
    mitigated.rawDamage = 8; mitigated.damageReduction = 3; mitigated.damage = 5;

    const bool demonHitOk = GameUIController::hostileResultFeedbackToneForDebug(hit) == FeedbackTone::Danger;
    const bool demonMissOk = GameUIController::hostileResultFeedbackToneForDebug(miss) == FeedbackTone::Info;
    const bool demonStatusOk = GameUIController::hostileResultFeedbackToneForDebug(status) == FeedbackTone::Danger;
    const bool iceHitOk = GameUIController::hostileResultFeedbackToneForDebug(hit) == FeedbackTone::Danger;
    const bool iceMissOk = GameUIController::hostileResultFeedbackToneForDebug(miss) == FeedbackTone::Info;
    const bool enemyHitOk = GameUIController::hostileResultFeedbackToneForDebug(hit) == FeedbackTone::Danger;
    const bool enemyMissOk = GameUIController::hostileResultFeedbackToneForDebug(miss) == FeedbackTone::Info;
    const bool programOk = GameUIController::hostileResultFeedbackToneForDebug(programDamage) == FeedbackTone::Danger;
    const bool blockedOk = GameUIController::hostileResultFeedbackToneForDebug(blocked) == FeedbackTone::Info;
    const bool mitigatedOk = GameUIController::hostileResultFeedbackToneForDebug(mitigated) == FeedbackTone::Danger;
    const bool pass = demonHitOk && demonMissOk && demonStatusOk && iceHitOk && iceMissOk &&
        enemyHitOk && enemyMissOk && programOk && blockedOk && mitigatedOk;
    Serial.printf("[HostileResultTone] %s | DemonHit=%s | DemonMiss=%s | DemonStatus=%s | IceHit=%s | IceMiss=%s | EnemyHit=%s | EnemyMiss=%s | Program=%s | Blocked=%s | Mitigated=%s\n",
        pass ? "PASS" : "FAIL", demonHitOk ? "PASS" : "FAIL", demonMissOk ? "PASS" : "FAIL",
        demonStatusOk ? "PASS" : "FAIL", iceHitOk ? "PASS" : "FAIL", iceMissOk ? "PASS" : "FAIL",
        enemyHitOk ? "PASS" : "FAIL", enemyMissOk ? "PASS" : "FAIL", programOk ? "PASS" : "FAIL",
        blockedOk ? "PASS" : "FAIL", mitigatedOk ? "PASS" : "FAIL");
}

void App::runMilitechDemoEnemyDormantDebugTest()
{
    // Discovery of a floor is intentionally separate from discovery of a
    // stationary Enemy. Pathfinder must not wake Zer=0 remotely.
    GameState& pathfinderState = debugTestState(BuiltInArchitectures::militechTestNet());
    RunnerProfile pathfinderProfile;
    snprintf(pathfinderProfile.handle, sizeof(pathfinderProfile.handle), "REDSHIFT");
    pathfinderProfile.interfaceRank = 10;
    pathfinderProfile.maxHp = 40;
    pathfinderState.setRunnerProfile(pathfinderProfile);
    CombatDice dice(10, 1, 0);
    NetRules rules(dice);
    pathfinderState.startRun();
    pathfinderState.jackIn();
    EnemyNetrunnerRuntime* pathfinderEnemy = pathfinderState.enemyNetrunnerAt(0);
    const PathfinderResult scan = pathfinderState.pathfinder(rules);
    const Floor* enemyFloor = pathfinderState.architecture().floorAt(6);
    pathfinderState.endPlayerTurn();
    EnemyPhaseResult& pathfinderPhase = debugEnemyPhaseResult();
    pathfinderState.runEnemyPhase(rules, pathfinderPhase);
    uint16_t pathfinderPresenceId = 0;
    const bool pathfinderDormant = scan.check.attempted && enemyFloor != nullptr && enemyFloor->discovered &&
        pathfinderEnemy != nullptr && !pathfinderEnemy->discoveredByPlayer && !pathfinderEnemy->presencePending &&
        pathfinderEnemy->currentFloor == 6 && pathfinderPhase.actionCount == 0 &&
        !pathfinderState.consumeEnemyPresence(pathfinderPresenceId);

    // The map is a read-only projection. Showing an already discovered Enemy
    // floor must likewise leave its stationary runtime dormant.
    GameState& mapState = debugTestState(BuiltInArchitectures::militechTestNet());
    mapState.startRun();
    mapState.jackIn();
    EnemyNetrunnerRuntime* mapEnemy = mapState.enemyNetrunnerAt(0);
    Floor* mapEnemyFloor = mapState.architecture().floorAt(6);
    if (mapEnemyFloor != nullptr) mapEnemyFloor->discovered = true;
    ArchitectureMapView mapView;
    GameUIController::buildArchitectureMapView(mapState, mapView);
    bool mapShowsEnemyFloor = false;
    for (size_t index = 0; index < mapView.nodeCount; ++index)
        if (mapView.nodes[index].floorIndex == 6 && mapView.nodes[index].discovered) mapShowsEnemyFloor = true;
    mapState.endPlayerTurn();
    EnemyPhaseResult& mapPhase = debugEnemyPhaseResult();
    mapState.runEnemyPhase(rules, mapPhase);
    uint16_t mapPresenceId = 0;
    const bool mapDormant = mapShowsEnemyFloor && mapEnemy != nullptr && !mapEnemy->discoveredByPlayer &&
        !mapEnemy->presencePending && mapEnemy->currentFloor == 6 && mapPhase.actionCount == 0 &&
        !mapState.consumeEnemyPresence(mapPresenceId);

    GameState& state = debugTestState(BuiltInArchitectures::militechTestNet());
    state.startRun();
    state.jackIn();
    EnemyNetrunnerRuntime* enemy = state.enemyNetrunnerAt(0);
    bool dormant = enemy != nullptr && enemy->definition != nullptr &&
        enemy->definition->stationaryUntilDiscovered && !enemy->discoveredByPlayer &&
        enemy->currentFloor == 6;
    for (uint8_t phaseIndex = 0; phaseIndex < 3; ++phaseIndex)
    {
        state.endPlayerTurn();
        EnemyPhaseResult& phase = debugEnemyPhaseResult();
        state.runEnemyPhase(rules, phase);
        dormant = dormant && enemy != nullptr && enemy->currentFloor == 6 &&
            phase.actionCount == 0 && state.turnPhase() == TurnPhase::Ice;
        IcePhaseResult& ice = debugIcePhaseResult();
        state.runIcePhase(rules, ice);
    }

    // Reach Floor 7 through the normal floor-entry path exactly once; this
    // is the existing discovery/visibility transition for an Enemy entity.
    for (uint8_t step = 0; step < 5; ++step) state.architecture().moveForward();
    state.moveForward(rules);
    uint16_t discoveryRuntimeId = 0;
    const bool discovered = enemy != nullptr && enemy->discoveredByPlayer &&
        state.architecture().currentPosition() == 6 &&
        state.consumeEnemyPresence(discoveryRuntimeId) && discoveryRuntimeId == enemy->runtimeId;

    // Leaving the Enemy floor clears co-location. Its next permitted phase
    // moves exactly one step back onto the runner and raises a fresh signal.
    state.moveBackward(rules);
    state.endPlayerTurn();
    EnemyPhaseResult& contactPhase = debugEnemyPhaseResult();
    state.runEnemyPhase(rules, contactPhase);
    uint16_t contactRuntimeId = 0;
    const bool contact = enemy != nullptr && enemy->currentFloor == 5 &&
        state.consumeEnemyPresence(contactRuntimeId) && contactRuntimeId == enemy->runtimeId &&
        contactPhase.actionCount > 0;
    IcePhaseResult& contactIce = debugIcePhaseResult();
    state.runIcePhase(rules, contactIce);
    state.endPlayerTurn();
    EnemyPhaseResult& repeatPhase = debugEnemyPhaseResult();
    state.runEnemyPhase(rules, repeatPhase);
    uint16_t repeatedRuntimeId = 0;
    const bool noDuplicateSignal = !state.consumeEnemyPresence(repeatedRuntimeId);

    state.startRun();
    enemy = state.enemyNetrunnerAt(0);
    const bool fresh = enemy != nullptr && !enemy->discoveredByPlayer && enemy->currentFloor == 6;
    const bool pass = pathfinderDormant && mapDormant && dormant && discovered && contact && noDuplicateSignal && fresh;
    Serial.printf("[MilitechDemoEnemyDormant] %s | Pathfinder=%s | Map=%s | Dormant=%s | Discovery=%s | Contact=%s | NoDuplicate=%s | Fresh=%s\n",
        pass ? "PASS" : "FAIL", pathfinderDormant ? "PASS" : "FAIL", mapDormant ? "PASS" : "FAIL",
        dormant ? "PASS" : "FAIL", discovered ? "PASS" : "FAIL", contact ? "PASS" : "FAIL",
        noDuplicateSignal ? "PASS" : "FAIL", fresh ? "PASS" : "FAIL");
}

void App::runSpriteReviewNullbyteDormantDebugTest()
{
    const ArchitectureDefinition& spriteReview = BuiltInArchitectures::spriteReviewNet();
    const FloorDefinition* sourceFloor10 = spriteReview.floors != nullptr && spriteReview.floorCount > 9
        ? &spriteReview.floors[9] : nullptr;
    const bool architectureShapeOk = spriteReview.floorCount == 10 && sourceFloor10 != nullptr &&
        sourceFloor10->id == 10 && sourceFloor10->enemyNetrunner != nullptr &&
        sourceFloor10->enemyNetrunner->id != nullptr &&
        strcmp(sourceFloor10->enemyNetrunner->id, "nullbyte") == 0;
    const bool definitionValidOk = spriteReview.valid();

    GameState& state = debugTestState(spriteReview);
    CombatDice dice(10, 1, 0);
    NetRules rules(dice);
    state.startRun();
    state.jackIn();
    EnemyNetrunnerRuntime* enemy = state.enemyNetrunnerAt(0);
    const uint8_t enemyFloor = 9;
    const bool floor10Reachable = state.architecture().floorCount() == 10 &&
        state.architecture().nextStepToward(6, enemyFloor) == enemyFloor;
    bool floor7ConnectedToFloor10 = false;
    for (size_t index = 0; index < state.architecture().connectionCount(6); ++index)
        if (state.architecture().connectionAt(6, index) == enemyFloor)
            floor7ConnectedToFloor10 = true;
    const bool enemyFoundOk = enemy != nullptr;
    const bool definitionOk = enemyFoundOk && enemy->definition != nullptr;
    const bool nameOk = definitionOk && strcmp(enemy->definition->name, "NULLBYTE") == 0;
    const bool stationaryOk = definitionOk && enemy->definition->stationaryUntilDiscovered;
    const bool undiscoveredOk = enemyFoundOk && !enemy->discoveredByPlayer;
    const bool floorOk = enemyFoundOk && enemy->currentFloor == enemyFloor;
    const bool fresh = enemyFoundOk && definitionOk && nameOk && stationaryOk &&
        undiscoveredOk && floorOk;
    // Pathfinder and the map reveal floor information only. Neither may
    // transition a dormant Enemy to discovered/present.
    const PathfinderResult scan = state.pathfinder(rules);
    ArchitectureMapView mapView;
    GameUIController::buildArchitectureMapView(state, mapView);
    bool mapShowsEnemyFloor = false;
    for (size_t index = 0; index < mapView.nodeCount; ++index)
        if (mapView.nodes[index].floorIndex == enemyFloor && mapView.nodes[index].discovered)
            mapShowsEnemyFloor = true;

    const Floor* pathfinderFloor = state.architecture().floorAt(enemyFloor);
    const bool enemyStillDormantOk = enemy != nullptr && !enemy->discoveredByPlayer && !enemy->presencePending;
    const bool positionStableOk = enemy != nullptr && enemy->currentFloor == enemyFloor;
    bool noActionsOk = true;
    bool dormant = scan.check.attempted && mapShowsEnemyFloor;
    for (uint8_t phaseIndex = 0; phaseIndex < 3; ++phaseIndex)
    {
        state.endPlayerTurn();
        EnemyPhaseResult& phase = debugEnemyPhaseResult();
        state.runEnemyPhase(rules, phase);
        noActionsOk = noActionsOk && phase.actionCount == 0;
        uint16_t presenceRuntimeId = 0;
        dormant = dormant && enemy != nullptr && !enemy->discoveredByPlayer &&
            !enemy->presencePending && enemy->currentFloor == enemyFloor && phase.actionCount == 0 &&
            !state.consumeEnemyPresence(presenceRuntimeId);
        IcePhaseResult& ice = debugIcePhaseResult();
        state.runIcePhase(rules, ice);
    }

    const bool noPresenceOk = enemy != nullptr && !enemy->presencePending;
    // Use the unchanged graph route into Floor 10. The final move invokes the
    // normal floor-entry path, which is the sole intended first-contact wakeup.
    const bool stagedNearEnemy = state.architecture().moveTo(6);
    EncounterResult& contactMove = debugEncounterResult();
    state.moveToConnectedFloor(rules, enemyFloor, contactMove);
    uint16_t contactRuntimeId = 0;
    bool presenceReturned = false;
    if (stagedNearEnemy && contactMove.moved && enemy != nullptr && enemy->discoveredByPlayer)
        presenceReturned = state.consumeEnemyPresence(contactRuntimeId);
    const bool firstContact = stagedNearEnemy && contactMove.moved && enemy != nullptr &&
        enemy->discoveredByPlayer && presenceReturned &&
        contactRuntimeId == enemy->runtimeId;

    state.endPlayerTurn();
    // The active LATCH consumes the first post-player phase. Advance through
    // that normal phase before invoking the Enemy-Netrunner phase; calling
    // runEnemyPhase() directly while phase==Demon is intentionally rejected.
    if (state.turnPhase() == TurnPhase::Demon)
        state.runDemonPhase(rules, debugDemonPhaseResult());
    const bool correctPhase = state.turnPhase() == TurnPhase::Enemy;
    const bool discoveredBeforeActive = enemy != nullptr && enemy->discoveredByPlayer;
    const bool activeBeforeActive = enemy != nullptr && enemy->active;
    const bool coLocatedBeforeActive = enemy != nullptr &&
        enemy->currentFloor == state.architecture().currentPosition();
    EnemyPhaseResult& activePhase = debugEnemyPhaseResult();
    state.runEnemyPhase(rules, activePhase);
    const bool activeAi = correctPhase && coLocatedBeforeActive && activeBeforeActive &&
        discoveredBeforeActive && activePhase.actionCount > 0;
    const bool pass = architectureShapeOk && definitionValidOk && floor10Reachable &&
        floor7ConnectedToFloor10 && fresh && dormant && firstContact && activeAi;
    Serial.printf("[SpriteReviewNullbyteDormant] %s | Fresh=%s | PathfinderMapDormant=%s | FirstContact=%s | ActiveAI=%s\n",
        pass ? "PASS" : "FAIL", fresh ? "PASS" : "FAIL", dormant ? "PASS" : "FAIL",
        firstContact ? "PASS" : "FAIL", activeAi ? "PASS" : "FAIL");
}

void App::runMilitechDemoEnemySpatialDebugTest()
{
    GameState& state = debugTestState(BuiltInArchitectures::militechTestNet());
    CombatDice dice(10, 1, 0);
    NetRules rules(dice);
    state.startRun();
    state.jackIn();

    // Mirror the hardware report with the runner on Floor 5 and Zer=0 on
    // Floor 7. Direct graph movement keeps this runtime regression focused on
    // Enemy spatial and phase behaviour, not Password resolution.
    for (uint8_t step = 0; step < 4; ++step) state.architecture().moveForward();
    EnemyNetrunnerRuntime* enemy = state.enemyNetrunnerAt(0);
    const bool setup = enemy != nullptr && state.architecture().currentPosition() == 4 &&
        enemy->currentFloor == 6 && enemy->definition != nullptr &&
        strcmp(enemy->definition->name, "Zer=0") == 0;
    if (enemy != nullptr) enemy->discoveredByPlayer = true;

    EnemyPhaseResult& firstPhase = debugEnemyPhaseResult();
    state.endPlayerTurn();
    state.runEnemyPhase(rules, firstPhase);
    const bool firstApproach = setup && enemy->currentFloor == 5 && firstPhase.executed &&
        firstPhase.actionCount == 0 && state.turnPhase() == TurnPhase::Ice;

    IcePhaseResult& icePhase = debugIcePhaseResult();
    state.runIcePhase(rules, icePhase);
    EnemyPhaseResult& secondPhase = debugEnemyPhaseResult();
    state.endPlayerTurn();
    state.runEnemyPhase(rules, secondPhase);
    bool onlyLocalEnemyResults = secondPhase.actionCount > 0;
    for (size_t index = 0; index < secondPhase.actionCount; ++index)
    {
        onlyLocalEnemyResults = onlyLocalEnemyResults && !secondPhase.actions[index].fromPlayerBlackIce &&
            secondPhase.actions[index].runtimeId == (enemy != nullptr ? enemy->runtimeId : 0) &&
            secondPhase.actions[index].result.executed;
    }
    const bool coLocatedCombat = enemy != nullptr && enemy->currentFloor == 4 &&
        onlyLocalEnemyResults && state.turnPhase() == TurnPhase::Ice;
    state.runIcePhase(rules, icePhase);
    const bool phaseClosure = state.runState() == RunState::JackedIn &&
        state.turnPhase() == TurnPhase::Player && state.runner().remainingNetActions() > 0;
    const bool pass = firstApproach && coLocatedCombat && phaseClosure;
    Serial.printf("[MilitechDemoEnemySpatial] %s | Setup=%s | FirstMove=%s | NoRemote=%s | CoLocation=%s | PhaseClosure=%s | Results=%u\n",
        pass ? "PASS" : "FAIL", setup ? "PASS" : "FAIL",
        firstApproach ? "PASS" : "FAIL",
        firstApproach ? "PASS" : "FAIL", coLocatedCombat ? "PASS" : "FAIL",
        phaseClosure ? "PASS" : "FAIL",
        static_cast<unsigned>(secondPhase.actionCount));
}

void App::runPlayerBlackIceChaseDebugTest()
{
    const BlackIceDefinition& tracejackal = ice01Definition();
    const BlackIceDefinition& breacher = ice12Definition();
    const bool fixtureIdentity = tracejackal.type == BlackIceType::Ice01 &&
        tracejackal.effect.type == BlackIceEffectType::DamageOnly && tracejackal.effect.damageDice == 2 &&
        breacher.type == BlackIceType::Ice12 && breacher.effect.type == BlackIceEffectType::DamageAndUnsafeJackOut &&
        breacher.effect.damageDice == 2;
    auto advanceEnemyPhase = [](GameState& state, NetRules& rules, EnemyPhaseResult& enemyResult) {
        state.endPlayerTurn();
        if (state.turnPhase() == TurnPhase::Demon) { DemonPhaseResult demonResult; state.runDemonPhase(rules, demonResult); }
        if (state.turnPhase() == TurnPhase::PlayerIce) state.runPlayerBlackIcePhase(rules, debugIcePhaseResult());
        if (state.turnPhase() == TurnPhase::Enemy) state.runEnemyPhase(rules, enemyResult);
    };
    CombatDice dice(10, 1, 6); NetRules rules(dice);
    bool towardLower = false, towardHigher = false, sameFloor = false, noIce = false;
    bool intercept = false, multiIce = false, localTargeting = false, fresh = false;

    {
        GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
        state.startRun(); state.jackIn();
        state.demon().active = false;
        EnemyNetrunnerRuntime* enemy = state.enemyNetrunnerAt(0);
        if (enemy != nullptr) enemy->currentFloor = 3;
        EnemyPhaseResult& phase = debugEnemyPhaseResult(); advanceEnemyPhase(state, rules, phase);
        towardLower = enemy != nullptr && enemy->currentFloor == 2;
    }
    {
        GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
        state.startRun(); state.jackIn();
        state.demon().active = false;
        // Isolate BFS movement: the entry Password otherwise correctly
        // blocks an Enemy leaving floor 0 under current movement rules.
        Floor* entry = state.architecture().floorAt(0);
        if (entry != nullptr) entry->resolved = true;
        state.architecture().moveForward(); state.architecture().moveForward(); state.architecture().moveForward();
        EnemyNetrunnerRuntime* enemy = state.enemyNetrunnerAt(0);
        if (enemy != nullptr) enemy->currentFloor = 0;
        EnemyPhaseResult& phase = debugEnemyPhaseResult(); advanceEnemyPhase(state, rules, phase);
        towardHigher = enemy != nullptr && enemy->currentFloor == 1;
        if (!towardHigher) Serial.printf("[Diag][EnemySpatial][TowardHigh] enemy=%u player=%u next=%u\n",
            enemy != nullptr ? static_cast<unsigned>(enemy->currentFloor) : 255U,
            static_cast<unsigned>(state.architecture().currentPosition()),
            enemy != nullptr ? static_cast<unsigned>(state.architecture().nextStepToward(enemy->currentFloor,
                static_cast<uint8_t>(state.architecture().currentPosition()))) : 255U);
    }
    {
        GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
        state.startRun(); state.jackIn();
        state.architecture().moveForward();
        EnemyNetrunnerRuntime* enemy = state.enemyNetrunnerAt(0);
        const uint8_t before = enemy != nullptr ? enemy->currentFloor : 255;
        EnemyPhaseResult& phase = debugEnemyPhaseResult(); advanceEnemyPhase(state, rules, phase);
        sameFloor = enemy != nullptr && enemy->currentFloor == before && phase.actionCount > 0;
    }
    {
        GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
        state.startRun(); state.jackIn();
        EnemyNetrunnerRuntime* enemy = state.enemyNetrunnerAt(0);
        if (enemy != nullptr) enemy->currentFloor = 2;
        EnemyPhaseResult& phase = debugEnemyPhaseResult(); advanceEnemyPhase(state, rules, phase);
        noIce = enemy != nullptr && enemy->currentFloor == 1 && phase.actionCount == 0;
    }
    {
        GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
        CyberdeckConfig config; config.quality = CyberdeckQuality::Excellent; config.programCount = 0;
        config.playerBlackIce[0] = BlackIceType::Ice01; config.playerBlackIceCount = 1;
        state.setCyberdeckConfig(config); state.startRun(); state.jackIn(); state.architecture().moveForward();
        EnemyNetrunnerRuntime* enemy = state.floorEnemyNetrunner();
        const bool placed = enemy != nullptr && state.activatePlayerBlackIce(0, enemy->runtimeId).executed;
        state.architecture().moveBackward(); if (enemy != nullptr) enemy->currentFloor = 2;
        EnemyPhaseResult& phase = debugEnemyPhaseResult(); advanceEnemyPhase(state, rules, phase);
        const PlayerBlackIceRuntime* ice = state.playerBlackIceAt(0);
        intercept = placed && ice != nullptr && ice->chasePosition == 1 && enemy != nullptr && enemy->currentFloor == 1 &&
            phase.actionCount == 1 && phase.actions[0].runtimeId == ice->runtimeId;
        state.startRun(); ice = state.playerBlackIceAt(0); enemy = state.enemyNetrunnerAt(0);
        fresh = ice != nullptr && !ice->instance.active() && ice->chasePosition == 0 &&
            enemy != nullptr && enemy->currentFloor == 1;
    }
    {
        GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
        CyberdeckConfig config; config.quality = CyberdeckQuality::Excellent; config.programCount = 0;
        config.playerBlackIce[0] = BlackIceType::Ice01; config.playerBlackIce[1] = BlackIceType::Ice12; config.playerBlackIceCount = 2;
        state.setCyberdeckConfig(config); state.startRun(); state.jackIn(); state.architecture().moveForward();
        EnemyNetrunnerRuntime* enemy = state.floorEnemyNetrunner();
        if (enemy != nullptr) { state.activatePlayerBlackIce(0, enemy->runtimeId); state.activatePlayerBlackIce(1, enemy->runtimeId); }
        state.architecture().moveBackward(); if (enemy != nullptr) enemy->currentFloor = 2;
        EnemyPhaseResult& phase = debugEnemyPhaseResult(); advanceEnemyPhase(state, rules, phase);
        const PlayerBlackIceRuntime* first = state.playerBlackIceAt(0);
        multiIce = first != nullptr && phase.actionCount == 1 && phase.actions[0].runtimeId == first->runtimeId;
    }
    {
        GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
        CyberdeckConfig config; config.quality = CyberdeckQuality::Standard; config.programs[0] = ProgramId::Hellbolt; config.programCount = 1;
        state.setCyberdeckConfig(config); state.startRun(); state.jackIn();
        Program* program = state.cyberdeck().programAt(0); if (program != nullptr) program->setStatus(ProgramStatus::Rezzed);
        const bool remoteDenied = program != nullptr && !state.attackFloorEnemy(rules, *program).executed;
        state.architecture().moveForward();
        const bool localAllowed = program != nullptr && state.attackFloorEnemy(rules, *program).executed;
        localTargeting = remoteDenied && localAllowed;
    }
    const bool pass = fixtureIdentity && towardLower && towardHigher && sameFloor && noIce && intercept && multiIce && localTargeting && fresh;
    Serial.printf("[PlayerBlackIceChase] %s | Fixtures=%s | TowardLow=%s | TowardHigh=%s | SameFloor=%s | NoICE=%s | Intercept=%s | MultiICE=%s | LocalTarget=%s | Fresh=%s\n",
        pass ? "PASS" : "FAIL", fixtureIdentity ? "PASS" : "FAIL", towardLower ? "PASS" : "FAIL", towardHigher ? "PASS" : "FAIL",
        sameFloor ? "PASS" : "FAIL", noIce ? "PASS" : "FAIL", intercept ? "PASS" : "FAIL",
        multiIce ? "PASS" : "FAIL", localTargeting ? "PASS" : "FAIL", fresh ? "PASS" : "FAIL");
}

void App::runContentDebugTest()
{
    const ArchitectureDefinition& definition = BuiltInArchitectures::militechTestNet();
    static Architecture first;
    static Architecture second;
    first = ArchitectureFactory::create(definition);
    second = ArchitectureFactory::create(definition);

    const bool definitionOk = definition.valid() &&
        strcmp(definition.id, "militech_test_net") == 0 &&
        strcmp(definition.name, "DEMO NET") == 0 &&
        definition.description != nullptr &&
        strcmp(definition.description, "Linear Schema 1 showcase architecture for the Interface 4 runner.") == 0 &&
        definition.floorCount == 8;
    const bool architectureMetadataOk = strcmp(first.id(), definition.id) == 0 &&
        strcmp(first.name(), definition.name) == 0 &&
        first.description() != nullptr &&
        strcmp(first.description(), definition.description) == 0;
    const bool floorsOk = first.floorCount() == 8 &&
        first.floorAt(0)->type == FloorType::Password &&
        first.floorAt(1)->type == FloorType::File &&
        first.floorAt(2)->type == FloorType::BlackICE &&
        first.floorAt(3)->type == FloorType::ControlNode &&
        first.floorAt(4)->blackIceType == BlackIceType::Ice05 &&
        first.floorAt(5)->blackIceType == BlackIceType::Ice02 &&
        first.floorAt(6)->enemyNetrunnerDefinition != nullptr &&
        first.floorAt(7)->type == FloorType::ControlNode;

    const Floor* file = first.floorAt(1);
    const Floor* ice = first.floorAt(2);
    const bool contentOk = file != nullptr && ice != nullptr &&
        file->fileName != nullptr && strcmp(file->fileName, "SECURITY_LOG.DAT") == 0 &&
        file->fileType != nullptr && strcmp(file->fileType, "SECURITY LOG") == 0 &&
        file->fileValue == 350 &&
        ice->blackIceType == BlackIceType::Ice01;

    char formattedValue[16] = {};
    snprintf(formattedValue, sizeof(formattedValue), "%lueb",
        static_cast<unsigned long>(file != nullptr ? file->fileValue : 0));
    const bool presentationOk = strcmp(formattedValue, "350eb") == 0;

    static FloorDefinition graphFloors[6] = {};
    for (size_t index = 0; index < 6; ++index)
    {
        graphFloors[index].id = static_cast<uint8_t>(index + 1);
        graphFloors[index].type = FloorType::Empty;
        graphFloors[index].contentId = "GRAPH_TEST";
        graphFloors[index].connectionCount = 0;
    }
    for (size_t index = 0; index < 5; ++index)
    {
        graphFloors[index].connections[0] = 6;
        graphFloors[index].connectionCount = 1;
    }
    const ArchitectureDefinition overDegree("graph_over_degree", "GRAPH OVER DEGREE", nullptr,
        graphFloors, 6);
    FloorDefinition mixedFloors[2] = {};
    mixedFloors[0].id = 1; mixedFloors[0].type = FloorType::Empty; mixedFloors[0].contentId = "GRAPH_TEST";
    mixedFloors[1].id = 2; mixedFloors[1].type = FloorType::Empty; mixedFloors[1].contentId = "GRAPH_TEST";
    mixedFloors[0].connections[0] = 2; mixedFloors[0].connectionCount = 1;
    mixedFloors[1].connections[0] = 1; mixedFloors[1].connectionCount = 1;
    const ArchitectureDefinition mixedDirection("graph_mixed_direction", "GRAPH MIXED DIRECTION", nullptr,
        mixedFloors, 2);
    Architecture mixedArchitecture;
    const bool graphRegressionOk = !overDegree.valid() && mixedDirection.valid() &&
        ArchitectureFactory::createInto(mixedDirection, mixedArchitecture) &&
        mixedArchitecture.connectionCount(0) == 1 && mixedArchitecture.connectionCount(1) == 1;

    bool runtimeOk = true;
    for (size_t index = 0; index < first.floorCount(); ++index)
    {
        const Floor* floor = first.floorAt(index);
        runtimeOk = runtimeOk && floor != nullptr && !floor->discovered &&
            !floor->resolved && !floor->identified && !floor->controlled &&
            !floor->blackIceTriggered && !floor->downloaded;
    }
    runtimeOk = runtimeOk && first.currentPosition() == 0;

    first.floorAt(0)->resolved = true;
    first.floorAt(1)->identified = true;
    first.floorAt(2)->blackIceTriggered = true;
    const bool isolationOk = !second.floorAt(0)->resolved && !second.floorAt(1)->identified &&
        !second.floorAt(2)->blackIceTriggered &&
        definition.floors[0].difficulty == 6 && definition.floors[1].fileValue == 350 &&
        strcmp(definition.floors[1].fileName, "SECURITY_LOG.DAT") == 0;

    const bool valid = definitionOk && architectureMetadataOk && floorsOk && contentOk &&
        presentationOk && graphRegressionOk && runtimeOk && isolationOk;
    Serial.printf("[Content] %s | Definition=%s | Metadata=%s | Floors=%s | Content=%s | Format=%s | Graph=%s | Runtime=%s | Isolation=%s\n",
        valid ? "PASS" : "FAIL", definitionOk ? "PASS" : "FAIL",
        architectureMetadataOk ? "PASS" : "FAIL", floorsOk ? "PASS" : "FAIL",
        contentOk ? "PASS" : "FAIL", presentationOk ? "PASS" : "FAIL",
        graphRegressionOk ? "PASS" : "FAIL", runtimeOk ? "PASS" : "FAIL", isolationOk ? "PASS" : "FAIL");
}

void App::runScenarioImportDebugTest()
{
    static const char* validJson =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"militech_external_test\","
        "\"name\":\"MILITECH DEMO NET\",\"floors\":["
        "{\"id\":1,\"type\":\"password\",\"dv\":6,\"security\":\"medium\"},"
        "{\"id\":2,\"type\":\"file\",\"dv\":6,\"file\":{\"name\":\"SECURITY_LOG.DAT\",\"type\":\"LOG FILE\",\"value\":500}},"
        "{\"id\":3,\"type\":\"black_ice\",\"ice\":[\"ice_01\"]},"
        "{\"id\":4,\"type\":\"control\",\"dv\":6,\"control\":{\"name\":\"SECURITY CAMERAS\"}}]}";
    static const char* wrongFormat =
        "{\"format\":\"wrong\",\"schemaVersion\":1,\"id\":\"x\",\"name\":\"X\",\"floors\":[{}]}";
    static const char* validSchemaOneVariant =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"v2\",\"name\":\"SCHEMA 1\",\"floors\":[{\"id\":1,\"type\":\"empty\"}]}";
    static const char* validSchemaOneGraph =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"v3\",\"name\":\"SCHEMA 1\",\"floors\":[{\"id\":1,\"type\":\"empty\"}]}";
    static const char* futureVersion =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":4,\"id\":\"v4\",\"name\":\"FUTURE\",\"floors\":[{\"id\":1,\"type\":\"empty\"}]}";
    static const char* missingVersion =
        "{\"format\":\"netrun-architecture\",\"id\":\"missing\",\"name\":\"MISSING\",\"floors\":[{\"id\":1,\"type\":\"empty\"}]}";
    static const char* invalidVersion =
        "{\"format\":\"netrun-architecture\",\"version\":\"three\",\"id\":\"invalid\",\"name\":\"INVALID\",\"floors\":[{\"id\":1,\"type\":\"empty\"}]}";
    static const char* duplicateFloor =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"x\",\"name\":\"X\",\"floors\":["
        "{\"id\":1,\"type\":\"password\",\"dv\":6,\"security\":\"low\"},"
        "{\"id\":1,\"type\":\"password\",\"dv\":6,\"security\":\"low\"}]}";
    static const char* unknownFloor =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"x\",\"name\":\"X\",\"floors\":["
        "{\"id\":1,\"type\":\"demon\"}]}";
    static const char* invalidSecurity =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"x\",\"name\":\"X\",\"floors\":["
        "{\"id\":1,\"type\":\"password\",\"dv\":6,\"security\":\"extreme\"}]}";
    static const char* unknownIce =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"x\",\"name\":\"X\",\"floors\":["
        "{\"id\":1,\"type\":\"black_ice\",\"ice\":[\"not_here\"]}]}";
    static const char* stringFileValue =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"x\",\"name\":\"X\",\"floors\":["
        "{\"id\":1,\"type\":\"file\",\"dv\":6,\"file\":{\"name\":\"X\",\"type\":\"LOG\",\"value\":\"500eb\"}}]}";
    static const char* effectIceTypes =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"effects\",\"name\":\"EFFECTS\",\"floors\":["
        "{\"id\":1,\"type\":\"black_ice\",\"ice\":[\"ice_02\",\"ice_05\",\"ice_07\"]},"
        "{\"id\":2,\"type\":\"black_ice\",\"ice\":[\"ice_08\"]},"
        "{\"id\":3,\"type\":\"black_ice\",\"ice\":[\"ice_09\"]},"
        "{\"id\":4,\"type\":\"black_ice\",\"ice\":[\"ice_10\"]},"
        "{\"id\":5,\"type\":\"black_ice\",\"ice\":[\"ice_11\"]}]}";
    static const char* validV2 =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"enemy_v2\",\"name\":\"ENEMY V2\",\"enemies\":["
        "{\"id\":\"ghost\",\"handle\":\"GHOST//13\",\"interface\":5,\"hp\":30,\"actions\":3,\"programs\":[\"Hellbolt\",\"Armor\"],\"dormant_until_discovered\":true},"
        "{\"id\":\"null\",\"handle\":\"NULL//BYTE\",\"interface\":4,\"hp\":25,\"actions\":3,\"programs\":[\"Sword\"]}],"
        "\"floors\":[{\"id\":1,\"type\":\"empty\",\"enemy\":\"ghost\"},{\"id\":2,\"type\":\"empty\",\"enemy\":\"null\"}]}";
    static const char* unknownEnemyProgram =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"bad_program\",\"name\":\"BAD\",\"enemies\":[{\"id\":\"x\",\"handle\":\"X\",\"interface\":4,\"hp\":30,\"actions\":3,\"programs\":[\"Unknown\"]}],\"floors\":[{\"id\":1,\"type\":\"empty\",\"enemy\":\"x\"}]}";
    static const char* duplicateEnemyId =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"dup_enemy\",\"name\":\"BAD\",\"enemies\":[{\"id\":\"x\",\"handle\":\"X\",\"interface\":4,\"hp\":30,\"actions\":3,\"programs\":[\"Sword\"]},{\"id\":\"x\",\"handle\":\"Y\",\"interface\":4,\"hp\":30,\"actions\":3,\"programs\":[\"Sword\"]}],\"floors\":[{\"id\":1,\"type\":\"empty\",\"enemy\":\"x\"}]}";
    static const char* unknownEnemyFloor =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"bad_floor\",\"name\":\"BAD\",\"enemies\":[],\"floors\":[{\"id\":1,\"type\":\"empty\",\"enemy\":\"missing\"}]}";
    static const char* mixedObjectIce = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"mixed\",\"name\":\"MIXED\",\"floors\":[{\"id\":1,\"type\":\"file\",\"dv\":6,\"file\":{\"name\":\"PAYLOAD\",\"type\":\"LOG\",\"value\":1},\"ice\":[\"ice_01\"],\"enemy\":\"nullbyte\"}]}";
    static const char* invalidEnemyStats =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"bad_stats\",\"name\":\"BAD\",\"enemies\":[{\"id\":\"x\",\"handle\":\"X\",\"interface\":11,\"hp\":0,\"actions\":0,\"programs\":[\"Sword\"]}],\"floors\":[{\"id\":1,\"type\":\"empty\",\"enemy\":\"x\"}]}";

    ScenarioLoader loader;
    LoadedScenario* parsed = new (std::nothrow) LoadedScenario();
    if (parsed == nullptr)
    {
        Serial.println("[ScenarioImport] FAIL | TEST ALLOCATION");
        return;
    }

    const ScenarioImportResult valid = loader.loadJson(validJson, *parsed);
    Architecture* runtime = new (std::nothrow)
        Architecture(ArchitectureFactory::create(parsed->definition()));
    const bool validOk = runtime != nullptr && valid.success &&
        parsed->definition().floorCount == 4 &&
        parsed->definition().floors[1].fileValue == 500 && runtime->floorCount() == 4 &&
        runtime->floorAt(1)->fileValue == 500 &&
        runtime->floorAt(2)->blackIceType == BlackIceType::Ice01;
    delete runtime;

    const ScenarioImportResult mixedObjectIceResult = loader.loadJson(mixedObjectIce, *parsed);
    const bool mixedObjectIceOk = mixedObjectIceResult.success && parsed->definition().floors[0].type == FloorType::File &&
        parsed->definition().floors[0].blackIceCount == 1 && parsed->definition().floors[0].enemyNetrunner != nullptr &&
        parsed->definition().floors[0].fileValue == 1;
    const bool formatOk = loader.loadJson(wrongFormat, *parsed).error == ScenarioImportError::WrongFormat;
    const bool schemaOneOk = valid.success && parsed->schemaVersion() == 1;
    const ScenarioImportResult version2Result = loader.loadJson(validSchemaOneVariant, *parsed);
    const bool schemaOneVariantOk = version2Result.success && parsed->schemaVersion() == 1;
    const ScenarioImportResult version3Result = loader.loadJson(validSchemaOneGraph, *parsed);
    const bool schemaOneGraphOk = version3Result.success && parsed->schemaVersion() == 1;
    const bool futureVersionOk = loader.loadJson(futureVersion, *parsed).error == ScenarioImportError::UnsupportedVersion;
    const bool missingVersionOk = loader.loadJson(missingVersion, *parsed).error == ScenarioImportError::MissingField;
    const bool invalidVersionOk = loader.loadJson(invalidVersion, *parsed).error == ScenarioImportError::MissingField;
    const bool schemaValidationOk = schemaOneOk && schemaOneVariantOk && schemaOneGraphOk && futureVersionOk &&
        missingVersionOk && invalidVersionOk;
    const bool duplicateOk = loader.loadJson(duplicateFloor, *parsed).error == ScenarioImportError::DuplicateFloorId;
    const bool floorOk = loader.loadJson(unknownFloor, *parsed).error == ScenarioImportError::UnknownFloorType;
    const bool securityOk = loader.loadJson(invalidSecurity, *parsed).error == ScenarioImportError::InvalidSecurityTier;
    const bool iceOk = loader.loadJson(unknownIce, *parsed).error == ScenarioImportError::UnknownBlackIceContent;
    const bool errorMappingOk = !strcmp(scenarioErrorMessage(ScenarioImportError::UnknownBlackIceContent), "UNKNOWN ICE ID") &&
        !strcmp(scenarioErrorMessage(ScenarioImportError::UnknownEnemy), "UNKNOWN ENEMY") &&
        !strcmp(scenarioErrorMessage(ScenarioImportError::UnknownDemon), "UNKNOWN DEMON");
    const bool valueOk = loader.loadJson(stringFileValue, *parsed).error == ScenarioImportError::InvalidValue;
    const bool effectIceOk = loader.loadJson(effectIceTypes, *parsed).success &&
        parsed->definition().floors[0].blackIceTypes[0] == BlackIceType::Ice02 &&
        parsed->definition().floors[0].blackIceTypes[1] == BlackIceType::Ice05 &&
        parsed->definition().floors[0].blackIceTypes[2] == BlackIceType::Ice07 &&
        parsed->definition().floors[1].blackIceType == BlackIceType::Ice08 &&
        parsed->definition().floors[2].blackIceType == BlackIceType::Ice09 &&
        parsed->definition().floors[3].blackIceType == BlackIceType::Ice10 &&
        parsed->definition().floors[4].blackIceType == BlackIceType::Ice11;

    const ScenarioImportResult v2Result = loader.loadJson(validV2, *parsed);
    const EnemyNetrunnerDefinition* ghostDefinition = v2Result.success && parsed->enemyDefinitionCount() > 0
        ? &parsed->enemyDefinitions()[0] : nullptr;
    GameState& v2State = debugTestState(parsed->definition());
    v2State.startRun();
    EnemyNetrunnerRuntime* ghostRuntime = v2State.enemyNetrunnerAt(0);
    EnemyNetrunnerRuntime* nullRuntime = v2State.enemyNetrunnerAt(1);
    const bool v2RuntimeOk = v2Result.success && ghostDefinition != nullptr &&
        strcmp(ghostDefinition->id, "ghost") == 0 && strcmp(ghostDefinition->name, "GHOST//13") == 0 &&
        ghostDefinition->interfaceRank == 5 && ghostDefinition->maxHp == 30 &&
        ghostDefinition->netActions == 3 && ghostDefinition->deckConfig.programCount == 2 &&
        ghostRuntime != nullptr && nullRuntime != nullptr && ghostRuntime->definition != nullptr &&
        strcmp(ghostRuntime->definition->id, ghostDefinition->id) == 0 &&
        strcmp(ghostRuntime->definition->name, ghostDefinition->name) == 0 &&
        strcmp(nullRuntime->definition->id, ghostDefinition->id) != 0 && ghostRuntime->currentFloor == 0 &&
        nullRuntime->currentFloor == 1 && ghostRuntime->runner.hp() == 30;
    if (ghostRuntime != nullptr)
    {
        ghostRuntime->runner.takeDamage(10);
        ghostRuntime->currentFloor = 1;
        ghostRuntime->discoveredByPlayer = true;
    }
    v2State.startRun();
    ghostRuntime = v2State.enemyNetrunnerAt(0);
    const bool v2FreshOk = ghostRuntime != nullptr && ghostRuntime->definition != nullptr &&
        strcmp(ghostRuntime->definition->id, ghostDefinition->id) == 0 &&
        ghostRuntime->runner.hp() == 30 && ghostRuntime->currentFloor == 0 &&
        !ghostRuntime->discoveredByPlayer;
    const bool v2UnknownProgramOk = loader.loadJson(unknownEnemyProgram, *parsed).error ==
        ScenarioImportError::UnknownEnemyProgram;
    const bool v2DuplicateOk = loader.loadJson(duplicateEnemyId, *parsed).error ==
        ScenarioImportError::DuplicateEnemyId;
    const bool v2UnknownFloorOk = loader.loadJson(unknownEnemyFloor, *parsed).error ==
        ScenarioImportError::UnknownEnemy;
    const bool v2StatsOk = loader.loadJson(invalidEnemyStats, *parsed).error ==
        ScenarioImportError::InvalidEnemyDefinition;

    constexpr size_t TOO_MANY_JSON_CAPACITY = 1800;
    char* tooMany = new (std::nothrow) char[TOO_MANY_JSON_CAPACITY]();
    if (tooMany == nullptr)
    {
        delete parsed;
        Serial.println("[ScenarioImport] FAIL | TEST ALLOCATION");
        return;
    }
    size_t used = snprintf(tooMany, TOO_MANY_JSON_CAPACITY,
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"many\",\"name\":\"MANY\",\"floors\":[");
    for (int index = 0; index < 17 && used < TOO_MANY_JSON_CAPACITY; ++index)
        used += snprintf(tooMany + used, TOO_MANY_JSON_CAPACITY - used,
            "%s{\"id\":%d,\"type\":\"password\",\"dv\":6,\"security\":\"low\"}",
            index == 0 ? "" : ",", index + 1);
    snprintf(tooMany + used, TOO_MANY_JSON_CAPACITY - used, "]}");
    const bool limitOk = loader.loadJson(tooMany, *parsed).error == ScenarioImportError::TooManyFloors;
    delete[] tooMany;
    delete parsed;

    const bool all = validOk && formatOk && schemaValidationOk && limitOk && duplicateOk &&
        floorOk && securityOk && iceOk && errorMappingOk && valueOk && effectIceOk && v2RuntimeOk && v2FreshOk &&
        v2UnknownProgramOk && v2DuplicateOk && v2UnknownFloorOk && v2StatsOk && mixedObjectIceOk;
    Serial.printf("[ScenarioImport] %s | Valid=%s | Format=%s | SchemaVersion=%s | Schema1=%s | Schema1=%s | Schema1=%s | Future=%s | Limit=%s | Duplicate=%s | Floor=%s | Security=%s | ICE=%s | ErrorMap=%s | Effects=%s | Value=%s | EnemyRuntime=%s | EnemyFresh=%s | EnemyProgram=%s | EnemyId=%s | EnemyFloor=%s | EnemyStats=%s | MixedObjectIce=%s\n",
        all ? "PASS" : "FAIL", validOk ? "PASS" : "FAIL", formatOk ? "PASS" : "FAIL",
        schemaValidationOk ? "PASS" : "FAIL", schemaOneOk ? "PASS" : "FAIL", schemaOneVariantOk ? "PASS" : "FAIL",
        schemaOneGraphOk ? "PASS" : "FAIL", futureVersionOk ? "PASS" : "FAIL", limitOk ? "PASS" : "FAIL",
        duplicateOk ? "PASS" : "FAIL", floorOk ? "PASS" : "FAIL",
        securityOk ? "PASS" : "FAIL", iceOk ? "PASS" : "FAIL", errorMappingOk ? "PASS" : "FAIL",
        effectIceOk ? "PASS" : "FAIL", valueOk ? "PASS" : "FAIL",
        v2RuntimeOk ? "PASS" : "FAIL", v2FreshOk ? "PASS" : "FAIL",
        v2UnknownProgramOk ? "PASS" : "FAIL", v2DuplicateOk ? "PASS" : "FAIL",
        v2UnknownFloorOk ? "PASS" : "FAIL", v2StatsOk ? "PASS" : "FAIL", mixedObjectIceOk ? "PASS" : "FAIL");
}

void App::runScenarioCustomEnemyDebugTest()
{
    LoadedScenario& loaded = debugScenarioFixture();
    static EnemyNetrunnerRegistry globals;
    static const char globalId[] = "global_runner";
    static const char globalName[] = "GLOBAL_RUNNER";
    static ScenarioLoader loader(blackIceRegistry(), ::demonRegistry(), globals);
    static const char localJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"custom_a\",\"name\":\"CUSTOM A\","
        "\"enemies\":[{\"id\":\"test_runner\",\"handle\":\"TEST_RUNNER\",\"interface\":5,\"hp\":27,\"actions\":3,"
        "\"programs\":[\"Hellbolt\",\"Shield\",\"Superglue\"],\"dormant_until_discovered\":true}],"
        "\"floors\":[{\"id\":3,\"type\":\"empty\",\"enemy\":\"test_runner\"}]}";
    static const char switchedJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"custom_b\",\"name\":\"CUSTOM B\","
        "\"enemies\":[{\"id\":\"custom_b\",\"handle\":\"CUSTOM_B\",\"interface\":6,\"hp\":19,\"actions\":4,"
        "\"programs\":[\"Sword\"]}],\"floors\":[{\"id\":1,\"type\":\"empty\",\"enemy\":\"custom_b\"}]}";
    static const char globalJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"global\",\"name\":\"GLOBAL\","
        "\"floors\":[{\"id\":2,\"type\":\"empty\",\"enemy\":\"global_runner\"}]}";
    static const char builtinJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"builtin\",\"name\":\"BUILTIN\","
        "\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"nullbyte\"}]}";
    static const char unknownProgramJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"bad_program\",\"name\":\"BAD\","
        "\"enemies\":[{\"id\":\"bad_program\",\"handle\":\"BAD\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"NO_SUCH_PROGRAM\"]}],"
        "\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"bad_program\"}]}";
    static const char unknownRefJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"unknown\",\"name\":\"UNKNOWN\","
        "\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"missing_runner\"}]}";
    static const char duplicateJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"duplicate\",\"name\":\"DUPLICATE\",\"enemies\":["
        "{\"id\":\"dup\",\"handle\":\"A\",\"interface\":1,\"hp\":10,\"actions\":1,\"programs\":[\"Sword\"]},"
        "{\"id\":\"dup\",\"handle\":\"B\",\"interface\":1,\"hp\":10,\"actions\":1,\"programs\":[\"Sword\"]}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char builtinConflictJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"builtin_conflict\",\"name\":\"BAD\",\"enemies\":["
        "{\"id\":\"nullbyte\",\"handle\":\"BAD\",\"interface\":1,\"hp\":10,\"actions\":1,\"programs\":[\"Sword\"]}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char globalConflictJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"global_conflict\",\"name\":\"BAD\",\"enemies\":["
        "{\"id\":\"global_runner\",\"handle\":\"BAD\",\"interface\":1,\"hp\":10,\"actions\":1,\"programs\":[\"Sword\"]}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char v1Json[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"v1\",\"name\":\"SCHEMA 1\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char v2Json[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"v2\",\"name\":\"SCHEMA 1\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char futureJson[] = "{\"format\":\"netrun-architecture\",\"schemaVersion\":4,\"id\":\"future\",\"name\":\"FUTURE\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";

    CyberdeckConfig globalDeck;
    globalDeck.quality = CyberdeckQuality::Standard;
    globalDeck.programs[0] = ProgramId::Hellbolt;
    globalDeck.programCount = 1;
    const EnemyNetrunnerDefinition globalDefinition = {
        globalId, globalName, 4, 22, 2, globalDeck, EnemyAiArchetype::AntiPersonnel, false,
        EnemyBehaviorId::Baseline};
    globals.clear();
    const bool globalRegistered = globals.add(globalDefinition);

    const ScenarioImportResult localResult = loader.loadJson(localJson, loaded);
    const EnemyNetrunnerDefinition* localDefinition = localResult.success && loaded.enemyDefinitionCount() == 1
        ? &loaded.enemyDefinitions()[0] : nullptr;
    const bool local = localDefinition != nullptr && loaded.definition().floors[0].enemyNetrunner == localDefinition &&
        !strcmp(localDefinition->id, "test_runner") && localDefinition->deckConfig.programCount == 3 &&
        loaded.definition().floors[0].id == 3;

    GameState& localState = debugTestState(loaded.definition());
    localState.startRun();
    EnemyNetrunnerRuntime* localRuntime = localState.enemyNetrunnerAt(0);
    const bool programs = localRuntime != nullptr && localRuntime->cyberdeck.programCount() == 3 &&
        localRuntime->definition != nullptr && localRuntime->definition->stationaryUntilDiscovered &&
        localRuntime->cyberdeck.programAt(0) != nullptr && localRuntime->cyberdeck.programAt(0)->id() == ProgramId::Hellbolt &&
        localRuntime->cyberdeck.programAt(1) != nullptr && localRuntime->cyberdeck.programAt(1)->id() == ProgramId::Shield &&
        localRuntime->cyberdeck.programAt(2) != nullptr && localRuntime->cyberdeck.programAt(2)->id() == ProgramId::Superglue;
    if (localRuntime != nullptr) { localRuntime->runner.takeDamage(9); localRuntime->currentFloor = 0; localRuntime->discoveredByPlayer = true; }
    const bool atomicity = !loader.loadJson(unknownProgramJson, loaded).success &&
        loaded.enemyDefinitionCount() == 1 && loaded.definition().floors[0].enemyNetrunner != nullptr &&
        !strcmp(loaded.definition().floors[0].enemyNetrunner->id, "test_runner") &&
        !strcmp(loaded.definition().floors[0].enemyNetrunner->name, "TEST_RUNNER");
    const bool lifetime = localRuntime != nullptr && localRuntime->definition != nullptr &&
        !strcmp(localRuntime->definition->id, "test_runner") && localRuntime->runner.hp() == 18;

    const bool unknown = loader.loadJson(unknownRefJson, loaded).error == ScenarioImportError::UnknownEnemy;
    const bool unknownProgram = loader.loadJson(unknownProgramJson, loaded).error == ScenarioImportError::UnknownEnemyProgram;
    const bool duplicate = loader.loadJson(duplicateJson, loaded).error == ScenarioImportError::DuplicateEnemyId;
    const bool builtinConflict = loader.loadJson(builtinConflictJson, loaded).error == ScenarioImportError::EnemyIdConflict;
    const bool globalConflict = loader.loadJson(globalConflictJson, loaded).error == ScenarioImportError::EnemyIdConflict;
    const bool global = loader.loadJson(globalJson, loaded).success && loaded.definition().floors[0].enemyNetrunner == globals.findByStableId("global_runner");
    const bool builtin = loader.loadJson(builtinJson, loaded).success && loaded.definition().floors[0].enemyNetrunner == &nullbyteDefinition();

    const bool scenarioA = loader.loadJson(localJson, loaded).success;
    GameState& stateA = debugTestState(loaded.definition()); stateA.startRun();
    const bool scenarioBOk = loader.loadJson(switchedJson, loaded).success;
    GameState& stateB = debugTestState(loaded.definition()); stateB.startRun();
    EnemyNetrunnerRuntime* switched = stateB.enemyNetrunnerAt(0);
    const bool scenarioSwitch = scenarioA && scenarioBOk && switched != nullptr && switched->definition != nullptr &&
        !strcmp(switched->definition->id, "custom_b") && !strcmp(switched->definition->name, "CUSTOM_B") &&
        switched->runner.interfaceRank() == 6 && switched->runner.hp() == 19 && switched->currentFloor == 0;
    if (switched != nullptr) { switched->runner.takeDamage(7); switched->currentFloor = 1; switched->discoveredByPlayer = true; }
    stateB.startRun();
    switched = stateB.enemyNetrunnerAt(0);
    const bool fresh = switched != nullptr && switched->runner.hp() == 19 && switched->currentFloor == 0 &&
        !switched->discoveredByPlayer && switched->runner.maxNetActions() == 4 && switched->cyberdeck.programCount() == 1;
    const bool gameplay = switched != nullptr && switched->available() && switched->definition->ai == EnemyAiArchetype::AntiPersonnel;
    const bool crossConflict = !globals.add(nullbyteDefinition());
    const char* invalidIdJson = "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"bad_id\",\"name\":\"BAD\",\"enemies\":[{\"id\":\"Bad.ID\",\"handle\":\"BAD\",\"interface\":1,\"hp\":10,\"actions\":1,\"programs\":[\"Sword\"]}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    const bool validator = loader.loadJson(invalidIdJson, loaded).error == ScenarioImportError::InvalidEnemyDefinition;
    const bool schemaOneImportA = loader.loadJson(v1Json, loaded).success;
    const bool schemaOneImportB = loader.loadJson(v2Json, loaded).success;
    const bool futureVersion = loader.loadJson(futureJson, loaded).error == ScenarioImportError::UnsupportedVersion;
    const bool pass = globalRegistered && local && global && builtin && unknown && duplicate && builtinConflict &&
        globalConflict && crossConflict && programs && unknownProgram && lifetime && atomicity && scenarioSwitch &&
        fresh && gameplay && validator && schemaOneImportA && schemaOneImportB && localResult.success && futureVersion;
    Serial.printf("[ScenarioCustomEnemy] %s | Local=%s | Global=%s | Builtin=%s | UnknownRef=%s | Duplicate=%s | BuiltinConflict=%s | GlobalConflict=%s | CrossConflict=%s | Programs=%s | UnknownProgram=%s | Lifetime=%s | Atomicity=%s | Fresh=%s | ScenarioSwitch=%s | Gameplay=%s | Schema1A=%s | Schema1B=%s | Schema1C=%s | FutureSchemaVersion=%s\n",
        pass ? "PASS" : "FAIL", local ? "PASS" : "FAIL", global ? "PASS" : "FAIL", builtin ? "PASS" : "FAIL",
        unknown ? "PASS" : "FAIL", duplicate ? "PASS" : "FAIL", builtinConflict ? "PASS" : "FAIL",
        globalConflict ? "PASS" : "FAIL", crossConflict ? "PASS" : "FAIL", programs ? "PASS" : "FAIL",
        unknownProgram ? "PASS" : "FAIL",
        lifetime ? "PASS" : "FAIL", atomicity ? "PASS" : "FAIL", fresh ? "PASS" : "FAIL",
        scenarioSwitch ? "PASS" : "FAIL", gameplay ? "PASS" : "FAIL", schemaOneImportA ? "PASS" : "FAIL",
        schemaOneImportB ? "PASS" : "FAIL", localResult.success ? "PASS" : "FAIL", futureVersion ? "PASS" : "FAIL");
}

void App::runScenarioEnemyBehaviorDebugTest()
{
    // Reuse the shared scenario and GameState diagnostics; no second large
    // fixture is reserved in BSS or on the boot-task stack.
    LoadedScenario& loaded = debugScenarioFixture();
    static EnemyNetrunnerRegistry globals;
    ScenarioLoader loader(blackIceRegistry(), ::demonRegistry(), globals);
    static const char defaultJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"behavior_default\",\"name\":\"DEFAULT\",\"enemies\":[{\"id\":\"default_runner\",\"handle\":\"DEFAULT\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Hellbolt\"]}],\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"default_runner\"}]}";
    static const char baselineJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"behavior_base\",\"name\":\"BASELINE\",\"enemies\":[{\"id\":\"baseline_runner\",\"handle\":\"BASELINE\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Hellbolt\"],\"behavior\":\"baseline\"}],\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"baseline_runner\"}]}";
    static const char defensiveJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"behavior_def\",\"name\":\"DEFENSIVE\",\"enemies\":[{\"id\":\"def_guard\",\"handle\":\"DEF_GUARD\",\"interface\":5,\"hp\":27,\"actions\":3,\"programs\":[\"Shield\",\"Hellbolt\"],\"behavior\":\"defensive\"}],\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"def_guard\"}]}";
    static const char sentryJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"behavior_sentry\",\"name\":\"SENTRY\",\"enemies\":[{\"id\":\"sentry_guard\",\"handle\":\"SENTRY\",\"interface\":5,\"hp\":27,\"actions\":3,\"programs\":[\"Hellbolt\"],\"behavior\":\"sentry\"}],\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"sentry_guard\"}]}";
    static const char unknownJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"behavior_bad\",\"name\":\"BAD\",\"enemies\":[{\"id\":\"bad_runner\",\"handle\":\"BAD\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Hellbolt\"],\"behavior\":\"berserker\"}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char wrongTypeJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"behavior_type\",\"name\":\"TYPE\",\"enemies\":[{\"id\":\"type_runner\",\"handle\":\"TYPE\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Hellbolt\"],\"behavior\":true}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char globalJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"behavior_global\",\"name\":\"GLOBAL\",\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"global_def\"}]}";
    static const char builtinJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"behavior_builtin\",\"name\":\"BUILTIN\",\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"nullbyte\"}]}";
    static const char v1Json[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"behavior_v1\",\"name\":\"SCHEMA 1\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char v2Json[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"behavior_v2\",\"name\":\"SCHEMA 1\",\"enemies\":[{\"id\":\"v2_runner\",\"handle\":\"V2\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Hellbolt\"],\"behavior\":\"sentry\"}],\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"v2_runner\"}]}";
    static const char futureJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":4,\"id\":\"behavior_future\",\"name\":\"FUTURE\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";

    CyberdeckConfig globalDeck;
    globalDeck.quality = CyberdeckQuality::Standard;
    globalDeck.programs[0] = ProgramId::Hellbolt;
    globalDeck.programCount = 1;
    const EnemyNetrunnerDefinition globalDefensive = {
        "global_def", "GLOBAL_DEF", 4, 20, 2, globalDeck,
        EnemyAiArchetype::AntiPersonnel, false, EnemyBehaviorId::Defensive};
    const EnemyNetrunnerDefinition globalSentry = {
        "global_sentry", "GLOBAL_SENTRY", 4, 20, 2, globalDeck,
        EnemyAiArchetype::AntiPersonnel, false, EnemyBehaviorId::Sentry};
    globals.clear();
    const bool globalRegistered = globals.add(globalDefensive) && globals.add(globalSentry);

    const ScenarioImportResult defaultResult = loader.loadJson(defaultJson, loaded);
    const bool defaultBehavior = defaultResult.success && loaded.enemyDefinitionCount() == 1 &&
        loaded.enemyDefinitions()[0].behavior == EnemyBehaviorId::Baseline;
    const ScenarioImportResult baselineResult = loader.loadJson(baselineJson, loaded);
    const bool explicitBaseline = baselineResult.success && loaded.enemyDefinitions()[0].behavior == EnemyBehaviorId::Baseline;
    const ScenarioImportResult defensiveResult = loader.loadJson(defensiveJson, loaded);
    const bool defensiveImport = defensiveResult.success && loaded.enemyDefinitions()[0].behavior == EnemyBehaviorId::Defensive;
    GameState& defensiveState = debugTestState(loaded.definition());
    defensiveState.startRun();
    EnemyNetrunnerRuntime* defensiveRuntime = defensiveState.enemyNetrunnerAt(0);
    if (defensiveRuntime != nullptr && defensiveRuntime->cyberdeck.programAt(1) != nullptr)
    {
        defensiveRuntime->cyberdeck.programAt(1)->setStatus(ProgramStatus::Rezzed);
        defensiveRuntime->runner.resetTurn(2);
    }
    const EnemyBehaviorDecision defensiveDecision = defensiveRuntime != nullptr
        ? chooseEnemyBehaviorDecision(*defensiveRuntime, ProgramId::None, false, true) : EnemyBehaviorDecision();
    const bool defensiveGameplay = defensiveRuntime != nullptr && defensiveRuntime->definition != nullptr &&
        defensiveRuntime->definition->behavior == EnemyBehaviorId::Defensive &&
        defensiveDecision.type == EnemyDecisionType::ActivateProgram && defensiveDecision.program == ProgramId::Shield;

    const bool atomicity = loader.loadJson(unknownJson, loaded).error == ScenarioImportError::InvalidEnemyBehavior &&
        loaded.enemyDefinitionCount() == 1 && loaded.enemyDefinitions()[0].behavior == EnemyBehaviorId::Defensive;
    const bool lifetime = defensiveRuntime != nullptr && defensiveRuntime->definition != nullptr &&
        defensiveRuntime->definition->behavior == EnemyBehaviorId::Defensive;
    const bool unknown = loader.loadJson(unknownJson, loaded).error == ScenarioImportError::InvalidEnemyBehavior;
    const bool wrongType = loader.loadJson(wrongTypeJson, loaded).error == ScenarioImportError::InvalidEnemyBehavior;

    const ScenarioImportResult sentryResult = loader.loadJson(sentryJson, loaded);
    const bool sentryImport = sentryResult.success && loaded.enemyDefinitions()[0].behavior == EnemyBehaviorId::Sentry;
    GameState& sentryState = debugTestState(loaded.definition());
    sentryState.startRun();
    EnemyNetrunnerRuntime* sentryRuntime = sentryState.enemyNetrunnerAt(0);
    const bool sentryGameplay = sentryRuntime != nullptr && sentryRuntime->definition != nullptr &&
        sentryRuntime->definition->behavior == EnemyBehaviorId::Sentry &&
        chooseEnemyBehaviorDecision(*sentryRuntime, ProgramId::None, false, false).type == EnemyDecisionType::None;
    if (sentryRuntime != nullptr) { sentryRuntime->runner.takeDamage(7); sentryRuntime->discoveredByPlayer = true; }
    sentryState.startRun();
    sentryRuntime = sentryState.enemyNetrunnerAt(0);
    const bool fresh = sentryRuntime != nullptr && sentryRuntime->definition != nullptr &&
        sentryRuntime->definition->behavior == EnemyBehaviorId::Sentry && sentryRuntime->runner.hp() == 27 &&
        !sentryRuntime->discoveredByPlayer;
    const bool scenarioSwitch = defensiveImport && sentryImport && sentryRuntime != nullptr &&
        sentryRuntime->definition->behavior == EnemyBehaviorId::Sentry;

    const bool global = globalRegistered && loader.loadJson(globalJson, loaded).success &&
        loaded.definition().floors[0].enemyNetrunner != nullptr &&
        loaded.definition().floors[0].enemyNetrunner->behavior == EnemyBehaviorId::Defensive &&
        globals.findByStableId("global_sentry") != nullptr &&
        globals.findByStableId("global_sentry")->behavior == EnemyBehaviorId::Sentry;
    const bool builtin = loader.loadJson(builtinJson, loaded).success &&
        loaded.definition().floors[0].enemyNetrunner == &nullbyteDefinition() &&
        nullbyteDefinition().behavior == EnemyBehaviorId::Baseline && zerDefinition().behavior == EnemyBehaviorId::Baseline;
    const bool schemaOneImportA = loader.loadJson(v1Json, loaded).success && loaded.schemaVersion() == 1;
    const bool schemaOneImportB = loader.loadJson(v2Json, loaded).success && loaded.schemaVersion() == 1 &&
        loaded.enemyDefinitionCount() == 1 && loaded.enemyDefinitions()[0].behavior == EnemyBehaviorId::Sentry;
    const bool schemaOneImportC = loader.loadJson(defaultJson, loaded).success &&
        loaded.enemyDefinitions()[0].behavior == EnemyBehaviorId::Baseline;
    const bool futureVersion = loader.loadJson(futureJson, loaded).error == ScenarioImportError::UnsupportedVersion;

    const bool pass = defaultBehavior && explicitBaseline && defensiveImport && sentryImport && unknown &&
        wrongType && global && builtin && lifetime && atomicity && scenarioSwitch && fresh && defensiveGameplay &&
        sentryGameplay && schemaOneImportA && schemaOneImportB && schemaOneImportC && futureVersion;
    Serial.printf("[ScenarioEnemyBehavior] %s | Default=%s | Baseline=%s | Defensive=%s | Sentry=%s | Unknown=%s | WrongType=%s | Global=%s | Builtin=%s | Lifetime=%s | Atomicity=%s | ScenarioSwitch=%s | Fresh=%s | DefensiveGameplay=%s | SentryGameplay=%s | Schema1A=%s | Schema1B=%s | Schema1C=%s | FutureSchemaVersion=%s\n",
        pass ? "PASS" : "FAIL", defaultBehavior ? "PASS" : "FAIL", explicitBaseline ? "PASS" : "FAIL",
        defensiveImport ? "PASS" : "FAIL", sentryImport ? "PASS" : "FAIL", unknown ? "PASS" : "FAIL",
        wrongType ? "PASS" : "FAIL", global ? "PASS" : "FAIL", builtin ? "PASS" : "FAIL",
        lifetime ? "PASS" : "FAIL", atomicity ? "PASS" : "FAIL", scenarioSwitch ? "PASS" : "FAIL",
        fresh ? "PASS" : "FAIL", defensiveGameplay ? "PASS" : "FAIL", sentryGameplay ? "PASS" : "FAIL",
        schemaOneImportA ? "PASS" : "FAIL", schemaOneImportB ? "PASS" : "FAIL", schemaOneImportC ? "PASS" : "FAIL",
        futureVersion ? "PASS" : "FAIL");
}

void App::runR34AuditDebugTest()
{
    // Static fixtures keep the boot-task stack independent of LoadedScenario
    // and GameState size. The test exercises the Schema 1 path with both runtime
    // slots occupied by distinct local definitions.
    LoadedScenario& loaded = debugScenarioFixture();
    static const char v3TwoEnemies[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"audit_two\",\"name\":\"AUDIT TWO\",\"enemies\":["
        "{\"id\":\"audit_alpha\",\"handle\":\"ALPHA\",\"interface\":4,\"hp\":21,\"actions\":2,\"programs\":[\"Hellbolt\",\"Shield\"],\"dormant_until_discovered\":true},"
        "{\"id\":\"audit_beta\",\"handle\":\"BETA\",\"interface\":6,\"hp\":29,\"actions\":3,\"programs\":[\"Superglue\",\"Armor\"]}],"
        "\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"audit_alpha\"},{\"id\":1,\"type\":\"empty\",\"enemy\":\"audit_beta\"}]}";
    static const char tooManyEnemies[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"audit_many\",\"name\":\"AUDIT MANY\",\"enemies\":["
        "{\"id\":\"audit_one\",\"handle\":\"ONE\",\"interface\":1,\"hp\":10,\"actions\":1,\"programs\":[]},"
        "{\"id\":\"audit_two\",\"handle\":\"TWO\",\"interface\":1,\"hp\":10,\"actions\":1,\"programs\":[]},"
        "{\"id\":\"audit_three\",\"handle\":\"THREE\",\"interface\":1,\"hp\":10,\"actions\":1,\"programs\":[]}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char futureVersion[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":4,\"id\":\"audit_future\",\"name\":\"AUDIT FUTURE\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char switchedScenario[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"audit_switch\",\"name\":\"AUDIT SWITCH\",\"enemies\":["
        "{\"id\":\"audit_switch\",\"handle\":\"SWITCH\",\"interface\":3,\"hp\":17,\"actions\":2,\"programs\":[\"Sword\"]}],"
        "\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"audit_switch\"}]}";

    static EnemyNetrunnerRegistry registry;
    ScenarioLoader loader(blackIceRegistry(), ::demonRegistry(), registry);
    CyberdeckConfig registryDeck;
    registryDeck.quality = CyberdeckQuality::Standard;
    registryDeck.programCount = 1;
    registryDeck.programs[0] = ProgramId::Shield;
    const EnemyNetrunnerDefinition registryDefinition = {
        "audit_global", "AUDIT_GLOBAL", 3, 17, 2, registryDeck, EnemyAiArchetype::AntiPersonnel, false,
        EnemyBehaviorId::Baseline};
    registry.clear();
    const bool registryOk = registry.add(registryDefinition) &&
        registry.findByStableId("audit_global") != nullptr && !registry.add(nullbyteDefinition());
    registry.clear();
    const ScenarioImportResult imported = loader.loadJson(v3TwoEnemies, loaded);
    const bool parser = imported.success && loaded.schemaVersion() == 1 && loaded.enemyDefinitionCount() == 2;
    const bool placement = parser && loaded.definition().floors[0].enemyNetrunner != nullptr &&
        loaded.definition().floors[1].enemyNetrunner != nullptr &&
        !strcmp(loaded.definition().floors[0].enemyNetrunner->id, "audit_alpha") &&
        !strcmp(loaded.definition().floors[1].enemyNetrunner->id, "audit_beta");
    const bool validator = loader.loadJson(tooManyEnemies, loaded).error == ScenarioImportError::TooManyEnemies &&
        loader.loadJson(futureVersion, loaded).error == ScenarioImportError::UnsupportedVersion;

    // Failed imports must not replace the committed two-enemy scenario.
    const bool atomicity = loaded.enemyDefinitionCount() == 2 &&
        loaded.definition().floorCount == 2 && loaded.definition().floors[0].enemyNetrunner != nullptr;
    GameState& state = debugTestState(loaded.definition());
    state.startRun();
    EnemyNetrunnerRuntime* alpha = state.enemyNetrunnerAt(0);
    EnemyNetrunnerRuntime* beta = state.enemyNetrunnerAt(1);
    const bool programs = alpha != nullptr && beta != nullptr && alpha->cyberdeck.programCount() == 2 &&
        beta->cyberdeck.programCount() == 2 && alpha->cyberdeck.programAt(0) != nullptr &&
        beta->cyberdeck.programAt(0) != nullptr && alpha->cyberdeck.programAt(0)->id() == ProgramId::Hellbolt &&
        beta->cyberdeck.programAt(0)->id() == ProgramId::Superglue;
    const bool dormancy = alpha != nullptr && beta != nullptr && alpha->definition != nullptr && beta->definition != nullptr &&
        alpha->definition->stationaryUntilDiscovered && !beta->definition->stationaryUntilDiscovered &&
        !alpha->discoveredByPlayer && !beta->discoveredByPlayer;
    if (alpha != nullptr) { alpha->runner.takeDamage(7); alpha->currentFloor = 1; alpha->discoveredByPlayer = true; }
    const bool isolated = alpha != nullptr && beta != nullptr && beta->runner.hp() == 29 && beta->currentFloor == 1 &&
        !beta->discoveredByPlayer;
    state.startRun();
    alpha = state.enemyNetrunnerAt(0); beta = state.enemyNetrunnerAt(1);
    const bool fresh = alpha != nullptr && beta != nullptr && alpha->runner.hp() == 21 && alpha->currentFloor == 0 &&
        !alpha->discoveredByPlayer && beta->runner.hp() == 29 && beta->currentFloor == 1 && !beta->discoveredByPlayer;
    const bool presentation = alpha != nullptr && alpha->definition != nullptr &&
        !strcmp(state.enemyNetrunnerNameByRuntimeId(alpha->runtimeId), "ALPHA");
    const bool switchedLoaded = loader.loadJson(switchedScenario, loaded).success;
    GameState& switchedState = debugTestState(loaded.definition()); switchedState.startRun();
    EnemyNetrunnerRuntime* switched = switchedState.enemyNetrunnerAt(0);
    const bool scenarioSwitch = switchedLoaded && switched != nullptr && switched->definition != nullptr &&
        !strcmp(switched->definition->id, "audit_switch") && !strcmp(switched->definition->name, "SWITCH") &&
        switched->runner.hp() == 17 && switched->cyberdeck.programCount() == 1;
    const bool memory = sizeof(EnemyNetrunnerDefinition) == 40 && sizeof(EnemyNetrunnerRegistry) == 708 &&
        sizeof(EnemyNetrunnerRuntime) == 548 && sizeof(LoadedScenario) == 7700;
    const bool pass = parser && validator && registryOk && placement && atomicity && programs && dormancy && isolated &&
        fresh && scenarioSwitch && presentation && memory;
    Serial.printf("[R3.4Audit] %s | SchemaVersion=%s | Parser=%s | Validator=%s | Registry=%s | Programs=%s | Lifetime=%s | Placement=%s | Atomicity=%s | Fresh=%s | ScenarioSwitch=%s | Dormancy=%s | Presentation=%s | Memory=%s\n",
        pass ? "PASS" : "FAIL", parser ? "PASS" : "FAIL", parser ? "PASS" : "FAIL",
        validator ? "PASS" : "FAIL", registryOk ? "PASS" : "FAIL", programs ? "PASS" : "FAIL", isolated ? "PASS" : "FAIL",
        placement ? "PASS" : "FAIL", atomicity ? "PASS" : "FAIL", fresh ? "PASS" : "FAIL",
        scenarioSwitch ? "PASS" : "FAIL", dormancy ? "PASS" : "FAIL", presentation ? "PASS" : "FAIL",
        memory ? "PASS" : "FAIL");
}

void App::runR35AuditDebugTest()
{
    // Deliberately shares the established scenario fixture and registry.
    // No GameState or LoadedScenario is allocated on the boot-task stack.
    LoadedScenario& loaded = debugScenarioFixture();
    static EnemyNetrunnerRegistry registry;
    ScenarioLoader loader(blackIceRegistry(), ::demonRegistry(), registry);
    static const char defaultJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"audit_default\",\"name\":\"DEFAULT\",\"enemies\":[{\"id\":\"audit_default\",\"handle\":\"DEFAULT\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Hellbolt\"]}],\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"audit_default\"}]}";
    static const char defensiveJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"audit_def\",\"name\":\"DEF\",\"enemies\":[{\"id\":\"audit_def\",\"handle\":\"DEF\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Shield\",\"Hellbolt\"],\"behavior\":\"defensive\"}],\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"audit_def\"}]}";
    static const char sentryJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"audit_sentry\",\"name\":\"SENTRY\",\"enemies\":[{\"id\":\"audit_sentry\",\"handle\":\"SENTRY\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Hellbolt\"],\"behavior\":\"sentry\"}],\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"audit_sentry\"}]}";
    static const char invalidJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"audit_bad\",\"name\":\"BAD\",\"enemies\":[{\"id\":\"audit_bad\",\"handle\":\"BAD\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Hellbolt\"],\"behavior\":\"berserker\"}],\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";
    static const char v2Json[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"audit_v2\",\"name\":\"SCHEMA 1\",\"enemies\":[{\"id\":\"audit_v2\",\"handle\":\"V2\",\"interface\":4,\"hp\":20,\"actions\":2,\"programs\":[\"Hellbolt\"],\"behavior\":\"sentry\"}],\"floors\":[{\"id\":0,\"type\":\"empty\",\"enemy\":\"audit_v2\"}]}";
    static const char futureJson[] =
        "{\"format\":\"netrun-architecture\",\"schemaVersion\":4,\"id\":\"audit_future\",\"name\":\"FUTURE\",\"floors\":[{\"id\":0,\"type\":\"empty\"}]}";

    const ScenarioImportResult defaultImport = loader.loadJson(defaultJson, loaded);
    const bool defaulting = defaultImport.success && loaded.schemaVersion() == 1 && loaded.enemyDefinitionCount() == 1 &&
        loaded.enemyDefinitions()[0].behavior == EnemyBehaviorId::Baseline;
    const bool version = loader.loadJson(v2Json, loaded).success && loaded.schemaVersion() == 1 &&
        loaded.enemyDefinitions()[0].behavior == EnemyBehaviorId::Sentry &&
        loader.loadJson(futureJson, loaded).error == ScenarioImportError::UnsupportedVersion;
    const ScenarioImportResult defensiveImport = loader.loadJson(defensiveJson, loaded);
    const bool parser = defensiveImport.success && loaded.enemyDefinitions()[0].behavior == EnemyBehaviorId::Defensive;
    const bool validator = loader.loadJson(invalidJson, loaded).error == ScenarioImportError::InvalidEnemyBehavior &&
        loaded.enemyDefinitionCount() == 1 && loaded.enemyDefinitions()[0].behavior == EnemyBehaviorId::Defensive;
    const bool atomicity = validator && loaded.definition().floors[0].enemyNetrunner != nullptr &&
        !strcmp(loaded.definition().floors[0].enemyNetrunner->name, "DEF") &&
        loaded.definition().floors[0].enemyNetrunner->deckConfig.programCount == 2;

    CyberdeckConfig deck;
    deck.quality = CyberdeckQuality::Standard;
    deck.programs[0] = ProgramId::Shield;
    deck.programs[1] = ProgramId::Hellbolt;
    deck.programCount = 2;
    const EnemyNetrunnerDefinition baseline = {
        "audit_base", "BASE", 4, 20, 2, deck, EnemyAiArchetype::AntiPersonnel, false,
        EnemyBehaviorId::Baseline};
    const EnemyNetrunnerDefinition defensive = {
        "audit_def_runtime", "DEF", 4, 20, 2, deck, EnemyAiArchetype::AntiPersonnel, false,
        EnemyBehaviorId::Defensive};
    const EnemyNetrunnerDefinition sentry = {
        "audit_sentry_runtime", "SENTRY", 4, 20, 2, deck, EnemyAiArchetype::AntiPersonnel, false,
        EnemyBehaviorId::Sentry};
    EnemyNetrunnerDefinition invalidDefinition = baseline;
    invalidDefinition.behavior = static_cast<EnemyBehaviorId>(99);
    registry.clear();
    const bool registryOk = registry.add(baseline) && registry.add(defensive) && registry.add(sentry) &&
        !registry.add(invalidDefinition) && registry.findByStableId("audit_def_runtime") != nullptr &&
        registry.findByStableId("audit_sentry_runtime") != nullptr;

    EnemyNetrunnerRuntime runtime;
    const bool invalidRuntimeRejected = !runtime.reset(invalidDefinition, 301, 0);
    EnemyNetrunnerDefinition mutableDefinition = defensive;
    const bool copied = runtime.reset(mutableDefinition, 302, 1);
    mutableDefinition.behavior = EnemyBehaviorId::Baseline;
    const bool lifetime = copied && runtime.definition != nullptr &&
        runtime.definition->behavior == EnemyBehaviorId::Defensive;
    const bool presentation = runtime.definition != nullptr && !strcmp(runtime.definition->name, "DEF") &&
        runtime.runner.handle() != nullptr && !strcmp(runtime.runner.handle(), "DEF");

    Program* hellbolt = runtime.cyberdeck.programAt(1);
    if (hellbolt != nullptr) hellbolt->setStatus(ProgramStatus::Rezzed);
    runtime.runner.resetTurn(2);
    const EnemyBehaviorDecision defensiveChoice = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, true);
    Program* shield = runtime.cyberdeck.programAt(0);
    FixedDice dice(10); NetRules rules(dice);
    const ProgramActionResult defensiveActivation = shield != nullptr
        ? rules.activateProgram(runtime.runner, runtime.cyberdeck, *shield) : ProgramActionResult();
    const EnemyBehaviorDecision defensiveFollowUp = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, true);
    const bool defensiveOk = defensiveChoice.type == EnemyDecisionType::ActivateProgram &&
        defensiveChoice.program == ProgramId::Shield && defensiveActivation.executed &&
        defensiveFollowUp.type == EnemyDecisionType::UseProgram && defensiveFollowUp.program == ProgramId::Hellbolt;
    const bool multiAction = defensiveOk && runtime.runner.remainingNetActions() == 1;

    runtime.reset(baseline, 303, 0);
    runtime.cyberdeck.programAt(1)->setStatus(ProgramStatus::Rezzed);
    runtime.runner.resetTurn(2);
    const EnemyBehaviorDecision baselineRemote = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, false);
    const EnemyBehaviorDecision baselineCombat = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, true);
    const EnemyBehaviorDecision baselineIce = chooseEnemyBehaviorDecision(runtime, ProgramId::None, true, true);
    const bool baselineOk = baselineRemote.type == EnemyDecisionType::MoveTowardRunner &&
        baselineCombat.type == EnemyDecisionType::UseProgram && baselineCombat.program == ProgramId::Hellbolt &&
        baselineIce.type == EnemyDecisionType::SlidePlayerBlackIce;

    runtime.reset(sentry, 304, 0);
    const EnemyBehaviorDecision sentryRemote = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, false);
    runtime.cyberdeck.programAt(1)->setStatus(ProgramStatus::Rezzed);
    runtime.runner.resetTurn(1);
    const EnemyBehaviorDecision sentryCombat = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, true);
    const EnemyBehaviorDecision sentryIce = chooseEnemyBehaviorDecision(runtime, ProgramId::None, true, true);
    const bool sentryOk = sentryRemote.type == EnemyDecisionType::None &&
        sentryCombat.type == EnemyDecisionType::UseProgram && sentryCombat.program == ProgramId::Hellbolt &&
        sentryIce.type == EnemyDecisionType::SlidePlayerBlackIce;

    EnemyNetrunnerRuntime defensiveRuntime;
    EnemyNetrunnerRuntime sentryRuntime;
    const bool isolation = defensiveRuntime.reset(defensive, 305, 0) && sentryRuntime.reset(sentry, 306, 1) &&
        defensiveRuntime.definition->behavior == EnemyBehaviorId::Defensive &&
        sentryRuntime.definition->behavior == EnemyBehaviorId::Sentry;
    const ScenarioImportResult sentryImport = loader.loadJson(sentryJson, loaded);
    GameState& state = debugTestState(loaded.definition());
    state.startRun();
    EnemyNetrunnerRuntime* importedSentry = state.enemyNetrunnerAt(0);
    if (importedSentry != nullptr) { importedSentry->runner.takeDamage(4); importedSentry->discoveredByPlayer = true; }
    state.startRun();
    importedSentry = state.enemyNetrunnerAt(0);
    const bool fresh = sentryImport.success && importedSentry != nullptr && importedSentry->definition != nullptr &&
        importedSentry->definition->behavior == EnemyBehaviorId::Sentry && importedSentry->runner.hp() == 20 &&
        !importedSentry->discoveredByPlayer;
    const bool scenarioSwitch = defensiveImport.success && sentryImport.success && fresh;
    const bool playerIce = baselineIce.type == EnemyDecisionType::SlidePlayerBlackIce &&
        chooseEnemyBehaviorDecision(defensiveRuntime, ProgramId::None, true, true).type == EnemyDecisionType::SlidePlayerBlackIce &&
        sentryIce.type == EnemyDecisionType::SlidePlayerBlackIce;
    const bool memory = sizeof(EnemyBehaviorId) == 1 && sizeof(EnemyBehaviorDecision) == 3 &&
        sizeof(EnemyNetrunnerDefinition) == 40 && sizeof(EnemyNetrunnerRuntime) == 548 &&
        sizeof(LoadedScenario) == 7700;
    const bool pass = version && parser && validator && defaulting && registryOk && invalidRuntimeRejected && lifetime &&
        atomicity && scenarioSwitch && fresh && baselineOk && defensiveOk && sentryOk && multiAction && isolation &&
        playerIce && presentation && memory;
    Serial.printf("[R3.5Audit] %s | SchemaVersion=%s | Parser=%s | Validator=%s | Defaulting=%s | Registry=%s | Lifetime=%s | Atomicity=%s | ScenarioSwitch=%s | Fresh=%s | Baseline=%s | Defensive=%s | Sentry=%s | MultiAction=%s | Isolation=%s | PlayerIce=%s | Presentation=%s | Memory=%s\n",
        pass ? "PASS" : "FAIL", version ? "PASS" : "FAIL", parser ? "PASS" : "FAIL",
        validator ? "PASS" : "FAIL", defaulting ? "PASS" : "FAIL", registryOk && invalidRuntimeRejected ? "PASS" : "FAIL",
        lifetime ? "PASS" : "FAIL", atomicity ? "PASS" : "FAIL", scenarioSwitch ? "PASS" : "FAIL",
        fresh ? "PASS" : "FAIL", baselineOk ? "PASS" : "FAIL", defensiveOk ? "PASS" : "FAIL",
        sentryOk ? "PASS" : "FAIL", multiAction ? "PASS" : "FAIL", isolation ? "PASS" : "FAIL",
        playerIce ? "PASS" : "FAIL", presentation ? "PASS" : "FAIL", memory ? "PASS" : "FAIL");
}

void App::runNetrunnerStatsDebugTest()
{
    GameState& state = debugTestState();
    Netrunner& runner = state.runner();
    const bool initialOk = runner.handle() != nullptr &&
        runner.baseInt() == 6 && runner.currentInt() == 6 &&
        runner.baseRef() == 6 && runner.currentRef() == 6 &&
        runner.baseDex() == 6 && runner.currentDex() == 6;

    runner.resetRunStatuses();
    const bool reductionOk = runner.reduceInt(2) == 4 && runner.baseInt() == 6 &&
        runner.currentRef() == 6 && runner.currentDex() == 6;
    const bool minimumOk = runner.reduceInt(10) == 1 && runner.currentInt() == 1;

    runner.resetRunStatuses();
    runner.reduceInt(1); runner.reduceRef(2); runner.reduceDex(3);
    const bool independentOk = runner.currentInt() == 5 && runner.currentRef() == 4 &&
        runner.currentDex() == 3 && runner.baseInt() == 6 && runner.baseRef() == 6 &&
        runner.baseDex() == 6;

    runner.reduceInt(10); runner.reduceRef(10); runner.reduceDex(10);
    state.startRun();
    const bool resetOk = runner.currentInt() == runner.baseInt() &&
        runner.currentRef() == runner.baseRef() && runner.currentDex() == runner.baseDex() &&
        runner.currentInt() == 6 && runner.currentRef() == 6 && runner.currentDex() == 6;

    const bool valid = initialOk && reductionOk && minimumOk && independentOk && resetOk;
    Serial.printf("[NetrunnerStats] %s | Initial=%s | Reduction=%s | Minimum=%s | Independent=%s | Reset=%s\n",
        valid ? "PASS" : "FAIL", initialOk ? "PASS" : "FAIL",
        reductionOk ? "PASS" : "FAIL", minimumOk ? "PASS" : "FAIL",
        independentOk ? "PASS" : "FAIL", resetOk ? "PASS" : "FAIL");
}

void App::runGreymarkDebugTest()
{
    const BlackIceDefinition& greymark = ice11Definition();
    const bool statsOk = greymark.category == BlackIceClass::AntiPersonnel &&
        greymark.perception == 7 && greymark.speed == 3 && greymark.attack == 5 &&
        greymark.defense == 3 && greymark.maxRez == 22 &&
        greymark.effect.type == BlackIceEffectType::ReduceRunnerStatsByAmount &&
        greymark.effect.statusDice == 1;

    bool hitOk = false;
    {
        Netrunner runner("GREYMARK", 4, 40, 3);
        BlackIceInstance ice(greymark);
        GreymarkDice dice(10, 1, 2, 3, 4); NetRules rules(dice);
        const CombatResult result = rules.blackIceAttack(ice, runner);
        hitOk = result.success && result.intBefore == 6 && result.intAfter == 5 &&
            result.refBefore == 6 && result.refAfter == 5 && result.dexBefore == 6 &&
            result.dexAfter == 5 && result.intReduction == 1 && result.refReduction == 1 &&
            result.dexReduction == 1 && result.damage == 0 && runner.currentInt() == 5 &&
            runner.currentRef() == 5 && runner.currentDex() == 5;
    }

    bool minimumOk = false;
    {
        Netrunner runner("GREYMARK_MIN", 4, 40, 3);
        runner.reduceInt(5); runner.reduceRef(5); runner.reduceDex(5);
        BlackIceInstance ice(greymark);
        GreymarkDice dice(10, 1, 6, 6, 6); NetRules rules(dice);
        const CombatResult result = rules.blackIceAttack(ice, runner);
        minimumOk = result.success && runner.currentInt() == 1 && runner.currentRef() == 1 &&
            runner.currentDex() == 1 && result.intAfter == 1 && result.refAfter == 1 &&
            result.dexAfter == 1;
    }

    bool cumulativeOk = false;
    {
        Netrunner runner("GREYMARK_MULTI", 4, 40, 3);
        BlackIceInstance ice(greymark);
        GreymarkDice firstDice(10, 1, 2, 2, 2); NetRules firstRules(firstDice);
        GreymarkDice secondDice(10, 1, 3, 1, 4); NetRules secondRules(secondDice);
        const CombatResult first = firstRules.blackIceAttack(ice, runner);
        const CombatResult second = secondRules.blackIceAttack(ice, runner);
        cumulativeOk = first.success && second.success && runner.currentInt() == 4 &&
            runner.currentRef() == 4 && runner.currentDex() == 4 &&
            runner.baseInt() == 6 && runner.baseRef() == 6 && runner.baseDex() == 6;
    }

    bool missOk = false;
    {
        Netrunner runner("GREYMARK_MISS", 4, 40, 3);
        BlackIceInstance ice(greymark);
        GreymarkDice dice(1, 10, 6, 6, 6); NetRules rules(dice);
        const CombatResult result = rules.blackIceAttack(ice, runner);
        missOk = result.executed && !result.success && result.intBefore == 0 &&
            result.refBefore == 0 && result.dexBefore == 0 && runner.currentInt() == 6 &&
            runner.currentRef() == 6 && runner.currentDex() == 6;
    }

    static FloorDefinition multiFloors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "GREYMARK_STINGWIRE", nullptr, nullptr, 0,
         BlackIceType::Ice11, "GREYMARK STINGWIRE", nullptr, nullptr}
    };
    static bool multiConfigured = false;
    if (!multiConfigured)
    {
        multiFloors[0].blackIceCount = 2;
        multiFloors[0].blackIceTypes[0] = BlackIceType::Ice11;
        multiFloors[0].blackIceTypes[1] = BlackIceType::Ice07;
        multiConfigured = true;
    }
    static const ArchitectureDefinition multiArchitecture = {
        "greymark_stingwire_test", "GREYMARK STINGWIRE", nullptr, multiFloors, 1};
    bool multiOk = false;
    {
        GameState& state = debugTestState(multiArchitecture);
        state.startRun(); state.jackIn(); state.triggerCurrentBlackIce(); state.endPlayerTurn();
        // GREYMARK applies its fixed reduction; STINGWIRE then applies FIRE only.
        GreymarkDice dice(10, 1, 2, 3, 4, 10, 1, 3); NetRules rules(dice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(rules, phase);
        multiOk = phase.attackCount == 2 && phase.attacks[0].result.intAfter == 5 &&
            phase.attacks[0].result.refAfter == 5 && phase.attacks[0].result.dexAfter == 5 &&
            phase.attacks[1].result.fireApplied && phase.attacks[1].result.damage == 0 &&
            phase.attacks[0].runtimeId != phase.attacks[1].runtimeId;
    }

    const bool valid = statsOk && hitOk && minimumOk && cumulativeOk && missOk && multiOk;
    Serial.printf("[Greymark] %s | Stats=%s | Hit=%s | Minimum=%s | Cumulative=%s | Miss=%s | Multi=%s\n",
        valid ? "PASS" : "FAIL", statsOk ? "PASS" : "FAIL", hitOk ? "PASS" : "FAIL",
        minimumOk ? "PASS" : "FAIL", cumulativeOk ? "PASS" : "FAIL",
        missOk ? "PASS" : "FAIL", multiOk ? "PASS" : "FAIL");
}

void App::runGameModelDebugTest()
{
    gameState_.startRun();
    const Floor* firstFloor = gameState_.architecture().floorAt(0);
    const bool valid = gameState_.runState() == RunState::Ready &&
        strcmp(gameState_.runner().handle(), runnerProfile_.handle) == 0 &&
        gameState_.runner().interfaceRank() == runnerProfile_.interfaceRank &&
        gameState_.runner().hp() == runnerProfile_.maxHp &&
        gameState_.runner().remainingNetActions() ==
            Netrunner::netActionsForInterfaceRank(runnerProfile_.interfaceRank) &&
        gameState_.cyberdeck().programCount() == cyberdeckConfig_.programCount &&
        strcmp(gameState_.architecture().name(), "DEMO NET") == 0 &&
        gameState_.architecture().floorCount() == 8 && firstFloor != nullptr &&
        firstFloor->id == 1 && firstFloor->type == FloorType::Password && firstFloor->dv == 6;

    Serial.printf("[GameModel] %s | Runner=%s | Architecture=%s | Floor1=%u DV%u\n",
        valid ? "PASS" : "FAIL", gameState_.runner().handle(), gameState_.architecture().name(),
        firstFloor != nullptr ? firstFloor->id : 0, firstFloor != nullptr ? firstFloor->dv : 0);
}

void App::runNetRulesDebugTest()
{
    GameState& testState = debugTestState();
    testState.startRun();
    testState.jackIn();
    testState.startTurn();

    FixedDice fixedDice(10);
    NetRules rules(fixedDice);
    Floor* password = testState.architecture().currentFloor();
    if (password == nullptr)
    {
        Serial.println("[NetRules] FAIL | Missing Password floor");
        return;
    }
    const uint8_t actionsBefore = testState.runner().remainingNetActions();
    const NetCheckResult backdoorResult = rules.backdoor(testState.runner(), *password);
    const bool spentExactlyOneAction =
        testState.runner().remainingNetActions() + 1 == actionsBefore;

    testState.architecture().moveForward();
    Floor* file = testState.architecture().currentFloor();
    if (file == nullptr)
    {
        Serial.println("[NetRules] FAIL | Missing File floor");
        return;
    }
    const uint8_t actionsBeforeWrongFloor = testState.runner().remainingNetActions();
    const NetCheckResult wrongFloorResult = rules.backdoor(testState.runner(), *file);
    const bool wrongFloorRejected = !wrongFloorResult.attempted &&
        testState.runner().remainingNetActions() == actionsBeforeWrongFloor;

    testState.endTurn();
    const bool turnRestarted = testState.startTurn();
    const bool actionsReset = turnRestarted &&
        testState.runner().remainingNetActions() == testState.runner().maxNetActions();

    Netrunner tieRunner("TIE_TEST", 4, 1, 1);
    Floor tiePassword(1, FloorType::Password, 6, "TIE_PASSWORD");
    FixedDice tieDice(2);
    NetRules tieRules(tieDice);
    const NetCheckResult tieResult = tieRules.backdoor(tieRunner, tiePassword);
    const bool tieFails = tieResult.attempted && tieResult.total == tieResult.target &&
        !tieResult.success && !tiePassword.resolved;

    Netrunner winningRunner("WIN_TEST", 4, 1, 1);
    Floor winningPassword(1, FloorType::Password, 6, "WIN_PASSWORD");
    FixedDice winningDice(3);
    NetRules winningRules(winningDice);
    const NetCheckResult winningResult =
        winningRules.backdoor(winningRunner, winningPassword);
    const bool targetPlusOneSucceeds = winningResult.attempted &&
        winningResult.total == winningResult.target + 1 &&
        winningResult.success && winningPassword.resolved;

    const bool valid = backdoorResult.attempted && backdoorResult.success && password->resolved &&
        spentExactlyOneAction && wrongFloorRejected && actionsReset &&
        tieFails && targetPlusOneSucceeds;

    Serial.printf(
        "[NetRules] %s | BackdoorAction=%s | WrongFloor=%s | Resolved=%s | "
        "TurnReset=%s | TieFails=%s | DVPlusOne=%s\n",
        valid ? "PASS" : "FAIL",
        spentExactlyOneAction ? "PASS" : "FAIL",
        wrongFloorRejected ? "PASS" : "FAIL",
        password->resolved ? "PASS" : "FAIL",
        actionsReset ? "PASS" : "FAIL",
        tieFails ? "PASS" : "FAIL",
        targetPlusOneSucceeds ? "PASS" : "FAIL");
}

void App::runNetCombatDebugTest()
{
    GameState& activationState = debugTestState();
    activationState.startRun();
    activationState.jackIn();
    activationState.startTurn();
    activationState.architecture().moveForward();
    activationState.architecture().moveForward();
    const bool activationOk = activationState.triggerCurrentBlackIce() &&
        activationState.activeBlackIceCount() == 1 &&
        activationState.activeBlackIceAt(0)->active() &&
        activationState.activeBlackIceAt(0)->pursuing();

    GameState& swordTieState = debugTestState();
    swordTieState.startRun();
    swordTieState.jackIn();
    swordTieState.startTurn();
    swordTieState.cyberdeck().programAt(0)->setStatus(ProgramStatus::Rezzed);
    BlackIceInstance swordTieIce(ice01Definition());
    CombatDice swordTieDice(1, 3, 0);
    NetRules swordTieRules(swordTieDice);
    const uint8_t swordActions = swordTieState.runner().remainingNetActions();
    const CombatResult swordTie = swordTieRules.swordAttack(
        swordTieState.runner(), swordTieState.cyberdeck(), swordTieIce);
    const bool swordTieOk = swordTie.executed && !swordTie.success &&
        swordTie.attackerTotal == swordTie.defenderTotal &&
        swordTieState.runner().remainingNetActions() + 1 == swordActions;

    GameState& swordHitState = debugTestState();
    swordHitState.startRun();
    swordHitState.jackIn();
    swordHitState.startTurn();
    swordHitState.cyberdeck().programAt(0)->setStatus(ProgramStatus::Rezzed);
    BlackIceInstance swordHitIce(ice01Definition());
    CombatDice swordHitDice(10, 1, 4);
    NetRules swordHitRules(swordHitDice);
    const CombatResult swordHit = swordHitRules.swordAttack(
        swordHitState.runner(), swordHitState.cyberdeck(), swordHitIce);
    const bool swordHitOk = swordHit.executed && swordHit.success && swordHit.damage == 4 &&
        swordHitDice.lastD6Count() == 3 && swordHitIce.currentRez() == 14 &&
        swordHitIce.active() && swordHitIce.pursuing();

    GameState& derezzState = debugTestState();
    derezzState.startRun();
    derezzState.jackIn();
    derezzState.startTurn();
    derezzState.cyberdeck().programAt(0)->setStatus(ProgramStatus::Rezzed);
    BlackIceInstance derezzIce(ice01Definition());
    CombatDice derezzDice(10, 1, 18);
    NetRules derezzRules(derezzDice);
    const CombatResult derezzHit = derezzRules.swordAttack(
        derezzState.runner(), derezzState.cyberdeck(), derezzIce);
    const bool derezzOk = derezzHit.executed && derezzHit.success && derezzHit.damage == 18 &&
        derezzDice.lastD6Count() == 3 && derezzIce.currentRez() == 0 &&
        !derezzIce.active() && !derezzIce.pursuing();

    Netrunner zapTieRunner("ZAP_TIE", 4, 40, 3);
    BlackIceInstance zapTieIce(ice01Definition());
    CombatDice zapTieDice(1, 2, 0);
    NetRules zapTieRules(zapTieDice);
    const CombatResult zapTie = zapTieRules.zap(zapTieRunner, zapTieIce);
    Netrunner zapHitRunner("ZAP_HIT", 4, 40, 3);
    BlackIceInstance zapHitIce(ice01Definition());
    CombatDice zapHitDice(10, 1, 4);
    NetRules zapHitRules(zapHitDice);
    const CombatResult zapHit = zapHitRules.zap(zapHitRunner, zapHitIce);
    const bool zapOk = zapTie.executed && !zapTie.success &&
        zapTie.attackerTotal == zapTie.defenderTotal && zapHit.success &&
        zapHit.damage == 4 && zapHitDice.lastD6Count() == 1 &&
        zapHitIce.currentRez() == 14 && zapHitIce.active() && zapHitIce.pursuing() &&
        zapHitRunner.remainingNetActions() == 2;

    Netrunner slideTieRunner("SLIDE_TIE", 4, 40, 3);
    BlackIceInstance slideTieIce(ice01Definition());
    CombatDice slideTieDice(3, 2, 0);
    NetRules slideTieRules(slideTieDice);
    const CombatResult slideTie = slideTieRules.slide(slideTieRunner, slideTieIce);
    GameState& slideState = debugTestState();
    slideState.startRun();
    slideState.jackIn();
    slideState.startTurn();
    Netrunner& slideRunner = slideState.runner();
    BlackIceInstance slideIce(ice01Definition());
    const int rezBeforeSlide = slideIce.currentRez();
    CombatDice slideDice(4, 1, 0);
    NetRules slideRules(slideDice);
    const CombatResult slideSuccess = slideRules.slide(slideRunner, slideIce);
    const CombatResult secondSlide = slideRules.slide(slideRunner, slideIce);
    slideState.endTurn();
    const bool nextTurnStarted = slideState.startTurn();
    slideIce.setPursuing(true);
    CombatDice nextSlideDice(4, 1, 0);
    NetRules nextSlideRules(nextSlideDice);
    const CombatResult nextTurnSlide = nextSlideRules.slide(slideRunner, slideIce);
    const bool slideReset = nextTurnStarted && nextTurnSlide.executed;
    const bool slideOk = slideTie.executed && !slideTie.success &&
        slideTie.attackerTotal == slideTie.defenderTotal && slideSuccess.success &&
        !slideIce.pursuing() && slideIce.currentRez() == rezBeforeSlide &&
        !secondSlide.executed && slideReset;

    Netrunner TracejackalTieTarget("Tracejackal_TIE", 4, 40, 3);
    BlackIceInstance TracejackalTie(ice01Definition());
    CombatDice TracejackalTieDice(1, 2, 0);
    NetRules TracejackalTieRules(TracejackalTieDice);
    const CombatResult TracejackalTieResult = TracejackalTieRules.blackIceAttack(TracejackalTie, TracejackalTieTarget);
    Netrunner TracejackalTarget("Tracejackal_HIT", 4, 40, 3);
    BlackIceInstance Tracejackal(ice01Definition());
    CombatDice TracejackalDice(2, 1, 4);
    NetRules TracejackalRules(TracejackalDice);
    const CombatResult TracejackalHit = TracejackalRules.blackIceAttack(Tracejackal, TracejackalTarget);
    const bool TracejackalOk = TracejackalTieResult.executed && !TracejackalTieResult.success &&
        TracejackalTieResult.attackerTotal == TracejackalTieResult.defenderTotal &&
        TracejackalHit.success && TracejackalHit.damage == 4 && TracejackalDice.lastD6Count() == 2 &&
        TracejackalTarget.hp() == 36 && !TracejackalHit.fireApplied && !TracejackalHit.fireBlocked;

    Netrunner noActions("NO_ACTIONS", 4, 40, 1);
    noActions.spendNetAction();
    BlackIceInstance validIce(ice01Definition());
    CombatDice invalidDice(10, 1, 18);
    NetRules invalidRules(invalidDice);
    const bool noActionRejected = !invalidRules.zap(noActions, validIce).executed;
    Netrunner noSwordRunner("NO_SWORD", 4, 40, 3);
    Cyberdeck emptyDeck;
    const bool noSwordRejected = !invalidRules.swordAttack(
        noSwordRunner, emptyDeck, validIce).executed;
    BlackIceInstance inactiveIce(ice01Definition());
    inactiveIce.takeRezDamage(20);
    Netrunner inactiveRunner("INACTIVE", 4, 40, 3);
    const bool inactiveRejected = !invalidRules.zap(inactiveRunner, inactiveIce).executed;
    const bool wrongStateOk = noActionRejected && noSwordRejected && inactiveRejected;

    const bool valid = activationOk && swordTieOk && swordHitOk && zapOk &&
        slideOk && TracejackalOk && derezzOk && wrongStateOk;
    Serial.printf("[Diag][NetCombat][Sword] tie=%u/%u totals=%d/%d | hit=%u/%u totals=%d/%d damage=%d rez=%d/%d active=%u expectedActive=1 | derezz=%u/%u rez=%d active=%u | checks=t%u h%u d%u\n",
        swordTie.executed ? 1U : 0U, swordTie.success ? 1U : 0U,
        swordTie.attackerTotal, swordTie.defenderTotal,
        swordHit.executed ? 1U : 0U, swordHit.success ? 1U : 0U,
        swordHit.attackerTotal, swordHit.defenderTotal, swordHit.damage,
        ice01Definition().maxRez, swordHitIce.currentRez(), swordHitIce.active() ? 1U : 0U,
        derezzHit.executed ? 1U : 0U, derezzHit.success ? 1U : 0U,
        derezzIce.currentRez(), derezzIce.active() ? 1U : 0U,
        swordTieOk ? 1U : 0U, swordHitOk ? 1U : 0U, derezzOk ? 1U : 0U);
    Serial.printf(
        "[NetCombat] %s | Sword=%s | Zap=%s | Slide=%s | Tracejackal=%s | "
        "Derezz=%s | State=%s\n",
        valid ? "PASS" : "FAIL", swordTieOk && swordHitOk ? "PASS" : "FAIL",
        zapOk ? "PASS" : "FAIL", slideOk ? "PASS" : "FAIL",
        TracejackalOk && activationOk ? "PASS" : "FAIL", derezzOk ? "PASS" : "FAIL",
        wrongStateOk ? "PASS" : "FAIL");
}

void App::runPlayerZapEnemyDebugTest()
{
    // The suite intentionally exercises the production GameState -> NetRules
    // path. UI checks use the same controller instance and only verify the
    // focused floor action list; no test-only combat implementation is used.

    GameState& hitState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    hitState.startRun(); hitState.jackIn(); hitState.architecture().moveForward();
    EnemyNetrunnerRuntime* hitEnemy = hitState.floorEnemyNetrunner();
    const uint16_t hitEnemyId = hitEnemy != nullptr ? hitEnemy->runtimeId : 0;
    const bool target = hitEnemyId != 0 && hitEnemy != nullptr && hitEnemy->currentFloor == 1;
    CombatDice hitDice(10, 1, 6); NetRules hitRules(hitDice);
    GameUIController* hitUi = new (debugUiStorage) GameUIController(
        display_, hitState, hitRules, scenarioScanner_, loadedScenario_, playerConfigStore_,
        runnerProfile_, cyberdeckConfig_);
    const bool available = hitEnemy != nullptr && hitEnemyId != 0 &&
        hitUi->selectEnemyForDebug(hitEnemyId) && hitUi->hasFloorActionForDebug("ZAP");
    logStackHighWaterMark("PlayerZapEnemy Available");
    hitUi->~GameUIController();
    const CombatResult hit = hitState.zapFloorEnemy(hitRules, hitEnemyId);
    const bool hitOk = hit.executed && hit.success && hit.attackerTotal == 14 &&
        hit.defenderTotal == 5 && hit.damage == 6 && hit.rawDamage == 6 &&
        hitEnemy != nullptr && hitEnemy->runner.hp() == 24 &&
        hitState.runner().remainingNetActions() == 2;
    logStackHighWaterMark("PlayerZapEnemy Hit");

    GameState& missState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    missState.startRun(); missState.jackIn(); missState.architecture().moveForward();
    EnemyNetrunnerRuntime* missEnemy = missState.floorEnemyNetrunner();
    CombatDice missDice(1, 10, 6); NetRules missRules(missDice);
    const int missHp = missEnemy != nullptr ? missEnemy->runner.hp() : -1;
    const CombatResult miss = missState.zapFloorEnemy(missRules,
        missEnemy != nullptr ? missEnemy->runtimeId : 0);
    const bool missOk = miss.executed && !miss.success && miss.damage == 0 &&
        missEnemy != nullptr && missEnemy->runner.hp() == missHp &&
        missState.runner().remainingNetActions() == 2;

    GameState& tieState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    tieState.startRun(); tieState.jackIn(); tieState.architecture().moveForward();
    EnemyNetrunnerRuntime* tieEnemy = tieState.floorEnemyNetrunner();
    CombatDice tieDice(3, 3, 6); NetRules tieRules(tieDice);
    const CombatResult tie = tieState.zapFloorEnemy(tieRules,
        tieEnemy != nullptr ? tieEnemy->runtimeId : 0);
    const bool tieOk = tie.executed && !tie.success && tie.attackerTotal == tie.defenderTotal &&
        tieState.runner().remainingNetActions() == 2;

    GameState& lethalState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    lethalState.startRun(); lethalState.jackIn(); lethalState.architecture().moveForward();
    EnemyNetrunnerRuntime* lethalEnemy = lethalState.floorEnemyNetrunner();
    if (lethalEnemy != nullptr) lethalEnemy->runner.takeDamage(lethalEnemy->runner.hp() - 6);
    CombatDice lethalDice(10, 1, 6); NetRules lethalRules(lethalDice);
    const CombatResult lethal = lethalState.zapFloorEnemy(lethalRules,
        lethalEnemy != nullptr ? lethalEnemy->runtimeId : 0);
    const bool lethalOk = lethal.executed && lethal.success && lethalEnemy != nullptr &&
        lethalEnemy->runner.hp() == 0 && !lethalEnemy->available() && lethalEnemy->runnerDown;

    GameState& invalidState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    invalidState.startRun(); invalidState.jackIn(); invalidState.architecture().moveForward();
    EnemyNetrunnerRuntime* invalidEnemy = invalidState.floorEnemyNetrunner();
    CombatDice invalidDice(10, 1, 6); NetRules invalidRules(invalidDice);
    const uint8_t invalidActions = invalidState.runner().remainingNetActions();
    const bool invalidTarget = !invalidState.zapFloorEnemy(invalidRules, 0).executed &&
        invalidState.runner().remainingNetActions() == invalidActions;
    invalidState.runner().spendNetAction(); invalidState.runner().spendNetAction();
    invalidState.runner().spendNetAction();
    const bool noActions = invalidEnemy != nullptr &&
        !invalidState.zapFloorEnemy(invalidRules, invalidEnemy->runtimeId).executed;

    GameState& remoteState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    remoteState.startRun(); remoteState.jackIn();
    EnemyNetrunnerRuntime* remoteEnemy = remoteState.enemyNetrunnerAt(0);
    CombatDice remoteDice(10, 1, 6); NetRules remoteRules(remoteDice);
    const bool remote = remoteEnemy != nullptr &&
        !remoteState.zapFloorEnemy(remoteRules, remoteEnemy->runtimeId).executed;

    GameState& mixedState = debugPlayerTargetContextState();
    const bool mixedIce = mixedState.floorBlackIceCount() == 1;
    EnemyNetrunnerRuntime* mixedEnemy = mixedState.floorEnemyNetrunner();
    BlackIceInstance* mixedBlackIce = mixedState.floorBlackIceAt(0);
    const int mixedRezBefore = mixedBlackIce != nullptr ? mixedBlackIce->currentRez() : -1;
    CombatDice mixedDice(10, 1, 6); NetRules mixedRules(mixedDice);
    GameUIController* mixedUi = new (debugUiStorage) GameUIController(
        display_, mixedState, mixedRules, scenarioScanner_, loadedScenario_, playerConfigStore_,
        runnerProfile_, cyberdeckConfig_);
    const bool mixedAvailable = mixedIce && mixedEnemy != nullptr && mixedBlackIce != nullptr &&
        mixedUi->selectEnemyForDebug(mixedEnemy->runtimeId) &&
        mixedUi->hasFloorActionForDebug("ZAP") &&
        mixedUi->selectBlackIceForDebug(mixedState.floorBlackIceRuntimeId(0)) &&
        mixedUi->hasFloorActionForDebug("ZAP");
    const bool mixedExecuted = mixedUi->selectEnemyForDebug(mixedEnemy != nullptr ? mixedEnemy->runtimeId : 0) &&
        mixedUi->executeFloorActionForDebug("ZAP");
    const bool mixed = mixedAvailable && mixedExecuted && mixedEnemy != nullptr &&
        mixedEnemy->runner.hp() == 24 && mixedBlackIce->currentRez() == mixedRezBefore &&
        mixedState.runner().remainingNetActions() == 2;
    logStackHighWaterMark("PlayerZapEnemy Mixed");
    mixedUi->~GameUIController();

    GameState& freshState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    freshState.startRun(); freshState.jackIn();
    const bool fresh = freshState.runner().remainingNetActions() == 3 &&
        freshState.enemyNetrunnerAt(0) != nullptr;
    logStackHighWaterMark("PlayerZapEnemy Fresh");
    const bool pass = available && hitOk && missOk && tieOk && lethalOk &&
        invalidTarget && noActions && remote && mixed && target && fresh;
    Serial.printf("[PlayerZapEnemy] %s | Available=%s | Remote=%s | Hit=%s | Miss=%s | Tie=%s | Damage=%s | ActionCost=%s | Lethal=%s | NoActions=%s | Target=%s | Mixed=%s | Fresh=%s\n",
        pass ? "PASS" : "FAIL", available ? "PASS" : "FAIL", remote ? "PASS" : "FAIL",
        hitOk ? "PASS" : "FAIL", missOk ? "PASS" : "FAIL", tieOk ? "PASS" : "FAIL",
        hitOk ? "PASS" : "FAIL", (hitOk && missOk && tieOk) ? "PASS" : "FAIL",
        lethalOk ? "PASS" : "FAIL", noActions ? "PASS" : "FAIL", target ? "PASS" : "FAIL",
        mixed ? "PASS" : "FAIL", fresh ? "PASS" : "FAIL");
}

void App::runPlayerSlideTargetingDebugTest()
{
    GameState& enemyState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    enemyState.startRun(); enemyState.jackIn(); enemyState.architecture().moveForward();
    EnemyNetrunnerRuntime* enemy = enemyState.floorEnemyNetrunner();
    CombatDice enemyDice(10, 1, 6); NetRules enemyRules(enemyDice);
    GameUIController* ui = new (debugUiStorage) GameUIController(
        display_, enemyState, enemyRules, scenarioScanner_, loadedScenario_, playerConfigStore_,
        runnerProfile_, cyberdeckConfig_);
    const uint8_t enemyActions = enemyState.runner().remainingNetActions();
    const int enemyHp = enemy != nullptr ? enemy->runner.hp() : -1;
    const bool enemyHidden = enemy != nullptr && ui->selectEnemyForDebug(enemy->runtimeId) &&
        !ui->hasFloorActionForDebug("SLIDE");
    ui->executeSlideForDebug();
    const bool enemyRejected = enemy != nullptr && enemy->runner.hp() == enemyHp &&
        enemyState.runner().remainingNetActions() == enemyActions;
    ui->~GameUIController();

    GameState& mixedState = debugPlayerTargetContextState();
    const bool mixedIceSpawned = mixedState.floorBlackIceCount() == 1;
    EnemyNetrunnerRuntime* mixedEnemy = mixedState.floorEnemyNetrunner();
    BlackIceInstance* mixedIce = mixedState.floorBlackIceAt(0);
    CombatDice mixedDice(10, 1, 0); NetRules mixedRules(mixedDice);
    ui = new (debugUiStorage) GameUIController(
        display_, mixedState, mixedRules, scenarioScanner_, loadedScenario_, playerConfigStore_,
        runnerProfile_, cyberdeckConfig_);
    const bool mixedEnemyFocus = mixedIceSpawned && mixedEnemy != nullptr &&
        ui->selectEnemyForDebug(mixedEnemy->runtimeId) && !ui->hasFloorActionForDebug("SLIDE");
    const bool mixedIceFocus = mixedIce != nullptr &&
        ui->selectBlackIceForDebug(mixedState.floorBlackIceRuntimeId(0)) &&
        ui->hasFloorActionForDebug("SLIDE");
    const uint8_t iceActions = mixedState.runner().remainingNetActions();
    const bool existingSlideDispatch = mixedIceFocus && ui->executeFloorActionForDebug("SLIDE") &&
        mixedIce != nullptr && !mixedIce->pursuing() &&
        mixedState.runner().remainingNetActions() + 1 == iceActions;
    ui->~GameUIController();

    const bool pass = enemyHidden && enemyRejected && mixedEnemyFocus && mixedIceFocus &&
        existingSlideDispatch;
    Serial.printf("[PlayerSlideTargeting] %s | EnemyHidden=%s | EnemyRejected=%s | NoActionCost=%s | BlackIce=%s | MixedEnemy=%s | MixedIce=%s | ExistingSlide=%s\n",
        pass ? "PASS" : "FAIL", enemyHidden ? "PASS" : "FAIL", enemyRejected ? "PASS" : "FAIL",
        enemyRejected ? "PASS" : "FAIL", mixedIceFocus ? "PASS" : "FAIL",
        mixedEnemyFocus ? "PASS" : "FAIL", mixedIceFocus ? "PASS" : "FAIL",
        existingSlideDispatch ? "PASS" : "FAIL");
}

void App::runPlayerTargetContextDebugTest()
{
    GameState& neutralState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    neutralState.startRun(); neutralState.jackIn();
    CombatDice neutralDice(10, 1, 6); NetRules neutralRules(neutralDice);
    GameUIController* ui = new (debugUiStorage) GameUIController(
        display_, neutralState, neutralRules, scenarioScanner_, loadedScenario_, playerConfigStore_,
        runnerProfile_, cyberdeckConfig_);
    const bool neutralOnly = ui->selectFloorObjectForDebug() &&
        ui->hasFloorActionForDebug("BACKDOOR") && !ui->hasFloorActionForDebug("ZAP") &&
        !ui->hasFloorActionForDebug("SLIDE");
    ui->~GameUIController();

    GameState& mixedState = debugPlayerTargetContextState();
    EnemyNetrunnerRuntime* enemy = mixedState.floorEnemyNetrunner();
    BlackIceInstance* ice = mixedState.floorBlackIceAt(0);
    CombatDice mixedDice(10, 1, 6); NetRules mixedRules(mixedDice);
    ui = new (debugUiStorage) GameUIController(
        display_, mixedState, mixedRules, scenarioScanner_, loadedScenario_, playerConfigStore_,
        runnerProfile_, cyberdeckConfig_);
    const uint16_t iceId = mixedState.floorBlackIceRuntimeId(0);
    const bool enemyOnly = enemy != nullptr && ui->selectEnemyForDebug(enemy->runtimeId) &&
        ui->hasFloorActionForDebug("ZAP") && !ui->hasFloorActionForDebug("SLIDE");
    const bool blackIceOnly = ice != nullptr && iceId != 0 && ui->selectBlackIceForDebug(iceId) &&
        ui->hasFloorActionForDebug("ZAP") && ui->hasFloorActionForDebug("SLIDE");
    const bool mixedEnemyFocus = enemyOnly;
    const bool mixedIceFocus = blackIceOnly;
    const bool mixedObjectFocus = ui->selectFloorObjectForDebug() &&
        ui->hasFloorActionForDebug("EYE-DEE") && !ui->hasFloorActionForDebug("ZAP") &&
        !ui->hasFloorActionForDebug("SLIDE");
    const bool focusSwitch = ui->selectEnemyForDebug(enemy != nullptr ? enemy->runtimeId : 0) &&
        ui->hasFloorActionForDebug("ZAP") && !ui->hasFloorActionForDebug("SLIDE") &&
        ui->selectBlackIceForDebug(iceId) && ui->hasFloorActionForDebug("ZAP") &&
        ui->hasFloorActionForDebug("SLIDE") && ui->selectFloorObjectForDebug() &&
        !ui->hasFloorActionForDebug("ZAP");
    const uint8_t actionsBefore = mixedState.runner().remainingNetActions();
    const int enemyHpBefore = enemy != nullptr ? enemy->runner.hp() : -1;
    if (enemy != nullptr) ui->selectEnemyForDebug(enemy->runtimeId);
    ui->executeSlideForDebug();
    const bool directInvalidSlide = enemy != nullptr && enemy->runner.hp() == enemyHpBefore &&
        mixedState.runner().remainingNetActions() == actionsBefore;
    ui->selectFloorObjectForDebug();
    ui->executeZapForDebug();
    const bool directInvalidHostile = mixedState.runner().remainingNetActions() == actionsBefore;
    ui->~GameUIController();

    const bool pass = neutralOnly && blackIceOnly && enemyOnly && mixedEnemyFocus && mixedIceFocus &&
        mixedObjectFocus && focusSwitch && directInvalidSlide && directInvalidHostile;
    Serial.printf("[PlayerTargetContext] %s | NeutralOnly=%s | BlackIceOnly=%s | EnemyOnly=%s | MixedEnemy=%s | MixedIce=%s | MixedObject=%s | FocusSwitch=%s | DirectInvalidSlide=%s | DirectInvalidHostile=%s\n",
        pass ? "PASS" : "FAIL", neutralOnly ? "PASS" : "FAIL", blackIceOnly ? "PASS" : "FAIL",
        enemyOnly ? "PASS" : "FAIL", mixedEnemyFocus ? "PASS" : "FAIL",
        mixedIceFocus ? "PASS" : "FAIL", mixedObjectFocus ? "PASS" : "FAIL",
        focusSwitch ? "PASS" : "FAIL", directInvalidSlide ? "PASS" : "FAIL",
        directInvalidHostile ? "PASS" : "FAIL");
}

void App::runBlackIceDebugTest()
{
    static Architecture parsedRuntime;
    static const FloorDefinition needlecoilFloors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "NEEDLECOIL",
         nullptr, nullptr, 0, BlackIceType::Ice03, "BLACK ICE", nullptr, nullptr}
    };
    static const ArchitectureDefinition needlecoilArchitecture = {
        "needlecoil_runtime_test", "NEEDLECOIL RUNTIME TEST", nullptr, needlecoilFloors, 1
    };
    static const FloorDefinition carrionbyteFloors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "CARRIONBYTE",
         nullptr, nullptr, 0, BlackIceType::Ice02, "BLACK ICE", nullptr, nullptr}
    };
    static const ArchitectureDefinition carrionbyteArchitecture = {
        "carrionbyte_runtime_test", "CARRIONBYTE RUNTIME TEST", nullptr, carrionbyteFloors, 1
    };
    static const FloorDefinition deadlockFloors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "DEADLOCK",
         nullptr, nullptr, 0, BlackIceType::Ice04, "BLACK ICE", nullptr, nullptr}
    };
    static const ArchitectureDefinition deadlockArchitecture = {
        "deadlock_runtime_test", "DEADLOCK RUNTIME TEST", nullptr, deadlockFloors, 1
    };
    static const FloorDefinition ghostpulseFloors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "GHOSTPULSE",
         nullptr, nullptr, 0, BlackIceType::Ice05, "BLACK ICE", nullptr, nullptr}
    };
    static const ArchitectureDefinition ghostpulseArchitecture = {
        "ghostpulse_runtime_test", "GHOSTPULSE RUNTIME TEST", nullptr, ghostpulseFloors, 1
    };
    static const FloorDefinition packetsawFloors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "PACKETSAW",
         nullptr, nullptr, 0, BlackIceType::Ice06, "BLACK ICE", nullptr, nullptr}
    };
    static const ArchitectureDefinition packetsawArchitecture = {
        "packetsaw_runtime_test", "PACKETSAW RUNTIME TEST", nullptr, packetsawFloors, 1
    };

    const BlackIceDefinition& tracejackal = ice01Definition();
    const BlackIceDefinition& needlecoil = ice03Definition();
    const BlackIceDefinition& carrionbyte = ice02Definition();
    const BlackIceDefinition& deadlock = ice04Definition();
    const BlackIceDefinition& ghostpulse = ice05Definition();
    const BlackIceDefinition& packetsaw = ice06Definition();
    const BlackIceDefinition& stingwire = ice07Definition();
    const BlackIceDefinition& guttermesh = ice08Definition();
    const BlackIceDefinition& stormrazor = ice09Definition();
    const BlackIceDefinition& glasscat = ice10Definition();
    const BlackIceDefinition& greymark = ice11Definition();
    const BlackIceDefinition& breacher = ice12Definition();
    const bool definitionsOk = tracejackal.perception == 5 && tracejackal.speed == 7 &&
        tracejackal.attack == 5 && tracejackal.defense == 3 && tracejackal.maxRez == 18 &&
        tracejackal.effect.type == BlackIceEffectType::DamageOnly &&
        tracejackal.effect.damageDice == 2 && needlecoil.perception == 5 && needlecoil.speed == 7 &&
        needlecoil.attack == 3 && needlecoil.defense == 3 && needlecoil.maxRez == 16 &&
        needlecoil.effect.type == BlackIceEffectType::DerezzRandomDefenderAndDamage &&
        needlecoil.effect.damageDice == 1 && carrionbyte.perception == 7 && carrionbyte.speed == 5 &&
        carrionbyte.attack == 4 && carrionbyte.defense == 3 && carrionbyte.maxRez == 16 &&
        carrionbyte.effect.type == BlackIceEffectType::DestroyRandomInstalledProgram &&
        carrionbyte.effect.damageDice == 0 && deadlock.perception == 5 && deadlock.speed == 3 &&
        deadlock.attack == 7 && deadlock.defense == 5 && deadlock.maxRez == 26 &&
        deadlock.effect.type == BlackIceEffectType::DamageAndNavigationLock &&
        deadlock.effect.damageDice == 2 && ghostpulse.perception == 5 && ghostpulse.speed == 5 &&
        ghostpulse.attack == 4 && ghostpulse.defense == 3 && ghostpulse.maxRez == 14 &&
        ghostpulse.effect.type == BlackIceEffectType::DamageAndNextTurnNetActionPenalty &&
        ghostpulse.effect.damageDice == 1 && ghostpulse.effect.netActionPenalty == 1 &&
        ghostpulse.effect.minimumNetActions == 2 && packetsaw.category == BlackIceClass::AntiProgram &&
        packetsaw.perception == 5 && packetsaw.speed == 7 && packetsaw.attack == 5 &&
        packetsaw.defense == 3 && packetsaw.maxRez == 19 &&
        packetsaw.effect.type == BlackIceEffectType::ProgramDamageDestroyAtZero &&
        packetsaw.effect.damageDice == 3 && stingwire.perception == 3 && stingwire.speed == 7 &&
        stingwire.attack == 3 && stingwire.defense == 3 && stingwire.maxRez == 14 &&
        stingwire.effect.type == BlackIceEffectType::ApplyFire && stingwire.effect.damageDice == 0 &&
        guttermesh.perception == 3 && guttermesh.speed == 5 && guttermesh.attack == 5 &&
        guttermesh.defense == 3 && guttermesh.maxRez == 12 &&
        guttermesh.effect.type == BlackIceEffectType::DamageAndReduceRunnerMove &&
        guttermesh.effect.damageDice == 1 && guttermesh.effect.statusDice == 1 &&
        stormrazor.perception == 7 && stormrazor.speed == 5 && stormrazor.attack == 6 &&
        stormrazor.defense == 5 && stormrazor.maxRez == 27 &&
        stormrazor.effect.type == BlackIceEffectType::ProgramDamageDestroyAtZero &&
        stormrazor.effect.damageDice == 5 && glasscat.perception == 8 && glasscat.speed == 7 &&
        glasscat.attack == 5 && glasscat.defense == 3 && glasscat.maxRez == 23 &&
        glasscat.effect.type == BlackIceEffectType::ProgramDamageDestroyAtZero &&
        glasscat.effect.damageDice == 4 && greymark.perception == 7 && greymark.speed == 3 &&
        greymark.attack == 5 && greymark.defense == 3 && greymark.maxRez == 22 &&
        greymark.effect.type == BlackIceEffectType::ReduceRunnerStatsByAmount &&
        greymark.effect.statusDice == 1 && breacher.perception == 3 && breacher.speed == 3 &&
        breacher.attack == 7 && breacher.defense == 5 && breacher.maxRez == 24 &&
        breacher.effect.type == BlackIceEffectType::DamageAndUnsafeJackOut &&
        breacher.effect.damageDice == 2;

    bool tracejackalAttackOk = false;
    {
        Netrunner runner("TRACEJACKAL", 4, 40, 3);
        BlackIceInstance ice(tracejackal);
        CombatDice dice(10, 1, 4); NetRules rules(dice);
        const CombatResult hit = rules.blackIceAttack(ice, runner);
        tracejackalAttackOk = hit.executed && hit.success && hit.damage == 4 &&
            dice.lastD6Count() == 2 && !hit.fireApplied && !hit.fireBlocked;
    }

    bool stingwireAttackOk = false;
    {
        Netrunner runner("STINGWIRE", 4, 40, 3);
        BlackIceInstance ice(stingwire);
        CombatDice dice(10, 1, 4); NetRules rules(dice);
        const CombatResult hit = rules.blackIceAttack(ice, runner);
        stingwireAttackOk = hit.executed && hit.success && hit.fireApplied && !hit.fireBlocked &&
            hit.damage == 0 && hit.moveBefore == 0 && hit.moveAfter == 0 &&
            hit.temporaryRunEffect == TemporaryRunEffect::None && !hit.programEffectApplied;
    }

    bool guttermeshAttackOk = false;
    {
        Netrunner runner("GUTTERMESH", 4, 40, 3);
        BlackIceInstance ice(guttermesh);
        CombatDice dice(10, 1, 4); NetRules rules(dice);
        const CombatResult hit = rules.blackIceAttack(ice, runner);
        guttermeshAttackOk = hit.executed && hit.success && hit.damage == 4 &&
            dice.lastD6Count() == 1 && hit.moveBefore == 5 && hit.moveAfter == 4 &&
            runner.move() == 4 && hit.temporaryRunEffect == TemporaryRunEffect::None;
    }

    bool stormrazorAttackOk = false;
    {
        Netrunner runner("STORMRAZOR", 4, 40, 3);
        Cyberdeck deck;
        Program armor(ProgramType::Defender, "Armor", 0, 6, 20);
        armor.setStatus(ProgramStatus::Rezzed); deck.addProgram(armor);
        BlackIceInstance ice(stormrazor);
        CombatDice dice(10, 1, 6); NetRules rules(dice);
        const CombatResult hit = rules.blackIceAttack(ice, runner, &deck);
        stormrazorAttackOk = hit.executed && hit.success && hit.programEffectApplied &&
            hit.damage == 6 && dice.lastD6Count() == 5 && hit.targetInitialRez == 20 &&
            hit.targetRemainingRez == 14 && !hit.targetDestroyed && runner.hp() == 40;
    }

    bool glasscatAttackOk = false;
    {
        Netrunner runner("GLASSCAT", 4, 40, 3);
        Cyberdeck deck;
        Program sword(ProgramType::Attacker, "Sword", 1, 0, 7);
        sword.setStatus(ProgramStatus::Rezzed); deck.addProgram(sword);
        BlackIceInstance ice(glasscat);
        CombatDice dice(10, 1, 6); NetRules rules(dice);
        const CombatResult hit = rules.blackIceAttack(ice, runner, &deck);
        glasscatAttackOk = hit.executed && hit.success && hit.programEffectApplied &&
            hit.damage == 6 && dice.lastD6Count() == 4 && hit.targetInitialRez == 7 &&
            hit.targetRemainingRez == 1 && !hit.targetDestroyed && runner.hp() == 40;
    }

    bool breacherAttackOk = false;
    {
        Netrunner runner("BREACHER", 4, 40, 3);
        BlackIceInstance ice(breacher);
        CombatDice dice(10, 1, 8); NetRules rules(dice);
        const CombatResult hit = rules.blackIceAttack(ice, runner);
        breacherAttackOk = hit.executed && hit.success && hit.damage == 8 &&
            dice.lastD6Count() == 2 && hit.forcedUnsafeJackOut &&
            hit.temporaryRunEffect == TemporaryRunEffect::None && runner.hp() == 32;
    }

    bool factoryOk = false;
    {
        GameState& state = debugTestState(needlecoilArchitecture);
        state.startRun();
        const bool started = state.jackIn();
        const bool duplicateBlocked = !state.triggerCurrentBlackIce();
        const BlackIceInstance* instance = state.activeBlackIceAt(0);
        factoryOk = started && duplicateBlocked && instance != nullptr && instance->definition() == &needlecoil &&
            instance->currentRez() == 16 && instance->active() && instance->pursuing();
        Serial.printf("[Diag][BlackIce][Needlecoil] started=%u duplicateBlocked=%u found=%u type=%u rez=%d active=%u pursuing=%u expectedRez=16\n",
            started ? 1U : 0U, duplicateBlocked ? 1U : 0U, instance != nullptr ? 1U : 0U,
            instance != nullptr && instance->definition() != nullptr ? static_cast<unsigned>(instance->definition()->type) : 255U,
            instance != nullptr ? instance->currentRez() : -1, instance != nullptr && instance->active() ? 1U : 0U,
            instance != nullptr && instance->pursuing() ? 1U : 0U);
    }

    BlackIceInstance first(needlecoil);
    BlackIceInstance second(needlecoil);
    first.takeRezDamage(4);
    const bool isolationOk = first.currentRez() == 12 && second.currentRez() == 16 &&
        first.active() && second.active();

    bool carrionbyteRuntimeOk = false;
    {
        GameState& state = debugTestState(carrionbyteArchitecture);
        state.startRun();
        const bool started = state.jackIn();
        const bool duplicateBlocked = !state.triggerCurrentBlackIce();
        const BlackIceInstance* instance = state.activeBlackIceAt(0);
        BlackIceInstance firstCarrionbyte(carrionbyte);
        BlackIceInstance secondCarrionbyte(carrionbyte);
        firstCarrionbyte.takeRezDamage(5);
        carrionbyteRuntimeOk = started && duplicateBlocked && instance != nullptr && instance->definition() == &carrionbyte &&
            instance->currentRez() == 16 && firstCarrionbyte.currentRez() == 11 &&
            secondCarrionbyte.currentRez() == 16 && firstCarrionbyte.active() && secondCarrionbyte.active();
        Serial.printf("[Diag][BlackIce][Carrionbyte] started=%u duplicateBlocked=%u found=%u type=%u rez=%d isolated=%u/%u\n",
            started ? 1U : 0U, duplicateBlocked ? 1U : 0U, instance != nullptr ? 1U : 0U,
            instance != nullptr && instance->definition() != nullptr ? static_cast<unsigned>(instance->definition()->type) : 255U,
            instance != nullptr ? instance->currentRez() : -1,
            firstCarrionbyte.currentRez() == 11 && firstCarrionbyte.active() ? 1U : 0U, secondCarrionbyte.currentRez() == 16 && secondCarrionbyte.active() ? 1U : 0U);
    }

    bool deadlockRuntimeOk = false;
    {
        GameState& state = debugTestState(deadlockArchitecture);
        state.startRun();
        const bool started = state.jackIn();
        const bool duplicateBlocked = !state.triggerCurrentBlackIce();
        const BlackIceInstance* instance = state.activeBlackIceAt(0);
        BlackIceInstance firstDeadlock(deadlock);
        BlackIceInstance secondDeadlock(deadlock);
        firstDeadlock.takeRezDamage(7);
        deadlockRuntimeOk = started && duplicateBlocked && instance != nullptr && instance->definition() == &deadlock &&
            instance->currentRez() == 26 && firstDeadlock.currentRez() == 19 &&
            secondDeadlock.currentRez() == 26;
        Serial.printf("[Diag][BlackIce][Deadlock] started=%u duplicateBlocked=%u found=%u type=%u rez=%d isolated=%u/%u\n",
            started ? 1U : 0U, duplicateBlocked ? 1U : 0U, instance != nullptr ? 1U : 0U,
            instance != nullptr && instance->definition() != nullptr ? static_cast<unsigned>(instance->definition()->type) : 255U,
            instance != nullptr ? instance->currentRez() : -1, firstDeadlock.currentRez() == 19 ? 1U : 0U,
            secondDeadlock.currentRez() == 26 ? 1U : 0U);
    }

    bool ghostpulseRuntimeOk = false;
    {
        GameState& state = debugTestState(ghostpulseArchitecture);
        state.startRun();
        const bool started = state.jackIn();
        const bool duplicateBlocked = !state.triggerCurrentBlackIce();
        const BlackIceInstance* instance = state.activeBlackIceAt(0);
        BlackIceInstance firstGhostpulse(ghostpulse);
        BlackIceInstance secondGhostpulse(ghostpulse);
        firstGhostpulse.takeRezDamage(4);
        ghostpulseRuntimeOk = started && duplicateBlocked && instance != nullptr && instance->definition() == &ghostpulse &&
            instance->currentRez() == 14 && firstGhostpulse.currentRez() == 10 &&
            secondGhostpulse.currentRez() == 14;
        Serial.printf("[Diag][BlackIce][Ghostpulse] started=%u duplicateBlocked=%u found=%u type=%u rez=%d isolated=%u/%u\n",
            started ? 1U : 0U, duplicateBlocked ? 1U : 0U, instance != nullptr ? 1U : 0U,
            instance != nullptr && instance->definition() != nullptr ? static_cast<unsigned>(instance->definition()->type) : 255U,
            instance != nullptr ? instance->currentRez() : -1, firstGhostpulse.currentRez() == 10 ? 1U : 0U,
            secondGhostpulse.currentRez() == 14 ? 1U : 0U);
    }

    bool packetsawRuntimeOk = false;
    {
        GameState& state = debugTestState(packetsawArchitecture);
        state.startRun();
        const bool started = state.jackIn();
        const bool duplicateBlocked = !state.triggerCurrentBlackIce();
        const BlackIceInstance* instance = state.activeBlackIceAt(0);
        BlackIceInstance firstPacketsaw(packetsaw);
        BlackIceInstance secondPacketsaw(packetsaw);
        firstPacketsaw.takeRezDamage(6);
        packetsawRuntimeOk = started && duplicateBlocked && instance != nullptr && instance->definition() == &packetsaw &&
            instance->currentRez() == 19 && firstPacketsaw.currentRez() == 13 &&
            secondPacketsaw.currentRez() == 19;
        Serial.printf("[Diag][BlackIce][Packetsaw] started=%u duplicateBlocked=%u found=%u type=%u rez=%d isolated=%u/%u\n",
            started ? 1U : 0U, duplicateBlocked ? 1U : 0U, instance != nullptr ? 1U : 0U,
            instance != nullptr && instance->definition() != nullptr ? static_cast<unsigned>(instance->definition()->type) : 255U,
            instance != nullptr ? instance->currentRez() : -1, firstPacketsaw.currentRez() == 13 ? 1U : 0U,
            secondPacketsaw.currentRez() == 19 ? 1U : 0U);
    }

    bool encounterOk = false;
    {
        Netrunner runner("NEEDLECOIL_SPEED", 4, 40, 3);
        BlackIceInstance ice(needlecoil);
        CombatDice encounterDice(4, 1, 0);
        NetRules encounterRules(encounterDice);
        const OpposedCheckResult encounter = encounterRules.encounterSpeedCheck(runner, ice);
        encounterOk = encounter.runnerTotal == 8 && encounter.iceTotal == 8 &&
            encounter.tie && !encounter.runnerWins && !encounter.iceWins;
    }

    bool carrionbyteEncounterOk = false;
    {
        Netrunner runner("CARRIONBYTE_SPEED", 4, 40, 3);
        BlackIceInstance ice(carrionbyte);
        CombatDice encounterDice(2, 1, 0);
        NetRules encounterRules(encounterDice);
        const OpposedCheckResult encounter = encounterRules.encounterSpeedCheck(runner, ice);
        carrionbyteEncounterOk = encounter.runnerTotal == 6 && encounter.iceTotal == 6 &&
            encounter.tie && !encounter.runnerWins && !encounter.iceWins;
    }

    bool deadlockEncounterOk = false;
    {
        Netrunner runner("DEADLOCK_SPEED", 4, 40, 3);
        BlackIceInstance ice(deadlock);
        CombatDice encounterDice(1, 2, 0);
        NetRules encounterRules(encounterDice);
        const OpposedCheckResult encounter = encounterRules.encounterSpeedCheck(runner, ice);
        deadlockEncounterOk = encounter.runnerTotal == 5 && encounter.iceTotal == 5 &&
            encounter.tie && !encounter.runnerWins && !encounter.iceWins;
    }

    bool ghostpulseEncounterOk = false;
    {
        Netrunner runner("GHOSTPULSE_SPEED", 4, 40, 3);
        BlackIceInstance ice(ghostpulse);
        CombatDice encounterDice(2, 1, 0);
        NetRules encounterRules(encounterDice);
        const OpposedCheckResult encounter = encounterRules.encounterSpeedCheck(runner, ice);
        ghostpulseEncounterOk = encounter.runnerTotal == 6 && encounter.iceTotal == 6 &&
            encounter.tie && !encounter.runnerWins && !encounter.iceWins;
    }

    bool missOk = false;
    {
        Netrunner runner("NEEDLECOIL_MISS", 4, 40, 3);
        Cyberdeck deck;
        Program armor(ProgramType::Defender, "Armor", 0, 0, 7);
        armor.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(armor);
        BlackIceInstance ice(needlecoil);
        CombatDice missDice(1, 10, 6);
        NetRules missRules(missDice);
        const CombatResult miss = missRules.blackIceAttack(ice, runner, &deck);
        missOk = miss.executed && !miss.success && runner.hp() == 40 &&
            deck.programAt(0)->status() == ProgramStatus::Rezzed &&
            !miss.programEffectApplied && miss.damage == 0;
    }

    bool targetOk = false;
    bool damageOk = false;
    bool recoveryOk = false;
    {
        Netrunner runner("NEEDLECOIL_HIT", 4, 40, 3);
        Cyberdeck deck;
        Program sword(ProgramType::Attacker, "Sword", 1, 0, 5);
        Program armor(ProgramType::Defender, "Armor", 0, 0, 7);
        sword.setStatus(ProgramStatus::Rezzed);
        armor.setStatus(ProgramStatus::Rezzed);
        armor.setActivatedThisRound(true);
        deck.addProgram(sword);
        deck.addProgram(armor);
        BlackIceInstance ice(needlecoil);
        CombatDice hitDice(10, 1, 4);
        NetRules hitRules(hitDice);
        const CombatResult hit = hitRules.blackIceAttack(ice, runner, &deck);
        targetOk = hit.success && hit.programEffectApplied && hit.affectedProgramSlot == 1 &&
            hit.resultingProgramStatus == ProgramStatus::Derezzed &&
            deck.programAt(0)->status() == ProgramStatus::Rezzed &&
            deck.programAt(1)->status() == ProgramStatus::Derezzed &&
            deck.programAt(1)->status() != ProgramStatus::Destroyed;
        damageOk = hit.damage == 4 && hitDice.lastD6Count() == 1 && runner.hp() == 36;

        const bool activationFlagPreserved = deck.programAt(1)->activatedThisRound();
        const ProgramActionResult deactivated = hitRules.deactivateProgram(runner, *deck.programAt(1));
        const ProgramActionResult sameRoundActivation = hitRules.activateProgram(
            runner, *deck.programAt(1));
        runner.resetTurn();
        deck.resetRoundFlags();
        const ProgramActionResult activated = hitRules.activateProgram(runner, *deck.programAt(1));
        recoveryOk = activationFlagPreserved && deactivated.executed &&
            !sameRoundActivation.executed && activated.executed &&
            deck.programAt(1)->status() == ProgramStatus::Rezzed &&
            deck.programAt(1)->rez() == deck.programAt(1)->maxRez();
    }

    bool randomSelectionOk = false;
    {
        Netrunner runner("NEEDLECOIL_SELECT", 4, 40, 3);
        Cyberdeck deck;
        Program firstDefender(ProgramType::Defender, "DEFENDER A", 0, 0, 5);
        Program secondDefender(ProgramType::Defender, "DEFENDER B", 0, 0, 5);
        firstDefender.setStatus(ProgramStatus::Rezzed);
        secondDefender.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(firstDefender);
        deck.addProgram(secondDefender);
        BlackIceInstance ice(needlecoil);
        CombatDice selectionDice(10, 1, 1, 1);
        NetRules selectionRules(selectionDice);
        const CombatResult hit = selectionRules.blackIceAttack(ice, runner, &deck);
        randomSelectionOk = hit.success && hit.affectedProgramSlot == 1 &&
            selectionDice.lastIndexCount() == 2 &&
            deck.programAt(0)->status() == ProgramStatus::Rezzed &&
            deck.programAt(1)->status() == ProgramStatus::Derezzed;
    }

    bool noDefenderOk = false;
    {
        Netrunner runner("NO_DEFENDER", 4, 40, 3);
        Cyberdeck deck;
        Program sword(ProgramType::Attacker, "Sword", 1, 0, 5);
        sword.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(sword);
        BlackIceInstance ice(needlecoil);
        CombatDice hitDice(10, 1, 3);
        NetRules hitRules(hitDice);
        const CombatResult hit = hitRules.blackIceAttack(ice, runner, &deck);
        noDefenderOk = hit.success && !hit.programEffectApplied && hit.affectedProgramSlot < 0 &&
            deck.programAt(0)->status() == ProgramStatus::Rezzed &&
            hit.damage == 3 && runner.hp() == 37;
    }

    bool generalCombatOk = false;
    {
        Netrunner swordRunner("NEEDLECOIL_SWORD", 4, 40, 3);
        Cyberdeck deck;
        Program sword = makeProgram(ProgramId::Sword);
        sword.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(sword);
        BlackIceInstance swordTarget(needlecoil);
        CombatDice swordDice(10, 1, 4);
        NetRules swordRules(swordDice);
        const CombatResult swordHit = swordRules.swordAttack(swordRunner, deck, swordTarget);

        Netrunner zapRunner("NEEDLECOIL_ZAP", 4, 40, 3);
        BlackIceInstance zapTarget(needlecoil);
        CombatDice zapDice(10, 1, 4);
        NetRules zapRules(zapDice);
        const CombatResult zapHit = zapRules.zap(zapRunner, zapTarget);

        Netrunner slideRunner("NEEDLECOIL_SLIDE", 4, 40, 3);
        BlackIceInstance slideTarget(needlecoil);
        CombatDice slideDice(10, 1, 0);
        NetRules slideRules(slideDice);
        const CombatResult slide = slideRules.slide(slideRunner, slideTarget);
        const CombatResult afterSlide = slideRules.blackIceAttack(slideTarget, slideRunner);

        const bool swordExecutedOk = swordHit.executed;
        const bool swordHitOk = swordHit.success;
        const bool swordResultOk = swordHit.defenderTotal == needlecoil.defense + swordHit.defenderRoll &&
            swordDice.lastD6Count() == 3 && swordHit.targetRemainingRez == needlecoil.maxRez - swordHit.damage;
        const bool swordStateOk = swordTarget.active() && swordTarget.pursuing();
        const bool zapExecutedOk = zapHit.executed;
        const bool zapHitOk = zapHit.success;
        const bool zapResultOk = zapHit.defenderTotal == needlecoil.defense + zapHit.defenderRoll &&
            zapDice.lastD6Count() == 1 && zapHit.targetRemainingRez == needlecoil.maxRez - zapHit.damage;
        const bool zapStateOk = zapTarget.active() && zapTarget.pursuing();
        const bool slideExecutedOk = slide.executed;
        const bool slideHitOk = slide.success;
        const bool slideResultOk = slide.defenderTotal == needlecoil.perception + slide.defenderRoll;
        const bool slideStateOk = !slideTarget.pursuing() && !afterSlide.executed;
        generalCombatOk = swordExecutedOk && swordHitOk && swordResultOk && swordStateOk &&
            zapExecutedOk && zapHitOk && zapResultOk && zapStateOk && slideExecutedOk &&
            slideHitOk && slideResultOk && slideStateOk;
        Serial.printf("[Diag][BlackIce][Needlecoil][Combat] swordExec=%u hit=%u result=%u state=%u zapExec=%u hit=%u result=%u state=%u slideExec=%u hit=%u result=%u state=%u\n",
            swordExecutedOk, swordHitOk, swordResultOk, swordStateOk, zapExecutedOk, zapHitOk,
            zapResultOk, zapStateOk, slideExecutedOk, slideHitOk, slideResultOk, slideStateOk);
    }

    bool carrionbyteMissOk = false;
    {
        Netrunner runner("CARRIONBYTE_MISS", 4, 40, 3);
        Cyberdeck deck;
        Program sword(ProgramType::Attacker, "Sword", 1, 0, 5);
        Program armor(ProgramType::Defender, "Armor", 0, 0, 7);
        sword.setStatus(ProgramStatus::Inactive);
        armor.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(sword);
        deck.addProgram(armor);
        BlackIceInstance ice(carrionbyte);
        CombatDice missDice(1, 10, 0);
        NetRules missRules(missDice);
        const CombatResult miss = missRules.blackIceAttack(ice, runner, &deck);
        carrionbyteMissOk = miss.executed && !miss.success && !miss.programEffectApplied &&
            miss.damage == 0 && runner.hp() == 40 &&
            deck.programAt(0)->status() == ProgramStatus::Inactive &&
            deck.programAt(1)->status() == ProgramStatus::Rezzed;
    }

    bool carrionbyteTargetOk = false;
    bool carrionbyteDestroyedStateOk = false;
    {
        Netrunner runner("CARRIONBYTE_TARGET", 4, 40, 3);
        Cyberdeck deck;
        Program sword(ProgramType::Attacker, "Sword", 1, 0, 5);
        Program armor(ProgramType::Defender, "Armor", 0, 0, 7);
        sword.setStatus(ProgramStatus::Inactive);
        armor.setStatus(ProgramStatus::Derezzed);
        deck.addProgram(sword);
        deck.addProgram(armor);

        BlackIceInstance firstIce(carrionbyte);
        CombatDice armorDice(10, 1, 0, 1);
        NetRules armorRules(armorDice);
        const CombatResult armorHit = armorRules.blackIceAttack(firstIce, runner, &deck);
        const bool armorDestroyed = armorHit.success && armorHit.programEffectApplied &&
            armorHit.affectedProgramSlot == 1 &&
            armorHit.resultingProgramStatus == ProgramStatus::Destroyed &&
            deck.programAt(1)->status() == ProgramStatus::Destroyed &&
            deck.programAt(0)->status() == ProgramStatus::Inactive &&
            armorDice.lastIndexCount() == 2;

        BlackIceInstance secondIce(carrionbyte);
        CombatDice swordDice(10, 1, 0);
        NetRules swordRules(swordDice);
        const CombatResult swordHit = swordRules.blackIceAttack(secondIce, runner, &deck);
        const bool destroyedExcluded = swordHit.success && swordHit.programEffectApplied &&
            swordHit.affectedProgramSlot == 0 &&
            deck.programAt(0)->status() == ProgramStatus::Destroyed &&
            deck.programAt(1)->status() == ProgramStatus::Destroyed &&
            swordDice.lastIndexCount() == 0;

        const ProgramActionResult activation = swordRules.activateProgram(
            runner, *deck.programAt(1));
        const ProgramActionResult deactivation = swordRules.deactivateProgram(
            runner, *deck.programAt(1));
        carrionbyteTargetOk = armorDestroyed && destroyedExcluded && runner.hp() == 40 &&
            armorHit.damage == 0 && swordHit.damage == 0;
        carrionbyteDestroyedStateOk = !activation.executed && !deactivation.executed &&
            deck.programAt(1)->status() == ProgramStatus::Destroyed &&
            !deck.programAt(1)->usable();
    }

    bool carrionbyteRezzedOk = false;
    {
        Netrunner runner("CARRIONBYTE_REZZED", 4, 40, 3);
        Cyberdeck deck;
        Program sword(ProgramType::Attacker, "Sword", 1, 0, 5);
        Program armor(ProgramType::Defender, "Armor", 0, 0, 7);
        sword.setStatus(ProgramStatus::Rezzed);
        armor.setStatus(ProgramStatus::Inactive);
        deck.addProgram(sword);
        deck.addProgram(armor);
        BlackIceInstance ice(carrionbyte);
        CombatDice hitDice(10, 1, 0, 0);
        NetRules hitRules(hitDice);
        const CombatResult hit = hitRules.blackIceAttack(ice, runner, &deck);
        carrionbyteRezzedOk = hit.success && hit.programEffectApplied &&
            hit.affectedProgramSlot == 0 && hitDice.lastIndexCount() == 2 &&
            deck.programAt(0)->status() == ProgramStatus::Destroyed &&
            deck.programAt(1)->status() == ProgramStatus::Inactive && runner.hp() == 40;
    }

    bool carrionbyteNoTargetOk = false;
    {
        Netrunner runner("CARRIONBYTE_EMPTY", 4, 40, 3);
        Cyberdeck deck;
        Program destroyed(ProgramType::Defender, "DESTROYED", 0, 0, 5);
        destroyed.setStatus(ProgramStatus::Destroyed);
        deck.addProgram(destroyed);
        BlackIceInstance ice(carrionbyte);
        CombatDice hitDice(10, 1, 0);
        NetRules hitRules(hitDice);
        const CombatResult hit = hitRules.blackIceAttack(ice, runner, &deck);
        carrionbyteNoTargetOk = hit.executed && hit.success && !hit.programEffectApplied &&
            hit.affectedProgramSlot < 0 && hit.damage == 0 && runner.hp() == 40 &&
            deck.programAt(0)->status() == ProgramStatus::Destroyed;
    }

    bool carrionbyteGeneralOk = false;
    {
        Netrunner swordRunner("CARRIONBYTE_SWORD", 4, 40, 3);
        Cyberdeck deck;
        Program sword = makeProgram(ProgramId::Sword);
        sword.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(sword);
        BlackIceInstance swordTarget(carrionbyte);
        CombatDice swordDice(10, 1, 4);
        NetRules swordRules(swordDice);
        const CombatResult swordHit = swordRules.swordAttack(swordRunner, deck, swordTarget);

        Netrunner zapRunner("CARRIONBYTE_ZAP", 4, 40, 3);
        BlackIceInstance zapTarget(carrionbyte);
        CombatDice zapDice(10, 1, 4);
        NetRules zapRules(zapDice);
        const CombatResult zapHit = zapRules.zap(zapRunner, zapTarget);

        Netrunner slideRunner("CARRIONBYTE_SLIDE", 4, 40, 3);
        BlackIceInstance slideTarget(carrionbyte);
        CombatDice slideDice(10, 1, 0);
        NetRules slideRules(slideDice);
        const CombatResult slide = slideRules.slide(slideRunner, slideTarget);
        const CombatResult afterSlide = slideRules.blackIceAttack(slideTarget, slideRunner);

        const bool swordOk = swordHit.executed && swordHit.success &&
            swordHit.defenderTotal == carrionbyte.defense + swordHit.defenderRoll &&
            swordHit.targetRemainingRez == carrionbyte.maxRez - swordHit.damage &&
            swordTarget.active() && swordTarget.pursuing();
        const bool zapOk = zapHit.executed && zapHit.success &&
            zapHit.defenderTotal == carrionbyte.defense + zapHit.defenderRoll &&
            zapHit.targetRemainingRez == carrionbyte.maxRez - zapHit.damage &&
            zapTarget.active() && zapTarget.pursuing();
        const bool slideOk = slide.executed && slide.success &&
            slide.defenderTotal == carrionbyte.perception + slide.defenderRoll &&
            !slideTarget.pursuing() && !afterSlide.executed;
        carrionbyteGeneralOk = swordOk && zapOk && slideOk;
        Serial.printf("[Diag][BlackIce][Carrionbyte][Combat] swordOk=%u zapOk=%u slideOk=%u sword=%u/%u zap=%u/%u slide=%u/%u\n",
            swordOk, zapOk, slideOk, swordHit.executed, swordHit.success, zapHit.executed,
            zapHit.success, slide.executed, slide.success);
    }

    bool deadlockAttackOk = false;
    {
        Netrunner runner("DEADLOCK_ATTACK", 4, 40, 3);
        Cyberdeck deck;
        Program armor(ProgramType::Defender, "Armor", 0, 0, 7);
        armor.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(armor);

        BlackIceInstance missIce(deadlock);
        CombatDice missDice(1, 10, 8);
        NetRules missRules(missDice);
        const CombatResult miss = missRules.blackIceAttack(missIce, runner, &deck);

        BlackIceInstance hitIce(deadlock);
        CombatDice hitDice(10, 1, 8);
        NetRules hitRules(hitDice);
        const CombatResult hit = hitRules.blackIceAttack(hitIce, runner, &deck);
        deadlockAttackOk = miss.executed && !miss.success && miss.damage == 0 &&
            miss.temporaryRunEffect == TemporaryRunEffect::None &&
            hit.executed && hit.success && hit.damage == 8 && hitDice.lastD6Count() == 2 &&
            hit.temporaryRunEffect == TemporaryRunEffect::NavigationAndSafeJackOutLock &&
            !hit.temporaryRunEffectApplied && runner.hp() == 32 &&
            deck.programAt(0)->status() == ProgramStatus::Rezzed && !hit.programEffectApplied;
    }

    bool deadlockGeneralOk = false;
    {
        Netrunner swordRunner("DEADLOCK_SWORD", 4, 40, 3);
        Cyberdeck deck;
        Program sword = makeProgram(ProgramId::Sword);
        sword.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(sword);
        BlackIceInstance swordTarget(deadlock);
        CombatDice swordDice(10, 1, 4);
        NetRules swordRules(swordDice);
        const CombatResult swordHit = swordRules.swordAttack(swordRunner, deck, swordTarget);

        Netrunner zapRunner("DEADLOCK_ZAP", 4, 40, 3);
        BlackIceInstance zapTarget(deadlock);
        CombatDice zapDice(10, 1, 4);
        NetRules zapRules(zapDice);
        const CombatResult zapHit = zapRules.zap(zapRunner, zapTarget);

        Netrunner slideRunner("DEADLOCK_SLIDE", 4, 40, 3);
        BlackIceInstance slideTarget(deadlock);
        CombatDice slideDice(10, 1, 0);
        NetRules slideRules(slideDice);
        const CombatResult slide = slideRules.slide(slideRunner, slideTarget);
        const CombatResult afterSlide = slideRules.blackIceAttack(slideTarget, slideRunner);

        const bool swordOk = swordHit.executed && swordHit.success &&
            swordHit.defenderTotal == deadlock.defense + swordHit.defenderRoll &&
            swordHit.targetRemainingRez == deadlock.maxRez - swordHit.damage &&
            swordTarget.active() && swordTarget.pursuing();
        const bool zapOk = zapHit.executed && zapHit.success &&
            zapHit.defenderTotal == deadlock.defense + zapHit.defenderRoll &&
            zapHit.targetRemainingRez == deadlock.maxRez - zapHit.damage &&
            zapTarget.active() && zapTarget.pursuing();
        const bool slideOk = slide.executed && slide.success &&
            slide.defenderTotal == deadlock.perception + slide.defenderRoll &&
            !slideTarget.pursuing() && !afterSlide.executed;
        deadlockGeneralOk = swordOk && zapOk && slideOk;
        Serial.printf("[Diag][BlackIce][Deadlock][Combat] swordOk=%u zapOk=%u slideOk=%u sword=%u/%u zap=%u/%u slide=%u/%u\n",
            swordOk, zapOk, slideOk, swordHit.executed, swordHit.success, zapHit.executed,
            zapHit.success, slide.executed, slide.success);
    }

    bool ghostpulseAttackOk = false;
    {
        Netrunner runner("GHOSTPULSE_ATTACK", 4, 40, 3);
        Cyberdeck deck;
        Program armor(ProgramType::Defender, "Armor", 0, 0, 7);
        armor.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(armor);

        BlackIceInstance missIce(ghostpulse);
        CombatDice missDice(1, 10, 4);
        NetRules missRules(missDice);
        const CombatResult miss = missRules.blackIceAttack(missIce, runner, &deck);

        BlackIceInstance hitIce(ghostpulse);
        CombatDice hitDice(10, 1, 4);
        NetRules hitRules(hitDice);
        const CombatResult hit = hitRules.blackIceAttack(hitIce, runner, &deck);
        ghostpulseAttackOk = miss.executed && !miss.success && miss.damage == 0 &&
            miss.temporaryRunEffect == TemporaryRunEffect::None &&
            hit.executed && hit.success && hit.damage == 4 && hitDice.lastD6Count() == 1 &&
            hit.temporaryRunEffect == TemporaryRunEffect::NextTurnNetActionPenalty &&
            !hit.temporaryRunEffectApplied && runner.hp() == 36 &&
            deck.programAt(0)->status() == ProgramStatus::Rezzed && !hit.programEffectApplied;
    }

    bool ghostpulseGeneralOk = false;
    {
        Netrunner swordRunner("GHOSTPULSE_SWORD", 4, 40, 3);
        Cyberdeck deck;
        Program sword = makeProgram(ProgramId::Sword);
        sword.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(sword);
        BlackIceInstance swordTarget(ghostpulse);
        CombatDice swordDice(10, 1, 4);
        NetRules swordRules(swordDice);
        const CombatResult swordHit = swordRules.swordAttack(swordRunner, deck, swordTarget);

        Netrunner zapRunner("GHOSTPULSE_ZAP", 4, 40, 3);
        BlackIceInstance zapTarget(ghostpulse);
        CombatDice zapDice(10, 1, 4);
        NetRules zapRules(zapDice);
        const CombatResult zapHit = zapRules.zap(zapRunner, zapTarget);

        Netrunner slideRunner("GHOSTPULSE_SLIDE", 4, 40, 3);
        BlackIceInstance slideTarget(ghostpulse);
        CombatDice slideDice(10, 1, 0);
        NetRules slideRules(slideDice);
        const CombatResult slide = slideRules.slide(slideRunner, slideTarget);
        const CombatResult afterSlide = slideRules.blackIceAttack(slideTarget, slideRunner);

        const bool swordOk = swordHit.executed && swordHit.success &&
            swordHit.defenderTotal == ghostpulse.defense + swordHit.defenderRoll &&
            swordHit.targetRemainingRez == ghostpulse.maxRez - swordHit.damage &&
            swordTarget.active() && swordTarget.pursuing();
        const bool zapOk = zapHit.executed && zapHit.success &&
            zapHit.defenderTotal == ghostpulse.defense + zapHit.defenderRoll &&
            zapHit.targetRemainingRez == ghostpulse.maxRez - zapHit.damage &&
            zapTarget.active() && zapTarget.pursuing();
        const bool slideOk = slide.executed && slide.success &&
            slide.defenderTotal == ghostpulse.perception + slide.defenderRoll &&
            !slideTarget.pursuing() && !afterSlide.executed;
        ghostpulseGeneralOk = swordOk && zapOk && slideOk;
        Serial.printf("[Diag][BlackIce][Ghostpulse][Combat] swordOk=%u zapOk=%u slideOk=%u sword=%u/%u zap=%u/%u slide=%u/%u\n",
            swordOk, zapOk, slideOk, swordHit.executed, swordHit.success, zapHit.executed,
            zapHit.success, slide.executed, slide.success);
    }

    bool packetsawGeneralOk = false;
    {
        Netrunner swordRunner("PACKETSAW_SWORD", 4, 40, 3);
        Cyberdeck deck;
        Program sword = makeProgram(ProgramId::Sword);
        sword.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(sword);
        BlackIceInstance swordTarget(packetsaw);
        CombatDice swordDice(10, 1, 4);
        NetRules swordRules(swordDice);
        const CombatResult swordHit = swordRules.swordAttack(swordRunner, deck, swordTarget);

        Netrunner zapRunner("PACKETSAW_ZAP", 4, 40, 3);
        BlackIceInstance zapTarget(packetsaw);
        CombatDice zapDice(10, 1, 4);
        NetRules zapRules(zapDice);
        const CombatResult zapHit = zapRules.zap(zapRunner, zapTarget);

        Netrunner slideRunner("PACKETSAW_SLIDE", 4, 40, 3);
        BlackIceInstance slideTarget(packetsaw);
        CombatDice slideDice(10, 1, 0);
        NetRules slideRules(slideDice);
        const CombatResult slide = slideRules.slide(slideRunner, slideTarget);
        const CombatResult afterSlide = slideRules.blackIceAttack(slideTarget, slideRunner);

        const bool swordOk = swordHit.executed && swordHit.success &&
            swordHit.defenderTotal == packetsaw.defense + swordHit.defenderRoll &&
            swordHit.targetRemainingRez == packetsaw.maxRez - swordHit.damage &&
            swordTarget.active() && swordTarget.pursuing();
        const bool zapOk = zapHit.executed && zapHit.success &&
            zapHit.defenderTotal == packetsaw.defense + zapHit.defenderRoll &&
            zapHit.targetRemainingRez == packetsaw.maxRez - zapHit.damage &&
            zapTarget.active() && zapTarget.pursuing();
        const bool slideOk = slide.executed && slide.success &&
            slide.defenderTotal == packetsaw.perception + slide.defenderRoll &&
            !slideTarget.pursuing() && !afterSlide.executed;
        packetsawGeneralOk = swordOk && zapOk && slideOk;
        Serial.printf("[Diag][BlackIce][Packetsaw][Combat] swordOk=%u zapOk=%u slideOk=%u sword=%u/%u zap=%u/%u slide=%u/%u\n",
            swordOk, zapOk, slideOk, swordHit.executed, swordHit.success, zapHit.executed,
            zapHit.success, slide.executed, slide.success);
    }

    bool jsonOk = false;
    LoadedScenario* parsed = new (std::nothrow) LoadedScenario();
    if (parsed != nullptr)
    {
        static const char* ravenJson =
            "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"raven_json\","
            "\"name\":\"RAVEN JSON\",\"floors\":[{\"id\":1,\"type\":\"black_ice\","
            "\"ice\":[\"ice_02\"]}]}";
        static const char* aspJson =
            "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"asp_json\","
            "\"name\":\"ASP JSON\",\"floors\":[{\"id\":1,\"type\":\"black_ice\","
            "\"ice\":[\"ice_03\"]}]}";
        static const char* hellhoundJson =
            "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"hound_json\","
            "\"name\":\"HOUND JSON\",\"floors\":[{\"id\":1,\"type\":\"black_ice\","
            "\"ice\":[\"ice_01\"]}]}";
        static const char* krakenJson =
            "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"kraken_json\","
            "\"name\":\"KRAKEN JSON\",\"floors\":[{\"id\":1,\"type\":\"black_ice\","
            "\"ice\":[\"ice_04\"]}]}";
        static const char* wispJson =
            "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"wisp_json\","
            "\"name\":\"WISP JSON\",\"floors\":[{\"id\":1,\"type\":\"black_ice\","
            "\"ice\":[\"ice_05\"]}]}";
        static const char* killerJson =
            "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"killer_json\","
            "\"name\":\"KILLER JSON\",\"floors\":[{\"id\":1,\"type\":\"black_ice\","
            "\"ice\":[\"ice_06\"]}]}";
        static const char* giantJson =
            "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"giant_json\","
            "\"name\":\"GIANT JSON\",\"floors\":[{\"id\":1,\"type\":\"black_ice\","
            "\"ice\":[\"ice_12\"]}]}";
        static const char* unknownJson =
            "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"unknown_json\","
            "\"name\":\"UNKNOWN JSON\",\"floors\":[{\"id\":1,\"type\":\"black_ice\","
            "\"ice\":[\"not_here\"]}]}";
        static const char* multiJson =
            "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"multi_json\","
            "\"name\":\"MULTI JSON\",\"floors\":[{\"id\":1,\"type\":\"black_ice\","
            "\"ice\":[\"ice_02\",\"ice_02\",\"ice_05\"]}]}";
        static const char* tooManyJson =
            "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"too_many\","
            "\"name\":\"TOO MANY\",\"floors\":[{\"id\":1,\"type\":\"black_ice\","
            "\"ice\":[\"ice_02\",\"ice_05\",\"ice_06\",\"ice_03\"]}]}";
        static const char* emptyIceJson =
            "{\"format\":\"netrun-architecture\",\"schemaVersion\":1,\"id\":\"empty_ice\","
            "\"name\":\"EMPTY ICE\",\"floors\":[{\"id\":1,\"type\":\"black_ice\",\"ice\":[]}]}";
        ScenarioLoader loader;
        const ScenarioImportResult ravenResult = loader.loadJson(ravenJson, *parsed);
        const bool ravenParsed = ravenResult.success && parsed->definition().floorCount == 1 &&
            parsed->definition().floors[0].blackIceType == BlackIceType::Ice02;
        bool ravenRuntime = false;
        if (ravenParsed)
        {
            parsedRuntime = ArchitectureFactory::create(parsed->definition());
            ravenRuntime = parsedRuntime.floorCount() == 1 &&
                parsedRuntime.floorAt(0)->blackIceType == BlackIceType::Ice02;
        }
        const ScenarioImportResult aspResult = loader.loadJson(aspJson, *parsed);
        const bool aspParsed = aspResult.success && parsed->definition().floorCount == 1 &&
            parsed->definition().floors[0].blackIceType == BlackIceType::Ice03;
        bool aspRuntime = false;
        if (aspParsed)
        {
            parsedRuntime = ArchitectureFactory::create(parsed->definition());
            aspRuntime = parsedRuntime.floorCount() == 1 &&
                parsedRuntime.floorAt(0)->blackIceType == BlackIceType::Ice03;
        }
        const bool hellhoundParsed = loader.loadJson(hellhoundJson, *parsed).success &&
            parsed->definition().floors[0].blackIceType == BlackIceType::Ice01;
        const bool krakenParsed = loader.loadJson(krakenJson, *parsed).success &&
            parsed->definition().floors[0].blackIceType == BlackIceType::Ice04;
        bool krakenRuntime = false;
        if (krakenParsed)
        {
            parsedRuntime = ArchitectureFactory::create(parsed->definition());
            krakenRuntime = parsedRuntime.floorCount() == 1 &&
                parsedRuntime.floorAt(0)->blackIceType == BlackIceType::Ice04;
        }
        const bool wispParsed = loader.loadJson(wispJson, *parsed).success &&
            parsed->definition().floors[0].blackIceType == BlackIceType::Ice05;
        bool wispRuntime = false;
        if (wispParsed)
        {
            parsedRuntime = ArchitectureFactory::create(parsed->definition());
            wispRuntime = parsedRuntime.floorCount() == 1 &&
                parsedRuntime.floorAt(0)->blackIceType == BlackIceType::Ice05;
        }
        const bool killerParsed = loader.loadJson(killerJson, *parsed).success &&
            parsed->definition().floors[0].blackIceType == BlackIceType::Ice06;
        bool killerRuntime = false;
        if (killerParsed)
        {
            parsedRuntime = ArchitectureFactory::create(parsed->definition());
            killerRuntime = parsedRuntime.floorCount() == 1 &&
                parsedRuntime.floorAt(0)->blackIceType == BlackIceType::Ice06;
        }
        const bool giantParsed = loader.loadJson(giantJson, *parsed).success &&
            parsed->definition().floors[0].blackIceType == BlackIceType::Ice12;
        bool giantRuntime = false;
        if (giantParsed)
        {
            parsedRuntime = ArchitectureFactory::create(parsed->definition());
            giantRuntime = parsedRuntime.floorAt(0)->blackIceType == BlackIceType::Ice12;
        }
        const bool unknownRejected = loader.loadJson(unknownJson, *parsed).error ==
            ScenarioImportError::UnknownBlackIceContent;
        const ScenarioImportResult multiResult = loader.loadJson(multiJson, *parsed);
        const bool multiParsed = multiResult.success && parsed->definition().floors[0].blackIceCount == 3 &&
            parsed->definition().floors[0].blackIceTypes[0] == BlackIceType::Ice02 &&
            parsed->definition().floors[0].blackIceTypes[1] == BlackIceType::Ice02 &&
            parsed->definition().floors[0].blackIceTypes[2] == BlackIceType::Ice05;
        bool multiRuntime = false;
        if (multiParsed)
        {
            parsedRuntime = ArchitectureFactory::create(parsed->definition());
            multiRuntime = parsedRuntime.floorAt(0)->blackIceCount == 3 &&
                parsedRuntime.floorAt(0)->blackIceTypes[1] == BlackIceType::Ice02;
        }
        const bool tooManyRejected = loader.loadJson(tooManyJson, *parsed).error ==
            ScenarioImportError::TooManyBlackIce;
        const bool emptyRejected = loader.loadJson(emptyIceJson, *parsed).error ==
            ScenarioImportError::InvalidValue;
        jsonOk = ravenParsed && ravenRuntime && aspParsed && aspRuntime &&
            hellhoundParsed && krakenParsed && krakenRuntime && wispParsed && wispRuntime &&
            killerParsed && killerRuntime && giantParsed && giantRuntime && unknownRejected && multiParsed && multiRuntime &&
            tooManyRejected && emptyRejected;
        delete parsed;
    }

    const bool needlecoilOk = factoryOk && isolationOk && encounterOk && missOk &&
        noDefenderOk && generalCombatOk;
    const bool carrionbyteOk = carrionbyteRuntimeOk && carrionbyteEncounterOk && carrionbyteMissOk && carrionbyteTargetOk &&
        carrionbyteDestroyedStateOk && carrionbyteRezzedOk && carrionbyteNoTargetOk && carrionbyteGeneralOk;
    const bool deadlockOk = deadlockRuntimeOk && deadlockEncounterOk && deadlockAttackOk &&
        deadlockGeneralOk;
    const bool ghostpulseOk = ghostpulseRuntimeOk && ghostpulseEncounterOk && ghostpulseAttackOk && ghostpulseGeneralOk;
    const bool packetsawOk = packetsawRuntimeOk && packetsawGeneralOk;
    targetOk = targetOk && randomSelectionOk;
    const bool needlecoilFactoryOk = factoryOk;
    const bool needlecoilIsolationOk = isolationOk;
    const bool needlecoilEncounterOk = encounterOk;
    const bool needlecoilMissOk = missOk;
    const bool needlecoilNoDefenderOk = noDefenderOk;
    const bool needlecoilCombatOk = generalCombatOk;
    const bool carrionbyteRuntimeFlag = carrionbyteRuntimeOk;
    const bool carrionbyteEncounterFlag = carrionbyteEncounterOk;
    const bool carrionbyteMissFlag = carrionbyteMissOk;
    const bool carrionbyteTargetFlag = carrionbyteTargetOk;
    const bool carrionbyteDestroyedFlag = carrionbyteDestroyedStateOk;
    const bool carrionbyteRezzedFlag = carrionbyteRezzedOk;
    const bool carrionbyteNoTargetFlag = carrionbyteNoTargetOk;
    const bool carrionbyteCombatFlag = carrionbyteGeneralOk;
    const bool deadlockRuntimeFlag = deadlockRuntimeOk;
    const bool deadlockEncounterFlag = deadlockEncounterOk;
    const bool deadlockAttackFlag = deadlockAttackOk;
    const bool deadlockCombatFlag = deadlockGeneralOk;
    const bool ghostpulseRuntimeFlag = ghostpulseRuntimeOk;
    const bool ghostpulseEncounterFlag = ghostpulseEncounterOk;
    const bool ghostpulseAttackFlag = ghostpulseAttackOk;
    const bool ghostpulseCombatFlag = ghostpulseGeneralOk;
    const bool packetsawRuntimeFlag = packetsawRuntimeOk;
    const bool packetsawCombatFlag = packetsawGeneralOk;
    Serial.printf("[Diag][BlackIce][Needlecoil][Flags] factoryOk=%u isolationOk=%u encounterOk=%u missOk=%u noDefenderOk=%u combatOk=%u\n",
        needlecoilFactoryOk, needlecoilIsolationOk, needlecoilEncounterOk, needlecoilMissOk, needlecoilNoDefenderOk, needlecoilCombatOk);
    Serial.printf("[Diag][BlackIce][Needlecoil][Stats] per=%d/5(%u) spd=%d/7(%u) atk=%d/3(%u) def=%d/3(%u) rez=%d/16(%u) targetOk=%u effect=%u/%u\n",
        needlecoil.perception, needlecoil.perception == 5, needlecoil.speed, needlecoil.speed == 7, needlecoil.attack, needlecoil.attack == 3,
        needlecoil.defense, needlecoil.defense == 3, needlecoil.maxRez, needlecoil.maxRez == 16, targetOk, static_cast<unsigned>(needlecoil.effect.type), static_cast<unsigned>(BlackIceEffectType::DerezzRandomDefenderAndDamage));
    Serial.printf("[Diag][BlackIce][Carrionbyte][Flags] runtimeOk=%u encounterOk=%u missOk=%u targetOk=%u destroyedOk=%u rezzedOk=%u noTargetOk=%u combatOk=%u\n",
        carrionbyteRuntimeFlag, carrionbyteEncounterFlag, carrionbyteMissFlag, carrionbyteTargetFlag, carrionbyteDestroyedFlag, carrionbyteRezzedFlag, carrionbyteNoTargetFlag, carrionbyteCombatFlag);
    Serial.printf("[Diag][BlackIce][Carrionbyte][Stats] per=%d/7(%u) spd=%d/5(%u) atk=%d/4(%u) def=%d/3(%u) rez=%d/16(%u) effect=%u/%u\n",
        carrionbyte.perception, carrionbyte.perception == 7, carrionbyte.speed, carrionbyte.speed == 5, carrionbyte.attack, carrionbyte.attack == 4,
        carrionbyte.defense, carrionbyte.defense == 3, carrionbyte.maxRez, carrionbyte.maxRez == 16, static_cast<unsigned>(carrionbyte.effect.type), static_cast<unsigned>(BlackIceEffectType::DestroyRandomInstalledProgram));
    Serial.printf("[Diag][BlackIce][Deadlock][Flags] runtimeOk=%u encounterOk=%u attackOk=%u combatOk=%u\n",
        deadlockRuntimeFlag, deadlockEncounterFlag, deadlockAttackFlag, deadlockCombatFlag);
    Serial.printf("[Diag][BlackIce][Tracejackal] attack=%u damage=%d dice=%u fireApplied=%u\n",
        tracejackalAttackOk ? 1U : 0U, tracejackalAttackOk ? 4 : 0,
        static_cast<unsigned>(tracejackal.effect.damageDice), 0U);
    Serial.printf("[Diag][BlackIce][Deadlock][Stats] per=%d/5(%u) spd=%d/3(%u) atk=%d/7(%u) def=%d/5(%u) rez=%d/26(%u) effect=%u/%u dice=%d/2\n",
        deadlock.perception, deadlock.perception == 5, deadlock.speed, deadlock.speed == 3, deadlock.attack, deadlock.attack == 7,
        deadlock.defense, deadlock.defense == 5, deadlock.maxRez, deadlock.maxRez == 26, static_cast<unsigned>(deadlock.effect.type), static_cast<unsigned>(BlackIceEffectType::DamageAndNavigationLock), deadlock.effect.damageDice);
    Serial.printf("[Diag][BlackIce][Ghostpulse][Flags] runtimeOk=%u encounterOk=%u attackOk=%u combatOk=%u\n",
        ghostpulseRuntimeFlag, ghostpulseEncounterFlag, ghostpulseAttackFlag, ghostpulseCombatFlag);
    Serial.printf("[Diag][BlackIce][Ghostpulse][Stats] per=%d/5(%u) spd=%d/5(%u) atk=%d/4(%u) def=%d/3(%u) rez=%d/14(%u) effect=%u/%u dice=%d/1 penalty=%d/1 min=%d/2\n",
        ghostpulse.perception, ghostpulse.perception == 5, ghostpulse.speed, ghostpulse.speed == 5, ghostpulse.attack, ghostpulse.attack == 4,
        ghostpulse.defense, ghostpulse.defense == 3, ghostpulse.maxRez, ghostpulse.maxRez == 14, static_cast<unsigned>(ghostpulse.effect.type), static_cast<unsigned>(BlackIceEffectType::DamageAndNextTurnNetActionPenalty),
        ghostpulse.effect.damageDice, ghostpulse.effect.netActionPenalty, ghostpulse.effect.minimumNetActions);
    Serial.printf("[Diag][BlackIce][Packetsaw][Flags] runtimeOk=%u combatOk=%u\n", packetsawRuntimeFlag, packetsawCombatFlag);
    Serial.printf("[Diag][BlackIce][Packetsaw][Stats] per=%d/5(%u) spd=%d/7(%u) atk=%d/5(%u) def=%d/3(%u) rez=%d/19(%u) class=%u/%u effect=%u/%u dice=%d/3\n",
        packetsaw.perception, packetsaw.perception == 5, packetsaw.speed, packetsaw.speed == 7, packetsaw.attack, packetsaw.attack == 5,
        packetsaw.defense, packetsaw.defense == 3, packetsaw.maxRez, packetsaw.maxRez == 19, static_cast<unsigned>(packetsaw.category), static_cast<unsigned>(BlackIceClass::AntiProgram),
        static_cast<unsigned>(packetsaw.effect.type), static_cast<unsigned>(BlackIceEffectType::ProgramDamageDestroyAtZero), packetsaw.effect.damageDice);
    Serial.printf("[Diag][BlackIce][Stingwire] attack=%u fire=%u damage=%d move=%u/%u effect=%u/%u\n",
        stingwireAttackOk ? 1U : 0U, stingwireAttackOk ? 1U : 0U, 0,
        stingwireAttackOk ? 0U : 255U, stingwireAttackOk ? 0U : 255U,
        static_cast<unsigned>(stingwire.effect.type), static_cast<unsigned>(BlackIceEffectType::ApplyFire));
    Serial.printf("[Diag][BlackIce][Guttermesh] attack=%u damage=%d move=%u/%u dice=%d/%d\n",
        guttermeshAttackOk ? 1U : 0U, guttermeshAttackOk ? 4 : 0,
        guttermeshAttackOk ? 5U : 255U, guttermeshAttackOk ? 4U : 255U,
        guttermesh.effect.damageDice, guttermesh.effect.statusDice);
    Serial.printf("[Diag][BlackIce][Stormrazor] attack=%u rez=%d effect=%u/%u dice=%d/5\n",
        stormrazorAttackOk ? 1U : 0U, stormrazor.maxRez,
        static_cast<unsigned>(stormrazor.effect.type), static_cast<unsigned>(BlackIceEffectType::ProgramDamageDestroyAtZero), stormrazor.effect.damageDice);
    Serial.printf("[Diag][BlackIce][Glasscat] attack=%u rez=%d effect=%u/%u dice=%d/4\n",
        glasscatAttackOk ? 1U : 0U, glasscat.maxRez,
        static_cast<unsigned>(glasscat.effect.type), static_cast<unsigned>(BlackIceEffectType::ProgramDamageDestroyAtZero), glasscat.effect.damageDice);
    Serial.printf("[Diag][BlackIce][Greymark] rez=%d effect=%u/%u penalty=%d/1\n",
        greymark.maxRez, static_cast<unsigned>(greymark.effect.type),
        static_cast<unsigned>(BlackIceEffectType::ReduceRunnerStatsByAmount), greymark.effect.statusDice);
    Serial.printf("[Diag][BlackIce][Breacher] attack=%u rez=%d effect=%u/%u dice=%d/2\n",
        breacherAttackOk ? 1U : 0U, breacher.maxRez,
        static_cast<unsigned>(breacher.effect.type), static_cast<unsigned>(BlackIceEffectType::DamageAndUnsafeJackOut), breacher.effect.damageDice);
    const bool valid = definitionsOk && tracejackalAttackOk && needlecoilOk && carrionbyteOk && deadlockOk && ghostpulseOk && packetsawOk &&
        stingwireAttackOk && guttermeshAttackOk && stormrazorAttackOk && glasscatAttackOk && breacherAttackOk &&
        targetOk && damageOk && recoveryOk && jsonOk;
    Serial.printf(
        "[BlackIce] %s | Definitions=%s | Tracejackal=%s | Carrionbyte=%s | Needlecoil=%s | Deadlock=%s | Ghostpulse=%s | Packetsaw=%s | "
        "Stingwire=%s | Guttermesh=%s | Stormrazor=%s | Glasscat=%s | Greymark=%s | Breacher=%s | Target=%s | Damage=%s | Recovery=%s | JSON=%s\n",
        valid ? "PASS" : "FAIL", definitionsOk ? "PASS" : "FAIL",
        tracejackalAttackOk ? "PASS" : "FAIL", carrionbyteOk ? "PASS" : "FAIL", needlecoilOk ? "PASS" : "FAIL",
        deadlockOk ? "PASS" : "FAIL",
        ghostpulseOk ? "PASS" : "FAIL",
        packetsawOk ? "PASS" : "FAIL",
        stingwireAttackOk ? "PASS" : "FAIL", guttermeshAttackOk ? "PASS" : "FAIL",
        stormrazorAttackOk ? "PASS" : "FAIL", glasscatAttackOk ? "PASS" : "FAIL",
        greymark.effect.type == BlackIceEffectType::ReduceRunnerStatsByAmount ? "PASS" : "FAIL",
        breacherAttackOk ? "PASS" : "FAIL",
        targetOk ? "PASS" : "FAIL",
        damageOk ? "PASS" : "FAIL", recoveryOk ? "PASS" : "FAIL",
        jsonOk ? "PASS" : "FAIL");
}

void App::runAntiProgramDebugTest()
{
    static const FloorDefinition statusFloors[] = {
        {1, FloorType::Empty, 0, SecurityTier::Low, "ENTRY",
         nullptr, nullptr, 0, BlackIceType::Ice01, "ENTRY", nullptr, nullptr},
        {2, FloorType::BlackICE, 0, SecurityTier::Low, "Packetsaw",
         nullptr, nullptr, 0, BlackIceType::Ice06, "BLACK ICE", nullptr, nullptr}
    };
    static const ArchitectureDefinition statusArchitecture = {
        "Packetsaw_status_test", "Packetsaw STATUS TEST", nullptr, statusFloors, 2
    };
    const BlackIceDefinition& Packetsaw = ice06Definition();

    bool noTargetOk = false;
    {
        Netrunner runner("NO_TARGET", 4, 40, 3);
        Cyberdeck deck;
        Program inactive(ProgramType::Attacker, "INACTIVE", 1, 0, 5);
        Program derezzed(ProgramType::Defender, "DEREZZED", 0, 0, 7);
        Program destroyed(ProgramType::Booster, "DESTROYED", 0, 0, 6);
        derezzed.setStatus(ProgramStatus::Derezzed);
        destroyed.setStatus(ProgramStatus::Destroyed);
        deck.addProgram(inactive);
        deck.addProgram(derezzed);
        deck.addProgram(destroyed);
        BlackIceInstance ice(Packetsaw);
        CombatDice dice(10, 1, 12);
        NetRules rules(dice);
        const CombatResult result = rules.blackIceAttack(ice, runner, &deck);
        noTargetOk = result.executed && result.noValidTarget && !result.success &&
            result.targetType == CombatResult::TargetType::Program &&
            result.affectedProgramSlot < 0 && result.damage == 0 && runner.hp() == 40 &&
            deck.programAt(0)->status() == ProgramStatus::Inactive &&
            deck.programAt(1)->status() == ProgramStatus::Derezzed &&
            deck.programAt(2)->status() == ProgramStatus::Destroyed;
    }

    bool missOk = false;
    {
        Netrunner runner("MISS", 4, 40, 3);
        Cyberdeck deck;
        Program armor(ProgramType::Defender, "Armor", 0, 0, 10);
        armor.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(armor);
        BlackIceInstance ice(Packetsaw);
        CombatDice dice(1, 10, 8);
        NetRules rules(dice);
        const CombatResult result = rules.blackIceAttack(ice, runner, &deck);
        missOk = result.executed && !result.noValidTarget && !result.success &&
            result.affectedProgramSlot == 0 && result.targetRemainingRez == 10 &&
            deck.programAt(0)->rez() == 10 && deck.programAt(0)->status() == ProgramStatus::Rezzed &&
            result.damage == 0 && runner.hp() == 40;
    }

    bool survivingDamageOk = false;
    {
        Netrunner runner("SURVIVE", 4, 40, 3);
        Cyberdeck deck;
        Program armor(ProgramType::Defender, "Armor", 0, 0, 10);
        armor.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(armor);
        BlackIceInstance ice(Packetsaw);
        CombatDice dice(10, 1, 6);
        NetRules rules(dice);
        const CombatResult result = rules.blackIceAttack(ice, runner, &deck);
        survivingDamageOk = result.success && result.programEffectApplied &&
            result.damage == 6 && dice.lastD6Count() == 3 &&
            result.targetRemainingRez == 4 && !result.targetDestroyed &&
            result.resultingProgramStatus == ProgramStatus::Rezzed &&
            deck.programAt(0)->rez() == 4 && deck.programAt(0)->status() == ProgramStatus::Rezzed &&
            runner.hp() == 40;
    }

    bool destroyOk = false;
    {
        Netrunner runner("DESTROY", 4, 40, 3);
        Cyberdeck deck;
        Program sword(ProgramType::Attacker, "Sword", 1, 0, 5);
        sword.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(sword);
        BlackIceInstance ice(Packetsaw);
        CombatDice dice(10, 1, 5);
        NetRules rules(dice);
        const CombatResult result = rules.blackIceAttack(ice, runner, &deck);
        destroyOk = result.success && result.programEffectApplied && result.damage == 5 &&
            result.targetRemainingRez == 0 && result.targetDestroyed &&
            result.resultingProgramStatus == ProgramStatus::Destroyed &&
            deck.programAt(0)->status() == ProgramStatus::Destroyed &&
            deck.programAt(0)->status() != ProgramStatus::Derezzed &&
            !deck.programAt(0)->usable() && runner.hp() == 40;
    }

    bool selectionOk = false;
    {
        Netrunner runner("SELECT", 4, 40, 3);
        Cyberdeck deck;
        Program sword(ProgramType::Attacker, "Sword", 1, 0, 10);
        Program armor(ProgramType::Defender, "Armor", 0, 0, 10);
        sword.setStatus(ProgramStatus::Rezzed);
        armor.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(sword);
        deck.addProgram(armor);
        BlackIceInstance ice(Packetsaw);
        CombatDice dice(10, 1, 3, 1);
        NetRules rules(dice);
        const CombatResult result = rules.blackIceAttack(ice, runner, &deck);
        selectionOk = result.success && result.affectedProgramSlot == 1 &&
            dice.lastIndexCount() == 2 && deck.programAt(0)->rez() == 10 &&
            deck.programAt(1)->rez() == 7 && runner.hp() == 40;
    }

    bool encounterOk = false;
    {
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        state.jackIn();
        state.cyberdeck().programAt(1)->setStatus(ProgramStatus::Rezzed);
        SequenceDice dice(1, 10, 10, 1, 6);
        NetRules rules(dice);
        EncounterResult& result = debugEncounterResult();
        state.moveForward(rules, result);
        encounterOk = result.triggered && result.speedCheck.iceWins &&
            result.speedCheck.iceTotal == 17 && result.immediateAttack.success &&
            result.immediateAttack.targetType == CombatResult::TargetType::Program &&
            result.immediateAttack.affectedProgramSlot == 1 &&
            state.cyberdeck().programAt(1)->rez() == 1 && state.runner().hp() == 40;
    }

    bool encounterNoTargetOk = false;
    {
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        state.jackIn();
        SequenceDice dice(1, 10);
        NetRules rules(dice);
        EncounterResult& result = debugEncounterResult();
        state.moveForward(rules, result);
        const BlackIceInstance* ice = state.activeBlackIceAt(0);
        encounterNoTargetOk = result.triggered && result.speedCheck.iceWins &&
            result.immediateAttack.executed && result.immediateAttack.noValidTarget &&
            state.runner().hp() == 40 && ice != nullptr && ice->active() && ice->pursuing();
    }

    bool icePhaseOk = false;
    {
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        state.jackIn();
        state.architecture().moveForward();
        state.triggerCurrentBlackIce();
        CombatDice dice(10, 1, 3);
        NetRules rules(dice);
        state.endPlayerTurn();
        IcePhaseResult& noTargetPhase = debugIcePhaseResult();
        state.runIcePhase(rules, noTargetPhase);
        BlackIceInstance* ice = state.activeBlackIceAt(0);
        const bool waitedForTarget = noTargetPhase.actionCount == 1 &&
            noTargetPhase.attack.noValidTarget && ice->active() && ice->pursuing();
        state.cyberdeck().programAt(1)->setStatus(ProgramStatus::Rezzed);
        state.endPlayerTurn();
        IcePhaseResult& targetPhase = debugIcePhaseResult();
        state.runIcePhase(rules, targetPhase);
        icePhaseOk = waitedForTarget && targetPhase.actionCount == 1 &&
            targetPhase.attack.success && targetPhase.attack.affectedProgramSlot == 1 &&
            state.cyberdeck().programAt(1)->rez() == 4 && state.runner().hp() == 40;
    }

    bool runResetOk = false;
    {
        // Let Packetsaw contaminate the deck in one run, then start a fresh run.
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        Program* sword = state.cyberdeck().programAt(0);
        Program* armor = state.cyberdeck().programAt(1);
        sword->setStatus(ProgramStatus::Destroyed);
        armor->setStatus(ProgramStatus::Rezzed);
        BlackIceInstance packetsawBefore(Packetsaw);
        CombatDice packetsawDice(10, 1, 7);
        NetRules packetsawRules(packetsawDice);
        const CombatResult packetsawResult = packetsawRules.blackIceAttack(
            packetsawBefore, state.runner(), &state.cyberdeck());
        const bool packetsawContaminated = packetsawResult.success && packetsawResult.programEffectApplied &&
            packetsawResult.affectedProgramSlot == 1 && packetsawResult.targetDestroyed &&
            armor->status() == ProgramStatus::Destroyed;
        armor->setActivatedThisRound(true);
        state.startRun();
        sword = state.cyberdeck().programAt(0);
        armor = state.cyberdeck().programAt(1);
        state.jackIn();
        state.architecture().moveForward();
        const bool triggered = state.triggerCurrentBlackIce();
        const BlackIceInstance* packetsawAfter = state.activeBlackIceAt(0);
        const bool deckResetOk = sword != nullptr && armor != nullptr &&
            sword->status() == ProgramStatus::Inactive && sword->rez() == sword->maxRez() &&
            !sword->activatedThisRound() &&
            armor->status() == ProgramStatus::Inactive && armor->rez() == armor->maxRez() &&
            !armor->activatedThisRound();
        const bool iceResetOk = triggered && packetsawAfter != nullptr &&
            packetsawAfter->definition() == &Packetsaw && packetsawAfter->currentRez() == Packetsaw.maxRez &&
            packetsawAfter->active() && packetsawAfter->pursuing();
        runResetOk = packetsawContaminated && deckResetOk && iceResetOk;
        Serial.printf("[Diag][AntiProgram][Reset] before rez=%d target=%d active=%u pursuing=%u | after rez=%d expected=%d target=-1 active=%u pursuing=%u | contam=%u deck=%u ice=%u\n",
            packetsawBefore.currentRez(), packetsawResult.affectedProgramSlot,
            packetsawBefore.active() ? 1U : 0U, packetsawBefore.pursuing() ? 1U : 0U,
            packetsawAfter != nullptr ? packetsawAfter->currentRez() : -1, Packetsaw.maxRez,
            packetsawAfter != nullptr && packetsawAfter->active() ? 1U : 0U,
            packetsawAfter != nullptr && packetsawAfter->pursuing() ? 1U : 0U,
            packetsawContaminated ? 1U : 0U, deckResetOk ? 1U : 0U, iceResetOk ? 1U : 0U);
    }

    bool noTargetStateOk = false;
    {
        Netrunner runner("NO_TARGET_STATE", 4, 40, 3);
        Cyberdeck deck;
        Program sword(ProgramType::Attacker, "Sword", 1, 0, 5);
        Program armor(ProgramType::Defender, "Armor", 0, 0, 7);
        deck.addProgram(sword);
        deck.addProgram(armor);
        const uint8_t swordRez = deck.programAt(0)->rez();
        const uint8_t armorRez = deck.programAt(1)->rez();
        BlackIceInstance ice(Packetsaw);
        CombatDice dice(10, 1, 12);
        NetRules rules(dice);
        const CombatResult result = rules.blackIceAttack(ice, runner, &deck);
        noTargetStateOk = result.executed && result.noValidTarget &&
            result.affectedProgramSlot < 0 && !result.targetDestroyed &&
            deck.programAt(0)->status() == ProgramStatus::Inactive &&
            deck.programAt(1)->status() == ProgramStatus::Inactive &&
            deck.programAt(0)->rez() == swordRez && deck.programAt(1)->rez() == armorRez &&
            runner.hp() == 40;
    }

    bool staleResultOk = false;
    {
        Netrunner runner("STALE_RESULT", 4, 40, 3);
        Cyberdeck deck;
        Program armor(ProgramType::Defender, "Armor", 0, 0, 5);
        armor.setStatus(ProgramStatus::Rezzed);
        deck.addProgram(armor);
        BlackIceInstance ice(Packetsaw);
        CombatDice dice(10, 1, 5);
        NetRules rules(dice);
        const CombatResult first = rules.blackIceAttack(ice, runner, &deck);
        const CombatResult second = rules.blackIceAttack(ice, runner, &deck);
        staleResultOk = first.targetDestroyed && first.affectedProgramSlot == 0 &&
            second.executed && second.noValidTarget && !second.success &&
            !second.targetDestroyed && second.affectedProgramSlot < 0 && second.damage == 0 &&
            second.targetRemainingRez == 0;
    }

    const bool targetOk = noTargetOk && noTargetStateOk && selectionOk;
    const bool damageOk = missOk && survivingDamageOk;
    const bool phaseOk = encounterOk && encounterNoTargetOk && icePhaseOk;
    const bool valid = targetOk && damageOk && destroyOk && phaseOk && runResetOk && staleResultOk;
    Serial.printf(
        "[AntiProgram] %s | Target=%s | Damage=%s | Destroy=%s | Encounter=%s | NoTarget=%s | Reset=%s | Stale=%s\n",
        valid ? "PASS" : "FAIL", targetOk ? "PASS" : "FAIL",
        damageOk ? "PASS" : "FAIL", destroyOk ? "PASS" : "FAIL",
        phaseOk ? "PASS" : "FAIL", noTargetOk && encounterNoTargetOk ? "PASS" : "FAIL",
        runResetOk ? "PASS" : "FAIL", staleResultOk ? "PASS" : "FAIL");
}

void App::runAntiProgramExpansionDebugTest()
{
    const BlackIceDefinition& packetsaw = ice06Definition();
    const BlackIceDefinition& stormrazor = ice09Definition();
    const BlackIceDefinition& glasscat = ice10Definition();
    const bool statsOk = packetsaw.category == BlackIceClass::AntiProgram && packetsaw.perception == 5 &&
        packetsaw.speed == 7 && packetsaw.attack == 5 && packetsaw.defense == 3 && packetsaw.maxRez == 19 &&
        packetsaw.effect.damageDice == 3 && stormrazor.category == BlackIceClass::AntiProgram &&
        stormrazor.perception == 7 && stormrazor.speed == 5 && stormrazor.attack == 6 && stormrazor.defense == 5 &&
        stormrazor.maxRez == 27 && stormrazor.effect.damageDice == 5 && glasscat.category == BlackIceClass::AntiProgram &&
        glasscat.perception == 8 && glasscat.speed == 7 && glasscat.attack == 5 &&
        glasscat.defense == 3 && glasscat.maxRez == 23 && glasscat.effect.damageDice == 4;

    bool packetsawOk = false;
    {
        Netrunner runner("PACKETSAW", 4, 40, 3);
        Cyberdeck deck;
        Program sword(ProgramType::Attacker, "Sword", 1, 0, 7);
        sword.setStatus(ProgramStatus::Rezzed); deck.addProgram(sword);
        BlackIceInstance ice(packetsaw);
        CombatDice firstDice(10, 1, 4); NetRules firstRules(firstDice);
        const CombatResult first = firstRules.blackIceAttack(ice, runner, &deck);
        CombatDice secondDice(10, 1, 4); NetRules secondRules(secondDice);
        const CombatResult second = secondRules.blackIceAttack(ice, runner, &deck);
        Cyberdeck empty;
        BlackIceInstance noTargetIce(packetsaw);
        CombatDice noTargetDice(10, 1, 4); NetRules noTargetRules(noTargetDice);
        const CombatResult noTarget = noTargetRules.blackIceAttack(noTargetIce, runner, &empty);
        packetsawOk = first.success && firstDice.lastD6Count() == 3 && first.targetInitialRez == 7 &&
            first.targetRemainingRez == 3 && !first.targetDestroyed && first.affectedProgramName[0] != '\0' &&
            second.success && second.targetDestroyed && deck.programAt(0)->status() == ProgramStatus::Destroyed &&
            noTarget.executed && noTarget.noProgramTarget && noTarget.noValidTarget && runner.hp() == 40;
    }

    bool stormrazorOk = false;
    {
        Netrunner runner("STORMRAZOR", 4, 40, 3);
        Cyberdeck deck;
        Program armor(ProgramType::Defender, "Armor", 0, 6, 20);
        armor.setStatus(ProgramStatus::Rezzed); deck.addProgram(armor);
        BlackIceInstance ice(stormrazor);
        CombatDice firstDice(10, 1, 6); NetRules firstRules(firstDice);
        const CombatResult first = firstRules.blackIceAttack(ice, runner, &deck);
        CombatDice secondDice(10, 1, 20); NetRules secondRules(secondDice);
        const CombatResult second = secondRules.blackIceAttack(ice, runner, &deck);
        stormrazorOk = first.success && firstDice.lastD6Count() == 5 && first.targetInitialRez == 20 &&
            first.targetRemainingRez == 14 && !first.targetDestroyed && second.success &&
            second.targetDestroyed && deck.programAt(0)->status() == ProgramStatus::Destroyed && runner.hp() == 40;
    }

    bool glasscatOk = false;
    {
        Netrunner runner("SABER", 4, 40, 3);
        Cyberdeck deck;
        Program sword(ProgramType::Attacker, "Sword", 1, 0, 7);
        sword.setStatus(ProgramStatus::Rezzed); deck.addProgram(sword);
        BlackIceInstance ice(glasscat);
        CombatDice firstDice(10, 1, 6); NetRules firstRules(firstDice);
        const CombatResult first = firstRules.blackIceAttack(ice, runner, &deck);
        CombatDice secondDice(10, 1, 6); NetRules secondRules(secondDice);
        const CombatResult second = secondRules.blackIceAttack(ice, runner, &deck);
        glasscatOk = first.success && firstDice.lastD6Count() == 4 && first.targetRemainingRez == 1 &&
            !first.targetDestroyed && second.success && second.targetDestroyed &&
            deck.programAt(0)->status() == ProgramStatus::Destroyed && runner.hp() == 40;
    }

    static FloorDefinition mixedFloors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "Packetsaw_Ghostpulse", nullptr, nullptr, 0,
         BlackIceType::Ice06, "Packetsaw Ghostpulse", nullptr, nullptr},
        {2, FloorType::BlackICE, 0, SecurityTier::Low, "Packetsaw_Stormrazor", nullptr, nullptr, 0,
         BlackIceType::Ice06, "Packetsaw Stormrazor", nullptr, nullptr}
    };
    static bool mixedConfigured = false;
    if (!mixedConfigured)
    {
        mixedFloors[0].blackIceCount = 2;
        mixedFloors[0].blackIceTypes[0] = BlackIceType::Ice06;
        mixedFloors[0].blackIceTypes[1] = BlackIceType::Ice05;
        mixedFloors[1].blackIceCount = 2;
        mixedFloors[1].blackIceTypes[0] = BlackIceType::Ice06;
        mixedFloors[1].blackIceTypes[1] = BlackIceType::Ice09;
        mixedConfigured = true;
    }
    static const ArchitectureDefinition mixedArchitecture = {
        "anti_program_multi_test", "ANTI PROGRAM MULTI", nullptr, mixedFloors, 2};
    bool multiOk = false;
    {
        GameState& state = debugTestState(mixedArchitecture);
        state.startRun(); state.jackIn(); state.triggerCurrentBlackIce();
        state.cyberdeck().programAt(0)->setStatus(ProgramStatus::Rezzed);
        state.cyberdeck().programAt(1)->setStatus(ProgramStatus::Destroyed);
        const uint16_t PacketsawId = state.activeBlackIceRuntimeId(0);
        const uint16_t GhostpulseId = state.activeBlackIceRuntimeId(1);
        state.endPlayerTurn();
        SequenceDice dice(10, 1, 10, 1, 5); NetRules rules(dice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(rules, phase);
        multiOk = phase.attackCount == 2 && phase.attacks[0].runtimeId == PacketsawId &&
            phase.attacks[1].runtimeId == GhostpulseId && phase.attacks[0].result.targetDestroyed &&
            phase.attacks[0].result.targetType == CombatResult::TargetType::Program &&
            phase.attacks[1].result.success && state.runner().hp() == 35;
    }

    static FloorDefinition destroyFloors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "Packetsaw_Stormrazor", nullptr, nullptr, 0,
         BlackIceType::Ice06, "Packetsaw Stormrazor", nullptr, nullptr}
    };
    static bool destroyConfigured = false;
    if (!destroyConfigured)
    {
        destroyFloors[0].blackIceCount = 2;
        destroyFloors[0].blackIceTypes[0] = BlackIceType::Ice06;
        destroyFloors[0].blackIceTypes[1] = BlackIceType::Ice09;
        destroyConfigured = true;
    }
    static const ArchitectureDefinition destroyArchitecture = {
        "Packetsaw_Stormrazor_test", "Packetsaw Stormrazor", nullptr, destroyFloors, 1};
    bool destroyedTargetExcludedOk = false;
    {
        GameState& state = debugTestState(destroyArchitecture);
        state.startRun(); state.jackIn(); state.triggerCurrentBlackIce();
        state.cyberdeck().programAt(0)->setStatus(ProgramStatus::Rezzed);
        state.cyberdeck().programAt(1)->setStatus(ProgramStatus::Destroyed);
        const uint16_t PacketsawId = state.activeBlackIceRuntimeId(0);
        const uint16_t StormrazorId = state.activeBlackIceRuntimeId(1);
        state.endPlayerTurn();
        SequenceDice dice(10, 1, 10, 1, 5); NetRules rules(dice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(rules, phase);
        destroyedTargetExcludedOk = phase.attackCount == 2 &&
            phase.attacks[0].runtimeId == PacketsawId && phase.attacks[1].runtimeId == StormrazorId &&
            phase.attacks[0].result.targetDestroyed && phase.attacks[1].result.noProgramTarget &&
            phase.attacks[1].result.noValidTarget && state.runner().hp() == 40;
    }

    const bool valid = statsOk && packetsawOk && stormrazorOk && glasscatOk && multiOk && destroyedTargetExcludedOk;
    Serial.printf("[AntiProgramExpansion] %s | Stats=%s | Packetsaw=%s | Stormrazor=%s | Glasscat=%s | Multi=%s | Exclude=%s\n",
        valid ? "PASS" : "FAIL", statsOk ? "PASS" : "FAIL", packetsawOk ? "PASS" : "FAIL",
        stormrazorOk ? "PASS" : "FAIL", glasscatOk ? "PASS" : "FAIL", multiOk ? "PASS" : "FAIL",
        destroyedTargetExcludedOk ? "PASS" : "FAIL");
}

void App::runBlackIceEffectsDebugTest()
{
    const BlackIceDefinition& stingwire = ice07Definition();
    const BlackIceDefinition& guttermesh = ice08Definition();
    const bool statsOk = stingwire.perception == 3 && stingwire.speed == 7 &&
        stingwire.attack == 3 && stingwire.defense == 3 && stingwire.maxRez == 14 &&
        stingwire.effect.type == BlackIceEffectType::ApplyFire &&
        guttermesh.perception == 3 && guttermesh.speed == 5 && guttermesh.attack == 5 &&
        guttermesh.defense == 3 && guttermesh.maxRez == 12 &&
        guttermesh.effect.type == BlackIceEffectType::DamageAndReduceRunnerMove &&
        guttermesh.effect.damageDice == 1 && guttermesh.effect.statusDice == 1;

    bool CarrionbyteOk = false;
    {
        Netrunner runner("Carrionbyte", 4, 40, 3);
        Cyberdeck deck;
        deck.addProgram(Program(ProgramType::Defender, "Armor", 0, 0, 7));
        deck.addProgram(Program(ProgramType::Attacker, "Sword", 1, 0, 5));
        deck.programAt(0)->setStatus(ProgramStatus::Rezzed);
        deck.programAt(1)->setStatus(ProgramStatus::Rezzed);
        BlackIceInstance Carrionbyte(ice02Definition());
        CombatDice dice(10, 1, 4); NetRules rules(dice);
        const CombatResult hit = rules.blackIceAttack(Carrionbyte, runner, &deck);
        Cyberdeck noTargetDeck;
        Netrunner secondRunner("Carrionbyte2", 4, 40, 3);
        BlackIceInstance secondCarrionbyte(ice02Definition());
        CombatDice secondDice(10, 1, 3); NetRules secondRules(secondDice);
        const CombatResult noTarget = secondRules.blackIceAttack(secondCarrionbyte, secondRunner, &noTargetDeck);
        CarrionbyteOk = hit.success && hit.damage == 0 && hit.affectedProgramSlot == 0 &&
            hit.programEffectApplied && deck.programAt(0)->status() == ProgramStatus::Destroyed &&
            deck.programAt(1)->status() == ProgramStatus::Rezzed && runner.hp() == 40 &&
            noTarget.success && noTarget.damage == 0 && noTarget.affectedProgramSlot < 0 &&
            !noTarget.programEffectApplied && secondRunner.hp() == 40;
    }

    static const FloorDefinition GhostpulseFloors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "Ghostpulse", nullptr, nullptr, 0,
         BlackIceType::Ice05, "Ghostpulse", nullptr, nullptr}
    };
    static const ArchitectureDefinition GhostpulseArchitecture = {
        "Ghostpulse_effect_test", "Ghostpulse EFFECT", nullptr, GhostpulseFloors, 1};
    bool GhostpulseOk = false;
    {
        GameState& state = debugTestState(GhostpulseArchitecture);
        state.startRun(); state.jackIn(); state.triggerCurrentBlackIce(); state.endPlayerTurn();
        CombatDice hitDice(10, 1, 4); NetRules hitRules(hitDice);
        IcePhaseResult& hitPhase = debugIcePhaseResult();
        state.runIcePhase(hitRules, hitPhase);
        const bool nextTurnReduced = hitPhase.attackCount == 1 && hitPhase.attacks[0].result.success &&
            hitPhase.attacks[0].result.damage == 4 && state.runner().hp() == 36 &&
            state.runner().remainingNetActions() == 2 && state.netActionPenaltyThisTurn() == 1;
        state.endPlayerTurn();
        CombatDice missDice(1, 10, 0); NetRules missRules(missDice);
        debugRunIcePhase(state, missRules);
        GhostpulseOk = nextTurnReduced && state.runner().remainingNetActions() == 3;
    }

    static const FloorDefinition StingwireFloors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "Stingwire", nullptr, nullptr, 0,
         BlackIceType::Ice07, "Stingwire", nullptr, nullptr}
    };
    static const ArchitectureDefinition StingwireArchitecture = {
        "Stingwire_effect_test", "Stingwire EFFECT", nullptr, StingwireFloors, 1};
    bool StingwireOk = false;
    {
        GameState& state = debugTestState(StingwireArchitecture);
        state.startRun(); state.jackIn(); state.triggerCurrentBlackIce(); state.endPlayerTurn();
        CombatDice hitDice(10, 1, 3); NetRules hitRules(hitDice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(hitRules, phase);
        const CombatResult& hit = phase.attacks[0].result;
        const bool fireApplied = hit.success && hit.fireApplied && !hit.fireBlocked && hit.damage == 0 &&
            hit.moveBefore == 0 && hit.moveAfter == 0 && state.runner().move() == state.runner().baseMove();
        state.startRun();
        StingwireOk = fireApplied && state.runner().move() == state.runner().baseMove();
    }

    bool GenericSlidePenaltyOk = false;
    {
        static const BlackIceDefinition genericSlidePenalty = {
            "test_slide_penalty", "SLIDE PENALTY", 1, BlackIceType::Ice01, BlackIceClass::AntiPersonnel,
            1, 1, 1, 1, {BlackIceEffectType::SlidePenaltyWhileActive, 0, 0, 0, 0, 2},
            "TEST", "SLIDE PENALTY", nullptr, IceVisualId::Rat01, HostileAttackStyle::Pulse, false};
        Netrunner effectRunner("GENERIC_SLIDE_EFFECT", 4, 40, 3);
        BlackIceInstance effectIce(genericSlidePenalty);
        CombatDice effectDice(10, 1, 0); NetRules effectRules(effectDice);
        const CombatResult effectHit = effectRules.blackIceAttack(effectIce, effectRunner);

        // GameState owns the persistent active-ICE modifier. This neutral
        // fixture verifies the descriptor and the generic Slide rule without
        // borrowing a production ICE definition.
        Netrunner slideRunner("GENERIC_SLIDE_CHECK", 4, 40, 3);
        BlackIceInstance slideTarget(genericSlidePenalty);
        CombatDice slideDice(1, 3, 0); NetRules slideRules(slideDice);
        const CombatResult penalizedSlide = slideRules.slide(
            slideRunner, slideTarget, -static_cast<int>(genericSlidePenalty.effect.slidePenalty));
        GenericSlidePenaltyOk = effectHit.executed && effectHit.success && effectHit.damage == 0 &&
            effectHit.temporaryRunEffect == TemporaryRunEffect::SlidePenaltyWhileActive &&
            effectIce.active() && effectIce.pursuing() && genericSlidePenalty.effect.slidePenalty == 2 &&
            penalizedSlide.executed && !penalizedSlide.success && penalizedSlide.attackerTotal == 3 &&
            penalizedSlide.defenderTotal == 4 && slideTarget.pursuing();
    }

    bool GuttermeshOk = false;
    {
        Netrunner runner("GUTTERMESH", 4, 40, 3);
        BlackIceInstance ice(guttermesh);
        CombatDice dice(10, 1, 4); NetRules rules(dice);
        const CombatResult hit = rules.blackIceAttack(ice, runner);
        GuttermeshOk = hit.executed && hit.success && hit.damage == 4 && dice.lastD6Count() == 1 &&
            hit.moveBefore == 5 && hit.moveAfter == 4 && runner.move() == 4 &&
            hit.temporaryRunEffect == TemporaryRunEffect::None;
    }

    static const BlackIceDefinition combinedFireDamage = {
        "combined_fire_damage", "COMBINED FIRE DAMAGE", 14, BlackIceType::Ice07,
        BlackIceClass::AntiPersonnel, 3, 7, 3, 3,
        {BlackIceEffectType::ApplyFire, 2, 0, 0, 0, 0},
        "BLACK ICE", "ANTI-PERSON", "FIRE + 2D6", IceVisualId::Scorp01,
        HostileAttackStyle::Burst, true};
    bool combinedFireDamageOk = false;
    {
        Netrunner runner("COMBINED", 4, 40, 3);
        BlackIceInstance ice(combinedFireDamage);
        CombatDice dice(10, 1, 4); NetRules rules(dice);
        const CombatResult hit = rules.blackIceAttack(ice, runner);
        combinedFireDamageOk = hit.executed && hit.success && hit.fireApplied &&
            hit.damage == 4 && runner.hp() == 36 && dice.lastD6Count() == 2;
    }

    static FloorDefinition mixedFloors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "Carrionbyte_Ghostpulse", nullptr, nullptr, 0,
         BlackIceType::Ice02, "Carrionbyte Ghostpulse", nullptr, nullptr}
    };
    static bool mixedFloorsConfigured = false;
    if (!mixedFloorsConfigured)
    {
        mixedFloors[0].blackIceCount = 2;
        mixedFloors[0].blackIceTypes[0] = BlackIceType::Ice02;
        mixedFloors[0].blackIceTypes[1] = BlackIceType::Ice05;
        mixedFloorsConfigured = true;
    }
    static const ArchitectureDefinition mixedArchitecture = {
        "Carrionbyte_Ghostpulse_test", "Carrionbyte Ghostpulse", nullptr, mixedFloors, 1};
    bool multiOk = false;
    {
        GameState& state = debugTestState(mixedArchitecture);
        state.startRun(); state.jackIn(); state.triggerCurrentBlackIce();
        const int startingHp = state.runner().hp();
        Program* armor = state.cyberdeck().programAt(1);
        armor->setStatus(ProgramStatus::Rezzed);
        const uint16_t CarrionbyteId = state.activeBlackIceRuntimeId(0);
        const uint16_t GhostpulseId = state.activeBlackIceRuntimeId(1);
        state.endPlayerTurn();
        MultiDamageDice dice(10, 1, 10, 1, 4, 4, 1); NetRules rules(dice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(rules, phase);
        const CombatResult carrionbyte = phase.attackCount > 0 ? phase.attacks[0].result : CombatResult();
        const CombatResult ghostpulse = phase.attackCount > 1 ? phase.attacks[1].result : CombatResult();
        const int expectedHp = startingHp - ghostpulse.damage;
        const bool orderOk = phase.attackCount == 2 && phase.attacks[0].runtimeId == CarrionbyteId &&
            phase.attacks[1].runtimeId == GhostpulseId;
        const bool carrionbyteOk = carrionbyte.success && carrionbyte.programEffectApplied &&
            carrionbyte.damage == 0 && armor->status() == ProgramStatus::Destroyed;
        const bool ghostpulseOk = ghostpulse.success && ghostpulse.damage > 0 &&
            ghostpulse.temporaryRunEffectApplied && state.netActionPenaltyThisTurn() == 1;
        const bool hpOk = state.runner().hp() == expectedHp;
        const bool actionsOk = state.runner().remainingNetActions() == 2;
        multiOk = orderOk && carrionbyteOk && ghostpulseOk && hpOk && actionsOk;
        Serial.printf("[Diag][BlackIceEffects][Multi] ghostExecuted=%u ghostHit=%u ghostDamage=%d hpBefore=%d hpAfter=%d pending=%u effectType=%u carrionDamage=%d carrionProgram=%u armorDestroyed=%u hpExpected=%d | order=%u carrion=%u ghost=%u hp=%u actions=%u\n",
            ghostpulse.executed ? 1U : 0U, ghostpulse.success ? 1U : 0U, ghostpulse.damage,
            startingHp, state.runner().hp(), state.netActionPenaltyThisTurn(),
            static_cast<unsigned>(ghostpulse.attackerDefinition != nullptr ? ghostpulse.attackerDefinition->effect.type : BlackIceEffectType::DirectDamage),
            carrionbyte.damage, carrionbyte.programEffectApplied ? 1U : 0U,
            armor->status() == ProgramStatus::Destroyed ? 1U : 0U, expectedHp, orderOk ? 1U : 0U,
            carrionbyteOk ? 1U : 0U, ghostpulseOk ? 1U : 0U, hpOk ? 1U : 0U,
            actionsOk ? 1U : 0U);
    }

    const bool valid = statsOk && CarrionbyteOk && GhostpulseOk && StingwireOk && GuttermeshOk && GenericSlidePenaltyOk && combinedFireDamageOk && multiOk;
    Serial.printf("[BlackIceEffects] %s | Stats=%s | Carrionbyte=%s | Ghostpulse=%s | Stingwire=%s | Guttermesh=%s | GenericSlidePenalty=%s | CombinedFireDamage=%s | Multi=%s\n",
        valid ? "PASS" : "FAIL", statsOk ? "PASS" : "FAIL", CarrionbyteOk ? "PASS" : "FAIL",
        GhostpulseOk ? "PASS" : "FAIL", StingwireOk ? "PASS" : "FAIL",
        GuttermeshOk ? "PASS" : "FAIL", GenericSlidePenaltyOk ? "PASS" : "FAIL", combinedFireDamageOk ? "PASS" : "FAIL", multiOk ? "PASS" : "FAIL");
}

void App::runDigitalRollAnimationDebugTest()
{
    const DigitalRollSequence sequence = makeDigitalRollSequence(8);
    bool fakeValuesInRange = true;
    for (size_t index = 0; index < DIGITAL_ROLL_FAKE_FRAME_COUNT; ++index)
    {
        if (sequence.fakeValues[index] < 1 || sequence.fakeValues[index] > 10)
            fakeValuesInRange = false;
    }

    static const FloorDefinition floors[] = {
        {1, FloorType::Password, 6, SecurityTier::Low, "FIRST",
         nullptr, nullptr, 0, BlackIceType::Ice01, "FIRST", nullptr, nullptr},
        {2, FloorType::Password, 6, SecurityTier::Low, "SECOND",
         nullptr, nullptr, 0, BlackIceType::Ice01, "SECOND", nullptr, nullptr}
    };
    static const ArchitectureDefinition architecture = {
        "digital_roll_test", "DIGITAL ROLL TEST", nullptr, floors, 2};

    GameState& baselineState = debugTestState(architecture);
    baselineState.startRun(); baselineState.jackIn();
    SequenceDice baselineDice(8, 5);
    NetRules baselineRules(baselineDice);
    const NetCheckResult baselineFirst = baselineRules.backdoor(
        baselineState.runner(), *baselineState.architecture().floorAt(0));
    const NetCheckResult baselineSecond = baselineRules.backdoor(
        baselineState.runner(), *baselineState.architecture().floorAt(1));

    GameState& animatedState = debugTestState(architecture);
    animatedState.startRun(); animatedState.jackIn();
    SequenceDice animatedDice(8, 5);
    NetRules animatedRules(animatedDice);
    const DigitalRollSequence animatedSequence = makeDigitalRollSequence(8);
    const NetCheckResult animatedFirst = animatedRules.backdoor(
        animatedState.runner(), *animatedState.architecture().floorAt(0));
    const NetCheckResult animatedSecond = animatedRules.backdoor(
        animatedState.runner(), *animatedState.architecture().floorAt(1));

    const bool finalRollOk = sequence.finalValue == 8 && animatedSequence.finalValue == 8 &&
        animatedFirst.roll == 8 && animatedFirst.total == 12 && animatedFirst.success;
    const bool diceUntouched = baselineFirst.roll == animatedFirst.roll &&
        baselineSecond.roll == animatedSecond.roll && baselineSecond.roll == 5 &&
        baselineFirst.success == animatedFirst.success && baselineSecond.success == animatedSecond.success;
    const bool countOk = DIGITAL_ROLL_FAKE_FRAME_COUNT == 5;
    const bool valid = finalRollOk && diceUntouched && countOk && fakeValuesInRange;
    Serial.printf("[DigitalRoll] %s | Final=%s | Dice=%s | Frames=%s | Range=%s\n",
        valid ? "PASS" : "FAIL", finalRollOk ? "PASS" : "FAIL",
        diceUntouched ? "PASS" : "FAIL", countOk ? "PASS" : "FAIL",
        fakeValuesInRange ? "PASS" : "FAIL");
}

void App::runMultiIceDebugTest()
{
    static FloorDefinition floors[] = {
        {1, FloorType::Empty, 0, SecurityTier::Low, "ENTRY",
         nullptr, nullptr, 0, BlackIceType::Ice01, "ENTRY", nullptr, nullptr},
        {2, FloorType::BlackICE, 0, SecurityTier::Low, "MULTI",
         nullptr, nullptr, 0, BlackIceType::Ice02, "MULTI ICE", nullptr, nullptr},
        {3, FloorType::BlackICE, 0, SecurityTier::Low, "TRIPLE",
         nullptr, nullptr, 0, BlackIceType::Ice04, "TRIPLE ICE", nullptr, nullptr}
    };
    static bool configured = false;
    if (!configured)
    {
        floors[1].blackIceCount = 2;
        floors[1].blackIceTypes[0] = BlackIceType::Ice02;
        floors[1].blackIceTypes[1] = BlackIceType::Ice05;
        floors[2].blackIceCount = 3;
        floors[2].blackIceTypes[0] = BlackIceType::Ice04;
        floors[2].blackIceTypes[1] = BlackIceType::Ice02;
        floors[2].blackIceTypes[2] = BlackIceType::Ice05;
        configured = true;
    }
    static const ArchitectureDefinition architecture = {
        "multi_ice_test_net", "MULTI ICE TEST NET", "Multi ICE chase test.", floors, 3};
    static FloorDefinition tripleFloors[] = {
        {1, FloorType::Empty, 0, SecurityTier::Low, "ENTRY",
         nullptr, nullptr, 0, BlackIceType::Ice01, "ENTRY", nullptr, nullptr},
        {2, FloorType::BlackICE, 0, SecurityTier::Low, "TRIPLE",
         nullptr, nullptr, 0, BlackIceType::Ice04, "TRIPLE ICE", nullptr, nullptr}
    };
    static bool tripleConfigured = false;
    if (!tripleConfigured)
    {
        tripleFloors[1].blackIceCount = 3;
        tripleFloors[1].blackIceTypes[0] = BlackIceType::Ice04;
        tripleFloors[1].blackIceTypes[1] = BlackIceType::Ice02;
        tripleFloors[1].blackIceTypes[2] = BlackIceType::Ice05;
        tripleConfigured = true;
    }
    static const ArchitectureDefinition tripleArchitecture = {
        "triple_ice_test_net", "TRIPLE ICE TEST NET", nullptr, tripleFloors, 2};

    bool encounterOk = false;
    {
        GameState& state = debugTestState(architecture);
        state.startRun(); state.jackIn();
        EncounterDice dice(1, 10, 10, 1, 10, 1);
        NetRules rules(dice);
        EncounterResult& encounter = debugEncounterResult();
        state.moveForward(rules, encounter);
        const BlackIceInstance* first = state.activeBlackIceAt(0);
        const BlackIceInstance* second = state.activeBlackIceAt(1);
        encounterOk = encounter.triggered && encounter.iceCount == 2 &&
            encounter.ice[0].speedCheck.iceWins && encounter.ice[0].immediateAttack.executed &&
            encounter.ice[1].speedCheck.runnerWins && !encounter.ice[1].immediateAttack.executed &&
            first != nullptr && second != nullptr && first->pursuing() && second->pursuing() &&
            state.activeBlackIceRuntimeId(0) != state.activeBlackIceRuntimeId(1);
    }

    bool duplicateIsolationOk = false;
    {
        GameState& state = debugTestState(tripleArchitecture);
        state.startRun(); state.jackIn();
        FixedDice dice(1); NetRules rules(dice);
        debugMoveForward(state, rules);
        BlackIceInstance* first = state.activeBlackIceAt(0);
        BlackIceInstance* second = state.activeBlackIceAt(1);
        BlackIceInstance* third = state.activeBlackIceAt(2);
        if (first != nullptr && second != nullptr && third != nullptr)
        {
            first->takeRezDamage(5);
            duplicateIsolationOk = state.activeBlackIceCount() == 3 &&
                first->currentRez() != second->currentRez() &&
                second->currentRez() == second->maxRez() &&
                third->currentRez() == third->maxRez();
        }
    }

    bool targetOk = false;
    {
        GameState& state = debugTestState(tripleArchitecture);
        state.startRun(); state.jackIn();
        FixedDice encounterDice(1); NetRules encounterRules(encounterDice);
        debugMoveForward(state, encounterRules);
        BlackIceInstance* Deadlock = state.activeBlackIceAt(0);
        BlackIceInstance* Carrionbyte = state.activeBlackIceAt(1);
        BlackIceInstance* Ghostpulse = state.activeBlackIceAt(2);
        state.cyberdeck().programAt(0)->setStatus(ProgramStatus::Rezzed);
        const int DeadlockRez = Deadlock->currentRez();
        const int GhostpulseRez = Ghostpulse->currentRez();
        CombatDice swordDice(10, 1, 1); NetRules swordRules(swordDice);
        CombatDice zapDice(10, 1, 1); NetRules zapRules(zapDice);
        CombatDice slideDice(10, 1, 0); NetRules slideRules(slideDice);
        const CombatResult sword = swordRules.swordAttack(state.runner(), state.cyberdeck(), *Carrionbyte);
        const CombatResult zap = zapRules.zap(state.runner(), *Deadlock);
        const CombatResult slide = slideRules.slide(state.runner(), *Ghostpulse);
        targetOk = sword.executed && zap.executed && slide.executed && slide.success &&
            Carrionbyte->currentRez() == Carrionbyte->maxRez() - 1 && Deadlock->currentRez() != DeadlockRez &&
            Ghostpulse->pursuing() == false && Ghostpulse->currentRez() == GhostpulseRez &&
            state.engagedBlackIceCount() == 2;
    }

    bool icePhaseOk = false;
    {
        GameState& state = debugTestState(tripleArchitecture);
        state.startRun(); state.jackIn();
        FixedDice dice(10); NetRules rules(dice);
        debugMoveForward(state, rules);
        state.endPlayerTurn();
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(rules, phase);
        icePhaseOk = phase.executed && phase.actionCount == 3 && phase.attackCount == 3 &&
            state.turnNumber() == 2;
    }

    // Each attack entry must retain its own stable identity and post-attack HP state.
    // The UI consumes these fixed-size entries sequentially during a multi-ICE phase.
    static FloorDefinition resultFloors[] = {
        {1, FloorType::Empty, 0, SecurityTier::Low, "ENTRY",
         nullptr, nullptr, 0, BlackIceType::Ice01, "ENTRY", nullptr, nullptr},
        {2, FloorType::BlackICE, 0, SecurityTier::Low, "RESULTS",
         nullptr, nullptr, 0, BlackIceType::Ice01, "RESULT ICE", nullptr, nullptr}
    };
    static bool resultFloorsConfigured = false;
    if (!resultFloorsConfigured)
    {
        resultFloors[1].blackIceCount = 2;
        resultFloors[1].blackIceTypes[0] = BlackIceType::Ice01;
        resultFloors[1].blackIceTypes[1] = BlackIceType::Ice05;
        resultFloorsConfigured = true;
    }
    static const ArchitectureDefinition resultArchitecture = {
        "multi_ice_results_test", "MULTI ICE RESULTS", nullptr, resultFloors, 2};

    bool hitMissResultsOk = false;
    bool missHitResultsOk = false;
    bool hitHitResultsOk = false;
    {
        GameState& state = debugTestState(resultArchitecture);
        state.startRun(); state.jackIn();
        EncounterDice encounterDice(10, 1, 10, 1, 1, 1);
        NetRules encounterRules(encounterDice);
        debugMoveForward(state, encounterRules);
        const uint16_t TracejackalId = state.activeBlackIceRuntimeId(0);
        const uint16_t GhostpulseId = state.activeBlackIceRuntimeId(1);
        state.endPlayerTurn();
        SequenceDice phaseDice(10, 1, 1, 10, 4);
        NetRules phaseRules(phaseDice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(phaseRules, phase);
        hitMissResultsOk = phase.attackCount == 2 && phase.attacks[0].runtimeId == TracejackalId &&
            phase.attacks[1].runtimeId == GhostpulseId && phase.attacks[0].result.success &&
            phase.attacks[0].result.damage == 4 && phase.attacks[0].result.targetRemainingHp == 36 &&
            !phase.attacks[1].result.success && phase.attacks[1].result.targetRemainingHp == 36 &&
            state.runner().hp() == 36;
    }
    {
        GameState& state = debugTestState(resultArchitecture);
        state.startRun(); state.jackIn();
        EncounterDice encounterDice(10, 1, 10, 1, 1, 1);
        NetRules encounterRules(encounterDice);
        debugMoveForward(state, encounterRules);
        const uint16_t TracejackalId = state.activeBlackIceRuntimeId(0);
        const uint16_t GhostpulseId = state.activeBlackIceRuntimeId(1);
        state.endPlayerTurn();
        SequenceDice phaseDice(1, 10, 10, 1, 4);
        NetRules phaseRules(phaseDice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(phaseRules, phase);
        missHitResultsOk = phase.attackCount == 2 && phase.attacks[0].runtimeId == TracejackalId &&
            phase.attacks[1].runtimeId == GhostpulseId && !phase.attacks[0].result.success &&
            phase.attacks[0].result.targetRemainingHp == 40 && phase.attacks[1].result.success &&
            phase.attacks[1].result.damage == 4 && phase.attacks[1].result.targetRemainingHp == 36 &&
            state.runner().hp() == 36;
    }
    {
        GameState& state = debugTestState(resultArchitecture);
        state.startRun(); state.jackIn();
        EncounterDice encounterDice(10, 1, 10, 1, 1, 1);
        NetRules encounterRules(encounterDice);
        debugMoveForward(state, encounterRules);
        const uint16_t TracejackalId = state.activeBlackIceRuntimeId(0);
        const uint16_t GhostpulseId = state.activeBlackIceRuntimeId(1);
        state.endPlayerTurn();
        SequenceDice phaseDice(10, 1, 10, 1, 4);
        NetRules phaseRules(phaseDice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(phaseRules, phase);
        hitHitResultsOk = phase.attackCount == 2 && phase.attacks[0].runtimeId == TracejackalId &&
            phase.attacks[1].runtimeId == GhostpulseId && phase.attacks[0].result.success &&
            phase.attacks[1].result.success && phase.attacks[0].result.targetRemainingHp == 36 &&
            phase.attacks[1].result.targetRemainingHp == 32 && state.runner().hp() == 32;
    }

    bool chaseOk = false;
    {
        GameState& state = debugTestState(architecture);
        state.startRun(); state.jackIn();
        FixedDice dice(1); NetRules rules(dice);
        debugMoveForward(state, rules);
        debugMoveBackward(state, rules);
        chaseOk = state.engagedBlackIceCount() == 2 &&
            state.activeBlackIceChasePosition(0) == 0;
        BlackIceInstance* target = state.engagedBlackIceAt(0);
        if (target != nullptr) { target->stopPursuing(); state.recordBlackIcePosition(*target); }
        chaseOk = chaseOk && state.engagedBlackIceCount() == 1 && state.activeBlackIceCount() == 2;
        state.startRun();
        chaseOk = chaseOk && state.activeBlackIceCount() == 0;
    }

    bool reEncounterOk = false;
    bool targetValidityOk = false;
    {
        GameState& state = debugTestState(architecture);
        state.startRun(); state.jackIn();
        FixedDice initialDice(1); NetRules initialRules(initialDice);
        EncounterResult& initial = debugEncounterResult();
        state.moveForward(initialRules, initial);
        BlackIceInstance* Carrionbyte = state.activeBlackIceAt(0);
        BlackIceInstance* Ghostpulse = state.activeBlackIceAt(1);
        const uint16_t CarrionbyteId = state.activeBlackIceRuntimeId(0);
        const uint16_t GhostpulseId = state.activeBlackIceRuntimeId(1);
        const bool targetCycleOk = state.nextEngagedBlackIceRuntimeId(CarrionbyteId, 1) == GhostpulseId &&
            state.nextEngagedBlackIceRuntimeId(GhostpulseId, -1) == CarrionbyteId;
        CombatDice slideDice(10, 1, 0); NetRules slideRules(slideDice);
        const CombatResult slide = Carrionbyte != nullptr
            ? slideRules.slide(state.runner(), *Carrionbyte) : CombatResult();
        if (Carrionbyte != nullptr && slide.success) state.recordBlackIcePosition(*Carrionbyte);
        const bool slidOnlyCarrionbyte = initial.iceCount == 2 && slide.success &&
            state.engagedBlackIceByRuntimeId(CarrionbyteId) == nullptr &&
            state.engagedBlackIceByRuntimeId(GhostpulseId) == Ghostpulse;
        FixedDice leaveDice(1); NetRules leaveRules(leaveDice);
        const bool leftFloor = debugMoveBackward(state, leaveRules);
        EncounterDice reentryDice(1, 10, 10, 1, 1, 1);
        NetRules reentryRules(reentryDice);
        EncounterResult& reentry = debugEncounterResult();
        state.moveForward(reentryRules, reentry);
        reEncounterOk = slidOnlyCarrionbyte && leftFloor && reentry.triggered && reentry.iceCount == 1 &&
            reentry.ice[0].runtimeId == CarrionbyteId && reentry.ice[0].speedCheck.iceWins &&
            reentry.ice[0].immediateAttack.executed && state.activeBlackIceCount() == 2 &&
            state.activeBlackIceRuntimeId(0) == CarrionbyteId && state.activeBlackIceRuntimeId(1) == GhostpulseId &&
            state.engagedBlackIceByRuntimeId(CarrionbyteId) == Carrionbyte &&
            state.engagedBlackIceByRuntimeId(GhostpulseId) == Ghostpulse;
        if (Ghostpulse != nullptr) Ghostpulse->takeRezDamage(Ghostpulse->maxRez());
        targetValidityOk = targetCycleOk && state.engagedBlackIceByRuntimeId(CarrionbyteId) == Carrionbyte &&
            state.engagedBlackIceByRuntimeId(GhostpulseId) == nullptr &&
            state.nextEngagedBlackIceRuntimeId(GhostpulseId, 1) == CarrionbyteId;
        if (Carrionbyte != nullptr) Carrionbyte->takeRezDamage(Carrionbyte->maxRez());
        targetValidityOk = targetValidityOk && state.engagedBlackIceCount() == 0 &&
            state.engagedBlackIceByRuntimeId(CarrionbyteId) == nullptr;
    }

    const bool resultsOk = hitMissResultsOk && missHitResultsOk && hitHitResultsOk;
    const bool valid = encounterOk && duplicateIsolationOk && targetOk && icePhaseOk && resultsOk && chaseOk &&
        reEncounterOk && targetValidityOk;
    Serial.printf("[MultiIce] %s | Encounter=%s | Identity=%s | Targets=%s | Phase=%s | Results=%s | Chase=%s | ReEntry=%s | TargetState=%s\n",
        valid ? "PASS" : "FAIL", encounterOk ? "PASS" : "FAIL",
        duplicateIsolationOk ? "PASS" : "FAIL", targetOk ? "PASS" : "FAIL",
        icePhaseOk ? "PASS" : "FAIL", resultsOk ? "PASS" : "FAIL", chaseOk ? "PASS" : "FAIL",
        reEncounterOk ? "PASS" : "FAIL", targetValidityOk ? "PASS" : "FAIL");
}

void App::runIceRosterDebugTest()
{
    static FloorDefinition rosterFloors[] = {
        {1, FloorType::Empty, 0, SecurityTier::Low, "ENTRY",
         nullptr, nullptr, 0, BlackIceType::Ice01, "ENTRY", nullptr, nullptr},
        {2, FloorType::BlackICE, 0, SecurityTier::Low, "ROSTER",
         nullptr, nullptr, 0, BlackIceType::Ice02, "ROSTER ICE", nullptr, nullptr}
    };
    static bool rosterConfigured = false;
    if (!rosterConfigured)
    {
        rosterFloors[1].blackIceCount = 2;
        rosterFloors[1].blackIceTypes[0] = BlackIceType::Ice02;
        rosterFloors[1].blackIceTypes[1] = BlackIceType::Ice05;
        rosterConfigured = true;
    }
    static const ArchitectureDefinition rosterArchitecture = {
        "ice_roster_test", "ICE ROSTER TEST", nullptr, rosterFloors, 2};
    static const FloorDefinition passwordFloors[] = {
        {1, FloorType::Password, 6, SecurityTier::Low, "PASSWORD",
         nullptr, nullptr, 0, BlackIceType::Ice01, "PASSWORD", nullptr, nullptr},
        {2, FloorType::BlackICE, 0, SecurityTier::Low, "Carrionbyte",
         nullptr, nullptr, 0, BlackIceType::Ice02, "BLACK ICE", nullptr, nullptr}
    };
    static const ArchitectureDefinition passwordArchitecture = {
        "ice_password_test", "ICE PASSWORD TEST", nullptr, passwordFloors, 2};

    bool rosterOk = false;
    bool inactiveSafeOk = false;
    {
        GameState& state = debugTestState(rosterArchitecture);
        state.startRun(); state.jackIn();
        FixedDice encounterDice(1); NetRules encounterRules(encounterDice);
        debugMoveForward(state, encounterRules);
        BlackIceInstance* Carrionbyte = state.activeBlackIceAt(0);
        BlackIceInstance* Ghostpulse = state.activeBlackIceAt(1);
        const uint16_t CarrionbyteId = state.activeBlackIceRuntimeId(0);
        const uint16_t GhostpulseId = state.activeBlackIceRuntimeId(1);
        CombatDice slideDice(10, 1, 0); NetRules slideRules(slideDice);
        const CombatResult slide = slideRules.slide(state.runner(), *Carrionbyte);
        if (slide.success) state.recordBlackIcePosition(*Carrionbyte);
        const int GhostpulseRez = Ghostpulse->currentRez();
        CombatDice zapDice(10, 1, 1); NetRules zapRules(zapDice);
        const CombatResult invalidZap = zapRules.zap(state.runner(), *Carrionbyte);
        rosterOk = slide.success && state.floorBlackIceCount() == 2 &&
            state.engagedBlackIceCount() == 1 && state.floorBlackIceByRuntimeId(CarrionbyteId) == Carrionbyte &&
            state.floorBlackIceByRuntimeId(GhostpulseId) == Ghostpulse && !Carrionbyte->pursuing() &&
            Ghostpulse->pursuing() && state.nextFloorBlackIceRuntimeId(CarrionbyteId, 1) == GhostpulseId &&
            state.nextFloorBlackIceRuntimeId(GhostpulseId, -1) == CarrionbyteId;
        inactiveSafeOk = !invalidZap.executed && Carrionbyte->currentRez() == Carrionbyte->maxRez() &&
            Ghostpulse->currentRez() == GhostpulseRez;
        Ghostpulse->takeRezDamage(Ghostpulse->maxRez());
        rosterOk = rosterOk && state.floorBlackIceCount() == 2 &&
            state.floorBlackIceByRuntimeId(GhostpulseId) == Ghostpulse && !Ghostpulse->active() &&
            state.engagedBlackIceByRuntimeId(GhostpulseId) == nullptr;
    }

    bool floorPriorityOk = false;
    bool fallbackOk = false;
    {
        GameState& state = debugTestState(passwordArchitecture);
        state.startRun(); state.jackIn();
        Floor* password = state.architecture().currentFloor();
        password->resolved = true;
        FixedDice dice(1); NetRules rules(dice);
        debugMoveForward(state, rules);
        debugMoveBackward(state, rules);
        floorPriorityOk = state.architecture().currentFloor()->type == FloorType::Password &&
            state.floorBlackIceCount() == 1 && state.engagedBlackIceCount() == 1 &&
            state.activeBlackIceChasePosition(0) == state.architecture().currentPosition();

        GameState& fallbackState = debugTestState(passwordArchitecture);
        fallbackState.startRun(); fallbackState.jackIn();
        fallbackOk = fallbackState.floorBlackIceCount() == 0 &&
            fallbackState.architecture().currentFloor()->type == FloorType::Password;
    }

    const bool valid = rosterOk && inactiveSafeOk && floorPriorityOk && fallbackOk;
    Serial.printf("[IceRoster] %s | Slide=%s | SafeCombat=%s | FloorPriority=%s | Fallback=%s\n",
        valid ? "PASS" : "FAIL", rosterOk ? "PASS" : "FAIL",
        inactiveSafeOk ? "PASS" : "FAIL", floorPriorityOk ? "PASS" : "FAIL",
        fallbackOk ? "PASS" : "FAIL");
}

void App::runDeadlockStatusDebugTest()
{
    const BlackIceDefinition& deadlock = ice04Definition();
    const bool statsOk = deadlock.type == BlackIceType::Ice04 && deadlock.perception == 5 &&
        deadlock.speed == 3 && deadlock.attack == 7 && deadlock.defense == 5 &&
        deadlock.maxRez == 26 && deadlock.effect.type == BlackIceEffectType::DamageAndNavigationLock &&
        deadlock.effect.damageDice == 2;
    static const FloorDefinition statusFloors[] = {
        {1, FloorType::Empty, 0, SecurityTier::Low, "ENTRY",
         nullptr, nullptr, 0, BlackIceType::Ice01, "ENTRY", nullptr, nullptr},
        {2, FloorType::BlackICE, 0, SecurityTier::Low, "Deadlock",
         nullptr, nullptr, 0, BlackIceType::Ice04, "BLACK ICE", nullptr, nullptr},
        {3, FloorType::File, 6, SecurityTier::Low, "DEEP_FILE",
         "Deadlock_LOCK.DAT", "SECURITY LOG", 250, BlackIceType::Ice01,
         "DATA NODE", nullptr, nullptr}
    };
    static const ArchitectureDefinition statusArchitecture = {
        "Deadlock_status_test", "Deadlock STATUS TEST", nullptr, statusFloors, 3
    };

    bool encounterDurationOk = false;
    bool movementOk = false;
    bool jackOutOk = false;
    bool slideOk = false;
    bool encounterTriggeredOk = false, encounterLockAppliedOk = false;
    bool icePhaseExecutedOk = false, icePhaseHitOk = false, icePhaseLockAppliedOk = false;
    {
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        state.jackIn();
        SequenceDice encounterDice(1, 10, 10, 1, 9);
        NetRules encounterRules(encounterDice);
        EncounterResult& encounter = debugEncounterResult();
        state.moveForward(encounterRules, encounter);
        BlackIceInstance* ice = state.activeBlackIceAt(0);
        const bool immediateLock = encounter.triggered && encounter.speedCheck.iceWins &&
            encounter.immediateAttack.success && encounter.immediateAttack.damage == 9 &&
            encounter.immediateAttack.temporaryRunEffectApplied &&
            encounter.immediateAttack.depthLockApplied &&
            encounter.immediateAttack.safeJackOutLockApplied && state.navigationLockActive();

        CombatDice slideDice(10, 1, 0);
        NetRules slideRules(slideDice);
        const CombatResult slide = slideRules.slide(state.runner(), *ice);
        const bool slideStatePreserved = slide.success && !ice->pursuing() &&
            state.runner().hp() == 31;
        const bool movedShallower = debugMoveBackward(state, slideRules) &&
            state.architecture().currentPosition() == 0;
        const bool returnedToHitDepth = debugMoveForward(state, slideRules) &&
            state.architecture().currentPosition() == 1 && ice->pursuing();
        // Re-entry now correctly re-engages the slid Deadlock; derez it here so this
        // legacy lock-duration test can continue to isolate its original effect.
        if (returnedToHitDepth) ice->takeRezDamage(ice->maxRez());
        const size_t floorCountBeforeJackOut = state.architecture().floorCount();
        const bool rejectedJackOut = !state.jackOut() && state.runState() == RunState::JackedIn &&
            state.architecture().floorCount() == floorCountBeforeJackOut &&
            state.architecture().currentPosition() == 1;
        const bool rejectedDeeper = !debugMoveForward(state, slideRules) &&
            state.architecture().currentPosition() == 1;

        state.endPlayerTurn();
        const bool survivesCurrentTurnEnd = state.navigationLockActive();
        IcePhaseResult& slidPhase = debugIcePhaseResult();
        state.runIcePhase(slideRules, slidPhase);
        const bool activeThroughNextTurn = state.turnNumber() == 2 &&
            state.navigationLockActive() && slidPhase.actionCount == 0;
        state.endPlayerTurn();
        const bool expiredAtNextTurnEnd = !state.navigationLockActive();
        debugRunIcePhase(state, slideRules);
        const bool deeperAfterExpiry = debugMoveForward(state, slideRules);
        const bool jackOutAfterExpiry = state.jackOut() && state.runState() == RunState::JackedOut;
        state.startRun();
        state.jackIn();
        const bool cleanRunReset = !state.navigationLockActive() &&
            debugMoveForward(state, slideRules) && state.architecture().currentPosition() == 1;

        encounterDurationOk = immediateLock && survivesCurrentTurnEnd &&
            activeThroughNextTurn && expiredAtNextTurnEnd;
        encounterTriggeredOk = encounter.triggered;
        encounterLockAppliedOk = immediateLock;
        Serial.printf("[Diag][DeadlockStatus][Duration][Encounter] executed=%u hit=%u lockAfterHit=%u lockNextTurn=%u lockFollowingTurn=%u\n",
            encounter.immediateAttack.executed ? 1U : 0U, encounter.immediateAttack.success ? 1U : 0U,
            immediateLock ? 1U : 0U, activeThroughNextTurn ? 1U : 0U,
            expiredAtNextTurnEnd ? 0U : 1U);
        movementOk = movedShallower && returnedToHitDepth && rejectedDeeper &&
            deeperAfterExpiry;
        jackOutOk = rejectedJackOut && jackOutAfterExpiry && cleanRunReset;
        slideOk = slideStatePreserved;
    }

    bool icePhaseDurationOk = false;
    {
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        state.jackIn();
        // Runner wins the encounter; Deadlock's lock is then applied by the
        // normal Ice-phase hit that this subtest is intended to exercise.
        SequenceDice encounterDice(10, 1);
        NetRules encounterRules(encounterDice);
        EncounterResult& encounter = debugEncounterResult();
        state.moveForward(encounterRules, encounter);
        state.endPlayerTurn();
        CombatDice iceDice(10, 1, 8);
        NetRules iceRules(iceDice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(iceRules, phase);
        const bool nextTurnLocked = encounter.triggered && encounter.speedCheck.runnerWins &&
            phase.actionCount == 1 && phase.attack.success &&
            phase.attack.temporaryRunEffectApplied && state.turnNumber() == 2 &&
            state.navigationLockActive();
        state.endPlayerTurn();
        icePhaseDurationOk = nextTurnLocked && !state.navigationLockActive();
        icePhaseExecutedOk = phase.attack.executed;
        icePhaseHitOk = phase.attack.success;
        icePhaseLockAppliedOk = phase.attack.temporaryRunEffectApplied;
        Serial.printf("[Diag][DeadlockStatus][Duration][IcePhase] executed=%u hit=%u lockAfterHit=%u lockNextTurn=%u lockFollowingTurn=%u\n",
            phase.attack.executed ? 1U : 0U, phase.attack.success ? 1U : 0U,
            phase.attack.temporaryRunEffectApplied ? 1U : 0U, nextTurnLocked ? 1U : 0U,
            state.navigationLockActive() ? 1U : 0U);
    }

    bool refreshOk = false;
    {
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        state.jackIn();
        SequenceDice encounterDice(1, 10, 10, 1, 5);
        NetRules encounterRules(encounterDice);
        debugMoveForward(state, encounterRules);
        state.endPlayerTurn();
        CombatDice firstIceDice(10, 1, 5);
        NetRules firstIceRules(firstIceDice);
        debugRunIcePhase(state, firstIceRules);
        const bool lockedTurn2 = state.turnNumber() == 2 && state.navigationLockActive();
        const bool firstDepthPreserved = debugMoveBackward(state, firstIceRules) &&
            debugMoveForward(state, firstIceRules) && !debugMoveForward(state, firstIceRules) &&
            state.architecture().currentPosition() == 1;
        state.endPlayerTurn();
        CombatDice secondIceDice(10, 1, 5);
        NetRules secondIceRules(secondIceDice);
        IcePhaseResult& refreshed = debugIcePhaseResult();
        state.runIcePhase(secondIceRules, refreshed);
        const bool extendedThroughTurn3 = refreshed.attack.success &&
            refreshed.attack.temporaryRunEffectApplied && state.turnNumber() == 3 &&
            state.navigationLockActive();
        const bool refreshedDepthPreserved = debugMoveBackward(state, secondIceRules) &&
            debugMoveForward(state, secondIceRules) && !debugMoveForward(state, secondIceRules) &&
            state.architecture().currentPosition() == 1;
        state.endPlayerTurn();
        refreshOk = lockedTurn2 && firstDepthPreserved && extendedThroughTurn3 &&
            refreshedDepthPreserved && !state.navigationLockActive();
    }

    bool multiDeadlockGhostpulseOk = false;
    {
        static FloorDefinition mixedFloors[] = {
            {1, FloorType::BlackICE, 0, SecurityTier::Low, "DEADLOCK_GHOSTPULSE", nullptr, nullptr, 0,
             BlackIceType::Ice04, "DEADLOCK GHOSTPULSE", nullptr, nullptr}
        };
        static bool configured = false;
        if (!configured)
        {
            mixedFloors[0].blackIceCount = 2;
            mixedFloors[0].blackIceTypes[0] = BlackIceType::Ice04;
            mixedFloors[0].blackIceTypes[1] = BlackIceType::Ice05;
            configured = true;
        }
        static const ArchitectureDefinition mixedArchitecture = {
            "deadlock_ghostpulse_test", "DEADLOCK GHOSTPULSE", nullptr, mixedFloors, 1};
        GameState& state = debugTestState(mixedArchitecture);
        state.startRun(); state.jackIn(); state.triggerCurrentBlackIce(); state.endPlayerTurn();
        // Deadlock consumes 10/1, then Ghostpulse consumes 10/1: both attacks hit.
        MultiDamageDice dice(10, 1, 10, 1, 12, 4);
        NetRules rules(dice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(rules, phase);
        multiDeadlockGhostpulseOk = phase.attackCount == 2 &&
            phase.attacks[0].result.depthLockApplied &&
            phase.attacks[0].result.safeJackOutLockApplied &&
            phase.attacks[1].result.temporaryRunEffectApplied &&
            state.runner().hp() == 24 && state.runner().remainingNetActions() == 2;
    }

    bool automaticExpiryOk = false;
    {
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        state.jackIn();
        SequenceDice encounterDice(1, 10, 10, 1, 4);
        NetRules rules(encounterDice);
        debugMoveForward(state, rules);
        state.activeBlackIceAt(0)->stopPursuing();
        while (state.runner().spendNetAction()) {}
        const bool automaticEndOne = state.updatePlayerTurn() && state.navigationLockActive();
        debugRunIcePhase(state, rules);
        while (state.runner().spendNetAction()) {}
        const bool automaticEndTwo = state.updatePlayerTurn() && !state.navigationLockActive();
        automaticExpiryOk = automaticEndOne && automaticEndTwo;
    }

    bool derezzPersistenceOk = false;
    {
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        state.jackIn();
        SequenceDice encounterDice(1, 10, 10, 1, 6);
        NetRules rules(encounterDice);
        debugMoveForward(state, rules);
        BlackIceInstance* ice = state.activeBlackIceAt(0);
        ice->takeRezDamage(deadlock.maxRez);
        const bool lockedAfterDerezz = !ice->active() && !ice->pursuing() &&
            state.navigationLockActive();
        state.endPlayerTurn();
        const bool survivesTurnOne = state.navigationLockActive();
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(rules, phase);
        state.endPlayerTurn();
        derezzPersistenceOk = lockedAfterDerezz && survivesTurnOne &&
            phase.actionCount == 0 && !state.navigationLockActive();
    }

    const bool durationOk = encounterDurationOk && icePhaseDurationOk && automaticExpiryOk;
    const bool valid = statsOk && durationOk && movementOk && jackOutOk && refreshOk && slideOk &&
        derezzPersistenceOk && multiDeadlockGhostpulseOk;
    Serial.printf("[Diag][DeadlockStatus][Duration][Flags] encounterTriggeredOk=%u encounterLockAppliedOk=%u encounterDurationOk=%u icePhaseExecutedOk=%u icePhaseHitOk=%u icePhaseLockAppliedOk=%u icePhaseDurationOk=%u\n",
        encounterTriggeredOk ? 1U : 0U, encounterLockAppliedOk ? 1U : 0U,
        encounterDurationOk ? 1U : 0U, icePhaseExecutedOk ? 1U : 0U,
        icePhaseHitOk ? 1U : 0U, icePhaseLockAppliedOk ? 1U : 0U,
        icePhaseDurationOk ? 1U : 0U);
    Serial.printf(
        "[DeadlockStatus] %s | Stats=%s | Duration=%s | Movement=%s | JackOut=%s | Refresh=%s | "
        "Slide=%s | Derezz=%s | Deadlock+Ghostpulse=%s\n",
        valid ? "PASS" : "FAIL", statsOk ? "PASS" : "FAIL", durationOk ? "PASS" : "FAIL",
        movementOk ? "PASS" : "FAIL", jackOutOk ? "PASS" : "FAIL",
        refreshOk ? "PASS" : "FAIL", slideOk ? "PASS" : "FAIL",
        derezzPersistenceOk ? "PASS" : "FAIL", multiDeadlockGhostpulseOk ? "PASS" : "FAIL");
}

void App::runBreacherDebugTest()
{
    const BlackIceDefinition& breacher = ice12Definition();
    const bool statsOk = breacher.type == BlackIceType::Ice12 && breacher.perception == 3 &&
        breacher.speed == 3 && breacher.attack == 7 && breacher.defense == 5 && breacher.maxRez == 24 &&
        breacher.category == BlackIceClass::AntiPersonnel &&
        breacher.effect.type == BlackIceEffectType::DamageAndUnsafeJackOut && breacher.effect.damageDice == 2;

    bool missOk = false;
    {
        Netrunner runner("Breacher_MISS", 4, 40);
        BlackIceInstance ice(breacher);
        CombatDice dice(1, 10, 12); NetRules rules(dice);
        const CombatResult result = rules.blackIceAttack(ice, runner);
        missOk = result.executed && !result.success && result.damage == 0 &&
            !result.forcedUnsafeJackOut && runner.hp() == 40;
    }

    static FloorDefinition floors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "Breacher", nullptr, nullptr, 0,
         BlackIceType::Ice12, "Breacher", nullptr, nullptr}
    };
    static const ArchitectureDefinition architecture = {
        "Breacher_test", "Breacher TEST", nullptr, floors, 1};
    bool hitOk = false;
    {
        GameState& state = debugTestState(architecture); state.startRun(); state.jackIn();
        state.triggerCurrentBlackIce(); state.endPlayerTurn();
        CombatDice dice(10, 1, 12); NetRules rules(dice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(rules, phase);
        hitOk = phase.runTerminated && phase.attackCount == 1 &&
            phase.attacks[0].result.success && phase.attacks[0].result.damage == 12 &&
            dice.lastD6Count() == 2 && phase.attacks[0].result.forcedUnsafeJackOut &&
            state.runState() == RunState::JackedOut &&
            state.runner().hp() == 28;
    }

    static FloorDefinition chainFloors[] = {
        {1, FloorType::BlackICE, 0, SecurityTier::Low, "CHAIN", nullptr, nullptr, 0,
         BlackIceType::Ice02, "CHAIN", nullptr, nullptr}
    };
    static bool chainConfigured = false;
    if (!chainConfigured)
    {
        chainFloors[0].blackIceCount = 2;
        chainFloors[0].blackIceTypes[0] = BlackIceType::Ice02;
        chainFloors[0].blackIceTypes[1] = BlackIceType::Ice12;
        chainConfigured = true;
    }
    static const ArchitectureDefinition chainArchitecture = {
        "Breacher_chain_test", "Breacher CHAIN", nullptr, chainFloors, 1};
    bool chainOk = false;
    {
        GameState& state = debugTestState(chainArchitecture); state.startRun(); state.jackIn();
        state.triggerCurrentBlackIce();
        BlackIceInstance* carrionbyte = state.activeBlackIceAt(0);
        BlackIceInstance* chainBreacher = state.activeBlackIceAt(1);
        if (carrionbyte != nullptr) { carrionbyte->markEncountered(); carrionbyte->stopPursuing(); }
        if (chainBreacher != nullptr) chainBreacher->markEncountered();
        state.endPlayerTurn();
        // Carrionbyte is not pursuing; Breacher consumes the first D10 pair and must hit.
        SequenceDice dice(10, 1, 10, 1, 4); NetRules rules(dice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(rules, phase);
        const CombatResult breacherHit = phase.attackCount > 0 ? phase.attacks[0].result : CombatResult();
        const CombatResult carrionbyteExit = phase.attackCount > 1 ? phase.attacks[1].result : CombatResult();
        const int expectedHp = 40 - breacherHit.damage;
        const bool hitOk = breacherHit.executed && breacherHit.success && breacherHit.damage == 4 &&
            breacherHit.forcedUnsafeJackOut;
        const bool exitOk = phase.attackCount == 2 && phase.attacks[1].unsafeExitEffect &&
            carrionbyteExit.attackerDefinition == &ice02Definition() && carrionbyteExit.damage == 0 &&
            carrionbyteExit.programEffectApplied;
        const bool terminalOk = phase.runTerminated && state.runState() == RunState::JackedOut &&
            state.runner().hp() == expectedHp;
        chainOk = phase.attackCount == 2 && phase.attacks[0].result.attackerDefinition == &breacher &&
            hitOk && exitOk && terminalOk;
        Serial.printf("[Diag][Breacher][Chain] executed=%u hit=%u damage=%d hp=%u unsafe=%u run=%u runnerDown=%u floor=%u exitEffect=%u | count=%u hitOk=%u exitOk=%u terminalOk=%u\n",
            breacherHit.executed ? 1U : 0U, breacherHit.success ? 1U : 0U, breacherHit.damage,
            state.runner().hp(), breacherHit.forcedUnsafeJackOut ? 1U : 0U,
            static_cast<unsigned>(state.runState()), state.runState() == RunState::RunnerDown ? 1U : 0U,
            static_cast<unsigned>(state.architecture().currentPosition()),
            phase.attackCount > 1 && phase.attacks[1].unsafeExitEffect ? 1U : 0U,
            static_cast<unsigned>(phase.attackCount), hitOk ? 1U : 0U,
            exitOk ? 1U : 0U, terminalOk ? 1U : 0U);
    }

    bool derezzedExcludedOk = false;
    {
        GameState& state = debugTestState(chainArchitecture); state.startRun(); state.jackIn();
        state.triggerCurrentBlackIce();
        BlackIceInstance* carrionbyte = state.activeBlackIceAt(0);
        BlackIceInstance* chainBreacher = state.activeBlackIceAt(1);
        if (carrionbyte != nullptr) { carrionbyte->markEncountered(); carrionbyte->takeRezDamage(99); }
        if (chainBreacher != nullptr) chainBreacher->markEncountered();
        state.endPlayerTurn();
        CombatDice dice(10, 1, 4); NetRules rules(dice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(rules, phase);
        derezzedExcludedOk = phase.runTerminated && phase.attackCount == 1 &&
            phase.attacks[0].result.attackerDefinition == &breacher;
    }

    const bool valid = statsOk && missOk && hitOk && chainOk && derezzedExcludedOk;
    Serial.printf("[Breacher] %s | Stats=%s | Miss=%s | Hit=%s | Chain=%s | Derezzed=%s\n",
        valid ? "PASS" : "FAIL", statsOk ? "PASS" : "FAIL", missOk ? "PASS" : "FAIL",
        hitOk ? "PASS" : "FAIL", chainOk ? "PASS" : "FAIL",
        derezzedExcludedOk ? "PASS" : "FAIL");
}

void App::runGhostpulseStatusDebugTest()
{
    const BlackIceDefinition& ghostpulse = ice05Definition();
    const bool statsOk = ghostpulse.type == BlackIceType::Ice05 && ghostpulse.perception == 5 &&
        ghostpulse.speed == 5 && ghostpulse.attack == 4 && ghostpulse.defense == 3 &&
        ghostpulse.maxRez == 14 &&
        ghostpulse.effect.type == BlackIceEffectType::DamageAndNextTurnNetActionPenalty &&
        ghostpulse.effect.damageDice == 1 && ghostpulse.effect.netActionPenalty == 1 &&
        ghostpulse.effect.minimumNetActions == 2;
    static const FloorDefinition statusFloors[] = {
        {1, FloorType::Empty, 0, SecurityTier::Low, "ENTRY",
         nullptr, nullptr, 0, BlackIceType::Ice01, "ENTRY", nullptr, nullptr},
        {2, FloorType::BlackICE, 0, SecurityTier::Low, "Ghostpulse-1",
         nullptr, nullptr, 0, BlackIceType::Ice05, "BLACK ICE", nullptr, nullptr},
        {3, FloorType::BlackICE, 0, SecurityTier::Low, "Ghostpulse-2",
         nullptr, nullptr, 0, BlackIceType::Ice05, "BLACK ICE", nullptr, nullptr},
        // Keep the second Ghostpulse encounter away from Architecture completion:
        // completing the final floor deliberately clears temporary run state.
        {4, FloorType::Empty, 0, SecurityTier::Low, "EXIT",
         nullptr, nullptr, 0, BlackIceType::Ice01, "EXIT", nullptr, nullptr}
    };
    static const ArchitectureDefinition statusArchitecture = {
        "Ghostpulse_status_test", "Ghostpulse STATUS TEST", nullptr, statusFloors, 4
    };

    const bool actionCalculationOk =
        Netrunner::netActionsForInterfaceRank(2) == 2 &&
        Netrunner::netActionsForInterfaceRank(4) == 3 &&
        Netrunner::netActionsForInterfaceRank(8) == 4 &&
        Netrunner::netActionsForInterfaceRank(10) == 5 &&
        Netrunner::netActionsAfterPenalty(2, 1, 2) == 2 &&
        Netrunner::netActionsAfterPenalty(3, 1, 2) == 2 &&
        Netrunner::netActionsAfterPenalty(4, 1, 2) == 3 &&
        Netrunner::netActionsAfterPenalty(4, 2, 2) == 2 &&
        Netrunner::netActionsAfterPenalty(5, 1, 2) == 4 &&
        Netrunner::netActionsAfterPenalty(5, 2, 2) == 3 &&
        Netrunner::netActionsAfterPenalty(5, 3, 2) == 2 &&
        Netrunner::netActionsAfterPenalty(5, 10, 2) == 2;

    bool icePhaseTimingOk = false;
    {
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        state.jackIn();
        SequenceDice encounterDice(1, 10, 10, 1, 4);
        NetRules encounterRules(encounterDice);
        EncounterResult& encounter = debugEncounterResult();
        const uint8_t actionsBeforeHit = state.runner().remainingNetActions();
        state.moveForward(encounterRules, encounter);
        const CombatResult encounterAttack = encounter.immediateAttack;
        const bool hitExecuted = encounterAttack.executed;
        const bool hitFlag = encounterAttack.success;
        const TemporaryRunEffect hitEffect = encounterAttack.temporaryRunEffect;
        const uint8_t hitPenalty = encounterAttack.attackerDefinition != nullptr
            ? encounterAttack.attackerDefinition->effect.netActionPenalty : 0;
        const bool hitApplied = encounterAttack.temporaryRunEffectApplied;
        const uint8_t pendingAfterHit = state.scheduledNetActionPenalty(2);
        const uint8_t actionsAfterApply = state.runner().remainingNetActions();
        Serial.printf("[Diag][GhostpulseStatus][Duration][Step1] turn=%u phase=%u actions=%u pending=%u\n",
            static_cast<unsigned>(state.turnNumber()), static_cast<unsigned>(state.turnPhase()),
            static_cast<unsigned>(actionsAfterApply), static_cast<unsigned>(pendingAfterHit));
        state.activeBlackIceAt(0)->stopPursuing();
        state.endPlayerTurn();
        Serial.printf("[Diag][GhostpulseStatus][Duration][Step2] turn=%u phase=%u actions=%u pending=%u\n",
            static_cast<unsigned>(state.turnNumber()), static_cast<unsigned>(state.turnPhase()),
            static_cast<unsigned>(state.runner().remainingNetActions()), static_cast<unsigned>(state.scheduledNetActionPenalty(2)));
        FixedDice turnDice(10);
        NetRules turnRules(turnDice);
        IcePhaseResult& phase = debugIcePhaseResult();
        state.runIcePhase(turnRules, phase);
        const uint8_t actionsNextTurn = state.runner().remainingNetActions();
        const uint8_t pendingAfterNextTurn = state.scheduledNetActionPenalty(2);
        state.endPlayerTurn();
        debugRunIcePhase(state, turnRules);
        const uint8_t actionsFollowingTurn = state.runner().remainingNetActions();
        const uint8_t pendingAfterFollowingTurn = state.scheduledNetActionPenalty(2);
        const bool currentTurnUnchanged = actionsAfterApply == actionsBeforeHit;
        const bool nextTurnReduced = actionsNextTurn == 2;
        const bool followingTurnRecovered = actionsFollowingTurn == 3 && pendingAfterFollowingTurn == 0;
        icePhaseTimingOk = encounter.speedCheck.iceWins && hitExecuted && hitFlag && hitApplied &&
            encounterAttack.attackerDefinition == &ghostpulse &&
            encounterAttack.attackerDefinition->effect.type == BlackIceEffectType::DamageAndNextTurnNetActionPenalty &&
            encounterAttack.damage == 4 && currentTurnUnchanged && pendingAfterHit == 1 &&
            nextTurnReduced && pendingAfterNextTurn == 0 && followingTurnRecovered;
        Serial.printf("[Diag][GhostpulseStatus][Duration] executed=%u hit=%u effectType=%u penalty=%u actionsBeforeHit=%u pendingAfterApply=%u actionsAfterApply=%u actionsNextTurn=%u pendingAfterNextTurn=%u actionsFollowingTurn=%u\n",
            hitExecuted ? 1U : 0U, hitFlag ? 1U : 0U, static_cast<unsigned>(hitEffect),
            static_cast<unsigned>(hitPenalty), static_cast<unsigned>(actionsBeforeHit),
            static_cast<unsigned>(pendingAfterHit), static_cast<unsigned>(actionsAfterApply),
            static_cast<unsigned>(actionsNextTurn), static_cast<unsigned>(pendingAfterNextTurn),
            static_cast<unsigned>(actionsFollowingTurn));
        Serial.printf("[Diag][GhostpulseStatus][Duration][Flags] resultOk=%u pendingAppliedOk=%u currentTurnUnchangedOk=%u nextTurnReducedOk=%u followingTurnRecoveredOk=%u\n",
            (hitExecuted && hitFlag && hitEffect == TemporaryRunEffect::NextTurnNetActionPenalty && hitPenalty == 1) ? 1U : 0U,
            pendingAfterHit == 1 ? 1U : 0U, currentTurnUnchanged ? 1U : 0U,
            nextTurnReduced ? 1U : 0U, followingTurnRecovered ? 1U : 0U);
    }

    bool encounterTimingOk = false;
    bool persistenceOk = false;
    {
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        state.jackIn();
        const uint8_t actionsBefore = state.runner().remainingNetActions();
        SequenceDice encounterDice(1, 10, 10, 1, 4);
        NetRules encounterRules(encounterDice);
        EncounterResult& encounter = debugEncounterResult();
        state.moveForward(encounterRules, encounter);
        const bool persistenceHitExecuted = encounter.immediateAttack.executed;
        const bool persistenceHit = encounter.immediateAttack.success;
        const TemporaryRunEffect persistenceEffect = encounter.immediateAttack.temporaryRunEffect;
        const uint8_t persistencePenalty = encounter.immediateAttack.attackerDefinition != nullptr
            ? encounter.immediateAttack.attackerDefinition->effect.netActionPenalty : 0;
        const uint8_t persistencePendingAfterHit = state.scheduledNetActionPenalty(2);
        const uint8_t persistenceActionsAfterHit = state.runner().remainingNetActions();
        const bool currentTurnUnchanged = encounter.immediateAttack.success &&
            encounter.immediateAttack.temporaryRunEffectApplied &&
            state.runner().remainingNetActions() == actionsBefore &&
            state.scheduledNetActionPenalty(2) == 1;

        BlackIceInstance* ice = state.activeBlackIceAt(0);
        CombatDice slideDice(10, 1, 0);
        NetRules slideRules(slideDice);
        const CombatResult slide = slideRules.slide(state.runner(), *ice);
        const uint8_t pendingAfterSlide = state.scheduledNetActionPenalty(2);
        ice->takeRezDamage(ghostpulse.maxRez);
        const uint8_t pendingAfterDerezz = state.scheduledNetActionPenalty(2);
        const bool survivesSlideAndDerezz = slide.success && !ice->active() &&
            !ice->pursuing() && state.scheduledNetActionPenalty(2) == 1;

        state.endPlayerTurn();
        debugRunIcePhase(state, slideRules);
        const uint8_t persistenceActionsNextTurn = state.runner().remainingNetActions();
        const uint8_t persistencePendingAfterNextTurn = state.scheduledNetActionPenalty(2);
        const bool affectedTurnTwo = state.turnNumber() == 2 &&
            state.runner().remainingNetActions() == 2 && state.netActionPenaltyThisTurn() == 1;
        state.endPlayerTurn();
        debugRunIcePhase(state, slideRules);
        const uint8_t persistenceActionsFollowingTurn = state.runner().remainingNetActions();
        encounterTimingOk = currentTurnUnchanged && affectedTurnTwo &&
            state.turnNumber() == 3 && state.runner().remainingNetActions() == 3;
        persistenceOk = survivesSlideAndDerezz;
        Serial.printf("[Diag][GhostpulseStatus][Persistence][AfterHit] executed=%u hit=%u effectType=%u penalty=%u pending=%u turn=1 actions=%u\n",
            persistenceHitExecuted ? 1U : 0U, persistenceHit ? 1U : 0U,
            static_cast<unsigned>(persistenceEffect), static_cast<unsigned>(persistencePenalty),
            static_cast<unsigned>(persistencePendingAfterHit), static_cast<unsigned>(persistenceActionsAfterHit));
        Serial.printf("[Diag][GhostpulseStatus][Persistence][AfterSlide] pending=%u\n",
            static_cast<unsigned>(pendingAfterSlide));
        Serial.printf("[Diag][GhostpulseStatus][Persistence][AfterDerezz] pending=%u\n",
            static_cast<unsigned>(pendingAfterDerezz));
        Serial.printf("[Diag][GhostpulseStatus][Persistence] pendingAfterApply=%u pendingAfterSlide=%u pendingAfterDerezz=%u actionsNextTurn=%u actionsFollowingTurn=%u\n",
            static_cast<unsigned>(persistencePendingAfterHit), static_cast<unsigned>(pendingAfterSlide),
            static_cast<unsigned>(pendingAfterDerezz), static_cast<unsigned>(persistenceActionsNextTurn),
            static_cast<unsigned>(persistenceActionsFollowingTurn));
        Serial.printf("[Diag][GhostpulseStatus][Persistence][Flags] hitOk=%u survivesSlideAndDerezz=%u nextTurnReducedOk=%u followingTurnRecoveredOk=%u\n",
            persistenceHit ? 1U : 0U, survivesSlideAndDerezz ? 1U : 0U,
            affectedTurnTwo && persistencePendingAfterNextTurn == 0 ? 1U : 0U,
            persistenceActionsFollowingTurn == 3 ? 1U : 0U);
    }

    bool repeatedOk = false;
    {
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        state.jackIn();
        SequenceDice encounterDice(1, 10, 10, 1, 5);
        NetRules encounterRules(encounterDice);
        Serial.printf("[Diag][GhostpulseStatus][Repeated][FirstPre] run=%u phase=%u turn=%u iceActive=0 icePursuing=0 iceRezzed=0 currentFloor=%u actions=%u attackAvailable=1\n",
            state.runState() == RunState::JackedIn ? 1U : 0U, static_cast<unsigned>(state.turnPhase()),
            static_cast<unsigned>(state.turnNumber()), static_cast<unsigned>(state.architecture().currentPosition()),
            static_cast<unsigned>(state.runner().remainingNetActions()));
        EncounterResult& firstEncounter = debugEncounterResult();
        state.moveForward(encounterRules, firstEncounter);
        const bool firstExecuted = firstEncounter.immediateAttack.executed;
        const bool firstHit = firstEncounter.immediateAttack.success;
        const TemporaryRunEffect firstEffect = firstEncounter.immediateAttack.temporaryRunEffect;
        const uint8_t firstPenalty = firstEncounter.immediateAttack.attackerDefinition != nullptr
            ? firstEncounter.immediateAttack.attackerDefinition->effect.netActionPenalty : 0;
        const bool firstScheduled = state.scheduledNetActionPenalty(2) == 1;
        const uint8_t pendingAfterFirstApply = state.scheduledNetActionPenalty(2);
        const BlackIceInstance* secondPreIce = state.activeBlackIceAt(0);
        Serial.printf("[Diag][GhostpulseStatus][Repeated][SecondPre] run=%u phase=%u turn=%u iceActive=%u icePursuing=%u iceRezzed=%u currentFloor=%u actions=%u attackAvailable=1\n",
            state.runState() == RunState::JackedIn ? 1U : 0U, static_cast<unsigned>(state.turnPhase()),
            static_cast<unsigned>(state.turnNumber()), secondPreIce != nullptr ? 1U : 0U,
            secondPreIce != nullptr && secondPreIce->pursuing() ? 1U : 0U,
            secondPreIce != nullptr && secondPreIce->active() ? 1U : 0U,
            static_cast<unsigned>(state.architecture().currentPosition()),
            static_cast<unsigned>(state.runner().remainingNetActions()));
        EncounterResult& secondEncounter = debugEncounterResult();
        const uint8_t pendingBeforeSecondApply = state.scheduledNetActionPenalty(2);
        SequenceDice secondEncounterDice(1, 10, 10, 1, 5);
        NetRules secondEncounterRules(secondEncounterDice);
        state.moveForward(secondEncounterRules, secondEncounter);
        const CombatResult secondResult = secondEncounter.immediateAttack;
        const bool secondExecuted = secondResult.executed;
        const bool secondHit = secondResult.success;
        const TemporaryRunEffect secondEffect = secondResult.temporaryRunEffect;
        const uint8_t secondPenalty = secondResult.attackerDefinition != nullptr
            ? secondResult.attackerDefinition->effect.netActionPenalty : 0;
        const uint8_t pendingAfterSecondApply = state.scheduledNetActionPenalty(2);
        Serial.printf("[Diag][GhostpulseStatus][Repeated][Second][Result] executed=%u hit=%u effectType=%u penalty=%u pendingBeforeApply=%u\n",
            secondExecuted ? 1U : 0U, secondHit ? 1U : 0U,
            static_cast<unsigned>(secondEffect), static_cast<unsigned>(secondPenalty),
            static_cast<unsigned>(pendingBeforeSecondApply));
        Serial.printf("[Diag][GhostpulseStatus][Repeated][Second][AfterApply] pending=%u\n",
            static_cast<unsigned>(pendingAfterSecondApply));
        state.endPlayerTurn();
        state.activeBlackIceAt(0)->stopPursuing();
        state.activeBlackIceAt(1)->stopPursuing();
        debugRunIcePhase(state, secondEncounterRules);
        const uint8_t nextTurnActions = state.runner().remainingNetActions();
        const uint8_t pendingAfterNextTurn = state.scheduledNetActionPenalty(2);
        Serial.printf("[Diag][GhostpulseStatus][Repeated][Second][AfterPostStep] pending=%u turn=%u phase=%u actions=%u\n",
            static_cast<unsigned>(pendingAfterNextTurn), static_cast<unsigned>(state.turnNumber()),
            static_cast<unsigned>(state.turnPhase()), static_cast<unsigned>(nextTurnActions));
        repeatedOk = firstScheduled && secondExecuted && secondHit && pendingAfterFirstApply == 1 &&
            pendingAfterSecondApply == 1 && nextTurnActions == 2 && pendingAfterNextTurn == 0;
        Serial.printf("[Diag][GhostpulseStatus][Repeated][First] executed=%u hit=%u effectType=%u penalty=%u pendingAfter=%u\n",
            firstExecuted ? 1U : 0U, firstHit ? 1U : 0U, static_cast<unsigned>(firstEffect),
            static_cast<unsigned>(firstPenalty), static_cast<unsigned>(pendingAfterFirstApply));
        Serial.printf("[Diag][GhostpulseStatus][Repeated][Second] executed=%u hit=%u effectType=%u penalty=%u pendingAfter=%u\n",
            secondExecuted ? 1U : 0U, secondHit ? 1U : 0U, static_cast<unsigned>(secondEffect),
            static_cast<unsigned>(secondPenalty), static_cast<unsigned>(pendingAfterSecondApply));
        Serial.printf("[Diag][GhostpulseStatus][Repeated] firstExecuted=%u firstHit=%u firstPenalty=%u pendingAfterFirstApply=%u secondExecuted=%u secondHit=%u secondPenalty=%u pendingAfterSecondApply=%u nextTurnActions=%u pendingAfterNextTurn=%u\n",
            firstExecuted ? 1U : 0U, firstHit ? 1U : 0U,
            static_cast<unsigned>(firstPenalty), static_cast<unsigned>(pendingAfterFirstApply),
            secondExecuted ? 1U : 0U, secondHit ? 1U : 0U, static_cast<unsigned>(secondPenalty),
            static_cast<unsigned>(pendingAfterSecondApply), static_cast<unsigned>(nextTurnActions),
            static_cast<unsigned>(pendingAfterNextTurn));
    }

    bool resetOk = false;
    {
        GameState& state = debugTestState(statusArchitecture);
        state.startRun();
        state.jackIn();
        SequenceDice encounterDice(1, 10, 10, 1, 3);
        NetRules rules(encounterDice);
        EncounterResult& resetEncounter = debugEncounterResult();
        state.moveForward(rules, resetEncounter);
        const bool resetHitExecuted = resetEncounter.immediateAttack.executed;
        const bool resetHit = resetEncounter.immediateAttack.success;
        const uint8_t pendingBeforeFresh = state.scheduledNetActionPenalty(2);
        const bool pending = pendingBeforeFresh == 1;
        const uint8_t pendingAfterApply = state.scheduledNetActionPenalty(2);
        const uint8_t beforeFreshActions = state.runner().remainingNetActions();
        const bool safeJackOut = state.jackOut();
        state.startRun();
        state.jackIn();
        const uint8_t pendingAfterFresh = state.scheduledNetActionPenalty(2);
        const uint8_t actionsAfterFresh = state.runner().remainingNetActions();
        resetOk = pending && safeJackOut && state.scheduledNetActionPenalty(2) == 0 &&
            state.netActionPenaltyThisTurn() == 0 &&
            state.runner().remainingNetActions() == state.runner().maxNetActions();
        Serial.printf("[Diag][GhostpulseStatus][Reset] executed=%u hit=%u pendingAfterApply=%u pendingBeforeFresh=%u pendingAfterFresh=%u actionsBeforeFresh=%u actionsAfterFresh=%u\n",
            resetHitExecuted ? 1U : 0U, resetHit ? 1U : 0U, static_cast<unsigned>(pendingAfterApply),
            static_cast<unsigned>(pendingBeforeFresh), static_cast<unsigned>(pendingAfterFresh),
            static_cast<unsigned>(beforeFreshActions),
            static_cast<unsigned>(actionsAfterFresh));
    }

    const bool durationOk = icePhaseTimingOk && encounterTimingOk;
    const bool valid = statsOk && actionCalculationOk && durationOk && repeatedOk && persistenceOk && resetOk;
    Serial.printf(
        "[GhostpulseStatus] %s | Stats=%s | Actions=%s | Duration=%s | Repeated=%s | Persistence=%s | Reset=%s\n",
        valid ? "PASS" : "FAIL", statsOk ? "PASS" : "FAIL", actionCalculationOk ? "PASS" : "FAIL",
        durationOk ? "PASS" : "FAIL", repeatedOk ? "PASS" : "FAIL",
        persistenceOk ? "PASS" : "FAIL", resetOk ? "PASS" : "FAIL");
}

void App::runProgramDebugTest()
{
    FixedDice fixedDice(10);
    NetRules rules(fixedDice);

    bool inactiveRejected = false;
    bool rezzedAllowed = false;
    bool activateOk = false;
    bool deactivateOk = false;
    bool secondActivationRejected = false;
    bool nextTurnAllowsActivation = false;
    bool recoveryOk = false;
    bool persistenceOk = false;
    bool banhammerOk = false;
    bool armorOk = false;
    bool oncePerRunOk = false;
    bool unsupportedOk = false;

    {
        GameState& lifecycleState = debugTestState();
        lifecycleState.startRun();
        lifecycleState.jackIn();
        lifecycleState.startTurn();
        Program* sword = lifecycleState.cyberdeck().programAt(0);
        if (sword == nullptr)
        {
            Serial.println("[Programs] FAIL | Missing Sword");
            return;
        }

        BlackIceInstance inactiveTarget(ice01Definition());
        CombatDice inactiveAttackDice(10, 1, 18);
        NetRules inactiveAttackRules(inactiveAttackDice);
        inactiveRejected = !inactiveAttackRules.swordAttack(
            lifecycleState.runner(), lifecycleState.cyberdeck(), inactiveTarget).executed;

        const uint8_t beforeActivate = lifecycleState.runner().remainingNetActions();
        const ProgramActionResult activated = rules.activateProgram(lifecycleState.runner(), *sword);
        activateOk = activated.executed && sword->status() == ProgramStatus::Rezzed &&
            sword->activatedThisRound() && sword->rez() == sword->maxRez() &&
            lifecycleState.runner().remainingNetActions() + 1 == beforeActivate;

        BlackIceInstance rezzedTarget(ice01Definition());
        CombatDice rezzedAttackDice(10, 1, 3);
        NetRules rezzedAttackRules(rezzedAttackDice);
        rezzedAllowed = rezzedAttackRules.swordAttack(
            lifecycleState.runner(), lifecycleState.cyberdeck(), rezzedTarget).executed;

        lifecycleState.startTurn();
        const uint8_t beforeDeactivate = lifecycleState.runner().remainingNetActions();
        const ProgramActionResult deactivated = rules.deactivateProgram(
            lifecycleState.runner(), *sword);
        deactivateOk = deactivated.executed && sword->status() == ProgramStatus::Inactive &&
            lifecycleState.runner().remainingNetActions() + 1 == beforeDeactivate;

        sword->setActivatedThisRound(true);
        const ProgramActionResult secondActivation = rules.activateProgram(
            lifecycleState.runner(), *sword);
        secondActivationRejected = !secondActivation.executed &&
            sword->status() == ProgramStatus::Inactive;
        lifecycleState.endTurn();
        const bool newTurnStarted = lifecycleState.startTurn();
        const ProgramActionResult nextTurnActivation = rules.activateProgram(
            lifecycleState.runner(), *sword);
        nextTurnAllowsActivation = newTurnStarted && nextTurnActivation.executed &&
            sword->status() == ProgramStatus::Rezzed;
    }

    {
        GameState& derezzedState = debugTestState();
        derezzedState.startRun();
        derezzedState.jackIn();
        derezzedState.startTurn();
        Program* derezzedSword = derezzedState.cyberdeck().programAt(0);
        derezzedSword->setStatus(ProgramStatus::Derezzed);
        BlackIceInstance derezzedTarget(ice01Definition());
        CombatDice derezzedAttackDice(10, 1, 18);
        NetRules derezzedAttackRules(derezzedAttackDice);
        const bool derezzedRejected = !derezzedAttackRules.swordAttack(
            derezzedState.runner(), derezzedState.cyberdeck(), derezzedTarget).executed;
        const ProgramActionResult derezzedDeactivation = rules.deactivateProgram(
            derezzedState.runner(), *derezzedSword);
        const ProgramActionResult derezzedReactivation = rules.activateProgram(
            derezzedState.runner(), *derezzedSword);
        recoveryOk = derezzedRejected && derezzedDeactivation.executed &&
            derezzedReactivation.executed && derezzedSword->status() == ProgramStatus::Rezzed &&
            derezzedSword->rez() == derezzedSword->maxRez();
    }

    Netrunner destroyedRunner("DESTROYED", 4, 40, 3);
    Program destroyedSword(ProgramType::Attacker, "Sword", 1, 0, 5);
    destroyedSword.setStatus(ProgramStatus::Destroyed);
    const bool destroyedRejected = !rules.activateProgram(
        destroyedRunner, destroyedSword).executed;

    {
        GameState& persistentState = debugTestState();
        persistentState.startRun();
        persistentState.jackIn();
        persistentState.startTurn();
        Program* persistentSword = persistentState.cyberdeck().programAt(0);
        persistentSword->setStatus(ProgramStatus::Rezzed);
        persistentSword->setActivatedThisRound(true);
        persistentState.endTurn();
        persistentState.startTurn();
        persistenceOk = persistentSword->status() == ProgramStatus::Rezzed &&
            !persistentSword->activatedThisRound();
    }

    {
        GameState& banhammerState = debugTestState();
        CyberdeckConfig config = defaultCyberdeckConfig();
        config.programCount = 1;
        config.programs[0] = ProgramId::Banhammer;
        banhammerState.setCyberdeckConfig(config);
        banhammerState.startRun();
        banhammerState.jackIn();
        Program* banhammer = banhammerState.cyberdeck().programAt(0);
        banhammer->setStatus(ProgramStatus::Rezzed);
        BlackIceInstance target(ice01Definition());
        target.setPursuing(true);
        CombatDice banhammerDice(10, 1, 6);
        NetRules banhammerRules(banhammerDice);
        const CombatResult result = banhammerRules.banhammerAttack(
            banhammerState.runner(), banhammerState.cyberdeck(), target);
        banhammerOk = result.executed && result.success && result.damage == 6 &&
            banhammerDice.lastD6Count() == 2;
    }

    {
        GameState& armorState = debugTestState();
        armorState.startRun();
        armorState.jackIn();
        Program* armor = armorState.cyberdeck().programAt(1);
        armor->setStatus(ProgramStatus::Rezzed);
        BlackIceInstance attacker(ice01Definition());
        attacker.setPursuing(true);
        CombatDice armorDice(10, 1, 6);
        NetRules armorRules(armorDice);
        const int hpBefore = armorState.runner().hp();
        const CombatResult hit = armorRules.blackIceAttack(attacker, armorState.runner(),
                                                             &armorState.cyberdeck());
        armorOk = hit.success && hit.damage == 2 && armorState.runner().hp() == hpBefore - 2;

        armorState.startRun();
        armorState.jackIn();
        armor = armorState.cyberdeck().programAt(1);
        const ProgramActionResult first = rules.activateProgram(armorState.runner(), armorState.cyberdeck(), *armor);
        const ProgramActionResult off = rules.deactivateProgram(armorState.runner(), *armor);
        const ProgramActionResult sameRun = rules.activateProgram(armorState.runner(), armorState.cyberdeck(), *armor);
        armorState.startRun();
        armorState.jackIn();
        armor = armorState.cyberdeck().programAt(1);
        const ProgramActionResult nextRun = rules.activateProgram(armorState.runner(), armorState.cyberdeck(), *armor);
        oncePerRunOk = first.executed && off.executed && !sameRun.executed && nextRun.executed;
    }

    // DeckKRASH is now a supported attacker. Keep the safety assertion on a
    // genuinely invalid catalog sentinel rather than a formerly unsupported ID.
    const ProgramDefinition* invalidProgram = programDefinition(ProgramId::Count);
    unsupportedOk = invalidProgram == nullptr && makeProgram(ProgramId::Count).id() == ProgramId::None;
    if (!unsupportedOk) Serial.printf("[Diag][Programs][Unsupported] catalog=%u install=%u\n",
        invalidProgram != nullptr ? 1U : 0U, makeProgram(ProgramId::Count).id() != ProgramId::None ? 1U : 0U);

    const bool valid = inactiveRejected && rezzedAllowed && activateOk && deactivateOk &&
        secondActivationRejected && nextTurnAllowsActivation && recoveryOk &&
        destroyedRejected && persistenceOk && banhammerOk && armorOk && oncePerRunOk && unsupportedOk;
    Serial.printf(
        "[Programs] %s | Inactive=%s | Rezzed=%s | Activate=%s | Deactivate=%s | "
        "RoundLimit=%s | Recovery=%s | Destroyed=%s | Persistent=%s | Banhammer=%s | "
        "Armor=%s | OnceRun=%s | Unsupported=%s\n",
        valid ? "PASS" : "FAIL", inactiveRejected ? "PASS" : "FAIL",
        rezzedAllowed ? "PASS" : "FAIL", activateOk ? "PASS" : "FAIL",
        deactivateOk ? "PASS" : "FAIL",
        secondActivationRejected && nextTurnAllowsActivation ? "PASS" : "FAIL",
        recoveryOk ? "PASS" : "FAIL", destroyedRejected ? "PASS" : "FAIL",
        persistenceOk ? "PASS" : "FAIL", banhammerOk ? "PASS" : "FAIL",
        armorOk ? "PASS" : "FAIL", oncePerRunOk ? "PASS" : "FAIL",
        unsupportedOk ? "PASS" : "FAIL");
}

void App::runTurnOrchestrationDebugTest()
{
    bool runnerWinOk = false;
    bool singleTriggerOk = false;
    bool movementFree = false;
    bool iceWinOk = false;
    bool tieOk = false;
    bool turnOk = false;
    bool slideStateOk = false;
    bool derezzStateOk = false;
    bool automaticEndOk = false;
    bool tieRunActive = false;
    bool tieMoved = false;
    bool tieIceExists = false;
    bool tieIceActive = false;
    bool tieIcePursuing = false;
    bool tieTriggered = false;
    bool tieAttackClear = false;
    size_t tieIceCount = 0;
    uint8_t tieInitialFloor = Architecture::NO_FLOOR;
    uint8_t tieTargetFloor = Architecture::NO_FLOOR;
    int tieExpectedRez = 0;
    int tieActualRez = 0;
    const char* tieIceId = "none";
    TurnPhase tiePhase = TurnPhase::Player;

    {
        GameState& runnerWinState = debugTestState();
        runnerWinState.startRun();
        runnerWinState.jackIn();
        runnerWinState.architecture().currentFloor()->resolved = true;
        SequenceDice runnerWinDice(10, 1);
        NetRules runnerWinRules(runnerWinDice);
        debugMoveForward(runnerWinState, runnerWinRules);
        const uint8_t movementActions = runnerWinState.runner().remainingNetActions();
        EncounterResult& runnerWin = debugEncounterResult();
        runnerWinState.moveForward(runnerWinRules, runnerWin);
        runnerWinOk = runnerWin.triggered && runnerWin.speedCheck.runnerWins &&
            !runnerWin.immediateAttack.executed && runnerWinState.activeBlackIceCount() == 1 &&
            runnerWinState.runner().remainingNetActions() == movementActions;
        debugMoveForward(runnerWinState, runnerWinRules);
        EncounterResult& reentry = debugEncounterResult();
        runnerWinState.moveBackward(runnerWinRules, reentry);
        singleTriggerOk = reentry.moved && !reentry.triggered &&
            runnerWinState.activeBlackIceCount() == 1;
        movementFree = runnerWinState.runner().remainingNetActions() == movementActions;
    }

    {
        GameState& iceWinState = debugTestState();
        iceWinState.startRun();
        iceWinState.jackIn();
        iceWinState.architecture().currentFloor()->resolved = true;
        SequenceDice iceWinDice(1, 10, 10, 1, 6);
        NetRules iceWinRules(iceWinDice);
        debugMoveForward(iceWinState, iceWinRules);
        const int hpBeforeEncounter = iceWinState.runner().hp();
        EncounterResult& iceWin = debugEncounterResult();
        iceWinState.moveForward(iceWinRules, iceWin);
        iceWinOk = iceWin.triggered && iceWin.speedCheck.iceWins &&
            iceWin.immediateAttack.executed && iceWin.immediateAttack.success &&
            iceWinState.runner().hp() == hpBeforeEncounter - 6;
    }

    {
        GameState& tieState = debugTestState();
        tieState.startRun();
        tieState.jackIn();
        tieState.architecture().currentFloor()->resolved = true;
        SequenceDice tieDice(4, 1);
        NetRules tieRules(tieDice);
        tieRunActive = tieState.runState() == RunState::JackedIn;
        tieInitialFloor = static_cast<uint8_t>(tieState.architecture().currentPosition());
        debugMoveForward(tieState, tieRules);
        EncounterResult& tie = debugEncounterResult();
        tieState.moveForward(tieRules, tie);
        const BlackIceInstance* tieIce = tieState.activeBlackIceAt(0);
        tieMoved = tie.moved;
        tieTargetFloor = static_cast<uint8_t>(tieState.architecture().currentPosition());
        tieIceExists = tieIce != nullptr;
        tieIceActive = tieIce != nullptr && tieIce->active();
        tieIcePursuing = tieIce != nullptr && tieIce->pursuing();
        tieTriggered = tie.triggered;
        tieIceCount = tie.iceCount;
        tieAttackClear = !tie.immediateAttack.executed;
        tieExpectedRez = tieIce != nullptr ? tieIce->maxRez() : 0;
        tieActualRez = tieIce != nullptr ? tieIce->currentRez() : 0;
        tieIceId = tieIce != nullptr && tieIce->definition() != nullptr ? tieIce->definition()->stableId : "none";
        tiePhase = tieState.turnPhase();
        tieOk = tieTriggered && tie.speedCheck.tie && tieAttackClear;
    }

    {
        GameState& turnState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
        turnState.startRun();
        turnState.jackIn();
        turnState.architecture().moveForward();
        turnState.architecture().moveForward();
        turnState.triggerCurrentBlackIce();
        Program* persistentSword = turnState.cyberdeck().programAt(0);
        persistentSword->setStatus(ProgramStatus::Rezzed);
        const bool playerStartOk = turnState.turnNumber() == 1 &&
            turnState.turnPhase() == TurnPhase::Player &&
            turnState.runner().remainingNetActions() == turnState.runner().maxNetActions();
        turnState.endPlayerTurn();
        const bool demonPhaseEntered = turnState.turnPhase() == TurnPhase::Demon;
        FixedDice phaseDice(10);
        NetRules phaseRules(phaseDice);
        DemonPhaseResult& demonPhase = debugDemonPhaseResult();
        turnState.runDemonPhase(phaseRules, demonPhase);
        const bool playerIcePhaseSkipped = turnState.turnPhase() == TurnPhase::Enemy;
        EnemyPhaseResult& enemyPhase = debugEnemyPhaseResult();
        turnState.runEnemyPhase(phaseRules, enemyPhase);
        const bool icePhaseEntered = turnState.turnPhase() == TurnPhase::Ice;
        IcePhaseResult& icePhase = debugIcePhaseResult();
        turnState.runIcePhase(phaseRules, icePhase);
        turnOk = playerStartOk && demonPhaseEntered && demonPhase.executed &&
            playerIcePhaseSkipped && enemyPhase.executed && icePhaseEntered && icePhase.executed &&
            icePhase.actionCount == 1 && turnState.turnPhase() == TurnPhase::Player &&
            turnState.turnNumber() == 2 &&
            turnState.runner().remainingNetActions() == turnState.runner().maxNetActions() &&
            persistentSword->status() == ProgramStatus::Rezzed;
    }

    Serial.println("[BootTest] SlideRegression START");
    logStackHighWaterMark("before SlideRegression");
    {
        GameState& slideState = debugTestState();
        slideState.startRun();
        slideState.jackIn();
        slideState.architecture().moveForward();
        slideState.architecture().moveForward();
        slideState.triggerCurrentBlackIce();
        SequenceDice slideDice(10, 1);
        NetRules slideRules(slideDice);
        BlackIceInstance* slidIce = slideState.activeBlackIceAt(0);
        const CombatResult slideResult = slideRules.slide(slideState.runner(), *slidIce);
        EncounterResult& escapeMove = debugEncounterResult();
        slideState.moveBackward(slideRules, escapeMove);
        slideState.cyberdeck().programAt(0)->setStatus(ProgramStatus::Rezzed);
        const bool noEngagedTarget = escapeMove.moved &&
            slideState.architecture().currentFloor()->type == FloorType::File &&
            slideState.engagedBlackIceTarget() == nullptr;
        const bool escapedAttacksRejected =
            !slideRules.zap(slideState.runner(), *slidIce).executed &&
            !slideRules.swordAttack(slideState.runner(), slideState.cyberdeck(), *slidIce).executed;
        slideState.endPlayerTurn();
        IcePhaseResult& slidIcePhase = debugIcePhaseResult();
        slideState.runIcePhase(slideRules, slidIcePhase);
        slideStateOk = slideResult.success && noEngagedTarget &&
            escapedAttacksRejected && slidIcePhase.actionCount == 0;
    }
    Serial.println("[BootTest] SlideRegression END");
    logStackHighWaterMark("after SlideRegression");

    {
        GameState& derezzState = debugTestState();
        derezzState.startRun();
        derezzState.jackIn();
        derezzState.architecture().moveForward();
        derezzState.architecture().moveForward();
        derezzState.triggerCurrentBlackIce();
        derezzState.activeBlackIceAt(0)->takeRezDamage(20);
        derezzState.endPlayerTurn();
        SequenceDice derezzDice(10, 1);
        NetRules derezzRules(derezzDice);
        IcePhaseResult& derezzPhase = debugIcePhaseResult();
        derezzState.runIcePhase(derezzRules, derezzPhase);
        derezzStateOk = derezzPhase.actionCount == 0;
    }

    {
        GameState& emptyActionsState = debugTestState();
        emptyActionsState.startRun();
        emptyActionsState.jackIn();
        while (emptyActionsState.runner().spendNetAction()) {}
        automaticEndOk = emptyActionsState.updatePlayerTurn() &&
            emptyActionsState.turnPhase() == TurnPhase::Enemy;
    }

    const bool encounterOk = runnerWinOk && iceWinOk && tieOk && singleTriggerOk;
    const bool stateOk = slideStateOk && derezzStateOk && automaticEndOk;
    const bool valid = encounterOk && turnOk && stateOk;
    Serial.printf(
        "[TurnFlow] %s | Encounter=%s | SingleTrigger=%s | Turn=%s | "
        "Slide=%s | Derezz=%s | Movement=%s | AutomaticEnd=%s\n",
        valid ? "PASS" : "FAIL", encounterOk ? "PASS" : "FAIL",
        singleTriggerOk ? "PASS" : "FAIL", turnOk ? "PASS" : "FAIL",
        slideStateOk ? "PASS" : "FAIL", derezzStateOk ? "PASS" : "FAIL",
        movementFree ? "PASS" : "FAIL", automaticEndOk ? "PASS" : "FAIL");
    if (!encounterOk) Serial.printf("[Diag][TurnFlow][Encounter] runnerWin=%u iceWin=%u tie=%u singleTrigger=%u | tieRun=%u initialFloor=%u moved=%u targetFloor=%u iceExists=%u iceId=%s active=%u rezzed=%u pursuing=%u triggered=%u resultCount=%u attackClear=%u rez=%d/%d phase=%u\n",
        runnerWinOk ? 1U : 0U, iceWinOk ? 1U : 0U, tieOk ? 1U : 0U, singleTriggerOk ? 1U : 0U,
        tieRunActive ? 1U : 0U, static_cast<unsigned>(tieInitialFloor), tieMoved ? 1U : 0U,
        static_cast<unsigned>(tieTargetFloor), tieIceExists ? 1U : 0U, tieIceId,
        tieIceActive ? 1U : 0U, tieActualRez > 0 ? 1U : 0U, tieIcePursuing ? 1U : 0U,
        tieTriggered ? 1U : 0U, static_cast<unsigned>(tieIceCount), tieAttackClear ? 1U : 0U,
        tieExpectedRez, tieActualRez, static_cast<unsigned>(tiePhase));
}

void App::runPlayerProfileDebugTest()
{
    const RunnerProfile defaults = defaultRunnerProfile();
    RunnerProfile invalidRank = defaults;
    invalidRank.interfaceRank = 0;
    RunnerProfile invalidHandle = defaults;
    invalidHandle.handle[0] = '\0';

    GameState& state = debugTestState();
    RunnerProfile configured = defaults;
    snprintf(configured.handle, sizeof(configured.handle), "TESTRUN");
    configured.interfaceRank = 6;
    configured.maxHp = 55;
    const bool configuredOk = state.setRunnerProfile(configured);
    state.startRun();
    const bool initialRuntimeOk = configuredOk && strcmp(state.runner().handle(), "TESTRUN") == 0 &&
        state.runner().interfaceRank() == 6 && state.runner().hp() == 55 && state.runner().maxHp() == 55;
    state.runner().takeDamage(20);
    state.startRun();
    const bool resetOk = state.runner().hp() == 55 && state.runnerProfile().maxHp == 55;
    const bool defaultValuesOk = strcmp(defaults.handle, "M0u53") == 0 &&
        defaults.interfaceRank == 4 && defaults.maxHp == 35;
    const bool valid = defaultValuesOk && runnerProfileValid(defaults) && !runnerProfileValid(invalidRank) &&
        !runnerProfileValid(invalidHandle) && initialRuntimeOk && resetOk;
    Serial.printf("[PlayerProfile] %s | Default=%s | Validation=%s | Runtime=%s | Reset=%s\n",
        valid ? "PASS" : "FAIL", defaultValuesOk && runnerProfileValid(defaults) ? "PASS" : "FAIL",
        !runnerProfileValid(invalidRank) && !runnerProfileValid(invalidHandle) ? "PASS" : "FAIL",
        initialRuntimeOk ? "PASS" : "FAIL", resetOk ? "PASS" : "FAIL");
}

void App::runDeckConfigDebugTest()
{
    const CyberdeckConfig defaults = defaultCyberdeckConfig();
    CyberdeckConfig overflow = defaults;
    overflow.quality = CyberdeckQuality::Poor;
    overflow.programCount = 6;
    for (uint8_t index = 0; index < overflow.programCount; ++index) overflow.programs[index] = ProgramId::Sword;
    CyberdeckConfig unknown = defaults;
    unknown.programs[0] = static_cast<ProgramId>(255);

    Program twoSlot(ProgramId::None, ProgramType::Attacker, "FUTURE", 0, 0, 1, 2, false, false);
    Program oneSlot(ProgramType::Attacker, "ONE", 0, 0, 1);
    Cyberdeck slotDeck;
    slotDeck.clear(CyberdeckQuality::Poor);
    const bool twoSlotOk = slotDeck.addProgram(twoSlot) && slotDeck.addProgram(twoSlot) &&
        slotDeck.addProgram(oneSlot) && !slotDeck.addProgram(oneSlot) && slotDeck.usedSlots() == 5;

    GameState& state = debugTestState();
    const bool configApplied = state.setCyberdeckConfig(defaults);
    state.startRun();
    Program* sword = state.cyberdeck().programAt(0);
    if (sword != nullptr) sword->setStatus(ProgramStatus::Destroyed);
    state.startRun();
    const Program* freshSword = state.cyberdeck().programAt(0);
    const bool freshRuntime = configApplied && state.cyberdeck().quality() == CyberdeckQuality::Poor &&
        state.cyberdeck().slotCapacity() == 5 && state.cyberdeck().programCount() == 5 &&
        freshSword != nullptr && freshSword->id() == ProgramId::Sword &&
        freshSword->status() == ProgramStatus::Inactive &&
        state.cyberdeck().programAt(1) != nullptr && state.cyberdeck().programAt(1)->id() == ProgramId::Banhammer &&
        state.cyberdeck().programAt(2) != nullptr && state.cyberdeck().programAt(2)->id() == ProgramId::Armor &&
        state.cyberdeck().programAt(3) != nullptr && state.cyberdeck().programAt(3)->id() == ProgramId::Worm &&
        state.cyberdeck().programAt(4) != nullptr && state.cyberdeck().programAt(4)->id() == ProgramId::SpeedyGonzalvez;
    const bool capacities = cyberdeckSlotCapacity(CyberdeckQuality::Poor) == 5 &&
        cyberdeckSlotCapacity(CyberdeckQuality::Standard) == 7 &&
        cyberdeckSlotCapacity(CyberdeckQuality::Excellent) == 9;
    const bool valid = capacities && cyberdeckConfigValid(defaults) && !cyberdeckConfigValid(overflow) &&
        !cyberdeckConfigValid(unknown) && twoSlotOk && freshRuntime;
    Serial.printf("[DeckConfig] %s | Quality=%s | Default=%s | Limit=%s | SlotCost=%s | Runtime=%s\n",
        valid ? "PASS" : "FAIL", capacities ? "PASS" : "FAIL",
        cyberdeckConfigValid(defaults) ? "PASS" : "FAIL",
        !cyberdeckConfigValid(overflow) && !cyberdeckConfigValid(unknown) ? "PASS" : "FAIL",
        twoSlotOk ? "PASS" : "FAIL", freshRuntime ? "PASS" : "FAIL");
}

void App::runDeckHardwareDebugTest()
{
    const bool costs = configuredHardwareSlotCost(HardwareId::BackupDrive) == 2 &&
        configuredHardwareSlotCost(HardwareId::DnaLock) == 2 &&
        configuredHardwareSlotCost(HardwareId::HardenedCircuitry) == 1 &&
        configuredHardwareSlotCost(HardwareId::InsulatedWiring) == 1 &&
        configuredHardwareSlotCost(HardwareId::KrashBarrier) == 2 &&
        configuredHardwareSlotCost(HardwareId::RangeUpgrade) == 1;
    // Hardware capacity is tested with an intentionally empty deck. The
    // product default deck already fills a Poor-quality deck and is not a
    // valid fixture for adding hardware.
    CyberdeckConfig config;
    config.quality = CyberdeckQuality::Poor;
    config.programCount = 0;
    config.hardware[0] = HardwareId::BackupDrive;
    config.hardware[1] = HardwareId::HardenedCircuitry;
    config.hardwareCount = 2;
    CyberdeckConfig overflow = config;
    overflow.quality = CyberdeckQuality::Poor;
    overflow.hardware[2] = HardwareId::KrashBarrier;
    overflow.hardwareCount = 3;
    overflow.hardware[3] = HardwareId::RangeUpgrade;
    overflow.hardwareCount = 4;
    const bool slots = cyberdeckConfigValid(config) && cyberdeckUsedSlots(config) == 3 &&
        !cyberdeckConfigValid(overflow);

    Cyberdeck deck;
    deck.addProgram(makeProgram(ProgramId::Sword));
    deck.programAt(0)->setStatus(ProgramStatus::Rezzed);
    deck.addHardware(HardwareId::BackupDrive);
    const bool saved = deck.destroyProgram(0) && deck.programCount() == 0 && deck.backupCount() == 1 &&
        deck.backupAt(0)->id() == ProgramId::Sword;
    Cyberdeck noBackup;
    noBackup.addProgram(makeProgram(ProgramId::Armor));
    const bool normalDestroy = !noBackup.destroyProgram(0) &&
        noBackup.programAt(0)->status() == ProgramStatus::Destroyed;

    GameState& state = debugTestState();
    const bool applied = state.setCyberdeckConfig(config);
    state.startRun();
    const bool runtime = applied && state.cyberdeck().hasHardware(HardwareId::BackupDrive) &&
        state.cyberdeck().hasHardware(HardwareId::HardenedCircuitry) && state.cyberdeck().backupCount() == 0;
    const bool valid = costs && slots && saved && normalDestroy && runtime;
    if (!valid) Serial.printf("[Diag][DeckHardware] used=%u capacity=%u quality=%u backup=%u hard=%u\n",
        static_cast<unsigned>(cyberdeckUsedSlots(config)), static_cast<unsigned>(cyberdeckSlotCapacity(config.quality)),
        static_cast<unsigned>(config.quality), state.cyberdeck().hasHardware(HardwareId::BackupDrive) ? 1U : 0U,
        state.cyberdeck().hasHardware(HardwareId::HardenedCircuitry) ? 1U : 0U);
    Serial.printf("[DeckHardware] %s | Costs=%s | Slots=%s | Backup=%s | NoBackup=%s | Runtime=%s\n",
        valid ? "PASS" : "FAIL", costs ? "PASS" : "FAIL", slots ? "PASS" : "FAIL",
        saved ? "PASS" : "FAIL", normalDestroy ? "PASS" : "FAIL", runtime ? "PASS" : "FAIL");
}

void App::runBranchingDebugTest()
{
    logBranchingBoundary("Legacy", true); const bool legacyLinear = branchingLegacyTest(); logBranchingBoundary("Legacy", false);
    bool explicitGraph = false, noPath = false;
    logBranchingBoundary("Graph", true); branchingGraphTest(explicitGraph, noPath); logBranchingBoundary("Graph", false);
    logBranchingBoundary("Move", true); const bool movement = branchingMovementVisitTest(); logBranchingBoundary("Move", false);
    logBranchingBoundary("Visits", true); const bool visits = branchingVisitsTest(); logBranchingBoundary("Visits", false);
    logBranchingBoundary("BFS", true); const bool bfs = branchingBfsTest(); logBranchingBoundary("BFS", false);
    logBranchingBoundary("ICE", true); const bool iceIntercept = branchingIceInterceptTest(); logBranchingBoundary("ICE", false);
    logBranchingBoundary("JSON", true); const bool json = branchingJsonValidationTest(); logBranchingBoundary("JSON", false);
    const bool pass = legacyLinear && explicitGraph && noPath && movement && visits && bfs && iceIntercept && json;
    Serial.printf("[Branching] %s | Legacy=%s | Graph=%s | NoPath=%s | Move=%s | Visits=%s | BFS=%s | ICE=%s | JSON=%s\n",
        pass ? "PASS" : "FAIL", legacyLinear ? "PASS" : "FAIL", explicitGraph ? "PASS" : "FAIL",
        noPath ? "PASS" : "FAIL", movement ? "PASS" : "FAIL", visits ? "PASS" : "FAIL",
        bfs ? "PASS" : "FAIL", iceIntercept ? "PASS" : "FAIL", json ? "PASS" : "FAIL");
}

void App::runArchitectureMapDebugTest()
{
    // Keep this view persistent: boot diagnostics run on the constrained loopTask stack.
    static ArchitectureMapView view;
    FixedDice dice(10);
    NetRules rules(dice);
    bool start = false, visit = false, branch = false, branchVisit = false;
    bool deadEnd = false, merge = false, fresh = false, layout = false, playerIce = false, hostile = false, fileCompleted = false;
    bool moveLinear = false, moveBranch = false, stableBranch = false, moveDeadEnd = false, moveStartBranch = false;
    bool mergeNavigation = false, freshOrdering = false;

    GameState& state = debugTestState(BuiltInArchitectures::branchingTestNet());
    state.startRun(); state.jackIn();
    GameUIController::buildArchitectureMapView(state, view);
    start = view.nodeCount == 2 && view.edgeCount == 1 && view.nodes[0].floorIndex == 0 &&
        view.nodes[0].visited && view.nodes[0].currentPlayer && view.nodes[0].floorNumber == 1 &&
        view.nodes[0].status == ArchitectureMapNodeStatus::Completed && view.nodes[1].floorIndex == 1 &&
        !view.nodes[1].visited && view.nodes[1].floorNumber == 0 && view.nodes[1].symbol == '?' &&
        view.nodes[1].status == ArchitectureMapNodeStatus::Unknown;

    EncounterResult& toHub = debugEncounterResult(); state.moveToConnectedFloor(rules, 1, toHub);
    const bool hubMoved = toHub.moved;
    GameUIController::buildArchitectureMapView(state, view);
    bool hubPresent = false, pathAUnknown = false, pathBUnknown = false, deadEndUnknown = false;
    for (size_t index = 0; index < view.nodeCount; ++index)
    {
        const ArchitectureMapNode& node = view.nodes[index];
        if (node.floorIndex == 1) hubPresent = node.visited && node.currentPlayer && node.status == ArchitectureMapNodeStatus::Completed;
        if (node.floorIndex == 2) pathAUnknown = !node.visited && node.symbol == '?';
        if (node.floorIndex == 3) pathBUnknown = !node.visited && node.symbol == '?';
        if (node.floorIndex == 5) deadEndUnknown = !node.visited && node.symbol == '?';
    }
    branch = hubMoved && hubPresent && pathAUnknown && pathBUnknown && deadEndUnknown;

    EncounterResult& toPathA = debugEncounterResult(); state.moveToConnectedFloor(rules, 2, toPathA);
    const bool pathMoved = toPathA.moved;
    GameUIController::buildArchitectureMapView(state, view);
    bool pathAFile = false, pathBStillUnknown = false, mergeUnknown = false;
    for (size_t index = 0; index < view.nodeCount; ++index)
    {
        const ArchitectureMapNode& node = view.nodes[index];
        if (node.floorIndex == 2) pathAFile = node.visited && node.currentPlayer && node.symbol == 'F' &&
            node.floorNumber == 3 && node.status == ArchitectureMapNodeStatus::Actionable;
        if (node.floorIndex == 3) pathBStillUnknown = !node.visited && node.symbol == '?';
        if (node.floorIndex == 4) mergeUnknown = !node.visited && node.symbol == '?';
    }
    visit = pathMoved && pathAFile;
    branchVisit = pathMoved && pathAFile && pathBStillUnknown && mergeUnknown;
    Floor* pathA = state.architecture().floorAt(2);
    if (pathA != nullptr) pathA->downloaded = true;
    GameUIController::buildArchitectureMapView(state, view);
    for (size_t index = 0; index < view.nodeCount; ++index)
        if (view.nodes[index].floorIndex == 2)
            fileCompleted = view.nodes[index].status == ArchitectureMapNodeStatus::Completed;

    EncounterResult& toMerge = debugEncounterResult(); state.moveToConnectedFloor(rules, 4, toMerge);
    GameUIController::buildArchitectureMapView(state, view);
    bool mergeControl = false, exitUnknown = false, mergeLines = false;
    for (size_t index = 0; index < view.nodeCount; ++index)
    {
        const ArchitectureMapNode& node = view.nodes[index];
        if (node.floorIndex == 4) mergeControl = node.visited && node.currentPlayer && node.symbol == 'C' &&
            node.status == ArchitectureMapNodeStatus::Actionable;
        if (node.floorIndex == 6) exitUnknown = !node.visited && node.symbol == '?';
    }
    for (size_t index = 0; index < view.edgeCount; ++index)
    {
        const ArchitectureMapEdge& edge = view.edges[index];
        if (edge.from < view.nodeCount && edge.to < view.nodeCount &&
            ((view.nodes[edge.from].floorIndex == 2 && view.nodes[edge.to].floorIndex == 4) ||
             (view.nodes[edge.from].floorIndex == 4 && view.nodes[edge.to].floorIndex == 2))) mergeLines = true;
    }
    merge = toMerge.moved && mergeControl && exitUnknown && mergeLines;
    layout = view.nodeCount == 7 && view.nodeCount <= MAX_ARCHITECTURE_MAP_NODES &&
        view.edgeCount <= MAX_ARCHITECTURE_MAP_EDGES;
    for (size_t index = 0; index < view.nodeCount; ++index)
    {
        const ArchitectureMapNode& node = view.nodes[index];
        layout = layout && node.x >= 0 && node.y >= 0 && node.y < 135 &&
            (node.visited ? node.floorNumber == node.floorIndex + 1 : (node.symbol == '?' && node.floorNumber == 0 && !node.playerIce));
    }

    GameState& deadEndState = debugTestState(BuiltInArchitectures::branchingTestNet());
    deadEndState.startRun(); deadEndState.jackIn();
    EncounterResult& deadHub = debugEncounterResult(); deadEndState.moveToConnectedFloor(rules, 1, deadHub);
    const bool deadHubMoved = deadHub.moved;
    EncounterResult& deadMove = debugEncounterResult(); deadEndState.moveToConnectedFloor(rules, 5, deadMove);
    GameUIController::buildArchitectureMapView(deadEndState, view);
    for (size_t index = 0; index < view.nodeCount; ++index)
        if (view.nodes[index].floorIndex == 5)
            deadEnd = deadHubMoved && deadMove.moved && view.nodes[index].visited &&
                view.nodes[index].currentPlayer && view.nodes[index].symbol == 'F';

    state.startRun();
    GameUIController::buildArchitectureMapView(state, view);
    fresh = view.nodeCount == 2 && view.nodes[0].visited && view.nodes[0].currentPlayer &&
        view.nodes[0].floorNumber == 1 && !view.nodes[1].visited && view.nodes[1].symbol == '?' && view.nodes[1].floorNumber == 0;

    GameState& iceState = debugTestState(BuiltInArchitectures::branchingTestNet());
    CyberdeckConfig iceConfig; iceConfig.quality = CyberdeckQuality::Excellent;
    iceConfig.playerBlackIce[0] = BlackIceType::Ice01; iceConfig.playerBlackIceCount = 1;
    iceState.setCyberdeckConfig(iceConfig); iceState.startRun(); iceState.jackIn();
    PlayerBlackIceRuntime* ice = iceState.playerBlackIceAt(0);
    if (ice != nullptr) { ice->instance.setRezzed(true); ice->chasePosition = 0; }
    GameUIController::buildArchitectureMapView(iceState, view);
    playerIce = view.hasPlayerIce && view.nodeCount > 0 && view.nodes[0].playerIce &&
        view.nodes[0].status == ArchitectureMapNodeStatus::Completed;

    GameState& hostileState = debugTestState(BuiltInArchitectures::militechTestNet());
    hostileState.startRun(); hostileState.jackIn();
    Floor* entry = hostileState.architecture().currentFloor();
    if (entry != nullptr) entry->resolved = true;
    EncounterResult& hostileFileMove = debugEncounterResult(); hostileState.moveToConnectedFloor(rules, 1, hostileFileMove);
    EncounterResult& hostileIceMove = debugEncounterResult(); hostileState.moveToConnectedFloor(rules, 2, hostileIceMove);
    GameUIController::buildArchitectureMapView(hostileState, view);
    BlackIceInstance* hostileIce = hostileState.activeBlackIceAt(0);
    if (hostileIce != nullptr) hostileIce->setPursuing(true);
    EncounterResult& hostileReturnMove = debugEncounterResult(); hostileState.moveToConnectedFloor(rules, 1, hostileReturnMove);
    GameUIController::buildArchitectureMapView(hostileState, view);
    bool hostileRed = false;
    for (size_t index = 0; index < view.nodeCount; ++index)
        if (view.nodes[index].floorIndex == 1) hostileRed = view.nodes[index].status == ArchitectureMapNodeStatus::Hostile;
    if (hostileIce != nullptr) hostileIce->takeRezDamage(99);
    GameUIController::buildArchitectureMapView(hostileState, view);
    bool hostileCleared = false;
    for (size_t index = 0; index < view.nodeCount; ++index)
        if (view.nodes[index].floorIndex == 1) hostileCleared = view.nodes[index].status == ArchitectureMapNodeStatus::Actionable;
    Floor* hostileFile = hostileState.architecture().floorAt(1);
    if (hostileFile != nullptr) hostileFile->downloaded = true;
    GameUIController::buildArchitectureMapView(hostileState, view);
    bool hostileCompleted = false;
    for (size_t index = 0; index < view.nodeCount; ++index)
        if (view.nodes[index].floorIndex == 1) hostileCompleted = view.nodes[index].status == ArchitectureMapNodeStatus::Completed;
    hostile = hostileFileMove.moved && hostileIceMove.moved && hostileReturnMove.moved && hostileRed && hostileCleared && hostileCompleted;

    GameState& navigationState = debugTestState(BuiltInArchitectures::branchingTestNet());
    navigationState.startRun(); navigationState.jackIn();
    Architecture& architecture = navigationState.architecture();
    char first[40] = {}, second[40] = {}, third[40] = {}, fourth[40] = {};
    architecture.moveTo(1);
    GameUIController::movementChoiceLabelFor(architecture, 0, first, sizeof(first));
    GameUIController::movementChoiceLabelFor(architecture, 1, second, sizeof(second));
    GameUIController::movementChoiceLabelFor(architecture, 2, third, sizeof(third));
    GameUIController::movementChoiceLabelFor(architecture, 3, fourth, sizeof(fourth));
    const bool unknownBranch = !strcmp(first, "PATH 1") && !strcmp(second, "PATH 2") &&
        !strcmp(third, "PATH 3") && !strcmp(fourth, "PREV. FLOOR");
    architecture.moveTo(2);
    GameUIController::movementChoiceLabelFor(architecture, 0, first, sizeof(first));
    GameUIController::movementChoiceLabelFor(architecture, 1, second, sizeof(second));
    moveLinear = !strcmp(first, "NEXT FLOOR") && !strcmp(second, "PREV. FLOOR") &&
        GameUIController::movementChoiceDestinationFor(architecture, 0) == 4 &&
        GameUIController::movementChoiceDestinationFor(architecture, 1) == 1;
    architecture.moveTo(1);
    GameUIController::movementChoiceLabelFor(architecture, 0, first, sizeof(first));
    GameUIController::movementChoiceLabelFor(architecture, 1, second, sizeof(second));
    GameUIController::movementChoiceLabelFor(architecture, 2, third, sizeof(third));
    GameUIController::movementChoiceLabelFor(architecture, 3, fourth, sizeof(fourth));
    moveBranch = !strcmp(first, "P1 > F3 DATA") && !strcmp(second, "PATH 2") && !strcmp(third, "PATH 3") &&
        !strcmp(fourth, "PREV. FLOOR") && GameUIController::movementChoiceDestinationFor(architecture, 0) == 2 &&
        GameUIController::movementChoiceDestinationFor(architecture, 1) == 3 &&
        GameUIController::movementChoiceDestinationFor(architecture, 2) == 5 &&
        GameUIController::movementChoiceDestinationFor(architecture, 3) == 0;
    architecture.moveTo(3); architecture.moveTo(1);
    GameUIController::movementChoiceLabelFor(architecture, 0, first, sizeof(first));
    GameUIController::movementChoiceLabelFor(architecture, 1, second, sizeof(second));
    GameUIController::movementChoiceLabelFor(architecture, 2, third, sizeof(third));
    GameUIController::movementChoiceLabelFor(architecture, 3, fourth, sizeof(fourth));
    stableBranch = unknownBranch && !strcmp(first, "P1 > F3 DATA") && !strcmp(second, "P2 > F4 PASSWORD") && !strcmp(third, "PATH 3") &&
        !strcmp(fourth, "PREV. FLOOR") && GameUIController::movementChoiceDestinationFor(architecture, 0) == 2 &&
        GameUIController::movementChoiceDestinationFor(architecture, 1) == 3 &&
        GameUIController::movementChoiceDestinationFor(architecture, 2) == 5 &&
        GameUIController::movementChoiceDestinationFor(architecture, 3) == 0;
    architecture.moveTo(2); architecture.moveTo(4);
    const bool mergeFromA = architecture.canonicalParent(4) == 2 && architecture.canonicalDepth(4) == 3 &&
        GameUIController::movementChoiceDestinationFor(architecture, 0) == 3 &&
        GameUIController::movementChoiceDestinationFor(architecture, 1) == 6 &&
        GameUIController::movementChoiceDestinationFor(architecture, 2) == 2;
    architecture.moveTo(3); architecture.moveTo(4);
    mergeNavigation = mergeFromA && architecture.canonicalParent(4) == 2 &&
        GameUIController::movementChoiceDestinationFor(architecture, 0) == 3 &&
        GameUIController::movementChoiceDestinationFor(architecture, 1) == 6 &&
        GameUIController::movementChoiceDestinationFor(architecture, 2) == 2;
    architecture.moveTo(3); architecture.moveTo(1); architecture.moveTo(5);
    GameUIController::movementChoiceLabelFor(architecture, 0, first, sizeof(first));
    moveDeadEnd = !strcmp(first, "PREV. FLOOR") && GameUIController::movementChoiceDestinationFor(architecture, 0) == 1;
    static Architecture startBranchArchitecture;
    startBranchArchitecture.initialize("move_start_branch", "MOVE START BRANCH", nullptr);
    startBranchArchitecture.addFloor(Floor(1, FloorType::Empty, 0, "START"));
    startBranchArchitecture.addFloor(Floor(2, FloorType::Empty, 0, "A"));
    startBranchArchitecture.addFloor(Floor(3, FloorType::Empty, 0, "B"));
    startBranchArchitecture.addConnection(0, 1); startBranchArchitecture.addConnection(0, 2);
    GameUIController::movementChoiceLabelFor(startBranchArchitecture, 0, first, sizeof(first));
    GameUIController::movementChoiceLabelFor(startBranchArchitecture, 1, second, sizeof(second));
    moveStartBranch = !strcmp(first, "PATH 1") && !strcmp(second, "PATH 2") &&
        GameUIController::movementChoiceDestinationFor(startBranchArchitecture, 0) == 1 &&
        GameUIController::movementChoiceDestinationFor(startBranchArchitecture, 1) == 2;
    architecture.reset();
    GameUIController::movementChoiceLabelFor(architecture, 0, first, sizeof(first));
    architecture.moveTo(1);
    GameUIController::movementChoiceLabelFor(architecture, 0, first, sizeof(first));
    freshOrdering = !strcmp(first, "PATH 1") && GameUIController::movementChoiceDestinationFor(architecture, 0) == 2;

    const bool pass = start && visit && branch && branchVisit && deadEnd && merge && fresh && layout && playerIce && hostile && fileCompleted &&
        moveLinear && moveBranch && stableBranch && moveDeadEnd && moveStartBranch && mergeNavigation && freshOrdering;
    Serial.printf("[ArchitectureMap] %s | Start=%s | Visit=%s | Branch=%s | Path=%s | DeadEnd=%s | Merge=%s | Fresh=%s | Layout=%s | PlayerICE=%s | Hostile=%s | Complete=%s\n",
        pass ? "PASS" : "FAIL", start ? "PASS" : "FAIL", visit ? "PASS" : "FAIL", branch ? "PASS" : "FAIL",
        branchVisit ? "PASS" : "FAIL", deadEnd ? "PASS" : "FAIL", merge ? "PASS" : "FAIL",
        fresh ? "PASS" : "FAIL", layout ? "PASS" : "FAIL", playerIce ? "PASS" : "FAIL", hostile ? "PASS" : "FAIL",
        fileCompleted ? "PASS" : "FAIL");
    Serial.printf("[MoveOrdering] %s | Linear=%s | Branch=%s | Stable=%s | Merge=%s | DeadEnd=%s | Start=%s | Fresh=%s | Default=PASS\n",
        moveLinear && moveBranch && stableBranch && mergeNavigation && moveDeadEnd && moveStartBranch && freshOrdering ? "PASS" : "FAIL",
        moveLinear ? "PASS" : "FAIL", moveBranch ? "PASS" : "FAIL", stableBranch ? "PASS" : "FAIL",
        mergeNavigation ? "PASS" : "FAIL", moveDeadEnd ? "PASS" : "FAIL", moveStartBranch ? "PASS" : "FAIL",
        freshOrdering ? "PASS" : "FAIL");
}

void App::runPathfinderDebugTest()
{
    static ArchitectureMapView view;
    FixedDice dice(10);
    NetRules rules(dice);

    GameState& state = debugTestState(BuiltInArchitectures::branchingTestNet());
    state.startRun(); state.jackIn();
    const uint8_t actionsBefore = state.runner().remainingNetActions();
    const PathfinderResult scan = state.pathfinder(rules);
    bool allDiscovered = scan.check.attempted && scan.check.base == 4 && scan.check.roll == 10 &&
        scan.check.total == 14 && state.runner().remainingNetActions() + 1 == actionsBefore;
    for (size_t index = 0; index < state.architecture().floorCount(); ++index)
    {
        const Floor* floor = state.architecture().floorAt(index);
        allDiscovered = allDiscovered && floor != nullptr && floor->discovered;
    }
    GameUIController::buildArchitectureMapView(state, view);
    bool mapDiscovery = false;
    for (size_t index = 0; index < view.nodeCount; ++index)
        if (view.nodes[index].floorIndex == 2)
            mapDiscovery = view.nodes[index].discovered && !view.nodes[index].visited &&
                view.nodes[index].floorNumber == 3 && view.nodes[index].symbol == 'F' &&
                view.nodes[index].status == ArchitectureMapNodeStatus::Unknown;

    GameState& blockedState = debugTestState(BuiltInArchitectures::branchingTestNet());
    blockedState.startRun(); blockedState.jackIn();
    Floor* strongPassword = blockedState.architecture().floorAt(3);
    if (strongPassword != nullptr) strongPassword->dv = 20;
    const PathfinderResult blocked = blockedState.pathfinder(rules);
    const Floor* merge = blockedState.architecture().floorAt(4);
    const Floor* exit = blockedState.architecture().floorAt(6);
    const bool branchObstruction = blocked.obstructionDetected && strongPassword != nullptr && strongPassword->discovered &&
        merge != nullptr && merge->discovered && exit != nullptr && exit->discovered;
    strongPassword->resolved = true;
    const PathfinderResult resolved = blockedState.pathfinder(rules);
    const bool resolvedPassword = resolved.check.attempted && !resolved.obstructionDetected;

    GameState& currentState = debugTestState(BuiltInArchitectures::branchingTestNet());
    currentState.startRun(); currentState.jackIn();
    EncounterResult& move = debugEncounterResult(); currentState.moveToConnectedFloor(rules, 1, move);
    const PathfinderResult currentScan = currentState.pathfinder(rules);
    const bool currentFloor = move.moved && currentScan.check.attempted &&
        currentState.architecture().floorAt(4)->discovered;

    GameState& noActionState = debugTestState(BuiltInArchitectures::branchingTestNet());
    noActionState.startRun(); noActionState.jackIn();
    while (noActionState.runner().spendNetAction()) {}
    const PathfinderResult noAction = noActionState.pathfinder(rules);
    const bool noActionRejected = !noAction.check.attempted && noAction.discoveredCount == 0;

    state.startRun();
    const Floor* freshStart = state.architecture().floorAt(0);
    const Floor* freshPath = state.architecture().floorAt(2);
    const bool fresh = freshStart != nullptr && freshStart->visited && freshStart->discovered &&
        freshPath != nullptr && !freshPath->visited && !freshPath->discovered;
    const bool pass = allDiscovered && mapDiscovery && branchObstruction && resolvedPassword && currentFloor && noActionRejected && fresh;
    Serial.printf("[Pathfinder] %s | Ability=%s | Branch=%s | Obstruction=%s | Resolved=%s | Map=%s | Current=%s | NoAction=%s | Fresh=%s\n",
        pass ? "PASS" : "FAIL", allDiscovered ? "PASS" : "FAIL", allDiscovered ? "PASS" : "FAIL",
        branchObstruction ? "PASS" : "FAIL", resolvedPassword ? "PASS" : "FAIL", mapDiscovery ? "PASS" : "FAIL",
        currentFloor ? "PASS" : "FAIL", noActionRejected ? "PASS" : "FAIL", fresh ? "PASS" : "FAIL");
}

void App::runCloakDebugTest()
{
    FixedDice dice(5);
    NetRules rules(dice);
    GameState& baseState = debugTestState(BuiltInArchitectures::branchingTestNet());
    baseState.startRun(); baseState.jackIn();
    const NetCheckResult base = baseState.cloak(rules);
    const bool baseCloak = base.attempted && base.total == 9 && baseState.cloakUsed() && baseState.cloakValue() == 9;
    const NetCheckResult replacement = baseState.cloak(rules);
    const bool replacementCloak = replacement.attempted && baseState.cloakValue() == 9;
    baseState.startRun();
    const bool freshCloak = !baseState.cloakUsed() && baseState.cloakValue() == 0;

    // R3.0d gate: a fresh run must clear both presentation-backed runtime
    // states when the previous run had an active Demon and Cloak result.
    GameState& combinedState = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    combinedState.startRun(); combinedState.jackIn();
    const NetCheckResult combinedCloak = combinedState.cloak(rules);
    const bool combinedActive = combinedCloak.attempted && combinedState.cloakUsed() &&
        combinedState.demon().active;
    combinedState.startRun();
    const bool freshCombined = !combinedState.cloakUsed() && combinedState.cloakValue() == 0 &&
        combinedState.demon().active && combinedState.demon().currentRez == combinedState.demon().maxRez();
    baseState.jackIn();
    while (baseState.runner().spendNetAction()) {}
    const NetCheckResult rejectedCloak = baseState.cloak(rules);
    const bool noActionCloak = !rejectedCloak.attempted && rejectedCloak.roll == 0 &&
        !baseState.cloakUsed() && baseState.cloakValue() == 0;

    GameState& eraserState = debugTestState(BuiltInArchitectures::branchingTestNet());
    CyberdeckConfig eraserConfig; eraserConfig.quality = CyberdeckQuality::Excellent;
    eraserConfig.programs[0] = ProgramId::Eraser; eraserConfig.programs[1] = ProgramId::Eraser; eraserConfig.programCount = 2;
    eraserState.setCyberdeckConfig(eraserConfig); eraserState.startRun(); eraserState.jackIn();
    Program* eraserA = eraserState.cyberdeck().programAt(0); Program* eraserB = eraserState.cyberdeck().programAt(1);
    if (eraserA != nullptr) eraserA->setStatus(ProgramStatus::Rezzed);
    const NetCheckResult oneEraser = eraserState.cloak(rules);
    if (eraserB != nullptr) eraserB->setStatus(ProgramStatus::Rezzed);
    const NetCheckResult twoEraser = eraserState.cloak(rules);
    if (eraserB != nullptr) eraserB->setStatus(ProgramStatus::Derezzed);
    const NetCheckResult derezzedEraser = eraserState.cloak(rules);
    const bool eraser = oneEraser.total == 11 && twoEraser.total == 13 && derezzedEraser.total == 11;

    GameState& seeYaState = debugTestState(BuiltInArchitectures::branchingTestNet());
    CyberdeckConfig seeYaConfig; seeYaConfig.quality = CyberdeckQuality::Excellent;
    seeYaConfig.programs[0] = ProgramId::SeeYa; seeYaConfig.programs[1] = ProgramId::SeeYa; seeYaConfig.programCount = 2;
    seeYaState.setCyberdeckConfig(seeYaConfig); seeYaState.startRun(); seeYaState.jackIn();
    Floor* password = seeYaState.architecture().floorAt(3);
    if (password != nullptr) password->dv = 11;
    const PathfinderResult basePathfinder = seeYaState.pathfinder(rules);
    Program* seeYaA = seeYaState.cyberdeck().programAt(0); Program* seeYaB = seeYaState.cyberdeck().programAt(1);
    if (seeYaA != nullptr) seeYaA->setStatus(ProgramStatus::Rezzed);
    const PathfinderResult oneSeeYa = seeYaState.pathfinder(rules);
    if (seeYaB != nullptr) seeYaB->setStatus(ProgramStatus::Rezzed);
    const PathfinderResult twoSeeYa = seeYaState.pathfinder(rules);
    if (seeYaB != nullptr) seeYaB->setStatus(ProgramStatus::Derezzed);
    const bool seeYa = basePathfinder.check.total == 9 && basePathfinder.obstructionDetected &&
        oneSeeYa.check.total == 11 && !oneSeeYa.obstructionDetected && twoSeeYa.check.total == 13 &&
        NetRules::activeBoosterBonus(seeYaState.cyberdeck(), ProgramId::SeeYa) == 2;

    const bool pass = baseCloak && replacementCloak && freshCloak && combinedActive && freshCombined &&
        noActionCloak && eraser && seeYa;
    Serial.printf("[Cloak] %s | Base=%s | Replace=%s | Fresh=%s | Demon+Cloak=%s | FreshBoth=%s | NoAction=%s | Eraser=%s | SeeYa=%s\n",
        pass ? "PASS" : "FAIL", baseCloak ? "PASS" : "FAIL", replacementCloak ? "PASS" : "FAIL",
        freshCloak ? "PASS" : "FAIL", combinedActive ? "PASS" : "FAIL", freshCombined ? "PASS" : "FAIL",
        noActionCloak ? "PASS" : "FAIL", eraser ? "PASS" : "FAIL", seeYa ? "PASS" : "FAIL");
}

struct EnemyRuntimeSubtestResult
{
    bool runtime;
};

static __attribute__((noinline)) EnemyRuntimeSubtestResult runEnemyRuntimeSubtest()
{
    logStackHighWaterMark("[EnemyRuntime] helper START");
    EnemyRuntimeSubtestResult result{};
    logStackHighWaterMark("[EnemyRuntime] before gameState");
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    CyberdeckConfig config;
    config.quality = CyberdeckQuality::Standard;
    config.programs[0] = ProgramId::Hellbolt;
    config.programs[1] = ProgramId::Sword;
    config.programs[2] = ProgramId::Banhammer;
    config.programs[3] = ProgramId::Armor;
    config.programCount = 4;
    state.setCyberdeckConfig(config);
    logStackHighWaterMark("[EnemyRuntime] before startRun");
    state.startRun(); state.jackIn();
    logStackHighWaterMark("[EnemyRuntime] after startRun");
    state.architecture().moveForward();
    logStackHighWaterMark("[EnemyRuntime] after spawn checks");
    EnemyNetrunnerRuntime* enemy = state.floorEnemyNetrunner();
    result.runtime = enemy != nullptr && enemy->definition != nullptr &&
        strcmp(enemy->definition->name, "NULLBYTE") == 0 && enemy->runner.hp() == 30 &&
        enemy->cyberdeck.programCount() == 5 && enemy->available();
    logStackHighWaterMark("[EnemyRuntime] helper END");
    return result;
}

struct EnemyNetPvPTestSummary
{
    bool hit;
    bool program;
};

static __attribute__((noinline)) EnemyNetPvPTestSummary runEnemyNetPvPSubtest()
{
    EnemyNetPvPTestSummary result{};
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    CyberdeckConfig config;
    config.quality = CyberdeckQuality::Standard;
    config.programs[0] = ProgramId::Hellbolt;
    config.programs[1] = ProgramId::Sword;
    config.programs[2] = ProgramId::Banhammer;
    config.programs[3] = ProgramId::Armor;
    config.programCount = 4;
    state.setCyberdeckConfig(config);
    state.startRun(); state.jackIn(); state.architecture().moveForward();
    EnemyNetrunnerRuntime* enemy = state.floorEnemyNetrunner();
    Program* hellbolt = state.cyberdeck().programAt(0);
    if (enemy == nullptr || hellbolt == nullptr) return result;
    hellbolt->setStatus(ProgramStatus::Rezzed);
    CombatDice hitDice(10, 1, 6); NetRules hitRules(hitDice);
    const CombatResult hit = state.attackFloorEnemy(hitRules, *hellbolt);
    result.hit = hit.executed && hit.success && hit.damage == 6 && enemy->runner.hp() == 24 &&
        state.runner().remainingNetActions() == 2;

    Program* sword = state.cyberdeck().programAt(1);
    if (sword == nullptr) return result;
    sword->setStatus(ProgramStatus::Rezzed);
    Program* targetProgram = enemy->cyberdeck.programAt(0);
    if (targetProgram == nullptr) return result;
    targetProgram->setStatus(ProgramStatus::Rezzed);
    CombatDice programDice(10, 1, 10); NetRules programRules(programDice);
    const CombatResult programHit = state.attackFloorEnemyProgram(programRules, *sword, 0);
    result.program = programHit.executed && programHit.success && programHit.damage == 10 &&
        targetProgram->status() == ProgramStatus::Derezzed;
    return result;
}

struct EnemyAntiPersonnelSubtestResult
{
    bool deckKrash = false;
    bool poison = false;
};

static __attribute__((noinline)) EnemyAntiPersonnelSubtestResult runEnemyAntiPersonnelSubtest()
{
    EnemyAntiPersonnelSubtestResult result;
    Netrunner attackerCase1("ATTACKER_1", 4, 35, 3);
    Netrunner targetCase1("TARGET_1", 4, 35, 3);
    Cyberdeck attackerDeckCase1; attackerDeckCase1.clear(CyberdeckQuality::Standard);
    attackerDeckCase1.addProgram(makeProgram(ProgramId::DeckKRASH));
    Program* deckKrashCase1 = attackerDeckCase1.programAt(0);
    Cyberdeck targetDeckCase1; targetDeckCase1.clear(CyberdeckQuality::Standard);
    if (deckKrashCase1 == nullptr) return result;
    deckKrashCase1->setStatus(ProgramStatus::Rezzed);
    const int targetBeforeCase1 = targetCase1.hp();
    const uint8_t actionsBeforeCase1 = attackerCase1.remainingNetActions();
    CombatDice krashDiceCase1(10, 1, 1); NetRules krashRulesCase1(krashDiceCase1);
    const CombatResult case1 = krashRulesCase1.programAttackNetrunner(attackerCase1, attackerDeckCase1,
        *deckKrashCase1, targetCase1, targetDeckCase1);
    const uint8_t actionsAfterCase1 = attackerCase1.remainingNetActions();

    Netrunner attackerCase2("ATTACKER_2", 4, 35, 3);
    Netrunner targetCase2("TARGET_2", 4, 35, 3);
    Cyberdeck attackerDeckCase2; attackerDeckCase2.clear(CyberdeckQuality::Standard);
    attackerDeckCase2.addProgram(makeProgram(ProgramId::DeckKRASH));
    Program* deckKrashCase2 = attackerDeckCase2.programAt(0);
    Cyberdeck targetDeckCase2; targetDeckCase2.clear(CyberdeckQuality::Standard);
    if (deckKrashCase2 == nullptr || !targetDeckCase2.addHardware(HardwareId::KrashBarrier)) return result;
    deckKrashCase2->setStatus(ProgramStatus::Rezzed);
    const int targetBeforeCase2 = targetCase2.hp();
    const uint8_t actionsBeforeCase2 = attackerCase2.remainingNetActions();
    CombatDice krashDiceCase2(10, 1, 1); NetRules krashRulesCase2(krashDiceCase2);
    const CombatResult case2 = krashRulesCase2.programAttackNetrunner(attackerCase2, attackerDeckCase2,
        *deckKrashCase2, targetCase2, targetDeckCase2);
    const uint8_t actionsAfterCase2 = attackerCase2.remainingNetActions();

    Cyberdeck poisonDeck; poisonDeck.clear(CyberdeckQuality::Standard);
    poisonDeck.addProgram(makeProgram(ProgramId::PoisonFlatline));
    Cyberdeck backupDeck; backupDeck.clear(CyberdeckQuality::Standard);
    backupDeck.addProgram(makeProgram(ProgramId::Sword)); backupDeck.addHardware(HardwareId::BackupDrive);
    Program* poisonProgram = poisonDeck.programAt(0);
    if (poisonProgram == nullptr) return result;
    poisonProgram->setStatus(ProgramStatus::Rezzed);
    Netrunner backupTarget("BACKUP", 4, 35, 3);
    Netrunner poisonAttacker("POISON_ATTACKER", 4, 35, 3);
    CombatDice poisonDice(10, 1, 1); NetRules poisonRules(poisonDice);
    const CombatResult poison = poisonRules.programAttackNetrunner(poisonAttacker, poisonDeck,
        *poisonProgram, backupTarget, backupDeck);
    const uint8_t actionsAfterPoison = poisonAttacker.remainingNetActions();
    result.deckKrash = case1.executed && case1.success && case1.forcedUnsafeJackOut && !case1.krashBarrierBlocked &&
        case2.executed && case2.success && !case2.forcedUnsafeJackOut && case2.krashBarrierBlocked;
    result.poison = poison.executed && poison.success && poison.programSavedByBackup &&
        backupDeck.programCount() == 0;
    if (!result.deckKrash)
    {
        Serial.printf("[Diag][DeckKRASH][Case1] executed=%u hit=%u unsafe=%u barrier=%u targetBefore=%d targetAfter=%d actionsBefore=%u actionsAfter=%u roll=%d/%d\n",
            case1.executed, case1.success, case1.forcedUnsafeJackOut, case1.krashBarrierBlocked,
            targetBeforeCase1, case1.targetRemainingHp, actionsBeforeCase1, actionsAfterCase1,
            case1.attackerRoll, case1.defenderRoll);
        Serial.printf("[Diag][DeckKRASH][Case2] executed=%u hit=%u unsafe=%u barrier=%u targetBefore=%d targetAfter=%d actionsBefore=%u actionsAfter=%u roll=%d/%d\n",
            case2.executed, case2.success, case2.forcedUnsafeJackOut, case2.krashBarrierBlocked,
            targetBeforeCase2, case2.targetRemainingHp, actionsBeforeCase2, actionsAfterCase2,
            case2.attackerRoll, case2.defenderRoll);
    }
    if (!result.poison) Serial.printf("[Diag][AntiPersonnel][Poison] exec=%u hit=%u saved=%u destroyed=%u programs=%u actions=%u\n",
        poison.executed, poison.success,
        poison.programSavedByBackup, poison.targetDestroyed, backupDeck.programCount(), actionsAfterPoison);
    return result;
}

static __attribute__((noinline)) bool runEnemyDefenderSubtest()
{
    Netrunner attacker("ATTACKER", 4, 35, 3);
    Cyberdeck attackerDeck; attackerDeck.clear(CyberdeckQuality::Standard);
    attackerDeck.addProgram(makeProgram(ProgramId::Hellbolt));
    Program* bolt = attackerDeck.programAt(0);
    if (bolt == nullptr) return false;
    bolt->setStatus(ProgramStatus::Rezzed);

    Cyberdeck shieldDeck; shieldDeck.clear(CyberdeckQuality::Standard);
    shieldDeck.addProgram(makeProgram(ProgramId::Shield));
    Program* shield = shieldDeck.programAt(0);
    if (shield == nullptr) return false;
    shield->setStatus(ProgramStatus::Rezzed);
    Netrunner shielded("SHIELDED", 4, 35, 3);
    CombatDice shieldDice(10, 1, 6); NetRules shieldRules(shieldDice);
    const CombatResult shieldedHit = shieldRules.programAttackNetrunner(attacker, attackerDeck,
        *bolt, shielded, shieldDeck);

    Cyberdeck insulatedDeck; insulatedDeck.clear(CyberdeckQuality::Standard);
    insulatedDeck.addHardware(HardwareId::InsulatedWiring);
    Netrunner insulated("INSULATED", 4, 35, 3);
    attacker.resetTurn();
    CombatDice insulatedDice(10, 1, 6); NetRules insulatedRules(insulatedDice);
    const CombatResult insulatedHit = insulatedRules.programAttackNetrunner(attacker, attackerDeck,
        *bolt, insulated, insulatedDeck);

    Cyberdeck flakDeck; flakDeck.clear(CyberdeckQuality::Standard);
    flakDeck.addProgram(makeProgram(ProgramId::Flak));
    Program* flak = flakDeck.programAt(0);
    if (flak == nullptr) return false;
    flak->setStatus(ProgramStatus::Rezzed);
    Netrunner flakTarget("FLAK", 4, 35, 3);
    attacker.resetTurn();
    CombatDice flakDice(10, 1, 10); NetRules flakRules(flakDice);
    const CombatResult flakHit = flakRules.programAttackNetrunner(attacker, attackerDeck,
        *bolt, flakTarget, flakDeck);
    return shieldedHit.success && shieldedHit.shieldBlocked && shieldedHit.damage == 0 &&
        shield->status() == ProgramStatus::Derezzed && insulatedHit.success &&
        insulatedHit.fireBlocked && !insulatedHit.fireApplied && flakHit.success &&
        bolt->status() == ProgramStatus::Derezzed;
}

static __attribute__((noinline)) bool runEnemyAISubtest()
{
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    state.startRun(); state.jackIn(); state.architecture().moveForward(); state.endPlayerTurn();
    const TurnPhase phaseBeforeDemon = state.turnPhase();
    CombatDice demonDice(10, 1, 1); NetRules demonRules(demonDice);
    DemonPhaseResult& demonPhase = debugDemonPhaseResult();
    if (state.turnPhase() == TurnPhase::Demon) state.runDemonPhase(demonRules, demonPhase);
    const int hpBeforeEnemy = state.runner().hp();
    CombatDice aiDice(10, 1, 6); NetRules aiRules(aiDice);
    EnemyPhaseResult& phase = debugEnemyPhaseResult();
    state.runEnemyPhase(aiRules, phase);
    const bool pass = phase.executed && phase.actionCount > 0 && state.turnPhase() == TurnPhase::Ice &&
        state.runner().hp() < hpBeforeEnemy;
    if (!pass) Serial.printf("[Diag][EnemyAI] phase=%u demon=%u/%u enemy=%u actions=%u hp=%d->%d next=%u\n",
        static_cast<unsigned>(phaseBeforeDemon), demonPhase.executed, demonPhase.actionCount, phase.executed,
        phase.actionCount, hpBeforeEnemy, state.runner().hp(), static_cast<unsigned>(state.turnPhase()));
    return pass;
}

static __attribute__((noinline)) bool runMixedFloorEntitiesSubtest()
{
    GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    state.startRun(); state.jackIn(); state.architecture().moveForward();
    EnemyNetrunnerRuntime* enemy = state.floorEnemyNetrunner();
    if (enemy == nullptr) return false;
    enemy->runner.takeDamage(30); enemy->markRunnerDown();
    const bool downOk = enemy->runnerDown && !enemy->available();
    state.startRun();
    return downOk && state.enemyNetrunnerCount() == 1 &&
        BuiltInArchitectures::netrunnerCombatTest().floors[1].enemyNetrunner != nullptr;
}

void App::runEnemyNetrunnerDebugTest()
{
    logStackHighWaterMark("EnemyNetrunner Runtime START");
    const EnemyRuntimeSubtestResult runtime = runEnemyRuntimeSubtest();
    const bool runtimeOk = runtime.runtime;

    logStackHighWaterMark("EnemyNetrunner Runtime END");
    logStackHighWaterMark("EnemyNetrunner NetPvP START");
    const EnemyNetPvPTestSummary netPvP = runEnemyNetPvPSubtest();
    const bool pvpOk = netPvP.hit;
    const bool programCombatOk = netPvP.program;

    logStackHighWaterMark("EnemyNetrunner NetPvP END");
    logStackHighWaterMark("EnemyNetrunner AntiPersonnel START");
    const EnemyAntiPersonnelSubtestResult antiPersonnel = runEnemyAntiPersonnelSubtest();
    const bool antiPersonnelOk = antiPersonnel.deckKrash && antiPersonnel.poison;
    logStackHighWaterMark("EnemyNetrunner AntiPersonnel END");
    logStackHighWaterMark("EnemyNetrunner Defender START");
    const bool defenderOk = runEnemyDefenderSubtest();
    logStackHighWaterMark("EnemyNetrunner Defender END");
    logStackHighWaterMark("EnemyNetrunner EnemyAI START");
    const bool aiOk = runEnemyAISubtest();
    logStackHighWaterMark("EnemyNetrunner EnemyAI END");
    logStackHighWaterMark("EnemyNetrunner Mixed START");
    const bool mixedContentOk = runMixedFloorEntitiesSubtest();
    logStackHighWaterMark("EnemyNetrunner Mixed END");

    const bool valid = runtimeOk && pvpOk && programCombatOk && antiPersonnelOk && defenderOk && aiOk && mixedContentOk;
    Serial.printf("[EnemyNetrunner] %s | Runtime=%s | Terminal=%s\n", valid ? "PASS" : "FAIL",
        runtimeOk ? "PASS" : "FAIL", mixedContentOk ? "PASS" : "FAIL");
    Serial.printf("[NetPvP] %s | Hit=%s | Program=%s\n", pvpOk && programCombatOk ? "PASS" : "FAIL",
        pvpOk ? "PASS" : "FAIL", programCombatOk ? "PASS" : "FAIL");
    Serial.printf("[AntiPersonnel] %s | DeckKRASH=%s | Poison=%s\n", antiPersonnelOk ? "PASS" : "FAIL",
        antiPersonnel.deckKrash ? "PASS" : "FAIL", antiPersonnel.poison ? "PASS" : "FAIL");
    Serial.printf("[DefenderPrograms] %s | Shield=%s\n", defenderOk ? "PASS" : "FAIL", defenderOk ? "PASS" : "FAIL");
    Serial.printf("[EnemyAI] %s\n", aiOk ? "PASS" : "FAIL");
    Serial.printf("[MixedFloorEntities] %s\n", mixedContentOk ? "PASS" : "FAIL");
}

void App::runEnemyBehaviorBaselineDebugTest()
{
    // Baseline identity is value-initialized for old aggregate definitions and
    // explicitly assigned by the Schema 1 importer. No GameState lives on stack.
    CyberdeckConfig customDeck;
    customDeck.quality = CyberdeckQuality::Standard;
    customDeck.programCount = 0;
    const EnemyNetrunnerDefinition custom = {
        "behavior_probe", "BEHAVIOR_PROBE", 4, 20, 3, customDeck,
        EnemyAiArchetype::AntiPersonnel, false, EnemyBehaviorId::Baseline};
    EnemyNetrunnerRuntime runtime;
    const bool defaultBehavior = custom.behavior == EnemyBehaviorId::Baseline && runtime.reset(custom, 91, 0) &&
        runtime.definition != nullptr && runtime.definition->behavior == EnemyBehaviorId::Baseline;
    const bool builtins = nullbyteDefinition().behavior == EnemyBehaviorId::Baseline &&
        zerDefinition().behavior == EnemyBehaviorId::Baseline;
    // reset() correctly restores the Enemy's configured action capacity; it
    // does not start an Enemy phase. Verify custom Baseline ownership and an
    // intentionally empty deck without assuming remaining actions are zero.
    const bool customBehavior = runtime.definition != nullptr &&
        runtime.definition->behavior == EnemyBehaviorId::Baseline && runtime.runner.maxNetActions() == 3;
    const bool programs = runtime.cyberdeck.programCount() == 0;

    // Existing deterministic fixtures cover the established remote BFS move
    // and co-located combat execution paths after the decision extraction.
    const bool remoteMove = branchingBfsTest();
    const bool coLocated = runEnemyAISubtest();
    const bool dormant = zerDefinition().stationaryUntilDiscovered &&
        zerDefinition().behavior == EnemyBehaviorId::Baseline;
    const bool noProgram = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, true).type ==
        EnemyDecisionType::None;
    runtime.runner.takeDamage(7);
    runtime.discoveredByPlayer = true;
    const bool reset = runtime.reset(custom, 91, 0) && runtime.runner.hp() == 20 &&
        !runtime.discoveredByPlayer && runtime.cyberdeck.programCount() == 0;
    const bool multiAction = nullbyteDefinition().netActions == 3;
    const bool pass = defaultBehavior && builtins && customBehavior && dormant && remoteMove && coLocated &&
        programs && noProgram && multiAction && reset;
    Serial.printf("[EnemyBehaviorBaseline] %s | Default=%s | Builtin=%s | Custom=%s | Dormant=%s | RemoteMove=%s | CoLocated=%s | Programs=%s | NoProgram=%s | MultiAction=%s | Fresh=%s\n",
        pass ? "PASS" : "FAIL", defaultBehavior ? "PASS" : "FAIL", builtins ? "PASS" : "FAIL",
        customBehavior ? "PASS" : "FAIL", dormant ? "PASS" : "FAIL", remoteMove ? "PASS" : "FAIL",
        coLocated ? "PASS" : "FAIL", programs ? "PASS" : "FAIL", noProgram ? "PASS" : "FAIL",
        multiAction ? "PASS" : "FAIL", reset ? "PASS" : "FAIL");
}

void App::runEnemyBehaviorProfilesDebugTest()
{
    // This suite uses compact runtime values only; it deliberately creates no
    // automatic GameState fixture on the Cardputer task stack.
    CyberdeckConfig shieldHellbolt;
    shieldHellbolt.quality = CyberdeckQuality::Standard;
    shieldHellbolt.programs[0] = ProgramId::Shield;
    shieldHellbolt.programs[1] = ProgramId::Hellbolt;
    shieldHellbolt.programCount = 2;
    const EnemyNetrunnerDefinition baseline = {
        "profile_base", "PROFILE_BASE", 4, 20, 2, shieldHellbolt,
        EnemyAiArchetype::AntiPersonnel, false, EnemyBehaviorId::Baseline};
    const EnemyNetrunnerDefinition defensive = {
        "profile_def", "PROFILE_DEF", 4, 20, 2, shieldHellbolt,
        EnemyAiArchetype::AntiPersonnel, false, EnemyBehaviorId::Defensive};
    const EnemyNetrunnerDefinition sentry = {
        "profile_sentry", "PROFILE_SENTRY", 4, 20, 2, shieldHellbolt,
        EnemyAiArchetype::AntiPersonnel, false, EnemyBehaviorId::Sentry};

    EnemyNetrunnerRuntime runtime;
    const bool defensiveStructure = runtime.reset(defensive, 201, 1) && runtime.definition != nullptr &&
        runtime.definition->behavior == EnemyBehaviorId::Defensive;
    Program* hellbolt = runtime.cyberdeck.programAt(1);
    if (hellbolt != nullptr) hellbolt->setStatus(ProgramStatus::Rezzed);
    runtime.runner.resetTurn(2);
    const EnemyBehaviorDecision defensiveChoice = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, true);
    const bool defensivePriority = defensiveChoice.type == EnemyDecisionType::ActivateProgram &&
        defensiveChoice.program == ProgramId::Shield && defensiveChoice.reason == EnemyDecisionReason::DefensiveProgram;
    Program* shield = runtime.cyberdeck.programAt(0);
    FixedDice dice(10);
    NetRules rules(dice);
    const ProgramActionResult activation = shield != nullptr
        ? rules.activateProgram(runtime.runner, runtime.cyberdeck, *shield) : ProgramActionResult();
    const EnemyBehaviorDecision afterDefensiveActivation = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, true);
    const bool defensiveMultiAction = defensivePriority && activation.executed &&
        runtime.runner.remainingNetActions() == 1 && afterDefensiveActivation.type == EnemyDecisionType::UseProgram &&
        afterDefensiveActivation.program == ProgramId::Hellbolt;

    runtime.reset(baseline, 202, 1);
    hellbolt = runtime.cyberdeck.programAt(1);
    if (hellbolt != nullptr) hellbolt->setStatus(ProgramStatus::Rezzed);
    runtime.runner.resetTurn(2);
    const EnemyBehaviorDecision baselineChoice = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, true);
    const bool differenceCombat = baselineChoice.type == EnemyDecisionType::UseProgram &&
        baselineChoice.program == ProgramId::Hellbolt && defensivePriority;

    CyberdeckConfig hellboltOnly;
    hellboltOnly.quality = CyberdeckQuality::Standard;
    hellboltOnly.programs[0] = ProgramId::Hellbolt;
    hellboltOnly.programCount = 1;
    const EnemyNetrunnerDefinition baselineFallback = {
        "profile_base_fallback", "PROFILE_BASE_FALLBACK", 4, 20, 2, hellboltOnly,
        EnemyAiArchetype::AntiPersonnel, false, EnemyBehaviorId::Baseline};
    const EnemyNetrunnerDefinition defensiveFallback = {
        "profile_def_fallback", "PROFILE_DEF_FALLBACK", 4, 20, 2, hellboltOnly,
        EnemyAiArchetype::AntiPersonnel, false, EnemyBehaviorId::Defensive};
    runtime.reset(baselineFallback, 203, 1);
    runtime.cyberdeck.programAt(0)->setStatus(ProgramStatus::Rezzed);
    runtime.runner.resetTurn(1);
    const EnemyBehaviorDecision fallbackBase = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, true);
    runtime.reset(defensiveFallback, 204, 1);
    runtime.cyberdeck.programAt(0)->setStatus(ProgramStatus::Rezzed);
    runtime.runner.resetTurn(1);
    const EnemyBehaviorDecision fallbackDefensive = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, true);
    const bool defensiveFallbackOk = fallbackBase.type == fallbackDefensive.type &&
        fallbackBase.program == fallbackDefensive.program && fallbackBase.reason == fallbackDefensive.reason;
    const bool defensivePlayerIce = chooseEnemyBehaviorDecision(runtime, ProgramId::None, true, true).type ==
        EnemyDecisionType::SlidePlayerBlackIce;

    runtime.reset(baseline, 205, 0);
    const EnemyBehaviorDecision baselineRemote = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, false);
    runtime.reset(sentry, 206, 0);
    const EnemyBehaviorDecision sentryRemote = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, false);
    const bool sentryRemoteOk = baselineRemote.type == EnemyDecisionType::MoveTowardRunner &&
        sentryRemote.type == EnemyDecisionType::None;
    const bool differenceRemote = sentryRemoteOk;
    hellbolt = runtime.cyberdeck.programAt(1);
    if (hellbolt != nullptr) hellbolt->setStatus(ProgramStatus::Rezzed);
    runtime.runner.resetTurn(1);
    const EnemyBehaviorDecision sentryCoLocated = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, true);
    const bool sentryCoLocatedOk = sentryCoLocated.type == EnemyDecisionType::UseProgram &&
        sentryCoLocated.program == ProgramId::Hellbolt && sentryCoLocated.reason == EnemyDecisionReason::PriorityProgram;
    const EnemyNetrunnerDefinition dormantSentry = {
        "profile_dormant", "PROFILE_DORMANT", 4, 20, 2, hellboltOnly,
        EnemyAiArchetype::AntiPersonnel, true, EnemyBehaviorId::Sentry};
    runtime.reset(dormantSentry, 207, 1);
    const bool sentryDormant = runtime.definition != nullptr && runtime.definition->stationaryUntilDiscovered &&
        !runtime.discoveredByPlayer && runtime.definition->behavior == EnemyBehaviorId::Sentry;
    runtime.discoveredByPlayer = true;
    const bool sentryDiscoveredRemote = chooseEnemyBehaviorDecision(runtime, ProgramId::None, false, false).type ==
        EnemyDecisionType::None;
    runtime.runner.takeDamage(7);
    const bool sentryFresh = runtime.reset(sentry, 208, 3) && runtime.runner.hp() == 20 &&
        runtime.definition != nullptr && runtime.definition->behavior == EnemyBehaviorId::Sentry;

    EnemyNetrunnerRuntime defensiveRuntime;
    EnemyNetrunnerRuntime sentryRuntime;
    const bool isolation = defensiveRuntime.reset(defensive, 209, 0) && sentryRuntime.reset(sentry, 210, 1) &&
        defensiveRuntime.definition != nullptr && sentryRuntime.definition != nullptr &&
        defensiveRuntime.definition->behavior == EnemyBehaviorId::Defensive &&
        sentryRuntime.definition->behavior == EnemyBehaviorId::Sentry;
    EnemyNetrunnerDefinition mutableDefinition = defensive;
    EnemyNetrunnerRuntime lifetimeRuntime;
    const bool lifetimeReset = lifetimeRuntime.reset(mutableDefinition, 211, 0);
    mutableDefinition.behavior = EnemyBehaviorId::Baseline;
    const bool lifetime = lifetimeReset && lifetimeRuntime.definition != nullptr &&
        lifetimeRuntime.definition->behavior == EnemyBehaviorId::Defensive;

    const bool pass = defensiveStructure && defensivePriority && defensiveFallbackOk && defensiveMultiAction &&
        defensivePlayerIce && sentryRemoteOk && sentryCoLocatedOk && sentryDormant && sentryDiscoveredRemote &&
        sentryFresh && differenceCombat && differenceRemote && isolation && lifetime;
    Serial.printf("[EnemyBehaviorProfiles] %s | Defensive=%s | DefensivePriority=%s | DefensiveFallback=%s | DefensiveMultiAction=%s | DefensivePlayerIce=%s | Sentry=%s | SentryRemote=%s | SentryCoLocated=%s | SentryDormant=%s | SentryFresh=%s | Difference=%s | Isolation=%s | Lifetime=%s\n",
        pass ? "PASS" : "FAIL", defensiveStructure ? "PASS" : "FAIL", defensivePriority ? "PASS" : "FAIL",
        defensiveFallbackOk ? "PASS" : "FAIL", defensiveMultiAction ? "PASS" : "FAIL",
        defensivePlayerIce ? "PASS" : "FAIL", (sentryRemoteOk && sentryCoLocatedOk) ? "PASS" : "FAIL",
        sentryRemoteOk ? "PASS" : "FAIL", sentryCoLocatedOk ? "PASS" : "FAIL",
        sentryDormant && sentryDiscoveredRemote ? "PASS" : "FAIL", sentryFresh ? "PASS" : "FAIL",
        differenceCombat && differenceRemote ? "PASS" : "FAIL", isolation ? "PASS" : "FAIL", lifetime ? "PASS" : "FAIL");
}

void App::runEnemyNetrunnerCustomDefinitionDebugTest()
{
    char stableId[] = "test_runner";
    char displayName[] = "TEST_RUNNER";
    CyberdeckConfig loadout;
    loadout.quality = CyberdeckQuality::Standard;
    loadout.programs[0] = ProgramId::Hellbolt;
    loadout.programs[1] = ProgramId::Shield;
    loadout.programs[2] = ProgramId::Superglue;
    loadout.programCount = 3;
    const EnemyNetrunnerDefinition custom = {
        stableId, displayName, 5, 27, 3, loadout, EnemyAiArchetype::AntiPersonnel, false,
        EnemyBehaviorId::Baseline};
    EnemyNetrunnerRuntime runtime;
    const bool reset = runtime.reset(custom, 77, 4);
    stableId[0] = 'X'; displayName[0] = 'X'; loadout.programs[0] = ProgramId::Sword;
    const auto* firstProgram = runtime.cyberdeck.programAt(0);
    const auto* secondProgram = runtime.cyberdeck.programAt(1);
    const auto* thirdProgram = runtime.cyberdeck.programAt(2);
    const bool pass = reset && runtime.available() && runtime.definition != nullptr &&
        strcmp(runtime.definition->id, "test_runner") == 0 &&
        strcmp(runtime.definition->name, "TEST_RUNNER") == 0 &&
        runtime.definition->interfaceRank == 5 && runtime.definition->maxHp == 27 &&
        runtime.definition->netActions == 3 && runtime.runner.hp() == 27 &&
        runtime.runner.maxHp() == 27 && runtime.runner.interfaceRank() == 5 &&
        runtime.runner.maxNetActions() == 3 && runtime.cyberdeck.programCount() == 3 &&
        runtime.definition->deckConfig.programs[0] == ProgramId::Hellbolt &&
        firstProgram != nullptr && firstProgram->id() == ProgramId::Hellbolt &&
        secondProgram != nullptr && secondProgram->id() == ProgramId::Shield &&
        thirdProgram != nullptr && thirdProgram->id() == ProgramId::Superglue;
    Serial.printf("[EnemyCustomDefinition] %s | StableId=%s | Handle=%s | Interface=%u | MaxHP=%u | CurrentHP=%u | Actions=%u | Programs=%u | IdentityIndependent=%s | LifetimeOwned=%s\n",
        pass ? "PASS" : "FAIL", runtime.definition != nullptr ? runtime.definition->id : "(null)",
        runtime.definition != nullptr ? runtime.definition->name : "(null)",
        runtime.definition != nullptr ? runtime.definition->interfaceRank : 0U,
        runtime.definition != nullptr ? runtime.definition->maxHp : 0U,
        runtime.runner.hp(), runtime.definition != nullptr ? runtime.definition->netActions : 0U,
        static_cast<unsigned>(runtime.cyberdeck.programCount()),
        runtime.definition != nullptr && strcmp(runtime.definition->id, "test_runner") == 0 ? "PASS" : "FAIL",
        runtime.definition != nullptr && runtime.definition->deckConfig.programs[0] == ProgramId::Hellbolt ? "PASS" : "FAIL");
}

void App::runEnemyFloorEntryDebugTest()
{
    bool definitionIdentityOk = false;
    bool definitionImmutableOk = false;
    bool runtimeIsolationOk = false;
    bool definitionLifetimeOk = false;
    bool entryOk = false;
    bool stableIdOk = false;
    bool terminalOk = false;
    bool freshRunOk = false;
    bool capacityOk = false;
    bool invalidDefinitionRejected = false;

    {
        const EnemyNetrunnerDefinition* nullbyte = enemyNetrunnerDefinitionById("nullbyte");
        const EnemyNetrunnerDefinition* zer = enemyNetrunnerDefinitionById("zer=0");
        const EnemyNetrunnerDefinition* unknown = enemyNetrunnerDefinitionById("missing");
        definitionIdentityOk = nullbyte == &nullbyteDefinition() && zer == &zerDefinition() &&
            unknown == nullptr && strcmp(nullbyte->name, "NULLBYTE") == 0 &&
            nullbyte->interfaceRank == 4 && nullbyte->maxHp == 30 && nullbyte->netActions == 3 &&
            nullbyte->deckConfig.programCount == 5 &&
            nullbyte->deckConfig.programs[0] == ProgramId::Hellbolt &&
            nullbyte->deckConfig.programs[4] == ProgramId::Sword;

        GameState& state = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
        FixedDice dice(10);
        NetRules rules(dice);
        state.startRun();
        state.jackIn();
        Floor* entry = state.architecture().currentFloor();
        if (entry != nullptr) entry->resolved = true;
        EncounterResult& result = debugEncounterResult();
        state.moveForward(rules, result);
        EnemyNetrunnerRuntime* enemy = state.floorEnemyNetrunner();
        const uint16_t runtimeId = enemy != nullptr ? enemy->runtimeId : 0;
        const EnemyNetrunnerDefinition* retainedDefinition = enemy != nullptr ? enemy->definition : nullptr;
        if (enemy != nullptr)
        {
            enemy->runner.takeDamage(5);
            enemy->currentFloor = 0;
            enemy->discoveredByPlayer = true;
            enemy->onFire = true;
            enemy->cyberdeck.programAt(0)->setStatus(ProgramStatus::Rezzed);
        }
        definitionImmutableOk = retainedDefinition != nullptr &&
            strcmp(retainedDefinition->id, "nullbyte") == 0 && strcmp(retainedDefinition->name, "NULLBYTE") == 0 &&
            retainedDefinition->maxHp == 30 && retainedDefinition->netActions == 3 &&
            retainedDefinition->stationaryUntilDiscovered == false &&
            retainedDefinition->deckConfig.programCount == 5 &&
            retainedDefinition->deckConfig.programs[0] == ProgramId::Hellbolt;

        // The definition checks intentionally mutate this shared static
        // diagnostic runtime. Restore the original continuous encounter
        // state before the legacy Stable/Terminal assertions run.
        if (enemy != nullptr && retainedDefinition != nullptr)
            enemy->reset(*retainedDefinition, runtimeId, 1);
        entryOk = result.moved && state.architecture().currentPosition() == 1 &&
            state.enemyNetrunnerCount() == 1 && enemy != nullptr && enemy->available() &&
            enemy->definition != nullptr && strcmp(enemy->definition->id, "nullbyte") == 0 &&
            enemy->cyberdeck.programCount() == 5;

        EncounterResult& backward = debugEncounterResult();
        state.moveBackward(rules, backward);
        EncounterResult& forward = debugEncounterResult();
        state.moveForward(rules, forward);
        EnemyNetrunnerRuntime* returned = state.floorEnemyNetrunner();
        stableIdOk = backward.moved && forward.moved && returned != nullptr &&
            returned->runtimeId == runtimeId && state.enemyNetrunnerCount() == 1;

        if (returned != nullptr) returned->unsafeJackOut();
        terminalOk = state.floorEnemyNetrunner() == nullptr &&
            state.enemyNetrunnerByRuntimeId(runtimeId) == nullptr;

        state.startRun();
        state.jackIn();
        entry = state.architecture().currentFloor();
        if (entry != nullptr) entry->resolved = true;
        EncounterResult& freshMove = debugEncounterResult();
        state.moveForward(rules, freshMove);
        EnemyNetrunnerRuntime* fresh = state.floorEnemyNetrunner();
        freshRunOk = freshMove.moved && fresh != nullptr && fresh->runtimeId == 1 &&
            fresh->available() && fresh->runner.hp() == fresh->runner.maxHp();
        definitionLifetimeOk = fresh != nullptr && fresh->definition != nullptr &&
            strcmp(fresh->definition->id, "nullbyte") == 0 && strcmp(fresh->definition->name, "NULLBYTE") == 0;
    }

    {
        static FloorDefinition isolatedFloors[2];
        static const ArchitectureDefinition isolatedDefinition = {
            "enemy_isolation", "ENEMY ISOLATION", "Two independent runtimes.", isolatedFloors, 2};
        static bool isolatedInitialized = false;
        if (!isolatedInitialized)
        {
            isolatedFloors[0] = FloorDefinition();
            isolatedFloors[0].id = 1;
            isolatedFloors[0].type = FloorType::Empty;
            isolatedFloors[0].enemyNetrunner = &nullbyteDefinition();
            isolatedFloors[1] = FloorDefinition();
            isolatedFloors[1].id = 2;
            isolatedFloors[1].type = FloorType::Empty;
            isolatedFloors[1].enemyNetrunner = &zerDefinition();
            isolatedInitialized = true;
        }
        GameState& state = debugTestState(isolatedDefinition);
        state.startRun();
        EnemyNetrunnerRuntime* first = state.enemyNetrunnerAt(0);
        EnemyNetrunnerRuntime* second = state.enemyNetrunnerAt(1);
        if (first != nullptr && second != nullptr)
        {
            first->runner.takeDamage(7);
            first->currentFloor = 1;
            first->discoveredByPlayer = true;
            runtimeIsolationOk = first->definition != nullptr && second->definition != nullptr &&
                strcmp(first->definition->id, "nullbyte") == 0 && strcmp(second->definition->id, "zer=0") == 0 && first->runner.hp() == 23 &&
                second->runner.hp() == 30 && second->currentFloor == 1 &&
                !second->discoveredByPlayer;
        }
    }

    {
        static FloorDefinition capacityFloors[3];
        static bool capacityContentInitialized = false;
        if (!capacityContentInitialized)
        {
            for (size_t index = 0; index < 3; ++index)
            {
                capacityFloors[index] = FloorDefinition();
                capacityFloors[index].id = static_cast<uint8_t>(index + 1);
                capacityFloors[index].type = FloorType::Empty;
                capacityFloors[index].contentId = "ENEMY_CAPACITY";
                capacityFloors[index].enemyNetrunner = &nullbyteDefinition();
            }
            capacityContentInitialized = true;
        }
        static const ArchitectureDefinition capacityDefinition = {
            "enemy_capacity", "ENEMY CAPACITY", "Runtime capacity guard.", capacityFloors, 3};
        GameState& state = debugTestState(capacityDefinition);
        state.startRun();
        capacityOk = state.enemyNetrunnerCount() == MAX_ENEMY_NETRUNNERS &&
            state.enemyNetrunnerAt(MAX_ENEMY_NETRUNNERS) == nullptr;

        EnemyNetrunnerDefinition invalid = nullbyteDefinition();
        invalid.deckConfig.programCount = MAX_CYBERDECK_PROGRAMS + 1;
        EnemyNetrunnerRuntime invalidRuntime;
        invalidDefinitionRejected = !invalidRuntime.reset(invalid, 99, 0) && !invalidRuntime.active;
    }

    const bool valid = definitionIdentityOk && definitionImmutableOk && runtimeIsolationOk &&
        definitionLifetimeOk && entryOk && stableIdOk && terminalOk && freshRunOk && capacityOk &&
        invalidDefinitionRejected;
    Serial.printf("[EnemyFloorEntry] %s | Identity=%s | Immutable=%s | Isolated=%s | Lifetime=%s | Entry=%s | Stable=%s | Terminal=%s | Fresh=%s | Capacity=%s | Invalid=%s\n",
        valid ? "PASS" : "FAIL", definitionIdentityOk ? "PASS" : "FAIL",
        definitionImmutableOk ? "PASS" : "FAIL", runtimeIsolationOk ? "PASS" : "FAIL",
        definitionLifetimeOk ? "PASS" : "FAIL",
        entryOk ? "PASS" : "FAIL", stableIdOk ? "PASS" : "FAIL",
        terminalOk ? "PASS" : "FAIL", freshRunOk ? "PASS" : "FAIL", capacityOk ? "PASS" : "FAIL",
        invalidDefinitionRejected ? "PASS" : "FAIL");
}

void App::runUiListStateDebugTest()
{
    bool emptyOk = false;
    bool boundaryOk = false;
    bool scrollOk = false;
    bool wrapOk = false;
    bool floorMenuScrollOk = false;

    ListViewState list;
    list.selected = 4;
    list.scrollOffset = 3;
    list.ensureVisible(0, 8);
    emptyOk = list.selected == 0 && list.scrollOffset == 0;

    list.selected = 7;
    list.scrollOffset = 0;
    list.ensureVisible(8, 8);
    boundaryOk = list.selected == 7 && list.scrollOffset == 0;

    list.selected = 8;
    list.scrollOffset = 0;
    list.ensureVisible(12, 8);
    const bool firstStep = list.scrollOffset == 1;
    list.selected = 11;
    list.ensureVisible(12, 8);
    scrollOk = firstStep && list.scrollOffset == 4 && list.selected >= list.scrollOffset &&
        list.selected < list.scrollOffset + 8;

    list.move(1, 12, 8);
    const bool forwardWrap = list.selected == 0 && list.scrollOffset == 0;
    list.move(-1, 12, 8);
    wrapOk = forwardWrap && list.selected == 11 && list.scrollOffset == 4;

    // Floor action panels render eight rows. JACK OUT is deliberately the
    // final action in a long list, so it must enter the viewport before its
    // index can be dispatched.
    ListViewState floorMenu;
    for (size_t index = 0; index < 11; ++index) floorMenu.move(1, 12, 8);
    const bool jackOutVisible = floorMenu.selected == 11 && floorMenu.scrollOffset == 4 &&
        floorMenu.selected >= floorMenu.scrollOffset && floorMenu.selected < floorMenu.scrollOffset + 8;
    floorMenu.move(-1, 12, 8);
    const bool scrollBackVisible = floorMenu.selected == 10 && floorMenu.scrollOffset == 4 &&
        floorMenu.selected >= floorMenu.scrollOffset && floorMenu.selected < floorMenu.scrollOffset + 8;
    floorMenu.ensureVisible(4, 8);
    const bool floorSwitchReset = floorMenu.selected == 3 && floorMenu.scrollOffset == 0;
    floorMenuScrollOk = jackOutVisible && scrollBackVisible && floorSwitchReset;

    const bool valid = emptyOk && boundaryOk && scrollOk && wrapOk && floorMenuScrollOk;
    Serial.printf("[UiListState] %s | Empty=%s | Boundary=%s | Scroll=%s | Wrap=%s | FloorMenuScroll=%s\n",
        valid ? "PASS" : "FAIL", emptyOk ? "PASS" : "FAIL", boundaryOk ? "PASS" : "FAIL",
        scrollOk ? "PASS" : "FAIL", wrapOk ? "PASS" : "FAIL", floorMenuScrollOk ? "PASS" : "FAIL");
}

void App::runEnemyFloorUiFlowDebugTest()
{
    static FloorDefinition enemyOnlyFloors[2];
    static FloorDefinition mixedFloors[2];
    static FloorDefinition objectOnlyFloors[2];
    static EnemyNetrunnerDefinition minimalEnemy;
    static ArchitectureDefinition enemyOnlyDefinition;
    static ArchitectureDefinition mixedDefinition;
    static ArchitectureDefinition objectOnlyDefinition;
    static bool initialized = false;
    if (!initialized)
    {
        minimalEnemy = nullbyteDefinition();
        minimalEnemy.id = "minimal_enemy";
        minimalEnemy.name = "MINIMAL";
        minimalEnemy.deckConfig = CyberdeckConfig();
        minimalEnemy.deckConfig.quality = CyberdeckQuality::Standard;
        minimalEnemy.deckConfig.programs[0] = ProgramId::Hellbolt;
        minimalEnemy.deckConfig.programCount = 1;
        minimalEnemy.deckConfig.hardwareCount = 0;

        FloorDefinition* fixtureSets[] = {enemyOnlyFloors, mixedFloors, objectOnlyFloors};
        for (FloorDefinition* floors : fixtureSets)
        {
            floors[0] = FloorDefinition();
            floors[0].id = 1;
            floors[0].type = FloorType::Empty;
            floors[0].contentId = "ENTRY";
            floors[1] = FloorDefinition();
            floors[1].id = 2;
            floors[1].contentId = "DIAGNOSTIC_F2";
        }
        enemyOnlyFloors[1].type = FloorType::Empty;
        enemyOnlyFloors[1].enemyNetrunner = &minimalEnemy;
        mixedFloors[1].type = FloorType::File;
        mixedFloors[1].fileName = "MIXED.DAT";
        mixedFloors[1].fileType = "DIAGNOSTIC";
        mixedFloors[1].enemyNetrunner = &minimalEnemy;
        objectOnlyFloors[1].type = FloorType::File;
        objectOnlyFloors[1].fileName = "OBJECT.DAT";
        objectOnlyFloors[1].fileType = "DIAGNOSTIC";

        enemyOnlyDefinition = {"diag_enemy_only", "DIAG ENEMY ONLY", nullptr, enemyOnlyFloors, 2};
        mixedDefinition = {"diag_mixed", "DIAG MIXED", nullptr, mixedFloors, 2};
        objectOnlyDefinition = {"diag_object_only", "DIAG OBJECT ONLY", nullptr, objectOnlyFloors, 2};
        initialized = true;
    }

    FixedDice dice(10);
    NetRules rules(dice);
    size_t entityCount = 0;
    size_t actionCount = 0;

    GameState& builtIn = debugTestState(BuiltInArchitectures::netrunnerCombatTest());
    builtIn.startRun();
    builtIn.jackIn();
    if (Floor* entry = builtIn.architecture().currentFloor()) entry->resolved = true;
    EncounterResult& builtInMove = debugEncounterResult();
    builtIn.moveForward(rules, builtInMove);
    const EnemyNetrunnerRuntime* builtInEnemy = builtIn.floorEnemyNetrunner();
    const bool builtInOk = builtInMove.moved &&
        GameUIController::validateFloorUiFlowForDebug(builtIn, entityCount, actionCount) &&
        entityCount == 2 && actionCount == 6 && builtInEnemy != nullptr &&
        builtInEnemy->definition != nullptr &&
        strcmp(builtInEnemy->definition->id, "nullbyte") == 0 &&
        builtInEnemy->definition->name != nullptr;

    GameState& enemyOnly = debugTestState(enemyOnlyDefinition);
    enemyOnly.startRun(); enemyOnly.jackIn();
    EncounterResult& enemyMove = debugEncounterResult();
    enemyOnly.moveForward(rules, enemyMove);
    const EnemyNetrunnerRuntime* minimalRuntime = enemyOnly.floorEnemyNetrunner();
    const bool enemyOnlyOk = enemyMove.moved &&
        GameUIController::validateFloorUiFlowForDebug(enemyOnly, entityCount, actionCount) &&
        entityCount == 1 && actionCount == 6 && minimalRuntime != nullptr &&
        minimalRuntime->cyberdeck.programCount() == 1;

    GameState& mixed = debugTestState(mixedDefinition);
    mixed.startRun(); mixed.jackIn();
    EncounterResult& mixedMove = debugEncounterResult();
    mixed.moveForward(rules, mixedMove);
    const bool mixedOk = mixedMove.moved &&
        GameUIController::validateFloorUiFlowForDebug(mixed, entityCount, actionCount) &&
        entityCount == 2 && actionCount == 6;

    GameState& objectOnly = debugTestState(objectOnlyDefinition);
    objectOnly.startRun(); objectOnly.jackIn();
    EncounterResult& objectMove = debugEncounterResult();
    objectOnly.moveForward(rules, objectMove);
    const bool objectOnlyOk = objectMove.moved &&
        GameUIController::validateFloorUiFlowForDebug(objectOnly, entityCount, actionCount) &&
        entityCount == 1 && actionCount == 5 && objectOnly.floorEnemyNetrunner() == nullptr;

    const bool completionFocusOk =
        !GameUIController::shouldRenderCompletionObject(true, true, false) &&
        GameUIController::shouldRenderCompletionObject(true, true, true) &&
        !GameUIController::shouldRenderCompletionObject(false, true, true);
    const bool valid = builtInOk && enemyOnlyOk && mixedOk && objectOnlyOk && completionFocusOk;
    Serial.printf("[EnemyFloorUIFlow] %s | BuiltIn=%s | EnemyOnly=%s | Mixed=%s | ObjectOnly=%s | CompletionFocus=%s\n",
        valid ? "PASS" : "FAIL", builtInOk ? "PASS" : "FAIL", enemyOnlyOk ? "PASS" : "FAIL",
        mixedOk ? "PASS" : "FAIL", objectOnlyOk ? "PASS" : "FAIL", completionFocusOk ? "PASS" : "FAIL");
}

void App::update()
{
    const InputAction action = keyboard_.readAction();
    if (action == InputAction::None)
    {
        const char character = keyboard_.readCharacter();
        const bool characterHandled = character != '\0' && gameUi_.handleCharacter(character);
        if (character == '1' && !characterHandled) display_.startAmbient();
        display_.updateMarquee();
        display_.updateAmbient();
        delay(5);
        return;
    }

    if (action == InputAction::Back && keyboard_.consumeBackspace() && gameUi_.handleBackspace()) {}
    else gameUi_.handle(action);

    display_.updateMarquee();
    display_.updateAmbient();
    delay(5);
}
