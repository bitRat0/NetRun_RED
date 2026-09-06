#pragma once

#include <stddef.h>
#include "DisplayManager.h"
#include "game/GameState.h"
#include "input/KeyboardManager.h"
#include "content/ScenarioScanner.h"
#include "game/PlayerConfigStore.h"

class GameUIController
{
public:
    GameUIController(DisplayManager& display, GameState& game, NetRules& rules,
                     ScenarioScanner& scanner, LoadedScenario& loadedScenario,
                     PlayerConfigStore& configStore, RunnerProfile& profile,
                     CyberdeckConfig& deckConfig)
        : display_(display), game_(game), rules_(rules), scanner_(scanner),
          loadedScenario_(loadedScenario), configStore_(configStore), profile_(profile),
          deckConfig_(deckConfig) {}
    void begin();
    void handle(InputAction input);
    bool handleCharacter(char value);
    bool handleBackspace();
    static bool validateFloorUiFlowForDebug(const GameState& game,
                                            size_t& entityCount, size_t& maxActionCount);
    static bool shouldRenderCompletionObject(bool architectureCompleted, bool atCompletionDepth,
                                              bool focusedFloorObject);
    static void buildArchitectureMapView(const GameState& game, ArchitectureMapView& view);
    static uint8_t movementChoiceDestinationFor(const Architecture& architecture, size_t choice);
    static void movementChoiceLabelFor(const Architecture& architecture, size_t choice,
                                       char* destination, size_t capacity);
    static void movementChoiceLabelFor(const GameState& game, size_t choice,
                                       char* destination, size_t capacity);
    static FeedbackTone hostileResultFeedbackToneForDebug(const CombatResult& result);
    static uint8_t demonPresentationRendererType(const DemonDefinition* definition);
    static bool shouldPendUnsafeExitPresentationForDebug(const CombatResult& result, bool targetIsEnemy);
    static bool shouldAnimatePlayerBlackIceAttackForDebug(const CombatResult& result, bool targetIsEnemy);
    static bool shouldScheduleAutomaticTurnEndForDebug(RunState runState, TurnPhase turnPhase,
                                                       uint8_t remainingNetActions, bool resultScreen,
                                                       bool awaitingEndDecision);
    bool selectFloorEntityForDebug(uint16_t runtimeId);
    bool selectBlackIceForDebug(uint16_t runtimeId);
    bool selectEnemyForDebug(uint16_t runtimeId);
    bool selectFloorObjectForDebug();
    bool hasFloorActionForDebug(const char* label);
    bool executeFloorActionForDebug(const char* label);
    void executeSlideForDebug();
    void executeZapForDebug();

private:
    enum class SystemActionVisual : uint8_t { None, EyeDee, Backdoor, Control, Pathfinder, Cloak };
    enum class Screen { Main, PlayerMenu, Profile, ProfileHandleEdit, ProfileNumberEdit, DeckEditor, ProgramSelect, HardwareSelect, BlackIceSelect, DeckItemInfo,
                        SelectRun, RunDetail, Runner, Cyberdeck, Floor,
                        Programs, ProgramAction, Move, Map, Result, JackOut, RunSummary,
                        Objective, RunComplete, ArchitectureComplete, VirusPrompt, RunnerDown, ScenarioError,
                        EnemyAttack, EnemyProgramTarget, EnemyProgramAttack, PlayerIce, PlayerIceAction, PlayerIceTarget,
                        UnsafeJackOutPrompt, DeckUnsavedChanges, EnemyAnimTest };
    enum class Action { PlaceVirus, Backdoor, EyeDee, Download, Control, Pathfinder, Cloak, Zap, Sword, Banhammer, Slide,
                        Programs, PlayerIce, DemonZap, DemonSword, Move, Map, EndTurn, JackOut, AttackEnemy, TargetEnemyProgram, Extinguish };
    enum class RunEndCause : uint8_t { SafeJackOut, UnsafeJackOut, RunnerDown };
    enum class FloorEntityKind : uint8_t { FloorObject, BlackIce, EnemyNetrunner, Trophy };
    struct FloorEntityDescriptor
    {
        FloorEntityKind kind = FloorEntityKind::FloorObject;
        uint16_t runtimeId = 0;
    };

    void redraw();
    void moveSelection(int direction, size_t count);
    void activateSelection();
    void goBack();
    void buildFloorActions();
    bool addFloorAction(Action action, const char* label);
    void executeFloorAction(Action action);
    void executeMovement(uint8_t destination);
    void executeCombat(Action action, BlackIceInstance& ice);
    BlackIceInstance* selectedIceTarget();
    BlackIceInstance* focusedIce();
    EnemyNetrunnerRuntime* focusedEnemy();
    FloorEntityKind focusedFloorEntityKind();
    void buildFloorEntities();
    bool appendFloorEntity(FloorEntityKind kind, uint16_t runtimeId);
    bool selectFloorEntityForDebug(FloorEntityKind kind, uint16_t runtimeId);
    void syncSelectedFloorEntity();
    void switchFloorEntity(int direction);
    void buildTargetSummary();
    void showNetResult(const char* name, const NetCheckResult& result, bool showTarget = true,
                       SystemActionVisual visual = SystemActionVisual::None);
    void showCombatResult(const char* name, const CombatResult& result, bool hpDamage = false,
                          bool targetIsEnemy = false, const char* targetName = nullptr,
                          PlayerActionVisual actionVisual = PlayerActionVisual::None,
                          bool renderFloorContext = false,
                          FeedbackTone toneOverride = FeedbackTone::Neutral,
                          HostileActorVisual hostileActor = HostileActorVisual::None,
                          FloorVisual hostileVisual = FloorVisual::Empty, uint8_t hostileDemonType = 0,
                          HostileAttackStyle hostileAttackStyle = HostileAttackStyle::Pulse,
                          bool presentationAlreadyPlayed = false);
    void renderFloorCombatContext();
    void showMessage(const char* title, const char* message, Screen next = Screen::Floor);
    void showEnemyPresenceIfPending();
    void showNextDemonPhaseAction();
    void finishPlayerTurn();
    void scheduleAutomaticTurnEndIfNeeded();
    void advanceSilentPhases();
    void queueIcePhaseResults(const IcePhaseResult& result, Screen next, bool playerIceResults = false);
    void showNextQueuedIceResult();
    void finishPendingUnsafeExitPresentation();
    void clearQueuedIceResults();
    const char* blackIceName(const CombatResult& result) const;
    size_t appendBlackIceEffectFeedback(const CombatResult& result, size_t firstLine);
    const char* programStatus(const Program& program) const;
    const char* floorName(const Floor& floor) const;
    BlackIceInstance* activeIce();
    void showScenarioError(const ScenarioEntry& entry, const ScenarioImportResult& result);
    void adjustProfileValue(int direction);
    void beginHandleEdit();
    void beginNumberEdit();
    bool savePlayerConfig();
    bool saveDeckEditor();
    void beginDeckEditor();
    void discardDeckEditor();
    void refreshDeckEditorDirty();
    static bool deckConfigsEqual(const CyberdeckConfig& left, const CyberdeckConfig& right);
    bool trySetDeckProgram(size_t slot, ProgramId id);
    bool trySetHardware(size_t slot, HardwareId id);
    bool trySetPlayerBlackIce(size_t slot, BlackIceType type);
    void cycleDeckQuality();
    void queueEnemyPhaseResults(const EnemyPhaseResult& result);
    void executeEnemyAnimTest();
    void panMap(int16_t x, int16_t y);
    void showRunSummary(RunEndCause cause);
    void buildPlayerStatus();

    DisplayManager& display_;
    GameState& game_;
    NetRules& rules_;
    ScenarioScanner& scanner_;
    LoadedScenario& loadedScenario_;
    PlayerConfigStore& configStore_;
    RunnerProfile& profile_;
    CyberdeckConfig& deckConfig_;
    CyberdeckConfig editorDeckConfig_;
    bool deckEditorDirty_ = false;
    Screen screen_ = Screen::Main;
    Screen previousScreen_ = Screen::Main;
    Screen resultNextScreen_ = Screen::Floor;
    Screen afterIceScreen_ = Screen::Floor;
    size_t selected_ = 0;
    size_t selectedProgram_ = 0;
    size_t selectedEnemyProgram_ = 0;
    size_t selectedEnemyAnimProgram_ = 0;
    size_t enemyMenuSlots_[MAX_CYBERDECK_PROGRAMS] = {};
    size_t enemyMenuCount_ = 0;
    // Reachable worst case is 15: four Architecture-ICE actions, two Demon
    // actions, plus Extinguish, Programs, Player ICE, Move, Pathfinder, Cloak,
    // Map, End Turn and Jack Out. Enemy/Object focus is mutually exclusive
    // with Architecture-ICE focus, but an active Demon can coexist with it.
    static constexpr size_t MAX_FLOOR_ACTIONS = 16;
    Action floorActions_[MAX_FLOOR_ACTIONS];
    const char* floorActionLabels_[MAX_FLOOR_ACTIONS];
    size_t floorActionCount_ = 0;
    char resultLines_[7][40] = {};
    size_t resultLineCount_ = 0;
    char resultTitle_[24] = {};
    FeedbackTone resultTone_ = FeedbackTone::Info;
    bool resultToneExplicit_ = false;
    InfoLayout resultInfoLayout_ = InfoLayout::Automatic;
    bool pendingIcePhase_ = false;
    bool pendingEnemyPhase_ = false;
    bool fireTickFeedbackPending_ = false;
    bool unsafeExitPresentationPending_ = false;
    IcePhaseResult::IceAttack queuedIceResults_[MAX_ACTIVE_BLACK_ICE] = {};
    size_t queuedIceResultCount_ = 0;
    size_t queuedIceResultIndex_ = 0;
    Screen queuedIceNextScreen_ = Screen::Floor;
    bool queuedResultsTargetEnemy_ = false;
    bool queuedResultFromEnemy_[MAX_ACTIVE_BLACK_ICE] = {};
    bool queuedResultFromPlayerIce_[MAX_ACTIVE_BLACK_ICE] = {};
    bool queuedResultIsUnsafeExit_[MAX_ACTIVE_BLACK_ICE] = {};
    uint16_t selectedIceRuntimeId_ = 0;
    static constexpr size_t FLOOR_ENTITY_CAPACITY = 1 + MAX_BLACK_ICE_PER_FLOOR + MAX_ENEMY_NETRUNNERS + 1;
    FloorEntityDescriptor floorEntities_[FLOOR_ENTITY_CAPACITY] = {};
    size_t floorEntityCount_ = 0;
    size_t selectedFloorEntity_ = 0;
    uint8_t lastEntityDiagnosticFloor_ = 0xff;
    size_t lastEntityDiagnosticCount_ = static_cast<size_t>(-1);
    size_t lastEntityDiagnosticSelected_ = static_cast<size_t>(-1);
    EncounterResult movementResult_;
    IcePhaseResult icePhaseResult_;
    EnemyPhaseResult enemyPhaseResult_;
    DemonPhaseResult demonPhaseResult_;
    size_t demonPhaseActionIndex_ = 0;
    ArchitectureMapView mapView_;
    int16_t mapPanX_ = 0;
    int16_t mapPanY_ = 0;
    char targetSummary_[48] = {};
    char playerStatus_[64] = {};
    uint32_t completedTurns_ = 0;
    bool completedControl_ = false;
    bool completedFile_ = false;
    bool encounterSummaryPending_ = false;
    bool demonAppearanceShown_ = false;
    RunEndCause runEndCause_ = RunEndCause::SafeJackOut;
    bool runSummaryRevealed_ = false;
    char scenarioErrorFile_[40] = {};
    char scenarioErrorMessage_[32] = {};
    int scenarioErrorFloor_ = -1;
    size_t editingDeckProgram_ = MAX_CYBERDECK_PROGRAMS;
    size_t editingHardware_ = MAX_CYBERDECK_HARDWARE;
    size_t editingPlayerIce_ = MAX_PLAYER_BLACK_ICE;
    size_t selectedPlayerIce_ = 0;
    bool deckInfoIsBlackIce_ = false;
    uint16_t playerIceTargetIds_[MAX_ENEMY_NETRUNNERS] = {};
    size_t playerIceTargetCount_ = 0;
    char handleEdit_[RunnerProfile::HANDLE_CAPACITY] = {};
    size_t handleEditLength_ = 0;
    uint8_t profileEditOriginal_ = 0;
    char configLabels_[MAX_CYBERDECK_PROGRAMS + MAX_CYBERDECK_HARDWARE + MAX_PLAYER_BLACK_ICE + 8][32] = {};
    const char* configItems_[MAX_CYBERDECK_PROGRAMS + MAX_CYBERDECK_HARDWARE + MAX_PLAYER_BLACK_ICE + 8] = {};
};
