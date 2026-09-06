#pragma once

#include <stddef.h>

#include "input/KeyboardManager.h"
#include "game/GameState.h"
#include "game/PlayerConfigStore.h"
#include "ui/DisplayManager.h"
#include "ui/GameUIController.h"
#include "content/ScenarioScanner.h"

class App
{
public:
    App();
    void begin();
    void update();

private:
    static constexpr unsigned long SERIAL_WAIT_TIMEOUT_MS = 1500;

    void runGameModelDebugTest();
    void runNetrunnerStatsDebugTest();
    void runGreymarkDebugTest();
    void runContentDebugTest();
    void runContentIdentityDebugTest();
    void runBehaviorIdDebugTest();
    void runAnimationIdDebugTest();
    void runVisualIdDebugTest();
    void runScenarioImportDebugTest();
    void runScenarioCustomEnemyDebugTest();
    void runScenarioEnemyBehaviorDebugTest();
    void runR34AuditDebugTest();
    void runR35AuditDebugTest();
    void runNetRulesDebugTest();
    void runNetCombatDebugTest();
    void runPlayerZapEnemyDebugTest();
    void runPlayerSlideTargetingDebugTest();
    void runPlayerTargetContextDebugTest();
    void runBlackIceDebugTest();
    void runBlackIceEffectsDebugTest();
    void runAntiProgramDebugTest();
    void runAntiProgramExpansionDebugTest();
    void runDigitalRollAnimationDebugTest();
    void runMultiIceDebugTest();
    void runIceRosterDebugTest();
    void runDeadlockStatusDebugTest();
    void runBreacherDebugTest();
    void runGhostpulseStatusDebugTest();
    void runProgramDebugTest();
    void runTurnOrchestrationDebugTest();
    void runAutomaticEndDebugTest();
    void runPlayerProfileDebugTest();
    void runDeckConfigDebugTest();
    void runDeckHardwareDebugTest();
    void runEnemyNetrunnerDebugTest();
    void runEnemyBehaviorBaselineDebugTest();
    void runEnemyBehaviorProfilesDebugTest();
    void runEnemyNetrunnerCustomDefinitionDebugTest();
    void runEnemyResultToneDebugTest();
    void runMilitechDemoEnemyDormantDebugTest();
    void runSpriteReviewNullbyteDormantDebugTest();
    void runMilitechDemoEnemySpatialDebugTest();
    void runEnemyFloorEntryDebugTest();
    void runEnemyFloorUiFlowDebugTest();
    void runBranchingDebugTest();
    void runArchitectureMapDebugTest();
    void runPathfinderDebugTest();
    void runCloakDebugTest();
    void runUiListStateDebugTest();
    void runPlayerIceIdentityDebugTest();
    void runPlayerBlackIceDebugTest();
    void runPlayerBlackIceCombatDebugTest();
    void runPlayerBlackIceSlideDebugTest();
    void runPlayerBlackIceChaseDebugTest();
    void runArchitectureCompletionDebugTest();
    void runDemonIdentityDebugTest();
    void runDemonCustomDefinitionDebugTest();
    void runScenarioCustomDemonDebugTest();
    void runDemonDebugTest();
    void runIntegrationDebugTest();
    DisplayManager display_;
    KeyboardManager keyboard_;
    ScenarioScanner scenarioScanner_;
    LoadedScenario loadedScenario_;
    PlayerConfigStore playerConfigStore_;
    RunnerProfile runnerProfile_;
    CyberdeckConfig cyberdeckConfig_;
    GameState gameState_;
    Dice dice_;
    NetRules rules_;
    GameUIController gameUi_;
};
