#include "GameUIController.h"

#include <stdio.h>
#include <string.h>
#include "content/BuiltInArchitectures.h"
#include "game/ProgramCatalog.h"
#include "game/HardwareCatalog.h"
#include "app/DebugConfig.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#ifndef NETRUN_DIAG_F2_STATIC_RENDER
#define NETRUN_DIAG_F2_STATIC_RENDER 0
#endif

#ifndef NETRUN_DIAG_F2_STATIC_ACTIONS
#define NETRUN_DIAG_F2_STATIC_ACTIONS 0
#endif

#ifndef NETRUN_DIAG_F2_SKIP_ENEMY_PHASE
#define NETRUN_DIAG_F2_SKIP_ENEMY_PHASE 0
#endif

namespace
{
constexpr ProgramId kSelectablePrograms[] = {
    ProgramId::Sword, ProgramId::Banhammer, ProgramId::Armor, ProgramId::Flak,
    ProgramId::Shield, ProgramId::Eraser, ProgramId::SeeYa, ProgramId::SpeedyGonzalvez, ProgramId::Worm,
    ProgramId::DeckKRASH, ProgramId::Hellbolt, ProgramId::Nervescrub,
    ProgramId::PoisonFlatline, ProgramId::Superglue, ProgramId::Vrizzbolt
};
constexpr HardwareId kSelectableHardware[] = {HardwareId::BackupDrive, HardwareId::DnaLock,
    HardwareId::HardenedCircuitry, HardwareId::InsulatedWiring, HardwareId::KrashBarrier,
    HardwareId::RangeUpgrade};
size_t selectablePlayerIceCount()
{
    size_t count = 0;
    for (size_t index = 0; index < blackIceDefinitionCount(); ++index)
        if (playerBlackIceSupported(blackIceDefinitionAt(index))) ++count;
    return count;
}

const BlackIceDefinition* selectablePlayerIceAt(size_t selectableIndex)
{
    for (size_t index = 0; index < blackIceDefinitionCount(); ++index)
    {
        const BlackIceDefinition* definition = blackIceDefinitionAt(index);
        if (!playerBlackIceSupported(definition)) continue;
        if (selectableIndex-- == 0) return definition;
    }
    return nullptr;
}
// These three built-ins are the normal no-SD showcase routes. Other regression
// architectures remain compiled in for diagnostics but are not listed here.
constexpr size_t BUILT_IN_SCENARIO_COUNT = 2;
constexpr ProgramId kEnemyAnimTestPrograms[] = {
    ProgramId::Hellbolt, ProgramId::Vrizzbolt, ProgramId::Nervescrub,
    ProgramId::Superglue, ProgramId::PoisonFlatline, ProgramId::DeckKRASH};

const char* qualityName(CyberdeckQuality quality)
{
    switch (quality)
    {
        case CyberdeckQuality::Poor: return "POOR";
        case CyberdeckQuality::Standard: return "STANDARD";
        case CyberdeckQuality::Excellent: return "EXCELLENT";
    }
    return "?";
}

bool isCombatFloor2(const GameState& game, bool targetFloor = false)
{
    return NETRUN_DEBUG_VERBOSE && strcmp(game.architecture().id(), "netrunner_combat_test") == 0 &&
        game.architecture().currentPosition() == (targetFloor ? 0U : 1U);
}

void logFloor2Memory(unsigned step)
{
    Serial.printf("[F2MEM] step=%u heap=%u minHeap=%u stack=%u\n", step,
        static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

PlayerActionVisual playerEnemyRunnerVisual(ProgramId id)
{
    switch (id)
    {
        case ProgramId::Hellbolt: return PlayerActionVisual::Hellbolt;
        case ProgramId::Vrizzbolt: return PlayerActionVisual::Vrizzbolt;
        case ProgramId::Nervescrub: return PlayerActionVisual::Nervescrub;
        case ProgramId::Superglue: return PlayerActionVisual::Superglue;
        case ProgramId::PoisonFlatline: return PlayerActionVisual::PoisonFlatline;
        case ProgramId::DeckKRASH: return PlayerActionVisual::DeckKrash;
        default: return PlayerActionVisual::None;
    }
}

FloorVisual blackIceFloorVisual(BlackIceType type)
{
    switch (type)
    {
        case BlackIceType::Ice02: return FloorVisual::Ice02;
        case BlackIceType::Ice03: return FloorVisual::Ice03;
        case BlackIceType::Ice04: return FloorVisual::Ice04;
        case BlackIceType::Ice05: return FloorVisual::Ice05;
        case BlackIceType::Ice06: return FloorVisual::Ice06;
        case BlackIceType::Ice07: return FloorVisual::Ice07;
        case BlackIceType::Ice08: return FloorVisual::Ice08;
        case BlackIceType::Ice09: return FloorVisual::Ice09;
        case BlackIceType::Ice10: return FloorVisual::Ice10;
        case BlackIceType::Ice11: return FloorVisual::Ice11;
        case BlackIceType::Ice12: return FloorVisual::Ice12;
        default: return FloorVisual::Ice01;
    }
}

FloorVisual playerIceFloorVisual(IceVisualId visualId)
{
    switch (visualId)
    {
        case IceVisualId::Bird01: return FloorVisual::Ice02;
        case IceVisualId::Serpent01: return FloorVisual::Ice03;
        case IceVisualId::Octopus01: return FloorVisual::Ice04;
        case IceVisualId::Wraith01: return FloorVisual::Ice05;
        case IceVisualId::Hunter01: return FloorVisual::Ice06;
        case IceVisualId::Scorp01: return FloorVisual::Ice07;
        case IceVisualId::Rat01: return FloorVisual::Ice08;
        case IceVisualId::Winged01: return FloorVisual::Ice09;
        case IceVisualId::Feline01: return FloorVisual::Ice10;
        case IceVisualId::Skull01: return FloorVisual::Ice11;
        case IceVisualId::BigGuy01: return FloorVisual::Ice12;
        default: return FloorVisual::Ice01;
    }
}

void pathfinderContentLabel(const Floor& floor, const GameState* game, uint8_t floorIndex,
                            char* output, size_t capacity)
{
    if (output == nullptr || capacity == 0) return;
    output[0] = '\0';
    char staticContent[32] = {};
    if (floor.type == FloorType::Password) snprintf(staticContent, sizeof(staticContent), "PASSWORD");
    else if (floor.type == FloorType::File) snprintf(staticContent, sizeof(staticContent), "DATA");
    else if (floor.type == FloorType::ControlNode) snprintf(staticContent, sizeof(staticContent), "CONTROL");
    if ((floor.blackIceCount > 0 || floor.type == FloorType::BlackICE) && (game == nullptr || !floor.blackIceTriggered))
    {
        const size_t count = floor.blackIceCount > 0 ? floor.blackIceCount : 1;
        size_t used = strlen(staticContent);
        for (size_t index = 0; index < count && index < MAX_BLACK_ICE_PER_FLOOR; ++index)
        {
            const BlackIceType type = floor.blackIceCount > 0 ? floor.blackIceTypes[index] : floor.blackIceType;
            const BlackIceDefinition* definition = floor.blackIceCount > 0
                ? floor.blackIceDefinitions[index] : floor.blackIceDefinition;
            if (definition == nullptr) definition = blackIceDefinition(type);
            if (definition == nullptr) continue;
            const int written = snprintf(staticContent + used, sizeof(staticContent) - used,
                used == 0 ? "%s" : "+%s", definition->displayName);
            if (written <= 0 || static_cast<size_t>(written) >= sizeof(staticContent) - used)
            {
                snprintf(staticContent, sizeof(staticContent), count > 1 ? "%u ICE" : "ICE",
                    static_cast<unsigned>(count));
                return;
            }
            used += static_cast<size_t>(written);
        }
    }
    snprintf(output, capacity, "%s", staticContent);
    if (game == nullptr) return;
    size_t used = strlen(output);
    uint8_t runtimeCount = 0;
    for (size_t index = 0; index < game->activeBlackIceCount(); ++index)
    {
        const BlackIceInstance* ice = game->activeBlackIceAt(index);
        if (ice == nullptr || !ice->active() || game->activeBlackIceChasePosition(index) != floorIndex) continue;
        ++runtimeCount;
        const BlackIceDefinition* definition = ice->definition();
        if (definition == nullptr) continue;
        const int written = snprintf(output + used, capacity > used ? capacity - used : 0,
            used == 0 ? "%s" : "+%s", definition->displayName);
        if (written <= 0 || used + static_cast<size_t>(written) >= capacity)
        {
            snprintf(output, capacity, staticContent[0] != '\0' ? "%s+%u ICE" : "%u ICE",
                staticContent, static_cast<unsigned>(runtimeCount));
            return;
        }
        used += static_cast<size_t>(written);
    }
}
}

void GameUIController::begin() { screen_ = Screen::Main; selected_ = 0; redraw(); }

void GameUIController::handle(InputAction input)
{
    if (screen_ == Screen::EnemyAnimTest)
    {
        constexpr size_t count = sizeof(kEnemyAnimTestPrograms) / sizeof(kEnemyAnimTestPrograms[0]);
        if (input == InputAction::Left || input == InputAction::Right)
        {
            selectedEnemyAnimProgram_ = (selectedEnemyAnimProgram_ + count +
                (input == InputAction::Right ? 1 : -1)) % count;
            redraw();
        }
        else if (input == InputAction::Confirm) executeEnemyAnimTest();
        else if (input == InputAction::Back) goBack();
        return;
    }
    if (screen_ == Screen::Map)
    {
        if (input == InputAction::Left) panMap(18, 0);
        else if (input == InputAction::Right) panMap(-18, 0);
        else if (input == InputAction::Up) panMap(0, 12);
        else if (input == InputAction::Down) panMap(0, -12);
        else if (input == InputAction::Back || input == InputAction::Confirm) goBack();
        return;
    }
    if (screen_ == Screen::ProfileHandleEdit)
    {
        bool changed = false;
        if (input == InputAction::Confirm && handleEditLength_ > 0)
        {
            snprintf(profile_.handle, sizeof(profile_.handle), "%s", handleEdit_);
            screen_ = Screen::Profile;
            changed = true;
        }
        else if (input == InputAction::Back || input == InputAction::Left) { screen_ = Screen::Profile; changed = true; }
        if (changed) redraw();
        return;
    }
    if (screen_ == Screen::ProfileNumberEdit)
    {
        const Screen previousScreen = screen_;
        const uint8_t previousValue = selected_ == 1 ? profile_.interfaceRank : profile_.maxHp;
        if (input == InputAction::Up || input == InputAction::Down)
            adjustProfileValue(input == InputAction::Up ? 1 : -1);
        else if (input == InputAction::Confirm) screen_ = Screen::Profile;
        else if (input == InputAction::Back || input == InputAction::Left)
        {
            if (selected_ == 1) profile_.interfaceRank = profileEditOriginal_;
            else profile_.maxHp = profileEditOriginal_;
            screen_ = Screen::Profile;
        }
        const uint8_t currentValue = selected_ == 1 ? profile_.interfaceRank : profile_.maxHp;
        if (screen_ != previousScreen || currentValue != previousValue) redraw();
        return;
    }
    if (screen_ == Screen::Floor && (input == InputAction::Left || input == InputAction::Right))
    {
        buildFloorEntities();
        if (floorEntityCount_ > 1)
        {
            switchFloorEntity(input == InputAction::Right ? 1 : -1);
            redraw();
            return;
        }
    }
    if (input == InputAction::Up) moveSelection(-1, screen_ == Screen::Floor ? floorActionCount_ : 8);
    else if (input == InputAction::Down) moveSelection(1, screen_ == Screen::Floor ? floorActionCount_ : 8);
    else if (input == InputAction::Confirm) activateSelection();
    else if (input == InputAction::Back || input == InputAction::Left) goBack();
}

bool GameUIController::handleBackspace()
{
    if (screen_ != Screen::ProfileHandleEdit) return false;
    if (handleEditLength_ == 0) return true;
    handleEdit_[--handleEditLength_] = '\0';
    redraw();
    return true;
}

bool GameUIController::handleCharacter(char value)
{
    if ((screen_ == Screen::ProgramSelect || screen_ == Screen::BlackIceSelect) &&
        (value == 'i' || value == 'I') && selected_ > 0)
    {
        deckInfoIsBlackIce_ = screen_ == Screen::BlackIceSelect;
        screen_ = Screen::DeckItemInfo; redraw(); return true;
    }
    if (screen_ != Screen::ProfileHandleEdit || value < 32 || value > 126 ||
        handleEditLength_ + 1 >= sizeof(handleEdit_)) return false;
    handleEdit_[handleEditLength_++] = value;
    handleEdit_[handleEditLength_] = '\0';
    redraw();
    return true;
}

void GameUIController::moveSelection(int direction, size_t ignored)
{
    size_t count = ignored;
    switch (screen_)
    {
        case Screen::Main: count = 2; break;
        case Screen::PlayerMenu: count = NETRUN_DEBUG_VERBOSE ? 5 : 4; break;
        case Screen::Profile: count = 5; break;
        case Screen::ProfileNumberEdit: count = 1; break;
        case Screen::DeckEditor: count = editorDeckConfig_.programCount + editorDeckConfig_.hardwareCount +
            editorDeckConfig_.playerBlackIceCount + 7; break;
        case Screen::ProgramSelect: count = 1 + sizeof(kSelectablePrograms) / sizeof(kSelectablePrograms[0]); break;
        case Screen::HardwareSelect: count = 1 + sizeof(kSelectableHardware) / sizeof(kSelectableHardware[0]); break;
        case Screen::BlackIceSelect: count = 1 + selectablePlayerIceCount(); break;
        case Screen::DeckUnsavedChanges: count = 3; break;
        case Screen::VirusPrompt: count = 2; break;
        case Screen::SelectRun: count = BUILT_IN_SCENARIO_COUNT + scanner_.count(); break;
        case Screen::EnemyAttack:
        case Screen::EnemyProgramTarget:
        case Screen::EnemyProgramAttack: count = enemyMenuCount_ > 0 ? enemyMenuCount_ : 1; break;
        case Screen::RunDetail: count = 2; break;
        case Screen::RunSummary: count = 1; break;
        case Screen::Programs: count = game_.cyberdeck().programCount() + 1; break;
        case Screen::ProgramAction:
        {
            const Program* p = game_.cyberdeck().programAt(selectedProgram_);
            count = p != nullptr && p->status() == ProgramStatus::Destroyed ? 1 : 2;
            break;
        }
        case Screen::PlayerIce: count = game_.playerBlackIceCount() + 1; break;
        case Screen::PlayerIceAction: count = 2; break;
        case Screen::PlayerIceTarget: count = playerIceTargetCount_ + 1; break;
        case Screen::Move: count = game_.architecture().connectionCount(game_.architecture().currentPosition()) + 1; break;
        case Screen::JackOut: count = 2; break;
        case Screen::UnsafeJackOutPrompt: count = 2; break;
        default: break;
    }
    if (count == 0) return;
    const size_t previous = selected_;
    selected_ = (selected_ + count + direction) % count;
    if (selected_ == previous) return;
    redraw();
}

void GameUIController::activateSelection()
{
    switch (screen_)
    {
        case Screen::Main:
            screen_ = selected_ == 0 ? Screen::SelectRun : Screen::PlayerMenu;
            selected_ = 0; break;
        case Screen::PlayerMenu:
            if (selected_ == 0) screen_ = Screen::Profile;
            else if (selected_ == 1) beginDeckEditor();
            else if (selected_ == 2)
            {
                configStore_.resetDefaults(profile_, deckConfig_);
                game_.setRunnerProfile(profile_);
                game_.setCyberdeckConfig(deckConfig_);
                showMessage("PLAYER", "DEFAULTS RESTORED", Screen::PlayerMenu);
                return;
            }
            else if (NETRUN_DEBUG_VERBOSE && selected_ == 3)
            {
                game_.setArchitectureDefinition(BuiltInArchitectures::netrunnerCombatTest());
                game_.startRun(); game_.jackIn(); game_.architecture().moveForward();
                selectedEnemyAnimProgram_ = 0;
                screen_ = Screen::EnemyAnimTest;
            }
            else screen_ = Screen::Main;
            selected_ = 0; break;
        case Screen::Profile:
            if (selected_ == 0) beginHandleEdit();
            else if (selected_ == 1 || selected_ == 2) beginNumberEdit();
            else if (selected_ == 3)
            {
                showMessage("PROFILE", savePlayerConfig() ? "PROFILE SAVED" : "INVALID PROFILE",
                            Screen::PlayerMenu);
                return;
            }
            else if (selected_ == 4) screen_ = Screen::PlayerMenu;
            break;
        case Screen::DeckEditor:
        {
            const size_t programCount = editorDeckConfig_.programCount;
            const size_t hardwareCount = editorDeckConfig_.hardwareCount;
            if (selected_ < programCount || selected_ == programCount)
            {
                editingDeckProgram_ = selected_;
                screen_ = Screen::ProgramSelect;
            }
            else if (selected_ <= programCount + hardwareCount + 1)
            {
                editingHardware_ = selected_ - programCount - 1;
                screen_ = Screen::HardwareSelect;
            }
            else if (selected_ <= programCount + hardwareCount + 2 + editorDeckConfig_.playerBlackIceCount)
            {
                editingPlayerIce_ = selected_ - programCount - hardwareCount - 2;
                screen_ = Screen::BlackIceSelect;
            }
            else if (selected_ == programCount + hardwareCount + 3 + editorDeckConfig_.playerBlackIceCount) cycleDeckQuality();
            else if (selected_ == programCount + hardwareCount + 4 + editorDeckConfig_.playerBlackIceCount)
            {
                editorDeckConfig_ = defaultCyberdeckConfig();
                refreshDeckEditorDirty();
            }
            else if (selected_ == programCount + hardwareCount + 5 + editorDeckConfig_.playerBlackIceCount)
            {
                showMessage("CYBERDECK", saveDeckEditor() ? "DECK SAVED" : "INVALID DECK",
                            Screen::PlayerMenu);
                return;
            }
            else if (deckEditorDirty_) { screen_ = Screen::DeckUnsavedChanges; selected_ = 0; }
            else screen_ = Screen::PlayerMenu;
            break;
        }
        case Screen::ProgramSelect:
        {
            const ProgramId id = selected_ == 0 ? ProgramId::None : kSelectablePrograms[selected_ - 1];
            if (trySetDeckProgram(editingDeckProgram_, id))
            {
                screen_ = Screen::DeckEditor;
                selected_ = editingDeckProgram_ < editorDeckConfig_.programCount ? editingDeckProgram_ : 0;
            }
            else showMessage("CYBERDECK", "SLOTS FULL", Screen::DeckEditor);
            break;
        }
        case Screen::HardwareSelect:
        {
            const HardwareId id = selected_ == 0 ? HardwareId::Count : kSelectableHardware[selected_ - 1];
            if (trySetHardware(editingHardware_, id))
            {
                screen_ = Screen::DeckEditor;
                selected_ = editorDeckConfig_.programCount + (editingHardware_ < editorDeckConfig_.hardwareCount ?
                    editingHardware_ + 1 : 0);
            }
            else showMessage("CYBERDECK", "NO FREE SLOTS", Screen::DeckEditor);
            break;
        }
        case Screen::BlackIceSelect:
        {
            const BlackIceDefinition* definition = selected_ == 0 ? nullptr : selectablePlayerIceAt(selected_ - 1);
            const BlackIceType type = definition != nullptr ? definition->type : BlackIceType::Count;
            if (trySetPlayerBlackIce(editingPlayerIce_, type))
            {
                screen_ = Screen::DeckEditor;
                selected_ = editorDeckConfig_.programCount + editorDeckConfig_.hardwareCount + 1 +
                    (editingPlayerIce_ < editorDeckConfig_.playerBlackIceCount ? editingPlayerIce_ + 1 : 0);
            }
            else showMessage("CYBERDECK", "NO FREE SLOTS", Screen::DeckEditor);
            break;
        }
        case Screen::DeckUnsavedChanges:
            if (selected_ == 0)
            {
                showMessage("CYBERDECK", saveDeckEditor() ? "DECK SAVED" : "INVALID DECK", Screen::PlayerMenu);
                return;
            }
            if (selected_ == 1) { discardDeckEditor(); screen_ = Screen::PlayerMenu; selected_ = 0; }
            else { screen_ = Screen::DeckEditor; selected_ = 0; }
            break;
        case Screen::SelectRun:
        {
            if (selected_ == 0) {
                game_.setArchitectureDefinition(BuiltInArchitectures::militechTestNet());
                screen_ = Screen::RunDetail; selected_ = 0; break;
            }
            if (selected_ == 1) {
                game_.setArchitectureDefinition(BuiltInArchitectures::enemyFloor1TestNet());
                screen_ = Screen::RunDetail; selected_ = 0; break;
            }
            scanner_.loadWindow(selected_ - BUILT_IN_SCENARIO_COUNT, 1);
            const ScenarioEntry* entry = scanner_.entry(selected_ - BUILT_IN_SCENARIO_COUNT);
            if (entry == nullptr) break;
            if (!entry->valid) { showScenarioError(*entry, entry->result); break; }
            const ScenarioImportResult result = scanner_.load(selected_ - BUILT_IN_SCENARIO_COUNT, loadedScenario_);
            if (!result.success || !game_.setArchitectureDefinition(loadedScenario_.definition()))
                showScenarioError(*entry, result);
            else { screen_ = Screen::RunDetail; selected_ = 0; }
            break;
        }
        case Screen::RunDetail:
            if (selected_ == 0)
            {
                game_.startRun(); game_.jackIn(); display_.animateJackIn(game_.architecture().name());
                demonAppearanceShown_ = false;
                if (game_.demon().definition != nullptr)
                    display_.animateHostileAppearance(FloorVisual::Empty,
                        game_.demon().definition->displayName,
                        demonPresentationRendererType(game_.demon().definition));
                demonAppearanceShown_ = true;
                screen_ = Screen::Floor;
            }
            else screen_ = Screen::SelectRun;
            selected_ = 0; break;
        case Screen::Runner:
        case Screen::Cyberdeck: screen_ = Screen::Main; selected_ = 0; break;
        case Screen::Floor:
            if (selected_ < floorActionCount_) executeFloorAction(floorActions_[selected_]);
            break;
        case Screen::Programs:
            if (selected_ >= game_.cyberdeck().programCount()) screen_ = previousScreen_;
            else { selectedProgram_ = selected_; screen_ = Screen::ProgramAction; selected_ = 0; }
            break;
        case Screen::ProgramAction:
        {
            Program* program = game_.cyberdeck().programAt(selectedProgram_);
            if (program == nullptr || program->status() == ProgramStatus::Destroyed || selected_ == 1)
            { screen_ = Screen::Programs; selected_ = selectedProgram_; }
            else
            {
                const bool activating = program->status() == ProgramStatus::Inactive;
                ProgramActionResult result = activating
                    ? rules_.activateProgram(game_.runner(), game_.cyberdeck(), *program)
                    : rules_.deactivateProgram(game_.runner(), *program);
                const char* feedback = result.executed
                    ? (activating ? "ACTIVATED" : "DEACTIVATED")
                    : game_.runner().remainingNetActions() == 0 ? "NO ACT LEFT" : "ACTION REJECTED";
                showMessage(program->name(), feedback, Screen::Programs);
                scheduleAutomaticTurnEndIfNeeded();
            }
            break;
        }
        case Screen::PlayerIce:
            if (selected_ >= game_.playerBlackIceCount()) { screen_ = Screen::Floor; selected_ = 0; }
            else { selectedPlayerIce_ = selected_; screen_ = Screen::PlayerIceAction; selected_ = 0; }
            break;
        case Screen::PlayerIceAction:
        {
            PlayerBlackIceRuntime* ice = game_.playerBlackIceAt(selectedPlayerIce_);
            if (ice == nullptr) { screen_ = Screen::PlayerIce; break; }
            if (selected_ == 1) { screen_ = Screen::PlayerIce; selected_ = selectedPlayerIce_; break; }
            if (ice->instance.active())
            {
                const ProgramActionResult result = game_.deactivatePlayerBlackIce(selectedPlayerIce_);
                showMessage(ice->definition != nullptr ? ice->definition->displayName : "BLACK ICE", result.executed ? "INACTIVE" : "ACTION REJECTED", Screen::PlayerIce);
                if (result.executed) scheduleAutomaticTurnEndIfNeeded();
                break;
            }
            playerIceTargetCount_ = 0;
            for (size_t i = 0; i < game_.enemyNetrunnerCount() && playerIceTargetCount_ < MAX_ENEMY_NETRUNNERS; ++i)
            {
                const EnemyNetrunnerRuntime* enemy = game_.enemyNetrunnerAt(i);
                if (enemy != nullptr && enemy->available() && enemy->currentFloor == game_.architecture().currentPosition())
                    playerIceTargetIds_[playerIceTargetCount_++] = enemy->runtimeId;
            }
            if (playerIceTargetCount_ == 0) { showMessage(ice->definition != nullptr ? ice->definition->displayName : "BLACK ICE", "NO TARGET", Screen::PlayerIce); break; }
            if (playerIceTargetCount_ == 1)
            {
                const ProgramActionResult result = game_.activatePlayerBlackIce(selectedPlayerIce_, playerIceTargetIds_[0]);
                if (result.executed)
                {
                    const EnemyNetrunnerRuntime* enemy = game_.enemyNetrunnerByRuntimeId(playerIceTargetIds_[0]);
                    snprintf(resultTitle_, sizeof(resultTitle_), "BLACK ICE REZZED");
                    snprintf(resultLines_[0], sizeof(resultLines_[0]), "%s", ice->definition != nullptr ? ice->definition->displayName : "BLACK ICE");
                    snprintf(resultLines_[1], sizeof(resultLines_[1]), "TARGET");
                    snprintf(resultLines_[2], sizeof(resultLines_[2]), "%s", enemy != nullptr ? enemy->definition->name : "ENEMY");
                    resultToneExplicit_ = false;
                    resultInfoLayout_ = InfoLayout::Automatic;
                    resultLineCount_ = 3; resultNextScreen_ = Screen::PlayerIce; screen_ = Screen::Result;
                    scheduleAutomaticTurnEndIfNeeded();
                }
                else showMessage(ice->definition != nullptr ? ice->definition->displayName : "BLACK ICE", "ACTION REJECTED", Screen::PlayerIce);
            }
            else { screen_ = Screen::PlayerIceTarget; selected_ = 0; }
            break;
        }
        case Screen::PlayerIceTarget:
            if (selected_ >= playerIceTargetCount_) { screen_ = Screen::PlayerIceAction; selected_ = 0; break; }
            if (game_.activatePlayerBlackIce(selectedPlayerIce_, playerIceTargetIds_[selected_]).executed)
            {
                PlayerBlackIceRuntime* ice = game_.playerBlackIceAt(selectedPlayerIce_);
                const EnemyNetrunnerRuntime* enemy = game_.enemyNetrunnerByRuntimeId(playerIceTargetIds_[selected_]);
                snprintf(resultTitle_, sizeof(resultTitle_), "BLACK ICE REZZED");
                snprintf(resultLines_[0], sizeof(resultLines_[0]), "%s", ice->definition != nullptr ? ice->definition->displayName : "BLACK ICE");
                snprintf(resultLines_[1], sizeof(resultLines_[1]), "TARGET");
                snprintf(resultLines_[2], sizeof(resultLines_[2]), "%s", enemy != nullptr ? enemy->definition->name : "ENEMY");
                resultToneExplicit_ = false;
                resultInfoLayout_ = InfoLayout::Automatic;
                resultLineCount_ = 3; resultNextScreen_ = Screen::PlayerIce; screen_ = Screen::Result;
                scheduleAutomaticTurnEndIfNeeded();
            }
            else showMessage("BLACK ICE", "ACTION REJECTED", Screen::PlayerIce);
            break;
        case Screen::Move:
        {
            const Architecture& architecture = game_.architecture();
            const size_t choices = architecture.connectionCount(architecture.currentPosition());
            if (selected_ < choices) executeMovement(movementChoiceDestinationFor(game_.architecture(), selected_));
            else { screen_ = Screen::Floor; selected_ = 0; }
            break;
        }
        case Screen::Result:
            if (encounterSummaryPending_)
            {
                encounterSummaryPending_ = false;
                if (queuedIceResultCount_ > 0) showNextQueuedIceResult();
                else if (unsafeExitPresentationPending_) finishPendingUnsafeExitPresentation();
                else { screen_ = game_.runState() == RunState::RunnerDown ? Screen::RunnerDown :
                    game_.isAwaitingEndDecision() ? Screen::VirusPrompt :
                    game_.runState() == RunState::JackedOut ? Screen::RunSummary :
                    game_.runState() == RunState::Completed ? Screen::ArchitectureComplete : Screen::Floor; selected_ = 0; }
            }
            else if (queuedIceResultCount_ > 0)
            {
                ++queuedIceResultIndex_;
                if (queuedIceResultIndex_ < queuedIceResultCount_)
                {
                    showNextQueuedIceResult();
                }
                else
                {
                    const Screen next = queuedIceNextScreen_;
                    clearQueuedIceResults();
                    if (unsafeExitPresentationPending_)
                    {
                        finishPendingUnsafeExitPresentation();
                    }
                    else if (next == Screen::Result)
                    {
                        // Result is a phase-flow sentinel for Player-ICE and
                        // Enemy queues. Do not leave an empty Result screen
                        // waiting for another input; immediately continue the
                        // next valid automatic phase.
                        pendingEnemyPhase_ = false;
                        pendingIcePhase_ = false;
                        advanceSilentPhases();
                    }
                    else
                    {
                        screen_ = next;
                        selected_ = 0;
                    }
                }
            }
            else if (demonPhaseActionIndex_ < demonPhaseResult_.actionCount)
            {
                showNextDemonPhaseAction();
            }
            else
            if (fireTickFeedbackPending_)
            {
                fireTickFeedbackPending_ = false;
                advanceSilentPhases();
            }
            else
            if (pendingEnemyPhase_)
            {
                pendingEnemyPhase_ = false;
                game_.runEnemyPhase(rules_, enemyPhaseResult_);
                afterIceScreen_ = Screen::Floor;
                if (enemyPhaseResult_.actionCount > 0) { showEnemyPresenceIfPending(); queueEnemyPhaseResults(enemyPhaseResult_); }
                else { pendingIcePhase_ = false; advanceSilentPhases(); }
            }
            else if (pendingIcePhase_)
            {
                pendingIcePhase_ = false;
                if (game_.runState() == RunState::RunnerDown) { screen_ = Screen::RunnerDown; break; }
                if (game_.isAwaitingEndDecision()) { screen_ = Screen::VirusPrompt; break; }
                game_.updatePlayerTurn();
                const uint8_t fireDamage = game_.consumeRunnerFireTickDamage();
                if (game_.runState() == RunState::RunnerDown)
                {
                    runEndCause_ = RunEndCause::RunnerDown;
                    screen_ = Screen::RunnerDown;
                    selected_ = 0;
                    break;
                }
                if (fireDamage > 0 && game_.runState() == RunState::JackedIn)
                {
                    char fireLine[20] = {};
                    snprintf(fireLine, sizeof(fireLine), "FIRE -%u HP", fireDamage);
                    fireTickFeedbackPending_ = true;
                    pendingIcePhase_ = true;
                    display_.animateFireDamage();
                    showMessage("FIRE", fireLine, Screen::Result);
                    resultToneExplicit_ = true;
                    resultTone_ = FeedbackTone::Danger;
                    break;
                }
                game_.runIcePhase(rules_, icePhaseResult_);
                if (icePhaseResult_.attackCount > 0)
                    queueIcePhaseResults(icePhaseResult_, game_.runState() == RunState::RunnerDown ? Screen::RunnerDown :
                    icePhaseResult_.runTerminated ? Screen::RunSummary : afterIceScreen_);
                else { pendingIcePhase_ = false; advanceSilentPhases(); }
            }
            else { screen_ = resultNextScreen_; selected_ = 0; }
            break;
        case Screen::EnemyAttack:
        {
            Program* program = selected_ < enemyMenuCount_
                ? game_.cyberdeck().programAt(enemyMenuSlots_[selected_]) : nullptr;
            if (program == nullptr || program->type() != ProgramType::Attacker ||
                program->id() == ProgramId::Sword || program->id() == ProgramId::Banhammer || !program->usable())
            { screen_ = Screen::Floor; selected_ = 0; break; }
            const CombatResult result = game_.attackFloorEnemy(rules_, *program);
            const EnemyNetrunnerRuntime* enemy = game_.floorEnemyNetrunner();
            showCombatResult(program->name(), result, true, true,
                enemy != nullptr && enemy->definition != nullptr ? enemy->definition->name : "ENEMY",
                playerEnemyRunnerVisual(program->id()), true);
            scheduleAutomaticTurnEndIfNeeded();
            break;
        }
        case Screen::EnemyProgramTarget:
        {
            EnemyNetrunnerRuntime* enemy = game_.floorEnemyNetrunner();
            Program* target = selected_ < enemyMenuCount_ && enemy != nullptr
                ? enemy->cyberdeck.programAt(enemyMenuSlots_[selected_]) : nullptr;
            if (target == nullptr || !target->usable()) { screen_ = Screen::Floor; selected_ = 0; break; }
            selectedEnemyProgram_ = enemyMenuSlots_[selected_]; screen_ = Screen::EnemyProgramAttack; selected_ = 0; break;
        }
        case Screen::EnemyProgramAttack:
        {
            Program* program = selected_ < enemyMenuCount_
                ? game_.cyberdeck().programAt(enemyMenuSlots_[selected_]) : nullptr;
            if (program == nullptr || (program->id() != ProgramId::Sword && program->id() != ProgramId::Banhammer) || !program->usable())
            { screen_ = Screen::EnemyProgramTarget; selected_ = selectedEnemyProgram_; break; }
            const CombatResult result = game_.attackFloorEnemyProgram(rules_, *program, selectedEnemyProgram_);
            const EnemyNetrunnerRuntime* enemy = game_.floorEnemyNetrunner();
            const Program* target = enemy != nullptr
                ? enemy->cyberdeck.programAt(selectedEnemyProgram_) : nullptr;
            showCombatResult(program->name(), result, false, false,
                target != nullptr ? target->name() : "PROGRAM",
                program->id() == ProgramId::Sword ? PlayerActionVisual::Sword :
                PlayerActionVisual::Banhammer, true);
            scheduleAutomaticTurnEndIfNeeded();
            break;
        }
        case Screen::JackOut:
            if (selected_ == 0)
            {
                if (!game_.canSafeJackOut())
                {
                    // Enter the dedicated confirmation state.  A transient
                    // message would return to its default Floor destination
                    // and bypass the authoritative unsafe-jack-out path.
                    screen_ = Screen::UnsafeJackOutPrompt;
                    selected_ = 0;
                    break;
                }
                bool complete = false;
                completedFile_ = false;
                for (size_t index = 0; index < game_.architecture().floorCount(); ++index) {
                    const Floor* floor = game_.architecture().floorAt(index);
                    if (floor->type == FloorType::ControlNode && floor->controlled) complete = true;
                    if (floor->type == FloorType::File && floor->downloaded) completedFile_ = true;
                }
                completedTurns_ = game_.turnNumber();
                completedControl_ = complete;
                if (game_.jackOut()) showRunSummary(RunEndCause::SafeJackOut);
            }
            else screen_ = Screen::Floor;
            selected_ = 0; break;
        case Screen::UnsafeJackOutPrompt:
            if (selected_ == 0)
            {
                if (game_.unsafeJackOut(rules_, movementResult_))
                {
                    if (game_.runState() == RunState::RunnerDown)
                    {
                        runEndCause_ = RunEndCause::RunnerDown;
                        screen_ = Screen::RunnerDown;
                        selected_ = 0;
                    }
                    else
                    {
                        display_.animateConnectionLoss();
                        showRunSummary(RunEndCause::UnsafeJackOut);
                    }
                }
                else screen_ = Screen::Floor;
            }
            else screen_ = Screen::Floor;
            selected_ = 0;
            break;
        case Screen::Objective:
            screen_ = game_.isAwaitingEndDecision() ? Screen::VirusPrompt : Screen::Floor;
            selected_ = 0;
            break;
        case Screen::RunSummary: screen_ = Screen::Main; selected_ = 0; break;
        case Screen::RunComplete: screen_ = Screen::Main; selected_ = 0; break;
        case Screen::ArchitectureComplete: screen_ = Screen::Main; selected_ = 0; break;
        case Screen::VirusPrompt:
            if (selected_ == 0) { display_.animateTransfer("UPLOADING VIRUS"); game_.finishArchitectureEnd(true); }
            screen_ = Screen::Floor; selected_ = 0; break;
        case Screen::RunnerDown:
            runEndCause_ = RunEndCause::RunnerDown;
            runSummaryRevealed_ = false;
            screen_ = Screen::RunSummary;
            selected_ = 0;
            break;
        case Screen::ScenarioError: screen_ = Screen::SelectRun; selected_ = 0; break;
    }
    redraw();
}

void GameUIController::goBack()
{
    switch (screen_)
    {
        case Screen::Main: return;
        case Screen::PlayerMenu: screen_ = Screen::Main; break;
        case Screen::Profile: screen_ = Screen::PlayerMenu; break;
        case Screen::ProfileHandleEdit: screen_ = Screen::Profile; break;
        case Screen::ProfileNumberEdit:
            if (selected_ == 1) profile_.interfaceRank = profileEditOriginal_;
            else profile_.maxHp = profileEditOriginal_;
            screen_ = Screen::Profile; break;
        case Screen::DeckEditor:
            if (deckEditorDirty_) { screen_ = Screen::DeckUnsavedChanges; selected_ = 0; }
            else screen_ = Screen::PlayerMenu;
            break;
        case Screen::DeckItemInfo: screen_ = deckInfoIsBlackIce_ ? Screen::BlackIceSelect : Screen::ProgramSelect; break;
        case Screen::ProgramSelect: screen_ = Screen::DeckEditor; break;
        case Screen::HardwareSelect: screen_ = Screen::DeckEditor; break;
        case Screen::BlackIceSelect: screen_ = Screen::DeckEditor; break;
        case Screen::DeckUnsavedChanges: screen_ = Screen::DeckEditor; selected_ = 0; break;
        case Screen::SelectRun: screen_ = Screen::Main; break;
        case Screen::RunDetail: screen_ = Screen::SelectRun; break;
        case Screen::ScenarioError: screen_ = Screen::SelectRun; break;
        case Screen::Runner:
        case Screen::Cyberdeck: screen_ = Screen::Main; break;
        case Screen::Programs: screen_ = previousScreen_; break;
        case Screen::ProgramAction: screen_ = Screen::Programs; break;
        case Screen::PlayerIce: screen_ = Screen::Floor; break;
        case Screen::PlayerIceAction: screen_ = Screen::PlayerIce; selected_ = selectedPlayerIce_; break;
        case Screen::PlayerIceTarget: screen_ = Screen::PlayerIceAction; break;
        case Screen::Move:
        case Screen::Map:
        case Screen::JackOut:
        case Screen::UnsafeJackOutPrompt: screen_ = Screen::Floor; break;
        case Screen::Result:
            if (pendingIcePhase_ || queuedIceResultCount_ > 0) { activateSelection(); return; }
            screen_ = resultNextScreen_; break;
        case Screen::Objective: screen_ = game_.isAwaitingEndDecision() ? Screen::VirusPrompt : Screen::Floor; break;
        case Screen::ArchitectureComplete: screen_ = Screen::Main; break;
        case Screen::VirusPrompt: screen_ = Screen::Floor; break;
        case Screen::RunnerDown:
            runEndCause_ = RunEndCause::RunnerDown;
            runSummaryRevealed_ = false;
            screen_ = Screen::RunSummary;
            break;
        case Screen::EnemyAttack:
        case Screen::EnemyProgramTarget:
        case Screen::EnemyProgramAttack: screen_ = Screen::Floor; break;
        case Screen::EnemyAnimTest: screen_ = Screen::PlayerMenu; break;
        case Screen::Floor:
        case Screen::RunSummary:
        case Screen::RunComplete: return;
    }
    selected_ = 0; redraw();
}

void GameUIController::buildFloorActions()
{
    floorActionCount_ = 0;
    Floor* floor = game_.architecture().currentFloor();
    const FloorEntityKind focus = focusedFloorEntityKind();
    BlackIceInstance* combatTarget = focus == FloorEntityKind::BlackIce ? selectedIceTarget() : nullptr;
    EnemyNetrunnerRuntime* enemyTarget = focus == FloorEntityKind::EnemyNetrunner ? focusedEnemy() : nullptr;
    #define ADD_ACTION(value, label) addFloorAction(value, label)
    if (!game_.virusPlaced() && game_.hasReachedArchitectureEnd())
        ADD_ACTION(Action::PlaceVirus, "PLACE VIRUS");
    // Keep the most urgent local status/action at the top of the Cardputer
    // list. Combat group ordering below remains unchanged.
    if (game_.runnerOnFire()) ADD_ACTION(Action::Extinguish, "EXTINGUISH");
    if (combatTarget != nullptr)
    {
        ADD_ACTION(Action::Zap, "ZAP");
        if (game_.cyberdeck().findUsableProgram(ProgramId::Sword) != nullptr) ADD_ACTION(Action::Sword, "SWORD");
        if (game_.cyberdeck().findUsableProgram(ProgramId::Banhammer) != nullptr) ADD_ACTION(Action::Banhammer, "BANHAMMER");
        if (!game_.runner().slideUsedThisTurn()) ADD_ACTION(Action::Slide, "SLIDE");
    }
    if (enemyTarget != nullptr)
    {
        ADD_ACTION(Action::Zap, "ZAP");
        ADD_ACTION(Action::AttackEnemy, "ATTACK");
        ADD_ACTION(Action::TargetEnemyProgram, "TARGET PROG");
    }
    if (game_.demon().active && game_.demon().currentRez > 0 && game_.runner().remainingNetActions() > 0)
    {
        static char demonZapLabel[24]; static char demonSwordLabel[24];
        const char* demonName = game_.demon().definition != nullptr ? game_.demon().definition->displayName : "DEMON";
        snprintf(demonZapLabel, sizeof(demonZapLabel), "ZAP %s", demonName);
        snprintf(demonSwordLabel, sizeof(demonSwordLabel), "SWORD %s", demonName);
        ADD_ACTION(Action::DemonZap, demonZapLabel);
        if (game_.cyberdeck().findUsableProgram(ProgramId::Sword) != nullptr) ADD_ACTION(Action::DemonSword, demonSwordLabel);
    }
    const bool floorObjectSelected = focus == FloorEntityKind::FloorObject;
    if (floorObjectSelected && floor != nullptr && floor->type == FloorType::Password && !floor->resolved)
        ADD_ACTION(Action::Backdoor, "BACKDOOR");
    else if (floorObjectSelected && floor != nullptr && floor->type == FloorType::File)
    {
        if (!floor->identified) ADD_ACTION(Action::EyeDee, "EYE-DEE");
        else if (!floor->downloaded) ADD_ACTION(Action::Download, "DOWNLOAD");
    }
    else if (floorObjectSelected && floor != nullptr && floor->type == FloorType::ControlNode && !floor->controlled)
        ADD_ACTION(Action::Control, "CONTROL");

    // Keep the Programs entry available whenever programs are installed. It
    // is a status/configuration view as well as an action view; filtering on
    // current action executability would incorrectly hide useful programs.
    if (game_.cyberdeck().programCount() > 0) ADD_ACTION(Action::Programs, "PROGRAMS");
    if (game_.playerBlackIceCount() > 0) ADD_ACTION(Action::PlayerIce, "BLACK ICE");
    if (game_.runner().remainingNetActions() > 0)
    {
        ADD_ACTION(Action::Pathfinder, "PATHFINDER");
        ADD_ACTION(Action::Cloak, "CLOAK");
    }
    ADD_ACTION(Action::Map, "MAP");
    ADD_ACTION(Action::Move, "MOVE");
    ADD_ACTION(Action::EndTurn, "END TURN");
    ADD_ACTION(Action::JackOut, "JACK OUT");
    #undef ADD_ACTION
    if (selected_ >= floorActionCount_) selected_ = 0;
}

bool GameUIController::addFloorAction(Action action, const char* label)
{
    if (floorActionCount_ >= MAX_FLOOR_ACTIONS)
    {
        Serial.println("[FloorActions] capacity reached");
        return false;
    }
    floorActions_[floorActionCount_] = action;
    floorActionLabels_[floorActionCount_++] = label;
    return true;
}

bool GameUIController::appendFloorEntity(FloorEntityKind kind, uint16_t runtimeId)
{
    if (floorEntityCount_ >= FLOOR_ENTITY_CAPACITY)
    {
        Serial.println("[Entities] OVERFLOW");
        return false;
    }
    floorEntities_[floorEntityCount_].kind = kind;
    floorEntities_[floorEntityCount_++].runtimeId = runtimeId;
    return true;
}

void GameUIController::executeFloorAction(Action action)
{
    Floor* floor = game_.architecture().currentFloor();
    afterIceScreen_ = Screen::Floor;
    switch (action)
    {
        case Action::PlaceVirus:
            if (game_.hasReachedArchitectureEnd() && !game_.virusPlaced() && game_.completeIfArchitectureEnd())
            {
                screen_ = Screen::VirusPrompt;
                selected_ = 0;
            }
            break;
        case Action::Backdoor:
        {
            const NetCheckResult result = rules_.backdoor(game_.runner(), game_.cyberdeck(), *floor);
            showNetResult("BACKDOOR", result, true, SystemActionVisual::Backdoor);
            break;
        }
        case Action::EyeDee: showNetResult("EYE-DEE", rules_.eyeDee(game_.runner(), *floor), true,
            SystemActionVisual::EyeDee); break;
        case Action::Control:
        {
            if (game_.runner().remainingNetActions() == 0)
            {
                showMessage("ACTION REJECTED", "NO ACT LEFT");
                break;
            }
            NetCheckResult result = rules_.control(game_.runner(), *floor);
            showNetResult("CONTROL", result, true, SystemActionVisual::Control);
            break;
        }
        case Action::Download:
            if (game_.runner().remainingNetActions() == 0)
                showMessage("ACTION REJECTED", "NO ACT LEFT");
            else
            {
                const bool downloaded = rules_.downloadFile(game_.runner(), *floor);
                if (downloaded) display_.animateDataTransfer();
                else display_.animateDataTransferFailure();
                showMessage("DOWNLOAD", downloaded ? "FILE DOWNLOADED" : "DOWNLOAD REJECTED");
            }
            break;
        case Action::Pathfinder:
        {
            const PathfinderResult pathfinder = game_.pathfinder(rules_);
            showNetResult("PATHFINDER", pathfinder.check, false, SystemActionVisual::Pathfinder);
            if (pathfinder.check.attempted && resultLineCount_ > 0)
            {
                snprintf(resultLines_[resultLineCount_ - 1], sizeof(resultLines_[0]), "%u FLOORS DISCOVERED", pathfinder.discoveredCount);
                if (pathfinder.obstructionDetected && resultLineCount_ < 7)
                    snprintf(resultLines_[resultLineCount_++], sizeof(resultLines_[0]), "OBSTRUCTION DETECTED");
            }
            break;
        }
        case Action::Cloak:
        {
            const NetCheckResult cloak = game_.cloak(rules_);
            showNetResult("CLOAK", cloak, false, SystemActionVisual::Cloak);
            if (cloak.attempted && resultLineCount_ > 0)
                snprintf(resultLines_[resultLineCount_ - 1], sizeof(resultLines_[0]), "TRACE MASKED");
            break;
        }
        case Action::Zap:
            if (focusedFloorEntityKind() == FloorEntityKind::BlackIce)
            {
                if (BlackIceInstance* ice = selectedIceTarget()) executeCombat(action, *ice);
                else showMessage("ZAP", "NO BLACK ICE");
            }
            else if (focusedFloorEntityKind() == FloorEntityKind::EnemyNetrunner)
            {
                if (EnemyNetrunnerRuntime* enemy = focusedEnemy())
                {
                    const CombatResult result = game_.zapFloorEnemy(rules_, enemy->runtimeId);
                    const char* enemyName = enemy->definition != nullptr ? enemy->definition->name : "ENEMY";
                    showCombatResult("PLAYER", result, true, true, enemyName, PlayerActionVisual::Zap);
                }
                else showMessage("ZAP", "NO ENEMY");
            }
            else showMessage("ZAP", "NO TARGET");
            break;
        case Action::Sword:
        case Action::Banhammer:
            if (focusedFloorEntityKind() == FloorEntityKind::BlackIce &&
                selectedIceTarget() != nullptr) executeCombat(action, *selectedIceTarget());
            else showMessage("BLACK ICE", "NO TARGET");
            break;
        case Action::Slide:
            if (focusedFloorEntityKind() == FloorEntityKind::BlackIce &&
                selectedIceTarget() != nullptr) executeCombat(action, *selectedIceTarget());
            else showMessage("SLIDE", "NO BLACK ICE");
            break;
        case Action::Programs: previousScreen_ = Screen::Floor; screen_ = Screen::Programs; selected_ = 0; break;
        case Action::PlayerIce: screen_ = Screen::PlayerIce; selected_ = 0; break;
        case Action::DemonZap:
        {
            const CombatResult result = rules_.zapDemon(game_.runner(), game_.demon());
            const char* demonName = game_.demon().definition != nullptr ? game_.demon().definition->displayName : "DEMON";
            if (result.executed && result.success && !game_.demon().active)
            {
                display_.animatePlayerAction(PlayerActionVisual::Zap, FeedbackTone::Info);
                showMessage("DEMON DEREZZED", demonName, Screen::Floor);
            }
            else { snprintf(resultTitle_, sizeof(resultTitle_), "DEMON // %s", demonName); showCombatResult(resultTitle_, result, false, false, game_.runner().handle(), PlayerActionVisual::Zap); }
            break;
        }
        case Action::DemonSword:
        {
            const CombatResult result = rules_.swordAttackDemon(game_.runner(), game_.cyberdeck(), game_.demon());
            const char* demonName = game_.demon().definition != nullptr ? game_.demon().definition->displayName : "DEMON";
            if (result.executed && result.success && !game_.demon().active)
            {
                display_.animatePlayerAction(PlayerActionVisual::Sword, FeedbackTone::Info);
                showMessage("DEMON DEREZZED", demonName, Screen::Floor);
            }
            else { snprintf(resultTitle_, sizeof(resultTitle_), "DEMON // %s", demonName); showCombatResult(resultTitle_, result, false, false, game_.runner().handle(), PlayerActionVisual::Sword); }
            break;
        }
        case Action::Move: screen_ = Screen::Move; selected_ = 0; break;
        case Action::Map:
            buildArchitectureMapView(game_, mapView_);
            mapPanX_ = 0; mapPanY_ = 0; screen_ = Screen::Map; selected_ = 0; break;
        case Action::EndTurn: finishPlayerTurn(); break;
        case Action::JackOut:
            if (!game_.canSafeJackOut()) { screen_ = Screen::UnsafeJackOutPrompt; selected_ = 0; }
            else { screen_ = Screen::JackOut; selected_ = 0; }
            break;
        case Action::AttackEnemy: screen_ = Screen::EnemyAttack; selected_ = 0; break;
        case Action::TargetEnemyProgram: screen_ = Screen::EnemyProgramTarget; selected_ = 0; break;
        case Action::Extinguish:
            showMessage("FIRE", game_.extinguishRunnerFire() ? "FIRE EXTINGUISHED" : "NO FIRE");
            break;
    }
    scheduleAutomaticTurnEndIfNeeded();
}

void GameUIController::scheduleAutomaticTurnEndIfNeeded()
{
    if (!shouldScheduleAutomaticTurnEndForDebug(game_.runState(), game_.turnPhase(),
            game_.runner().remainingNetActions(), screen_ == Screen::Result,
            game_.isAwaitingEndDecision()))
        return;

    pendingIcePhase_ = true;
}

bool GameUIController::shouldScheduleAutomaticTurnEndForDebug(RunState runState,
                                                               TurnPhase turnPhase,
                                                               uint8_t remainingNetActions,
                                                               bool resultScreen,
                                                               bool awaitingEndDecision)
{
    return resultScreen && runState == RunState::JackedIn && turnPhase == TurnPhase::Player &&
        !awaitingEndDecision && remainingNetActions == 0;
}

void GameUIController::executeCombat(Action action, BlackIceInstance& ice)
{
    CombatResult result;
    const char* title = "BLACK ICE";
    if (action == Action::Zap)
    {
        title = "ZAP";
        result = rules_.zap(game_.runner(), ice);
    }
    else if (action == Action::Sword)
    {
        title = "SWORD.EXE";
        result = rules_.swordAttack(game_.runner(), game_.cyberdeck(), ice);
    }
    else if (action == Action::Banhammer)
    {
        title = "BANHAMMER.EXE";
        result = rules_.banhammerAttack(game_.runner(), game_.cyberdeck(), ice);
    }
    else
    {
        title = "SLIDE";
        result = rules_.slide(game_.runner(), ice, game_.activeSlidePenalty());
        if (result.success) game_.recordBlackIcePosition(ice);
    }
    const char* targetName = ice.definition() != nullptr ? ice.definition()->displayName : "BLACK ICE";
    const PlayerActionVisual visual = action == Action::Zap ? PlayerActionVisual::Zap :
        action == Action::Sword ? PlayerActionVisual::Sword :
        action == Action::Banhammer ? PlayerActionVisual::Banhammer : PlayerActionVisual::Slide;
    showCombatResult("PLAYER", result, false, false, targetName, visual);
    if (action == Action::Slide)
    {
        // Replace the generic attack wording before the result screen is
        // rendered, keeping one unambiguous Slide result page.
        snprintf(resultLines_[0], sizeof(resultLines_[0]), ">> %s", targetName);
        snprintf(resultLines_[1], sizeof(resultLines_[1]), "SLIDE");
        snprintf(resultLines_[2], sizeof(resultLines_[2]), "%s", result.success ? "SUCCESS" : "FAIL");
        resultLineCount_ = 3;
    }
}

void GameUIController::adjustProfileValue(int direction)
{
    if (selected_ == 1)
    {
        const int value = static_cast<int>(profile_.interfaceRank) + direction;
        if (value >= 1 && value <= 10) profile_.interfaceRank = static_cast<uint8_t>(value);
    }
    else if (selected_ == 2)
    {
        const int value = static_cast<int>(profile_.maxHp) + direction;
        if (value >= 1 && value <= 99) profile_.maxHp = static_cast<uint8_t>(value);
    }
}

void GameUIController::beginHandleEdit()
{
    snprintf(handleEdit_, sizeof(handleEdit_), "%s", profile_.handle);
    handleEditLength_ = strnlen(handleEdit_, sizeof(handleEdit_));
    screen_ = Screen::ProfileHandleEdit;
}

void GameUIController::beginNumberEdit()
{
    profileEditOriginal_ = selected_ == 1 ? profile_.interfaceRank : profile_.maxHp;
    screen_ = Screen::ProfileNumberEdit;
}

bool GameUIController::savePlayerConfig()
{
    if (!configStore_.save(profile_, deckConfig_)) return false;
    configStore_.load(profile_, deckConfig_);
    return game_.setRunnerProfile(profile_) && game_.setCyberdeckConfig(deckConfig_);
}

bool GameUIController::saveDeckEditor()
{
    if (!cyberdeckConfigValid(editorDeckConfig_)) return false;
    const CyberdeckConfig committed = deckConfig_;
    deckConfig_ = editorDeckConfig_;
    if (!savePlayerConfig())
    {
        deckConfig_ = committed;
        return false;
    }
    editorDeckConfig_ = deckConfig_;
    deckEditorDirty_ = false;
    return true;
}

void GameUIController::beginDeckEditor()
{
    editorDeckConfig_ = deckConfig_;
    deckEditorDirty_ = false;
    screen_ = Screen::DeckEditor;
    selected_ = 0;
}

void GameUIController::discardDeckEditor()
{
    editorDeckConfig_ = deckConfig_;
    deckEditorDirty_ = false;
}

void GameUIController::refreshDeckEditorDirty()
{
    deckEditorDirty_ = !deckConfigsEqual(editorDeckConfig_, deckConfig_);
}

bool GameUIController::deckConfigsEqual(const CyberdeckConfig& left, const CyberdeckConfig& right)
{
    if (left.quality != right.quality || left.programCount != right.programCount ||
        left.hardwareCount != right.hardwareCount || left.playerBlackIceCount != right.playerBlackIceCount)
        return false;
    for (size_t index = 0; index < left.programCount; ++index)
        if (left.programs[index] != right.programs[index]) return false;
    for (size_t index = 0; index < left.hardwareCount; ++index)
        if (left.hardware[index] != right.hardware[index]) return false;
    for (size_t index = 0; index < left.playerBlackIceCount; ++index)
        if (left.playerBlackIce[index] != right.playerBlackIce[index]) return false;
    return true;
}

bool GameUIController::trySetDeckProgram(size_t slot, ProgramId id)
{
    CyberdeckConfig candidate = editorDeckConfig_;
    if (slot < candidate.programCount)
    {
        if (id == ProgramId::None)
        {
            for (size_t index = slot + 1; index < candidate.programCount; ++index)
                candidate.programs[index - 1] = candidate.programs[index];
            --candidate.programCount;
        }
        else candidate.programs[slot] = id;
    }
    else if (slot == candidate.programCount && id != ProgramId::None &&
             candidate.programCount < MAX_CYBERDECK_PROGRAMS)
    {
        candidate.programs[candidate.programCount++] = id;
    }
    else return id == ProgramId::None;
    if (!cyberdeckConfigValid(candidate)) return false;
    editorDeckConfig_ = candidate;
    refreshDeckEditorDirty();
    return true;
}

bool GameUIController::trySetHardware(size_t slot, HardwareId id)
{
    CyberdeckConfig candidate = editorDeckConfig_;
    if (slot < candidate.hardwareCount)
    {
        if (id == HardwareId::Count)
        {
            for (size_t index = slot + 1; index < candidate.hardwareCount; ++index)
                candidate.hardware[index - 1] = candidate.hardware[index];
            --candidate.hardwareCount;
        }
        else candidate.hardware[slot] = id;
    }
    else if (slot == candidate.hardwareCount && id != HardwareId::Count &&
             candidate.hardwareCount < MAX_CYBERDECK_HARDWARE)
    {
        candidate.hardware[candidate.hardwareCount++] = id;
    }
    else return id == HardwareId::Count;
    if (!cyberdeckConfigValid(candidate)) return false;
    editorDeckConfig_ = candidate;
    refreshDeckEditorDirty();
    return true;
}

bool GameUIController::trySetPlayerBlackIce(size_t slot, BlackIceType type)
{
    if (slot > editorDeckConfig_.playerBlackIceCount) return false;
    CyberdeckConfig candidate = editorDeckConfig_;
    if (type == BlackIceType::Count)
    {
        if (slot >= candidate.playerBlackIceCount) return true;
        for (size_t i = slot + 1; i < candidate.playerBlackIceCount; ++i)
            candidate.playerBlackIce[i - 1] = candidate.playerBlackIce[i];
        --candidate.playerBlackIceCount;
    }
    else
    {
        if (!playerBlackIceSupported(type)) return false;
        if (slot == candidate.playerBlackIceCount)
        {
            if (candidate.playerBlackIceCount >= MAX_PLAYER_BLACK_ICE) return false;
            ++candidate.playerBlackIceCount;
        }
        candidate.playerBlackIce[slot] = type;
    }
    if (!cyberdeckConfigValid(candidate)) return false;
    editorDeckConfig_ = candidate;
    refreshDeckEditorDirty();
    return true;
}

void GameUIController::cycleDeckQuality()
{
    CyberdeckConfig candidate = editorDeckConfig_;
    candidate.quality = candidate.quality == CyberdeckQuality::Poor ? CyberdeckQuality::Standard :
        candidate.quality == CyberdeckQuality::Standard ? CyberdeckQuality::Excellent : CyberdeckQuality::Poor;
    if (cyberdeckConfigValid(candidate)) { editorDeckConfig_ = candidate; refreshDeckEditorDirty(); }
    else showMessage("CYBERDECK", "REMOVE PROGRAMS FIRST", Screen::DeckEditor);
}

void GameUIController::executeMovement(uint8_t destination)
{
    if (destination == Architecture::NO_FLOOR)
    {
        display_.animateFloorMovementFailure();
        showMessage("MOVE", "NO CONNECTION");
        return;
    }
    const uint8_t currentPosition = static_cast<uint8_t>(game_.architecture().currentPosition());
    const bool traceFloor2 = destination == 1 && isCombatFloor2(game_, true);
    if (traceFloor2)
    {
        Serial.println("[F2] 01 movement accepted");
        logFloor2Memory(1);
        Serial.println("[F2] 02 before GameState movement");
    }
    // EncounterResult contains the complete fixed ICE-result queue. Keeping it
    // as controller state and using the out-parameter avoids a large temporary
    // on Arduino's loopTask stack during a floor transition.
    game_.moveToConnectedFloor(rules_, destination, movementResult_);
    const EncounterResult& result = movementResult_;
    if (traceFloor2)
    {
        Serial.println("[F2] 13 after GameState movement");
        logFloor2Memory(13);
    }
    if (!result.moved) { display_.animateFloorMovementFailure(); showMessage("MOVE", "MOVEMENT BLOCKED"); return; }
    const int8_t direction = destination == currentPosition + 1 ? 1 :
        destination + 1 == currentPosition ? -1 : 0;
    display_.animateFloorMovement(static_cast<uint8_t>(destination + 1), direction);
    if (traceFloor2) Serial.println("[F2] 14 before presentation state update");
    selectedFloorEntity_ = 0;
    selectedIceRuntimeId_ = 0;
    floorEntityCount_ = 0;
    // Presentation priority for a freshly entered mixed floor: an active
    // hostile Netrunner is actionable information. Encounter results still
    // take precedence below, before the normal floor view is displayed.
    buildFloorEntities();
    for (size_t index = 0; index < floorEntityCount_; ++index)
        if (floorEntities_[index].kind == FloorEntityKind::EnemyNetrunner)
        {
            selectedFloorEntity_ = index;
            break;
        }
    if (result.triggered)
    {
        const BlackIceInstance* encountered = result.iceCount > 0
            ? game_.engagedBlackIceByRuntimeId(result.ice[0].runtimeId) : nullptr;
        const char* iceName = encountered != nullptr && encountered->definition() != nullptr
            ? encountered->definition()->displayName : "BLACK ICE";
        display_.animateEncounterIntro(iceName);
        for (size_t index = 0; index < result.iceCount; ++index)
        {
            if (!result.ice[index].newlyEncountered) continue;
            const BlackIceInstance* ice = game_.engagedBlackIceByRuntimeId(result.ice[index].runtimeId);
            const BlackIceDefinition* definition = ice != nullptr ? ice->definition() : nullptr;
            display_.animateHostileAppearance(
                blackIceFloorVisual(definition != nullptr ? definition->type : BlackIceType::Ice01),
                definition != nullptr ? definition->displayName : "BLACK ICE", 0,
                definition != nullptr ? definition->visualId : IceVisualId::Hound01);
        }
        for (size_t index = 0; index < result.iceCount; ++index)
        {
            const CombatResult& attack = result.ice[index].immediateAttack;
            if (!attack.executed || attack.noValidTarget) continue;
            const BlackIceDefinition* definition = attack.attackerDefinition;
            display_.animateHostileAction(HostileActorVisual::BlackIce,
                blackIceFloorVisual(definition != nullptr ? definition->type : BlackIceType::Ice01),
                0, attack.success, ProgramId::None,
                definition != nullptr ? definition->visualId : IceVisualId::Hound01,
                definition != nullptr ? definition->animationStyle : HostileAttackStyle::Pulse);
        }
        snprintf(resultTitle_, sizeof(resultTitle_), "ENCOUNTER");
        if (result.iceCount > 1)
        {
            snprintf(resultLines_[0], sizeof(resultLines_[0]), "%u HOSTILES",
                static_cast<unsigned>(result.iceCount));
            for (size_t index = 0; index < result.iceCount && index < 5; ++index)
            {
                const BlackIceInstance* ice = game_.engagedBlackIceByRuntimeId(
                    result.ice[index].runtimeId);
                const char* name = ice != nullptr && ice->definition() != nullptr
                    ? ice->definition()->displayName : "BLACK ICE";
                const CombatResult& attack = result.ice[index].immediateAttack;
                const OpposedCheckResult& speed = result.ice[index].speedCheck;
                const char* outcome = speed.iceWins
                    ? (attack.noValidTarget ? "NO TARGET" : attack.success ? "HIT" : "MISS")
                    : speed.tie ? "TIE" : "EVADED";
                snprintf(resultLines_[index + 1], sizeof(resultLines_[index + 1]), "%s %s", name, outcome);
            }
            resultLineCount_ = result.iceCount < 5 ? result.iceCount + 1 : 6;
        }
        else
        {
            snprintf(resultLines_[0], sizeof(resultLines_[0]), "RUNNER SPD %d", result.speedCheck.runnerTotal);
            snprintf(resultLines_[1], sizeof(resultLines_[1]), "%s SPD %d", iceName,
                     result.speedCheck.iceTotal);
            snprintf(resultLines_[2], sizeof(resultLines_[2]), "%s",
                     result.speedCheck.iceWins ? "ICE WINS" : result.speedCheck.tie ? "TIE" : "RUNNER WINS");
            snprintf(resultLines_[3], sizeof(resultLines_[3]), "IMMEDIATE ATTACK: %s",
                     result.immediateAttack.noValidTarget ? "NO TARGET" :
                     result.immediateAttack.executed ? (result.immediateAttack.success ? "HIT" : "MISS") : "NONE");
            resultLineCount_ = 4;
            if (result.immediateAttack.success)
                resultLineCount_ = appendBlackIceEffectFeedback(result.immediateAttack, 4);
        }
        if (result.exitEffectCount > 0)
        {
            if (game_.runState() == RunState::RunnerDown) runEndCause_ = RunEndCause::RunnerDown;
            else if (game_.runState() == RunState::JackedOut) runEndCause_ = RunEndCause::UnsafeJackOut;
            clearQueuedIceResults();
            const size_t exitCount = result.exitEffectCount < MAX_ACTIVE_BLACK_ICE
                ? result.exitEffectCount : MAX_ACTIVE_BLACK_ICE;
            queuedIceResultCount_ = 0;
            for (size_t index = 0; index < exitCount; ++index)
            {
                if (result.exitEffects[index].noValidTarget) continue;
                const size_t queuedIndex = queuedIceResultCount_++;
                queuedIceResults_[queuedIndex].runtimeId = 0;
                queuedIceResults_[queuedIndex].result = result.exitEffects[index];
                queuedResultIsUnsafeExit_[queuedIndex] = true;
            }
            unsafeExitPresentationPending_ = true;
            queuedIceNextScreen_ = game_.runState() == RunState::RunnerDown ? Screen::RunnerDown :
                game_.isAwaitingEndDecision() ? Screen::VirusPrompt :
                game_.runState() == RunState::Completed ? Screen::ArchitectureComplete :
                game_.runState() == RunState::JackedOut ? Screen::RunSummary : Screen::Floor;
            encounterSummaryPending_ = true;
        }
        resultNextScreen_ = game_.runState() == RunState::RunnerDown ? Screen::RunnerDown :
            game_.isAwaitingEndDecision() ? Screen::VirusPrompt :
            game_.runState() == RunState::Completed ? Screen::ArchitectureComplete :
            game_.runState() == RunState::JackedOut ? Screen::RunSummary : Screen::Floor;
        if (game_.runState() == RunState::JackedOut || game_.runState() == RunState::RunnerDown)
            unsafeExitPresentationPending_ = true;
        resultToneExplicit_ = false;
        resultInfoLayout_ = InfoLayout::Automatic;
        screen_ = Screen::Result;
    }
    else { screen_ = game_.runState() == RunState::RunnerDown ? Screen::RunnerDown :
        game_.isAwaitingEndDecision() ? Screen::VirusPrompt :
        game_.runState() == RunState::Completed ? Screen::ArchitectureComplete : Screen::Floor; selected_ = 0; }
    if (traceFloor2) Serial.println("[F2] 15 after presentation state update");
}

uint8_t GameUIController::movementChoiceDestinationFor(const Architecture& architecture, size_t choice)
{
    const size_t current = architecture.currentPosition();
    const uint8_t parent = architecture.canonicalParent(static_cast<uint8_t>(current));
    size_t forward = 0;
    for (size_t index = 0; index < architecture.connectionCount(current); ++index)
    {
        if (architecture.connectionAt(current, index) != parent) ++forward;
    }
    if (choice >= forward) return parent;
    size_t forwardIndex = 0;
    for (size_t index = 0; index < architecture.connectionCount(current); ++index)
    {
        const uint8_t destination = architecture.connectionAt(current, index);
        if (destination == parent) continue;
        if (forwardIndex++ == choice) return destination;
    }
    return Architecture::NO_FLOOR;
}

void GameUIController::movementChoiceLabelFor(const Architecture& architecture, size_t choice,
                                               char* destination, size_t capacity)
{
    if (destination == nullptr || capacity == 0) return;
    const size_t current = architecture.currentPosition();
    const uint8_t parent = architecture.canonicalParent(static_cast<uint8_t>(current));
    size_t forward = 0;
    for (size_t index = 0; index < architecture.connectionCount(current); ++index)
    {
        if (architecture.connectionAt(current, index) != parent) ++forward;
    }
    if (choice >= forward && parent != Architecture::NO_FLOOR)
    {
        const Floor* previous = architecture.floorAt(parent);
        char content[32] = {};
        if (previous != nullptr && previous->discovered)
            pathfinderContentLabel(*previous, nullptr, parent, content, sizeof(content));
        if (content[0] != '\0')
        {
            const int written = snprintf(destination, capacity, "PREV F%u %s",
                static_cast<unsigned>(parent + 1), content);
            if (written >= static_cast<int>(capacity) && previous->type == FloorType::BlackICE)
                snprintf(destination, capacity, "PREV F%u %u ICE", static_cast<unsigned>(parent + 1),
                    static_cast<unsigned>(previous->blackIceCount > 1 ? previous->blackIceCount : 1));
        }
        else snprintf(destination, capacity, "PREV. FLOOR");
        return;
    }
    const uint8_t target = movementChoiceDestinationFor(architecture, choice);
    const Floor* targetFloor = architecture.floorAt(target);
    if (targetFloor != nullptr && targetFloor->discovered)
    {
        char content[32] = {};
        pathfinderContentLabel(*targetFloor, nullptr, target, content, sizeof(content));
        if (content[0] == '\0')
        {
            if (forward == 1) snprintf(destination, capacity, "NEXT FLOOR");
            else snprintf(destination, capacity, "P%u > F%u", static_cast<unsigned>(choice + 1),
                static_cast<unsigned>(target + 1));
            return;
        }
        const int written = snprintf(destination, capacity, "P%u > F%u %s",
            static_cast<unsigned>(choice + 1), static_cast<unsigned>(target + 1), content);
        if (written >= static_cast<int>(capacity) && targetFloor->type == FloorType::BlackICE)
            snprintf(destination, capacity, "P%u > F%u %u ICE", static_cast<unsigned>(choice + 1),
                static_cast<unsigned>(target + 1), static_cast<unsigned>(targetFloor->blackIceCount > 1
                    ? targetFloor->blackIceCount : 1));
        return;
    }
    if (forward == 1) { snprintf(destination, capacity, "NEXT FLOOR"); return; }
    snprintf(destination, capacity, "PATH %u", static_cast<unsigned>(choice + 1));
}

void GameUIController::movementChoiceLabelFor(const GameState& game, size_t choice,
                                               char* destination, size_t capacity)
{
    if (destination == nullptr || capacity == 0) return;
    const Architecture& architecture = game.architecture();
    const size_t current = architecture.currentPosition();
    const uint8_t parent = architecture.canonicalParent(static_cast<uint8_t>(current));
    size_t forward = 0;
    for (size_t index = 0; index < architecture.connectionCount(current); ++index)
        if (architecture.connectionAt(current, index) != parent) ++forward;

    if (choice >= forward && parent != Architecture::NO_FLOOR)
    {
        const Floor* previous = architecture.floorAt(parent);
        char content[32] = {};
        if (previous != nullptr && previous->discovered)
            pathfinderContentLabel(*previous, &game, parent, content, sizeof(content));
        if (content[0] != '\0')
            snprintf(destination, capacity, "PREV F%u %s", static_cast<unsigned>(parent + 1), content);
        else snprintf(destination, capacity, "PREV. FLOOR");
        return;
    }
    const uint8_t target = movementChoiceDestinationFor(architecture, choice);
    const Floor* targetFloor = architecture.floorAt(target);
    if (targetFloor != nullptr && targetFloor->discovered)
    {
        char content[32] = {};
        pathfinderContentLabel(*targetFloor, &game, target, content, sizeof(content));
        if (content[0] != '\0')
        {
            const int written = snprintf(destination, capacity, "P%u > F%u %s",
                static_cast<unsigned>(choice + 1), static_cast<unsigned>(target + 1), content);
            if (written >= static_cast<int>(capacity) && targetFloor->blackIceCount > 1)
                snprintf(destination, capacity, "P%u > F%u %u ICE", static_cast<unsigned>(choice + 1),
                    static_cast<unsigned>(target + 1), static_cast<unsigned>(targetFloor->blackIceCount));
        }
        else if (forward == 1) snprintf(destination, capacity, "NEXT FLOOR");
        else snprintf(destination, capacity, "P%u > F%u", static_cast<unsigned>(choice + 1),
            static_cast<unsigned>(target + 1));
        return;
    }
    if (forward == 1) snprintf(destination, capacity, "NEXT FLOOR");
    else snprintf(destination, capacity, "PATH %u", static_cast<unsigned>(choice + 1));
}

void GameUIController::panMap(int16_t x, int16_t y)
{
    mapPanX_ += x; mapPanY_ += y;
    if (mapPanX_ > 0) mapPanX_ = 0;
    if (mapPanX_ < -240) mapPanX_ = -240;
    if (mapPanY_ > 0) mapPanY_ = 0;
    if (mapPanY_ < -80) mapPanY_ = -80;
    redraw();
}

void GameUIController::buildArchitectureMapView(const GameState& game, ArchitectureMapView& view)
{
    memset(&view, 0, sizeof(view));
    const Architecture& architecture = game.architecture();
    view.architecture = architecture.name();
    const size_t count = architecture.floorCount();
    if (count == 0) return;
    bool visible[MAX_ARCHITECTURE_FLOORS] = {};
    uint8_t depth[MAX_ARCHITECTURE_FLOORS];
    uint8_t queue[MAX_ARCHITECTURE_FLOORS] = {};
    int8_t nodeForFloor[MAX_ARCHITECTURE_FLOORS];
    uint8_t perDepth[MAX_ARCHITECTURE_FLOORS] = {};
    uint8_t rankAtDepth[MAX_ARCHITECTURE_FLOORS] = {};
    for (size_t index = 0; index < MAX_ARCHITECTURE_FLOORS; ++index)
    { depth[index] = 0xff; nodeForFloor[index] = -1; }
    size_t read = 0, write = 0;
    queue[write++] = 0; depth[0] = 0;
    while (read < write)
    {
        const uint8_t source = queue[read++];
        for (size_t edge = 0; edge < architecture.connectionCount(source); ++edge)
        {
            const uint8_t target = architecture.connectionAt(source, edge);
            if (target >= count || depth[target] != 0xff) continue;
            depth[target] = static_cast<uint8_t>(depth[source] + 1);
            if (write < MAX_ARCHITECTURE_FLOORS) queue[write++] = target;
        }
    }
    for (size_t index = 0; index < count; ++index)
    {
        const Floor* floor = architecture.floorAt(index);
        if (floor != nullptr && floor->discovered)
        {
            visible[index] = true;
            if (floor->visited) for (size_t edge = 0; edge < architecture.connectionCount(index); ++edge)
            {
                const uint8_t target = architecture.connectionAt(index, edge);
                if (target < count) visible[target] = true;
            }
        }
    }
    for (size_t index = 0; index < count; ++index)
        if (visible[index] && depth[index] < MAX_ARCHITECTURE_FLOORS) ++perDepth[depth[index]];
    for (size_t index = 0; index < count && view.nodeCount < MAX_ARCHITECTURE_MAP_NODES; ++index)
    {
        if (!visible[index]) continue;
        const Floor* floor = architecture.floorAt(index);
        if (floor == nullptr) continue;
        const uint8_t level = depth[index] == 0xff ? 0 : depth[index];
        ArchitectureMapNode& node = view.nodes[view.nodeCount];
        node.floorIndex = static_cast<uint8_t>(index);
        node.visited = floor->visited;
        node.discovered = floor->discovered;
        node.floorNumber = node.discovered ? static_cast<uint8_t>(index + 1) : 0;
        node.currentPlayer = index == architecture.currentPosition();
        node.symbol = '?';
        node.status = ArchitectureMapNodeStatus::Unknown;
        if (node.discovered)
        {
            node.symbol = floor->type == FloorType::Password ? 'P' : floor->type == FloorType::File ? 'F' :
                floor->type == FloorType::ControlNode ? 'C' : floor->type == FloorType::BlackICE ? 'I' : 'o';
            bool hostile = false;
            for (size_t iceIndex = 0; iceIndex < game.activeBlackIceCount(); ++iceIndex)
            {
                const BlackIceInstance* ice = game.activeBlackIceAt(iceIndex);
                if (ice != nullptr && ice->active() && game.activeBlackIceChasePosition(iceIndex) == index)
                { hostile = true; break; }
            }
            const bool actionable = (floor->type == FloorType::Password && !floor->resolved) ||
                (floor->type == FloorType::File && !floor->downloaded) ||
                (floor->type == FloorType::ControlNode && !floor->controlled);
            node.status = !node.visited ? ArchitectureMapNodeStatus::Unknown : hostile ? ArchitectureMapNodeStatus::Hostile :
                actionable ? ArchitectureMapNodeStatus::Actionable : ArchitectureMapNodeStatus::Completed;
            for (size_t iceIndex = 0; iceIndex < game.playerBlackIceCount(); ++iceIndex)
            {
                const PlayerBlackIceRuntime* ice = game.playerBlackIceAt(iceIndex);
                if (ice != nullptr && ice->instance.active() && ice->chasePosition == index)
                { node.playerIce = true; view.hasPlayerIce = true; break; }
            }
        }
        const uint8_t rank = rankAtDepth[level]++;
        node.x = static_cast<int16_t>(12 + level * 30);
        node.y = static_cast<int16_t>(29 + (rank + 1) * 78 / (perDepth[level] + 1));
        nodeForFloor[index] = static_cast<int8_t>(view.nodeCount++);
    }
    for (size_t source = 0; source < count; ++source)
    {
        const Floor* floor = architecture.floorAt(source);
        if (floor == nullptr || !floor->discovered || nodeForFloor[source] < 0) continue;
        for (size_t edge = 0; edge < architecture.connectionCount(source); ++edge)
        {
            const uint8_t target = architecture.connectionAt(source, edge);
            if (target >= count || nodeForFloor[target] < 0) continue;
            const Floor* targetFloor = architecture.floorAt(target);
            if (targetFloor != nullptr && targetFloor->discovered && source > target) continue;
            if (view.edgeCount < MAX_ARCHITECTURE_MAP_EDGES)
            {
                view.edges[view.edgeCount].from = static_cast<uint8_t>(nodeForFloor[source]);
                view.edges[view.edgeCount++].to = static_cast<uint8_t>(nodeForFloor[target]);
            }
        }
    }
}

void GameUIController::showNetResult(const char* name, const NetCheckResult& result, bool showTarget,
                                     SystemActionVisual visual)
{
    resultToneExplicit_ = false;
    resultInfoLayout_ = InfoLayout::Automatic;
    memset(resultLines_, 0, sizeof(resultLines_));
    snprintf(resultTitle_, sizeof(resultTitle_), "%s", name);
    if (!result.attempted)
    {
        snprintf(resultTitle_, sizeof(resultTitle_), "ACTION REJECTED");
        snprintf(resultLines_[0], sizeof(resultLines_[0]), "NO ACT LEFT");
        resultLineCount_ = 1; resultNextScreen_ = Screen::Floor; screen_ = Screen::Result;
        if (visual == SystemActionVisual::EyeDee) display_.animateEyeDee(false);
        else if (visual == SystemActionVisual::Backdoor) display_.animatePasswordFailure();
        else if (visual == SystemActionVisual::Control) display_.animateControlFailure();
        else if (visual == SystemActionVisual::Pathfinder) display_.animatePathfinder();
        else if (visual == SystemActionVisual::Cloak) display_.animateCloak(false);
        else display_.animateTransition(FeedbackTone::Danger);
        return;
    }
    if (result.attempted) display_.animateDigitalRoll(name, static_cast<uint8_t>(result.roll));
    snprintf(resultLines_[0], sizeof(resultLines_[0]), "INTERFACE %d", result.base);
    size_t line = 1;
    if (result.programModifier != 0)
        snprintf(resultLines_[line++], sizeof(resultLines_[0]), "BONUS %+d", result.programModifier);
    if (result.hardwareModifier != 0)
        snprintf(resultLines_[line++], sizeof(resultLines_[0]), "HARDWARE %+d", result.hardwareModifier);
    snprintf(resultLines_[line++], sizeof(resultLines_[0]), "ROLL %d", result.roll);
    snprintf(resultLines_[line++], sizeof(resultLines_[0]), "TOTAL %d", result.total);
    if (showTarget) snprintf(resultLines_[line++], sizeof(resultLines_[0]), "DV %d", result.target);
    const char* feedback = "FAIL";
    if (!strcmp(name, "BACKDOOR")) feedback = result.success ? "ACCESS GRANTED" : "ACCESS DENIED";
    else if (!strcmp(name, "EYE-DEE")) feedback = result.success ? "IDENTIFIED" : "FAILED";
    else if (!strcmp(name, "CONTROL")) feedback = result.success ? "CONTROL ACQUIRED" : "FAILED";
    else if (result.success) feedback = "SUCCESS";
    if (line < 7) snprintf(resultLines_[line++], sizeof(resultLines_[0]), "%s", feedback);
    resultLineCount_ = line; resultNextScreen_ = Screen::Floor; screen_ = Screen::Result;
    if (visual == SystemActionVisual::EyeDee) display_.animateEyeDee(result.success);
    else if (visual == SystemActionVisual::Backdoor)
        result.success ? display_.animatePasswordUnlock() : display_.animatePasswordFailure();
    else if (visual == SystemActionVisual::Control)
        result.success ? display_.animateControlSuccess() : display_.animateControlFailure();
    else if (visual == SystemActionVisual::Pathfinder) display_.animatePathfinder();
    else if (visual == SystemActionVisual::Cloak) display_.animateCloak(result.success);
    else display_.animateTransition(result.success ? FeedbackTone::Success : FeedbackTone::Danger);
}

void GameUIController::renderFloorCombatContext()
{
    // Enemy attacks are selected from a menu. Render the already-current
    // Floor once as a presentation-only base before taking the overlay snapshot.
    const Screen previousScreen = screen_;
    const size_t previousSelection = selected_;
    display_.invalidateFloorCache();
    screen_ = Screen::Floor;
    selected_ = 0;
    redraw();
    screen_ = previousScreen;
    selected_ = previousSelection;
}

void GameUIController::showCombatResult(const char* name, const CombatResult& result, bool hpDamage,
                                        bool targetIsEnemy, const char* targetName,
                                        PlayerActionVisual actionVisual, bool renderFloorContext,
                                        FeedbackTone toneOverride, HostileActorVisual hostileActor,
                                        FloorVisual hostileVisual, uint8_t hostileDemonType,
                                        HostileAttackStyle hostileAttackStyle,
                                        bool presentationAlreadyPlayed)
{
    resultToneExplicit_ = toneOverride != FeedbackTone::Neutral;
    resultTone_ = toneOverride;
    resultInfoLayout_ = InfoLayout::Combat;
    snprintf(resultTitle_, sizeof(resultTitle_), "%s", name);
    if (result.noValidTarget)
    {
        snprintf(resultLines_[0], sizeof(resultLines_[0]), "%s",
            result.noProgramTarget ? "NO PROGRAM" : "NO TARGET");
        resultLineCount_ = 1;
        resultNextScreen_ = afterIceScreen_;
        screen_ = Screen::Result;
        return;
    }
    snprintf(resultLines_[0], sizeof(resultLines_[0]), "ATTACK");
    snprintf(resultLines_[1], sizeof(resultLines_[1]), "ATK %d DEF %d", result.attackerTotal, result.defenderTotal);
    size_t outcomeLine = 2;
    if (targetName != nullptr && targetName[0] != '\0')
    {
        snprintf(resultLines_[2], sizeof(resultLines_[2]),
            result.targetType == CombatResult::TargetType::Program ? "TARGET: %s" : ">> %s", targetName);
        outcomeLine = 3;
    }
    snprintf(resultLines_[outcomeLine], sizeof(resultLines_[outcomeLine]), "%s",
             !result.executed ? "ACTION REJECTED" : result.success ? "HIT / SUCCESS" : "MISS / FAIL");
    if (!result.success) resultLineCount_ = outcomeLine + 1;
    else if (result.targetType == CombatResult::TargetType::Program)
    {
        size_t line = outcomeLine + 1;
        if (result.damage > 0 && line < 7)
            snprintf(resultLines_[line++], sizeof(resultLines_[0]), "-%d REZ", result.damage);
        if (result.targetDestroyed && line < 7)
            snprintf(resultLines_[line++], sizeof(resultLines_[0]), "DESTROYED");
        else if (!result.targetDestroyed && line < 7)
            snprintf(resultLines_[line++], sizeof(resultLines_[0]), "REZ %d/%d",
                result.targetRemainingRez, result.targetInitialRez);
        resultLineCount_ = line;
    }
    else if (hpDamage && targetIsEnemy)
    {
        size_t line = outcomeLine + 1;
        if (result.rawDamage > 0)
        {
            if (result.damageReduction > 0)
                snprintf(resultLines_[line++], sizeof(resultLines_[0]), "DMG %d // ARMOR -%d",
                    result.rawDamage, result.damageReduction);
            else
                snprintf(resultLines_[line++], sizeof(resultLines_[0]), "DMG %d", result.rawDamage);
        }
        if (result.rawDamage > 0 && line < 7) snprintf(resultLines_[line++], sizeof(resultLines_[0]), "-%d HP", result.damage);
        if (result.forcedUnsafeJackOut && line < 7) snprintf(resultLines_[line++], sizeof(resultLines_[0]), "UNSAFE JACK");
        resultLineCount_ = line;
    }
    else if (hpDamage)
    {
        size_t line = outcomeLine + 1;
        if (result.damage > 0 && line < 7)
            snprintf(resultLines_[line++], sizeof(resultLines_[0]), "-%d HP", result.damage);
        if (result.nextTurnNetActionPenalty > 0 && line < 7)
            snprintf(resultLines_[line++], sizeof(resultLines_[0]), "NET ACTION -%u", result.nextTurnNetActionPenalty);
        if (result.superglueRounds > 0 && line < 7)
            snprintf(resultLines_[line++], sizeof(resultLines_[0]), "MOVE LOCK %u", result.superglueRounds);
        if (line == outcomeLine + 1) line = appendBlackIceEffectFeedback(result, line);
        resultLineCount_ = line;
    }
    else
    {
        snprintf(resultLines_[outcomeLine + 1], sizeof(resultLines_[outcomeLine + 1]), "-%d REZ", result.damage);
        resultLineCount_ = outcomeLine + 2;
    }
    // Keep result feedback tied to fields that prove an effect was applied.
    // The detailed effect helper remains responsible for the existing damage
    // and lock text; these compact lines make the shared HUD status explicit.
    if (result.success)
    {
        size_t statusLine = resultLineCount_;
        auto appendStatus = [&](const char* text) {
            if (statusLine < 7)
                snprintf(resultLines_[statusLine++], sizeof(resultLines_[0]), "STATUS: %s", text);
};
        if (result.fireApplied) appendStatus("FIRE");
        if (result.moveBefore > result.moveAfter) appendStatus("MOVE DOWN");
        if (result.intReduction > 0 || result.refReduction > 0 || result.dexReduction > 0)
            appendStatus("STAT DOWN");
        if (result.nextTurnNetActionPenalty > 0) {
            char status[16] = {};
            snprintf(status, sizeof(status), "NET-%u", result.nextTurnNetActionPenalty);
            appendStatus(status);
        }
        if (result.superglueRounds > 0 || result.depthLockApplied || result.safeJackOutLockApplied)
            appendStatus("HOLD");
        if (result.temporaryRunEffect == TemporaryRunEffect::SlidePenaltyWhileActive &&
            result.temporaryRunEffectApplied) {
            char status[18] = {};
            snprintf(status, sizeof(status), "SLIDE %d", result.slideModifier);
            appendStatus(status);
        }
        resultLineCount_ = statusLine;
    }
    if (renderFloorContext && result.executed && actionVisual != PlayerActionVisual::None)
        renderFloorCombatContext();
    resultNextScreen_ = afterIceScreen_; screen_ = Screen::Result;
    const FeedbackTone tone = toneOverride != FeedbackTone::Neutral
        ? toneOverride : result.success ? FeedbackTone::Danger : FeedbackTone::Info;
    if (hostileActor != HostileActorVisual::None && result.executed)
        display_.animateHostileAction(hostileActor, hostileVisual, hostileDemonType, result.success,
            hostileActor == HostileActorVisual::EnemyNetrunner ? result.attackerProgram : ProgramId::None,
            hostileActor == HostileActorVisual::BlackIce && result.attackerDefinition != nullptr
                ? result.attackerDefinition->visualId : IceVisualId::Count,
            hostileAttackStyle);
    if (shouldPendUnsafeExitPresentationForDebug(result, targetIsEnemy))
        unsafeExitPresentationPending_ = true;
    else if (!presentationAlreadyPlayed && result.executed && hostileActor == HostileActorVisual::None &&
             game_.runState() == RunState::JackedIn)
    {
        if (actionVisual != PlayerActionVisual::None)
            display_.animatePlayerAction(actionVisual, tone);
        else
            display_.animateTransition(tone);
    }
}

void GameUIController::showMessage(const char* title, const char* message, Screen next)
{
    resultToneExplicit_ = false;
    resultInfoLayout_ = InfoLayout::Automatic;
    snprintf(resultTitle_, sizeof(resultTitle_), "%s", title);
    snprintf(resultLines_[0], sizeof(resultLines_[0]), "%s", message);
    resultLineCount_ = 1; resultNextScreen_ = next; screen_ = Screen::Result;
}

void GameUIController::finishPlayerTurn()
{
    game_.endPlayerTurn();
    afterIceScreen_ = Screen::Floor;
    pendingEnemyPhase_ = false;
    pendingIcePhase_ = false;
    const uint8_t fireDamage = game_.consumeRunnerFireTickDamage();
    if (game_.runState() == RunState::RunnerDown)
    {
        runEndCause_ = RunEndCause::RunnerDown;
        screen_ = Screen::RunnerDown;
        selected_ = 0;
        return;
    }
    if (fireDamage > 0 && game_.runState() == RunState::JackedIn)
    {
        char fireLine[20] = {};
        snprintf(fireLine, sizeof(fireLine), "FIRE -%u HP", fireDamage);
        fireTickFeedbackPending_ = true;
        display_.animateFireDamage();
        showMessage("FIRE", fireLine, Screen::Result);
        resultToneExplicit_ = true;
        resultTone_ = FeedbackTone::Danger;
        return;
    }
    advanceSilentPhases();
}

void GameUIController::showEnemyPresenceIfPending()
{
    uint16_t presenceRuntimeId = 0;
    if (!game_.consumeEnemyPresence(presenceRuntimeId)) return;
    const EnemyNetrunnerRuntime* enemy = game_.enemyNetrunnerByRuntimeId(presenceRuntimeId);
    display_.animateEnemyPresence(enemy != nullptr && enemy->definition != nullptr
        ? enemy->definition->name : "NETRUNNER");
    display_.animateHostileAppearance(FloorVisual::EnemyNetrunner,
        enemy != nullptr && enemy->definition != nullptr ? enemy->definition->name : "NETRUNNER");
}

void GameUIController::showNextDemonPhaseAction()
{
    if (demonPhaseActionIndex_ >= demonPhaseResult_.actionCount)
    {
        advanceSilentPhases();
        return;
    }
    const DemonPhaseResult::ActionResult& action = demonPhaseResult_.actions[demonPhaseActionIndex_++];
    if (action.type == DemonPhaseResult::ActionType::ControlReclaim)
    {
        const Floor* node = game_.architecture().floorAt(action.controlledNodeFloor);
        const char* demonName = game_.demon().definition != nullptr
            ? game_.demon().definition->displayName : "DEMON";
        const char* nodeName = node != nullptr && node->controlName != nullptr && node->controlName[0] != '\0'
            ? node->controlName : "CONTROL NODE";
        display_.animateControlReclaim(demonName, nodeName);
        resultToneExplicit_ = true;
        resultTone_ = FeedbackTone::Danger;
        resultInfoLayout_ = InfoLayout::Automatic;
        snprintf(resultTitle_, sizeof(resultTitle_), "DEMON // %s", demonName);
        snprintf(resultLines_[0], sizeof(resultLines_[0]), "DEMON TOOK CONTROL");
        snprintf(resultLines_[1], sizeof(resultLines_[1]), "NODE %s", nodeName);
        resultLineCount_ = 2;
        resultNextScreen_ = Screen::Result;
        screen_ = Screen::Result;
        return;
    }
    if (action.type == DemonPhaseResult::ActionType::Zap)
    {
        const char* demonName = game_.demon().definition != nullptr ? game_.demon().definition->displayName : "DEMON";
        snprintf(resultTitle_, sizeof(resultTitle_), "DEMON // %s", demonName);
        showCombatResult(resultTitle_, action.attack, true, false, game_.runner().handle(),
            PlayerActionVisual::None, false, hostileResultFeedbackToneForDebug(action.attack),
            HostileActorVisual::Demon, FloorVisual::Empty,
            demonPresentationRendererType(game_.demon().definition), game_.demon().definition != nullptr ? game_.demon().definition->animationStyle : HostileAttackStyle::Pulse);
    }
}

void GameUIController::advanceSilentPhases()
{
    // Empty phases are presentation-only omissions. The GameState still
    // advances through exactly the same phase order as before.
    for (uint8_t guard = 0; guard < 4 && game_.runState() == RunState::JackedIn; ++guard)
    {
        if (game_.turnPhase() == TurnPhase::Demon)
        {
            game_.runDemonPhase(rules_, demonPhaseResult_);
            demonPhaseActionIndex_ = 0;
            pendingEnemyPhase_ = game_.turnPhase() == TurnPhase::Enemy;
            pendingIcePhase_ = game_.turnPhase() == TurnPhase::Ice;
            if (demonPhaseResult_.actionCount > 0)
            {
                showNextDemonPhaseAction();
                return;
            }
            continue;
        }
        if (game_.turnPhase() == TurnPhase::PlayerIce)
        {
            game_.runPlayerBlackIcePhase(rules_, icePhaseResult_);
            if (icePhaseResult_.attackCount > 0)
            {
                queueIcePhaseResults(icePhaseResult_, Screen::Result, true);
                return;
            }
            continue;
        }
        if (game_.turnPhase() == TurnPhase::Enemy)
        {
            game_.runEnemyPhase(rules_, enemyPhaseResult_);
            if (enemyPhaseResult_.actionCount > 0)
            {
                showEnemyPresenceIfPending();
                queueEnemyPhaseResults(enemyPhaseResult_);
                return;
            }
            continue;
        }
        if (game_.turnPhase() == TurnPhase::Ice)
        {
            game_.runIcePhase(rules_, icePhaseResult_);
            if (icePhaseResult_.attackCount > 0)
            {
                queueIcePhaseResults(icePhaseResult_, game_.runState() == RunState::RunnerDown ? Screen::RunnerDown :
                    icePhaseResult_.runTerminated ? Screen::RunSummary : Screen::Floor);
                return;
            }
            screen_ = afterIceScreen_;
            selected_ = 0;
            return;
        }
        return;
    }
}

void GameUIController::queueEnemyPhaseResults(const EnemyPhaseResult& result)
{
    if (result.runTerminated)
        runEndCause_ = game_.runState() == RunState::RunnerDown
            ? RunEndCause::RunnerDown : RunEndCause::UnsafeJackOut;
    clearQueuedIceResults();
    queuedIceResultCount_ = result.actionCount < MAX_ACTIVE_BLACK_ICE
        ? result.actionCount : MAX_ACTIVE_BLACK_ICE;
    for (size_t index = 0; index < queuedIceResultCount_; ++index)
    {
        queuedIceResults_[index].runtimeId = result.actions[index].runtimeId;
        queuedIceResults_[index].targetEnemyRuntimeId = result.actions[index].targetEnemyRuntimeId;
        queuedIceResults_[index].result = result.actions[index].result;
        queuedResultFromEnemy_[index] = !result.actions[index].fromPlayerBlackIce;
        queuedResultFromPlayerIce_[index] = result.actions[index].fromPlayerBlackIce;
    }
    queuedIceNextScreen_ = game_.runState() == RunState::RunnerDown ? Screen::RunnerDown :
        result.runTerminated ? Screen::RunSummary : Screen::Result;
    pendingIcePhase_ = !result.runTerminated;
    if (queuedIceResultCount_ > 0) showNextQueuedIceResult();
}

void GameUIController::executeEnemyAnimTest()
{
    // Recreate the disposable sandbox before every test so status, HP,
    // program state, and NET Actions cannot leak between selections.
    game_.startRun();
    game_.jackIn();
    game_.architecture().moveForward();
    EnemyPhaseResult result;
    const ProgramId id = kEnemyAnimTestPrograms[selectedEnemyAnimProgram_];
    if (!game_.runEnemyProgramTest(rules_, id, result))
    {
        showMessage("ENEMY ANIM TEST", "TEST ACTION FAILED", Screen::EnemyAnimTest);
        redraw();
        return;
    }
    queueEnemyPhaseResults(result);
    queuedIceNextScreen_ = Screen::EnemyAnimTest;
    pendingIcePhase_ = false;
    // This path is entered directly from handle(), outside the normal
    // activateSelection() redraw tail. Ensure the queued Result screen is
    // actually committed after the blocking presentation animation returns.
    redraw();
}

void GameUIController::queueIcePhaseResults(const IcePhaseResult& result, Screen next, bool playerIceResults)
{
    if (result.runTerminated)
        runEndCause_ = game_.runState() == RunState::RunnerDown
            ? RunEndCause::RunnerDown : RunEndCause::UnsafeJackOut;
    clearQueuedIceResults();
    const size_t sourceCount = result.attackCount < MAX_ACTIVE_BLACK_ICE
        ? result.attackCount : MAX_ACTIVE_BLACK_ICE;
    bool hasForcedUnsafe = false;
    for (size_t index = 0; index < sourceCount; ++index)
        if (result.attacks[index].result.forcedUnsafeJackOut) { hasForcedUnsafe = true; break; }
    const bool terminalForcedExit = hasForcedUnsafe && result.runTerminated &&
        game_.runState() != RunState::JackedIn;
    queuedIceResultCount_ = 0;
    for (size_t index = 0; index < sourceCount; ++index)
    {
        if (terminalForcedExit && !result.attacks[index].unsafeExitEffect &&
            !result.attacks[index].result.forcedUnsafeJackOut) continue;
        if (result.attacks[index].unsafeExitEffect && result.attacks[index].result.noValidTarget) continue;
        const size_t queuedIndex = queuedIceResultCount_++;
        queuedIceResults_[queuedIndex] = result.attacks[index];
        queuedResultFromPlayerIce_[queuedIndex] = playerIceResults;
        queuedResultIsUnsafeExit_[queuedIndex] = result.attacks[index].unsafeExitEffect;
    }
    queuedIceNextScreen_ = next;
    queuedResultsTargetEnemy_ = result.executed && game_.turnPhase() == TurnPhase::Enemy;
    memset(queuedResultFromEnemy_, 0, sizeof(queuedResultFromEnemy_));
    if (queuedIceResultCount_ > 0) showNextQueuedIceResult();
}

void GameUIController::showNextQueuedIceResult()
{
    const IcePhaseResult::IceAttack& attack = queuedIceResults_[queuedIceResultIndex_];
    char actor[32] = {};
    const char* targetName = nullptr;
    const char* iceName = blackIceName(attack.result);
    if (queuedResultFromPlayerIce_[queuedIceResultIndex_])
    {
        snprintf(actor, sizeof(actor), "PLAYER %s", iceName);
        if (attack.result.targetType == CombatResult::TargetType::Program &&
            attack.result.affectedProgramName[0] != '\0')
        {
            targetName = attack.result.affectedProgramName;
        }
        else
        {
            targetName = game_.enemyNetrunnerNameByRuntimeId(attack.targetEnemyRuntimeId);
        }
    }
    else if (queuedResultFromEnemy_[queuedIceResultIndex_])
    {
        const EnemyNetrunnerRuntime* enemy = game_.enemyNetrunnerByRuntimeId(attack.runtimeId);
        snprintf(actor, sizeof(actor), "%s", enemy != nullptr && enemy->definition != nullptr
            ? enemy->definition->name : "HOSTILE RUNNER");
        targetName = game_.runner().handle();
    }
    else
    {
        snprintf(actor, sizeof(actor), "%s", iceName);
        targetName = attack.result.targetType == CombatResult::TargetType::Program &&
            attack.result.affectedProgramName[0] != '\0'
            ? attack.result.affectedProgramName : game_.runner().handle();
    }
    const bool fromEnemy = queuedResultFromEnemy_[queuedIceResultIndex_];
    const bool playerBlackIceAttack = queuedResultFromPlayerIce_[queuedIceResultIndex_] &&
        shouldAnimatePlayerBlackIceAttackForDebug(attack.result, queuedResultsTargetEnemy_);
    if (playerBlackIceAttack)
    {
        const BlackIceDefinition* definition = attack.result.attackerDefinition;
        display_.animateHostileAction(HostileActorVisual::BlackIce,
            definition != nullptr ? blackIceFloorVisual(definition->type) : FloorVisual::Ice01,
            0, attack.result.success, ProgramId::None,
            definition != nullptr ? definition->visualId : IceVisualId::Count,
            definition != nullptr ? definition->animationStyle : HostileAttackStyle::Pulse,
            BlackIcePresentationSide::Player);
    }
    const bool hostileToRunner = fromEnemy || (!queuedResultFromPlayerIce_[queuedIceResultIndex_] &&
        !queuedResultsTargetEnemy_);
    HostileActorVisual hostileActor = HostileActorVisual::None;
    FloorVisual hostileVisual = FloorVisual::Empty;
    HostileAttackStyle hostileAttackStyle = HostileAttackStyle::Pulse;
    if (hostileToRunner && !queuedResultIsUnsafeExit_[queuedIceResultIndex_] &&
        attack.result.executed && !attack.result.noValidTarget)
    {
        if (fromEnemy)
        {
            hostileActor = HostileActorVisual::EnemyNetrunner;
            hostileVisual = FloorVisual::EnemyNetrunner;
            hostileAttackStyle = HostileAttackStyle::Burst;
        }
        else
        {
            hostileActor = HostileActorVisual::BlackIce;
            hostileVisual = blackIceFloorVisual(attack.result.attackerDefinition != nullptr
                ? attack.result.attackerDefinition->type : BlackIceType::Ice01);
            hostileAttackStyle = attack.result.attackerDefinition != nullptr
                ? attack.result.attackerDefinition->animationStyle : HostileAttackStyle::Pulse;
        }
    }
    showCombatResult(actor, attack.result, true, queuedResultsTargetEnemy_, targetName,
        PlayerActionVisual::None, false,
        hostileToRunner ? hostileResultFeedbackToneForDebug(attack.result) : FeedbackTone::Neutral,
        hostileActor, hostileVisual, 0, hostileAttackStyle, playerBlackIceAttack);
    // Confirm/Back advances through every fixed-size queued result before leaving the phase.
    resultNextScreen_ = Screen::Result;
}

uint8_t GameUIController::demonPresentationRendererType(const DemonDefinition* definition)
{
    return definition != nullptr ? demonRendererType(definition->visualId) : demonRendererType(DemonVisualId::Orb01);
}

FeedbackTone GameUIController::hostileResultFeedbackToneForDebug(const CombatResult& result)
{
    if (!result.executed || !result.success) return FeedbackTone::Info;

    // `success` only means that the opposed check hit. The Enemy result is
    // dangerous only when its existing structured fields record a negative
    // effect that was actually applied to the runner or the runner's deck.
    const bool runnerEffect = result.damage > 0 || result.intReduction > 0 ||
        result.refReduction > 0 || result.dexReduction > 0 || result.fireApplied ||
        result.moveBefore > result.moveAfter ||
        result.superglueRounds > 0 || result.nextTurnNetActionPenalty > 0 ||
        result.temporaryRunEffectApplied || result.depthLockApplied ||
        result.safeJackOutLockApplied || (result.forcedUnsafeJackOut && !result.krashBarrierBlocked);
    const bool programEffect = result.targetType == CombatResult::TargetType::Program &&
        (result.damage > 0 || result.targetDestroyed ||
         (result.programEffectApplied && !result.programSavedByBackup));
    return runnerEffect || programEffect ? FeedbackTone::Danger : FeedbackTone::Info;
}

bool GameUIController::shouldPendUnsafeExitPresentationForDebug(const CombatResult& result,
                                                                  bool targetIsEnemy)
{
    // An Enemy forced out by Player ICE is already terminal while the player
    // remains JackedIn. Only an unsafe exit that affects the player may arm
    // the run-summary transition after result acknowledgement.
    return result.forcedUnsafeJackOut && !result.krashBarrierBlocked && !targetIsEnemy;
}

bool GameUIController::shouldAnimatePlayerBlackIceAttackForDebug(const CombatResult& result,
                                                                  bool targetIsEnemy)
{
    return targetIsEnemy && result.executed && !result.noValidTarget &&
        result.targetType == CombatResult::TargetType::Netrunner;
}

void GameUIController::clearQueuedIceResults()
{
    queuedIceResultCount_ = 0;
    queuedIceResultIndex_ = 0;
    queuedIceNextScreen_ = Screen::Floor;
    queuedResultsTargetEnemy_ = false;
    memset(queuedResultFromEnemy_, 0, sizeof(queuedResultFromEnemy_));
    memset(queuedResultFromPlayerIce_, 0, sizeof(queuedResultFromPlayerIce_));
    memset(queuedResultIsUnsafeExit_, 0, sizeof(queuedResultIsUnsafeExit_));
}

void GameUIController::finishPendingUnsafeExitPresentation()
{
    unsafeExitPresentationPending_ = false;
    if (game_.runState() == RunState::RunnerDown)
    {
        runEndCause_ = RunEndCause::RunnerDown;
        screen_ = Screen::RunnerDown;
        selected_ = 0;
    }
    else if (game_.runState() == RunState::JackedOut)
    {
        display_.animateConnectionLoss();
        showRunSummary(RunEndCause::UnsafeJackOut);
    }
}

const char* GameUIController::programStatus(const Program& p) const
{
    switch (p.status())
    {
        case ProgramStatus::Inactive: return "OFF";
        case ProgramStatus::Rezzed: return "ON";
        case ProgramStatus::Derezzed: return "DEREZ";
        case ProgramStatus::Destroyed: return "DESTROY";
    }
    return "?";
}

const char* GameUIController::floorName(const Floor& floor) const
{
    switch (floor.type)
    {
        case FloorType::Password: return "PASSWORD";
        case FloorType::File: return "FILE";
        case FloorType::BlackICE:
        {
            const BlackIceDefinition* definition = floor.blackIceDefinition;
            if (definition == nullptr) definition = blackIceDefinition(floor.blackIceType);
            return definition != nullptr ? definition->displayName : "BLACK ICE";
        }
        case FloorType::ControlNode: return "CONTROL NODE";
        default: return "EMPTY";
    }
}

const char* GameUIController::blackIceName(const CombatResult& result) const
{
    const ProgramDefinition* program = programDefinition(result.attackerProgram);
    if (program != nullptr) return program->name;
    return result.attackerDefinition != nullptr ? result.attackerDefinition->displayName : "BLACK ICE";
}

size_t GameUIController::appendBlackIceEffectFeedback(
    const CombatResult& result, size_t firstLine)
{
    size_t line = firstLine;
    if (result.attackerDefinition == nullptr) return line;

    const BlackIceEffectType effect = result.attackerDefinition->effect.type;
    if (result.targetType == CombatResult::TargetType::Program)
    {
        const char* name = result.affectedProgramName[0] != '\0'
            ? result.affectedProgramName : "PROGRAM";
        if (result.programSavedByBackup)
        {
            snprintf(resultLines_[line], sizeof(resultLines_[line]), "%s SAVED", name);
            if (line + 1 < 7)
                snprintf(resultLines_[line + 1], sizeof(resultLines_[line + 1]), "BACKUP DRIVE");
            return line + (line + 1 < 7 ? 2 : 1);
        }
        if (result.targetDestroyed)
        {
            snprintf(resultLines_[line], sizeof(resultLines_[line]), "%s REZ %d -> DESTROY",
                name, result.targetInitialRez);
            return line + 1;
        }
        snprintf(resultLines_[line], sizeof(resultLines_[line]), "%s -%d REZ",
            name, result.damage);
        ++line;
        if (line < 7)
        {
            snprintf(resultLines_[line], sizeof(resultLines_[line]), "REZ %d -> %d",
                result.targetInitialRez, result.targetRemainingRez);
            ++line;
        }
        return line;
    }
    if (effect == BlackIceEffectType::DerezzRandomDefenderAndDamage ||
        effect == BlackIceEffectType::DestroyRandomInstalledProgram)
    {
        if (result.programEffectApplied && result.affectedProgramName[0] != '\0')
        {
            const char* state = result.resultingProgramStatus == ProgramStatus::Destroyed
                ? "DESTROYED" : "DEREZZED";
            snprintf(resultLines_[line], sizeof(resultLines_[line]), "%s %s",
                result.affectedProgramName, state);
        }
        else
        {
            snprintf(resultLines_[line], sizeof(resultLines_[line]), "%s",
                result.noProgramTarget || effect == BlackIceEffectType::DestroyRandomInstalledProgram
                    ? "NO PROGRAM" : "NO DEFENDER");
        }
        ++line;
    }

    if (result.rawDamage > 0 && line < 7)
    {
        snprintf(resultLines_[line++], sizeof(resultLines_[0]), "BRAIN DMG %d", result.rawDamage);
    }
    if (result.damageReduction > 0 && line < 7)
    {
        snprintf(resultLines_[line++], sizeof(resultLines_[0]), "ARMOR -%d", result.damageReduction);
    }
    if (result.rawDamage > 0 && line < 7)
    {
        snprintf(resultLines_[line], sizeof(resultLines_[line]), "DAMAGE %d", result.damage);
        ++line;
    }
    if (result.intReduction > 0 && line < 7)
    {
        snprintf(resultLines_[line++], sizeof(resultLines_[0]), "INT -%u", result.intReduction);
    }
    if (result.refReduction > 0 && line < 7)
    {
        snprintf(resultLines_[line++], sizeof(resultLines_[0]), "REF -%u", result.refReduction);
    }
    if (result.dexReduction > 0 && line < 7)
    {
        snprintf(resultLines_[line++], sizeof(resultLines_[0]), "DEX -%u", result.dexReduction);
    }
    if (result.targetType == CombatResult::TargetType::Netrunner && line < 7)
    {
        snprintf(resultLines_[line], sizeof(resultLines_[line]), "RUNNER HP %d/%d",
            result.targetRemainingHp, game_.runner().maxHp());
        ++line;
    }
    if (result.temporaryRunEffectApplied && line < 7)
    {
        if (result.temporaryRunEffect == TemporaryRunEffect::NextTurnNetActionPenalty)
            snprintf(resultLines_[line], sizeof(resultLines_[line]), "NEXT TURN -%u NET",
                result.attackerDefinition->effect.netActionPenalty);
        else if (result.temporaryRunEffect == TemporaryRunEffect::SlidePenaltyWhileActive)
            snprintf(resultLines_[line], sizeof(resultLines_[line]), "SLIDE %d", result.slideModifier);
        else if (result.depthLockApplied || result.safeJackOutLockApplied)
            snprintf(resultLines_[line], sizeof(resultLines_[line]), "DEPTH LOCK / SAFE OUT LOCK");
        else
            snprintf(resultLines_[line], sizeof(resultLines_[line]), "ACCESS LOCKED");
        ++line;
    }
    if (result.forcedUnsafeJackOut && line < 7)
        snprintf(resultLines_[line++], sizeof(resultLines_[0]), "UNSAFE JACK OUT");
    if (result.moveBefore > 0 && line < 7)
    {
        snprintf(resultLines_[line], sizeof(resultLines_[line]), "MOVE %u -> %u",
            result.moveBefore, result.moveAfter);
        ++line;
    }
    return line;
}

BlackIceInstance* GameUIController::activeIce()
{
    return selectedIceTarget();
}

BlackIceInstance* GameUIController::selectedIceTarget()
{
    buildFloorEntities();
    syncSelectedFloorEntity();
    if (floorEntityCount_ == 0 ||
        floorEntities_[selectedFloorEntity_].kind != FloorEntityKind::BlackIce) return nullptr;
    selectedIceRuntimeId_ = floorEntities_[selectedFloorEntity_].runtimeId;
    return game_.engagedBlackIceByRuntimeId(selectedIceRuntimeId_);
}

BlackIceInstance* GameUIController::focusedIce()
{
    buildFloorEntities();
    syncSelectedFloorEntity();
    if (floorEntityCount_ == 0 ||
        floorEntities_[selectedFloorEntity_].kind != FloorEntityKind::BlackIce) return nullptr;
    selectedIceRuntimeId_ = floorEntities_[selectedFloorEntity_].runtimeId;
    return game_.floorBlackIceByRuntimeId(selectedIceRuntimeId_);
}

EnemyNetrunnerRuntime* GameUIController::focusedEnemy()
{
    buildFloorEntities();
    syncSelectedFloorEntity();
    if (floorEntityCount_ == 0 ||
        floorEntities_[selectedFloorEntity_].kind != FloorEntityKind::EnemyNetrunner) return nullptr;
    return game_.enemyNetrunnerByRuntimeId(floorEntities_[selectedFloorEntity_].runtimeId);
}

GameUIController::FloorEntityKind GameUIController::focusedFloorEntityKind()
{
    buildFloorEntities();
    syncSelectedFloorEntity();
    return floorEntityCount_ == 0 ? FloorEntityKind::FloorObject :
        floorEntities_[selectedFloorEntity_].kind;
}

void GameUIController::buildFloorEntities()
{
    floorEntityCount_ = 0;
    const Floor* floor = game_.architecture().currentFloor();
    const bool hasFloorObject = floor != nullptr && (floor->type == FloorType::Password ||
        floor->type == FloorType::File || floor->type == FloorType::ControlNode);
    if (hasFloorObject) appendFloorEntity(FloorEntityKind::FloorObject, 0);
    for (size_t index = 0; index < game_.floorBlackIceCount(); ++index)
    {
        const uint16_t runtimeId = game_.floorBlackIceRuntimeId(index);
        if (runtimeId == 0) { Serial.println("[Entities] invalid ICE runtimeId"); continue; }
        if (!appendFloorEntity(FloorEntityKind::BlackIce, runtimeId)) break;
    }
    const EnemyNetrunnerRuntime* enemy = game_.floorEnemyNetrunner();
    if (enemy != nullptr && enemy->runtimeId != 0)
        appendFloorEntity(FloorEntityKind::EnemyNetrunner, enemy->runtimeId);
    if (game_.hasReachedArchitectureEnd())
        appendFloorEntity(FloorEntityKind::Trophy, 0);
    if (floorEntityCount_ == 0 && floor != nullptr)
        appendFloorEntity(FloorEntityKind::FloorObject, 0);
    syncSelectedFloorEntity();
    const uint8_t floorNumber = static_cast<uint8_t>(game_.architecture().currentPosition() + 1);
    if (NETRUN_DEBUG_VERBOSE && (lastEntityDiagnosticFloor_ != floorNumber || lastEntityDiagnosticCount_ != floorEntityCount_ ||
        lastEntityDiagnosticSelected_ != selectedFloorEntity_))
    {
        Serial.printf("[Entities] floor=%u count=%u selected=%u\n", static_cast<unsigned>(floorNumber),
            static_cast<unsigned>(floorEntityCount_), static_cast<unsigned>(selectedFloorEntity_));
        lastEntityDiagnosticFloor_ = floorNumber;
        lastEntityDiagnosticCount_ = floorEntityCount_;
        lastEntityDiagnosticSelected_ = selectedFloorEntity_;
    }
}

bool GameUIController::validateFloorUiFlowForDebug(const GameState& game,
                                                    size_t& entityCount, size_t& maxActionCount)
{
    entityCount = 0;
    maxActionCount = 0;
    const Floor* floor = game.architecture().currentFloor();
    if (floor == nullptr) return false;

    const bool hasFloorObject = floor->type == FloorType::Password || floor->type == FloorType::File ||
        floor->type == FloorType::ControlNode;
    if (hasFloorObject) ++entityCount;
    const size_t iceCount = game.floorBlackIceCount();
    if (iceCount > MAX_BLACK_ICE_PER_FLOOR) return false;
    for (size_t index = 0; index < iceCount; ++index)
        if (game.floorBlackIceRuntimeId(index) == 0) return false;
    entityCount += iceCount;

    const EnemyNetrunnerRuntime* enemy = game.floorEnemyNetrunner();
    if (enemy != nullptr)
    {
        if (!enemy->available() || enemy->runtimeId == 0 || enemy->definition == nullptr ||
            enemy->definition->id == nullptr || enemy->definition->name == nullptr ||
            enemy->cyberdeck.programCount() > MAX_CYBERDECK_PROGRAMS)
            return false;
        for (size_t slot = 0; slot < enemy->cyberdeck.programCount(); ++slot)
            if (enemy->cyberdeck.programAt(slot) == nullptr) return false;
        ++entityCount;
    }
    if (entityCount == 0) entityCount = 1;
    if (entityCount > FLOOR_ENTITY_CAPACITY) return false;

    size_t objectActions = 4;
    if (hasFloorObject && ((floor->type == FloorType::Password && !floor->resolved) ||
        floor->type == FloorType::File || (floor->type == FloorType::ControlNode && !floor->controlled)))
        ++objectActions;
    const size_t enemyActions = enemy != nullptr ? 6U : 0U;
    maxActionCount = objectActions > enemyActions ? objectActions : enemyActions;
    return maxActionCount <= MAX_FLOOR_ACTIONS;
}

bool GameUIController::selectFloorEntityForDebug(uint16_t runtimeId)
{
    buildFloorEntities();
    for (size_t index = 0; index < floorEntityCount_; ++index)
    {
        if (floorEntities_[index].runtimeId != runtimeId) continue;
        selectedFloorEntity_ = index;
        return true;
    }
    return false;
}

bool GameUIController::selectFloorEntityForDebug(FloorEntityKind kind, uint16_t runtimeId)
{
    buildFloorEntities();
    for (size_t index = 0; index < floorEntityCount_; ++index)
    {
        if (floorEntities_[index].kind != kind || floorEntities_[index].runtimeId != runtimeId) continue;
        selectedFloorEntity_ = index;
        return true;
    }
    return false;
}

bool GameUIController::selectBlackIceForDebug(uint16_t runtimeId)
{
    return selectFloorEntityForDebug(FloorEntityKind::BlackIce, runtimeId);
}

bool GameUIController::selectEnemyForDebug(uint16_t runtimeId)
{
    return selectFloorEntityForDebug(FloorEntityKind::EnemyNetrunner, runtimeId);
}

bool GameUIController::selectFloorObjectForDebug()
{
    return selectFloorEntityForDebug(FloorEntityKind::FloorObject, 0);
}

bool GameUIController::hasFloorActionForDebug(const char* label)
{
    if (label == nullptr) return false;
    buildFloorActions();
    for (size_t index = 0; index < floorActionCount_; ++index)
        if (floorActionLabels_[index] != nullptr && strcmp(floorActionLabels_[index], label) == 0) return true;
    return false;
}

bool GameUIController::executeFloorActionForDebug(const char* label)
{
    if (label == nullptr) return false;
    buildFloorActions();
    for (size_t index = 0; index < floorActionCount_; ++index)
    {
        if (floorActionLabels_[index] == nullptr || strcmp(floorActionLabels_[index], label) != 0) continue;
        executeFloorAction(floorActions_[index]);
        return true;
    }
    return false;
}

void GameUIController::executeSlideForDebug()
{
    executeFloorAction(Action::Slide);
}

void GameUIController::executeZapForDebug()
{
    executeFloorAction(Action::Zap);
}

bool GameUIController::shouldRenderCompletionObject(bool architectureCompleted, bool atCompletionDepth,
                                                     bool focusedFloorObject)
{
    return architectureCompleted && atCompletionDepth && focusedFloorObject;
}

void GameUIController::syncSelectedFloorEntity()
{
    if (floorEntityCount_ == 0) { selectedFloorEntity_ = 0; return; }
    if (selectedFloorEntity_ >= floorEntityCount_) selectedFloorEntity_ = 0;
}

void GameUIController::switchFloorEntity(int direction)
{
    syncSelectedFloorEntity();
    if (floorEntityCount_ < 2) return;
    selectedFloorEntity_ = direction > 0
        ? (selectedFloorEntity_ + 1) % floorEntityCount_
        : (selectedFloorEntity_ + floorEntityCount_ - 1) % floorEntityCount_;
}

void GameUIController::buildTargetSummary()
{
    buildFloorEntities();
    syncSelectedFloorEntity();
    // Focus cycling remains input-driven. The old lower "< n/n >" counter is
    // intentionally not rendered; the upper entity count is authoritative.
    targetSummary_[0] = '\0';
}

void GameUIController::showScenarioError(const ScenarioEntry& entry, const ScenarioImportResult& result)
{
    snprintf(scenarioErrorFile_, sizeof(scenarioErrorFile_), "%s", entry.fileName);
    snprintf(scenarioErrorMessage_, sizeof(scenarioErrorMessage_), "%s",
        ::scenarioErrorMessage(result.error));
    scenarioErrorFloor_ = result.floorId;
    screen_ = Screen::ScenarioError;
    selected_ = 0;
}

void GameUIController::showRunSummary(RunEndCause cause)
{
    runEndCause_ = cause;
    runSummaryRevealed_ = false;
    screen_ = Screen::RunSummary;
    selected_ = 0;
}

void GameUIController::buildPlayerStatus()
{
    playerStatus_[0] = '\0';
    size_t length = 0;
    auto appendToken = [&](const char* token)
    {
        if (token == nullptr || token[0] == '\0' || length + 1 >= sizeof(playerStatus_)) return;
        const int written = snprintf(playerStatus_ + length, sizeof(playerStatus_) - length,
            "%s%s", length == 0 ? "S: " : " // ", token);
        if (written > 0) length += static_cast<size_t>(written) < sizeof(playerStatus_) - length
            ? static_cast<size_t>(written) : sizeof(playerStatus_) - length - 1;
    };
    if (game_.runnerOnFire()) appendToken("FIRE");
    if (game_.navigationLockActive() || game_.moveLocked()) appendToken("HOLD");
    const uint8_t netPenalty = game_.netActionPenaltyThisTurn() > 0
        ? game_.netActionPenaltyThisTurn()
        : game_.scheduledNetActionPenalty(game_.turnNumber() + 1);
    if (netPenalty > 0)
    {
        char token[12] = {};
        snprintf(token, sizeof(token), "NET-%u", static_cast<unsigned>(netPenalty));
        appendToken(token);
    }
    if (!game_.moveLocked() && game_.runner().move() < game_.runner().baseMove()) appendToken("MOVE-");
    const int slideModifier = game_.activeSlidePenalty();
    if (slideModifier != 0)
    {
        char token[16] = {};
        snprintf(token, sizeof(token), "SLIDE%d", slideModifier);
        appendToken(token);
    }
    if (game_.runner().currentInt() < game_.runner().baseInt() ||
        game_.runner().currentRef() < game_.runner().baseRef() ||
        game_.runner().currentDex() < game_.runner().baseDex())
        appendToken("STAT DOWN");
}

void GameUIController::redraw()
{
    // Defensive presentation guard: a terminal runner must never be rendered
    // with interactive Floor actions, even if an older continuation flag is
    // still pending after a hostile effect.
    if (screen_ == Screen::Floor &&
        (game_.runState() == RunState::RunnerDown || game_.runner().hp() == 0))
    {
        runEndCause_ = RunEndCause::RunnerDown;
        screen_ = Screen::RunnerDown;
        selected_ = 0;
    }
    const char* items[13];
    char buffers[13][40];
    const char* lines[13];
    switch (screen_)
    {
        case Screen::Main:
        {
            static const char* menu[] = {"START RUN", "PLAYER"};
            display_.showMenu("NETRUN // RED", "1.0.0", menu, 2, selected_); break;
        }
        case Screen::PlayerMenu:
        {
#if NETRUN_DEBUG_VERBOSE
            static const char* menu[] = {"RUNNER PROFILE", "CYBERDECK", "RESET DEFAULTS", "ENEMY ANIM TEST", "BACK"};
            display_.showMenu("PLAYER", "CONFIGURATION // DEV", menu, 5, selected_);
#else
            static const char* menu[] = {"RUNNER PROFILE", "CYBERDECK", "RESET DEFAULTS", "BACK"};
            display_.showMenu("PLAYER", "CONFIGURATION", menu, 4, selected_);
#endif
            break;
        }
        case Screen::EnemyAnimTest:
        {
            static const char* labels[] = {"HELLBOLT", "VRIZZBOLT", "NERVESCRUB", "SUPERGLUE", "POISON FLATLINE", "DECKKRASH"};
            lines[0] = labels[selectedEnemyAnimProgram_];
            lines[1] = "LEFT / RIGHT CYCLE";
            lines[2] = "ENTER: TEST";
            lines[3] = "BACK: EXIT";
            display_.showInfo("ENEMY ANIM TEST", lines, 4, "SELECTED PROGRAM", FeedbackTone::Danger);
            break;
        }
        case Screen::Profile:
        {
            snprintf(configLabels_[0], sizeof(configLabels_[0]), "HANDLE: %s", profile_.handle);
            snprintf(configLabels_[1], sizeof(configLabels_[1]), "INTERFACE: %u", profile_.interfaceRank);
            snprintf(configLabels_[2], sizeof(configLabels_[2]), "MAX HP: %u", profile_.maxHp);
            configItems_[0] = configLabels_[0]; configItems_[1] = configLabels_[1];
            configItems_[2] = configLabels_[2]; configItems_[3] = "SAVE"; configItems_[4] = "BACK";
            display_.showMenu("RUNNER PROFILE", "UP/DOWN EDIT NUMBERS", configItems_, 5, selected_); break;
        }
        case Screen::ProfileHandleEdit:
        {
            snprintf(configLabels_[0], sizeof(configLabels_[0]), "%s_", handleEdit_);
            configItems_[0] = configLabels_[0]; configItems_[1] = "ENTER TO ACCEPT"; configItems_[2] = "BACK TO CANCEL";
            display_.showMenu("HANDLE", "TYPE ON KEYBOARD", configItems_, 3, 0); break;
        }
        case Screen::ProfileNumberEdit:
        {
            const bool interfaceEdit = selected_ == 1;
            snprintf(configLabels_[0], sizeof(configLabels_[0]), "[%u]", interfaceEdit ?
                profile_.interfaceRank : profile_.maxHp);
            configItems_[0] = configLabels_[0];
            configItems_[1] = "UP/DOWN CHANGE";
            configItems_[2] = "ENTER ACCEPT";
            configItems_[3] = "BACK CANCEL";
            display_.showMenu(interfaceEdit ? "INTERFACE EDIT" : "MAX HP EDIT", "EDIT MODE",
                configItems_, 4, 0); break;
        }
        case Screen::DeckEditor:
        {
            const size_t count = editorDeckConfig_.programCount;
            const size_t hardwareCount = editorDeckConfig_.hardwareCount;
            const size_t iceCount = editorDeckConfig_.playerBlackIceCount;
            for (size_t index = 0; index < count; ++index)
            {
                const ProgramDefinition* definition = programDefinition(editorDeckConfig_.programs[index]);
                snprintf(configLabels_[index], sizeof(configLabels_[index]), "%u %s", static_cast<unsigned>(index + 1),
                    definition != nullptr ? definition->name : "INVALID");
                configItems_[index] = configLabels_[index];
            }
            configItems_[count] = "ADD PROGRAM";
            for (size_t index = 0; index < hardwareCount; ++index)
            {
                const HardwareDefinition* definition = hardwareDefinition(editorDeckConfig_.hardware[index]);
                snprintf(configLabels_[count + 1 + index], sizeof(configLabels_[0]), "HW %s", definition->name);
                configItems_[count + 1 + index] = configLabels_[count + 1 + index];
            }
            configItems_[count + hardwareCount + 1] = "ADD HARDWARE";
            for (size_t index = 0; index < iceCount; ++index)
            {
                const BlackIceDefinition* definition = blackIceDefinition(editorDeckConfig_.playerBlackIce[index]);
                snprintf(configLabels_[count + hardwareCount + 2 + index], sizeof(configLabels_[0]), "ICE %s [2]",
                    definition != nullptr ? definition->displayName : "INVALID");
                configItems_[count + hardwareCount + 2 + index] = configLabels_[count + hardwareCount + 2 + index];
            }
            configItems_[count + hardwareCount + 2 + iceCount] = "ADD BLACK ICE [2]";
            snprintf(configLabels_[count + hardwareCount + 3 + iceCount], sizeof(configLabels_[0]), "TYPE: %s", qualityName(editorDeckConfig_.quality));
            configItems_[count + hardwareCount + 3 + iceCount] = configLabels_[count + hardwareCount + 3 + iceCount];
            configItems_[count + hardwareCount + 4 + iceCount] = "DEFAULTS";
            configItems_[count + hardwareCount + 5 + iceCount] = "SAVE";
            configItems_[count + hardwareCount + 6 + iceCount] = "BACK";
            snprintf(buffers[0], sizeof(buffers[0]), "SLOTS: %u/%u%s", cyberdeckUsedSlots(editorDeckConfig_),
                cyberdeckSlotCapacity(editorDeckConfig_.quality), deckEditorDirty_ ? " *" : "");
            display_.showMenu("CYBERDECK", buffers[0], configItems_, count + hardwareCount + iceCount + 7, selected_); break;
        }
        case Screen::DeckUnsavedChanges:
        {
            static const char* menu[] = {"SAVE", "DISCARD", "CANCEL"};
            display_.showMenu("UNSAVED CHANGES", "CYBERDECK", menu, 3, selected_); break;
        }
        case Screen::DeckItemInfo:
        {
            const char* lines[4] = {};
            if (deckInfoIsBlackIce_)
            {
                const BlackIceDefinition* d = selected_ > 0 ? selectablePlayerIceAt(selected_ - 1) : nullptr;
                lines[0] = d != nullptr ? d->shortRole : "NO INFO AVAILABLE"; lines[1] = d != nullptr ? d->shortEffect1 : ""; lines[2] = d != nullptr ? d->shortEffect2 : ""; lines[3] = d != nullptr ? "PLAYER ICE / 2 SLOTS" : "";
                display_.showInfo(d != nullptr ? d->displayName : "BLACK ICE", lines, 4, "BACK", FeedbackTone::Info);
            }
            else
            {
                const ProgramDefinition* d = selected_ > 0 ? programDefinition(kSelectablePrograms[selected_ - 1]) : nullptr;
                lines[0] = d != nullptr ? d->shortRole : "NO INFO AVAILABLE"; lines[1] = d != nullptr ? d->shortEffect1 : ""; lines[2] = d != nullptr ? d->shortEffect2 : ""; lines[3] = "";
                display_.showInfo(d != nullptr ? d->name : "SOFTWARE", lines, 4, "BACK", FeedbackTone::Info);
            }
            break;
        }
        case Screen::ProgramSelect:
        {
            configItems_[0] = "EMPTY";
            for (size_t index = 0; index < sizeof(kSelectablePrograms) / sizeof(kSelectablePrograms[0]); ++index)
            {
                const ProgramDefinition* definition = programDefinition(kSelectablePrograms[index]);
                snprintf(configLabels_[index], sizeof(configLabels_[index]), "%s %s%s", definition->name,
                    programClassName(*definition), definition->currentlyUnsupported ? " UNSUPPORTED" : "");
                configItems_[index + 1] = configLabels_[index];
            }
            display_.showMenu("PROGRAM SELECT", "I INFO / ENTER INSTALL", configItems_,
                1 + sizeof(kSelectablePrograms) / sizeof(kSelectablePrograms[0]), selected_); break;
        }
        case Screen::HardwareSelect:
        {
            configItems_[0] = "REMOVE";
            for (size_t index = 0; index < sizeof(kSelectableHardware) / sizeof(kSelectableHardware[0]); ++index)
            {
                const HardwareDefinition* definition = hardwareDefinition(kSelectableHardware[index]);
                snprintf(configLabels_[index], sizeof(configLabels_[0]), "%s [%u]%s", definition->name,
                    definition->slotCost, definition->currentlyUnsupported ? " *" : "");
                configItems_[index + 1] = configLabels_[index];
            }
            display_.showMenu("HARDWARE", "* NO CURRENT EFFECT", configItems_,
                1 + sizeof(kSelectableHardware) / sizeof(kSelectableHardware[0]), selected_); break;
        case Screen::BlackIceSelect:
        {
            configItems_[0] = "REMOVE";
            const size_t count = selectablePlayerIceCount();
            for (size_t index = 0; index < count; ++index)
            {
                const BlackIceDefinition* definition = selectablePlayerIceAt(index);
                snprintf(configLabels_[index], sizeof(configLabels_[0]), "%s [2]", definition->displayName);
                configItems_[index + 1] = configLabels_[index];
            }
            display_.showMenu("BLACK ICE", "I INFO / ENTER INSTALL", configItems_,
                1 + count, selected_); break;
        }
        }
        case Screen::SelectRun:
        {
            const size_t total = BUILT_IN_SCENARIO_COUNT + scanner_.count();
            const size_t visibleRows = 5;
            size_t first = selected_ > visibleRows - 1 ? selected_ - (visibleRows - 1) : 0;
            if (total > visibleRows && first > total - visibleRows) first = total - visibleRows;
            const size_t sdFirst = first > BUILT_IN_SCENARIO_COUNT ? first - BUILT_IN_SCENARIO_COUNT : 0;
            const size_t sdVisible = total > first ? ((total - first) < visibleRows ? total - first : visibleRows) : 0;
            if (sdFirst < scanner_.count()) scanner_.loadWindow(sdFirst, sdVisible);
            for (size_t row = 0; row < visibleRows && first + row < total; ++row)
            {
                const size_t logical = first + row;
                if (logical < BUILT_IN_SCENARIO_COUNT)
                    items[row] = logical == 0 ? BuiltInArchitectures::militechTestNet().name :
                        BuiltInArchitectures::enemyFloor1TestNet().name;
                else
                {
                    const ScenarioEntry* entry = scanner_.entry(logical - BUILT_IN_SCENARIO_COUNT);
                    items[row] = entry != nullptr ? (entry->name[0] != '\0' ? entry->name : entry->fileName) : "SCENARIO UNAVAILABLE";
                }
            }
            snprintf(buffers[0], sizeof(buffers[0]), "SD // %u SCENARIOS  %u-%u",
                static_cast<unsigned>(scanner_.count()), scanner_.count() == 0 ? 0 : sdFirst + 1,
                scanner_.count() == 0 ? 0 : ((sdFirst + visibleRows) < scanner_.count() ? sdFirst + visibleRows : scanner_.count()));
            display_.showMenu("SELECT RUN", buffers[0], items,
                total, selected_, false, first); break;
        }
        case Screen::RunDetail:
        {
            static const char* menu[] = {"JACK IN", "BACK"};
            snprintf(buffers[0], 40, "FLOORS: %u   THREAT: LOW",
                static_cast<unsigned>(game_.architecture().floorCount()));
            display_.showMenu(game_.architecture().name(), buffers[0], menu, 2, selected_, true); break;
        }
        case Screen::Runner:
            snprintf(buffers[0], 40, "%s", game_.runner().handle());
            snprintf(buffers[1], 40, "INTERFACE %u", game_.runner().interfaceRank());
            snprintf(buffers[2], 40, "HP %u/%u", game_.runner().hp(), game_.runner().maxHp());
            snprintf(buffers[3], 40, "NET ACTIONS %u", game_.runner().remainingNetActions());
            snprintf(buffers[4], 40, "MOVE %u", game_.runner().move());
            for (int i = 0; i < 5; ++i) lines[i] = buffers[i];
            display_.showInfo("RUNNER", lines, 5); break;
        case Screen::Cyberdeck:
        {
            const char* names[Cyberdeck::SLOT_COUNT];
            const char* states[Cyberdeck::SLOT_COUNT];
            for (size_t i = 0; i < Cyberdeck::SLOT_COUNT; ++i)
            {
                const Program* p = game_.cyberdeck().programAt(i);
                names[i] = p != nullptr ? p->name() : "EMPTY";
                states[i] = p != nullptr ? programStatus(*p) : "";
            }
            display_.showPrograms(names, states, Cyberdeck::SLOT_COUNT, Cyberdeck::SLOT_COUNT, false); break;
        }
        case Screen::Floor:
        {
            showEnemyPresenceIfPending();
            const bool traceFloor2 = isCombatFloor2(game_);
            if (traceFloor2) Serial.println("[F2] 16 before entity rebuild");
            buildFloorEntities();
            syncSelectedFloorEntity();
            buildTargetSummary();
            if (traceFloor2)
            {
                Serial.printf("[F2] 17 after entity rebuild count=%u selected=%u\n",
                    static_cast<unsigned>(floorEntityCount_), static_cast<unsigned>(selectedFloorEntity_));
                logFloor2Memory(17);
                Serial.println("[F2] 18 before action rebuild");
            }
#if NETRUN_DIAG_F2_STATIC_ACTIONS
            if (traceFloor2) { floorActionCount_ = 0; selected_ = 0; }
            else buildFloorActions();
#else
            buildFloorActions();
#endif
            if (traceFloor2)
                Serial.printf("[F2] 19 after action rebuild count=%u selected=%u\n",
                    static_cast<unsigned>(floorActionCount_), static_cast<unsigned>(selected_));
            const size_t rosterCount = floorEntityCount_;
            const size_t targetIndex = selectedFloorEntity_;
            if (traceFloor2) Serial.println("[F2] 20 before renderer input build");
            const Floor* floor = game_.architecture().currentFloor();
            if (floor == nullptr)
            {
                static const char* runtimeError[] = {"NO CURRENT FLOOR", "RETURN TO MENU"};
                Serial.println("[F2] ERROR null current floor");
                display_.showInfo("RUNTIME ERROR", runtimeError, 2, "ENTER", FeedbackTone::Danger);
                break;
            }
            BlackIceInstance* focus = focusedIce();
            EnemyNetrunnerRuntime* enemy = focusedEnemy();
            const PlayerBlackIceRuntime* deployedPlayerIce = nullptr;
            const uint8_t currentFloor = static_cast<uint8_t>(game_.architecture().currentPosition());
            for (size_t index = 0; index < game_.playerBlackIceCount(); ++index)
            {
                const PlayerBlackIceRuntime* ice = game_.playerBlackIceAt(index);
                if (ice != nullptr && ice->instance.active() && ice->chasePosition == currentFloor)
                { deployedPlayerIce = ice; break; }
            }
            FloorView view;
            view.architecture = game_.architecture().name();
            view.floor = static_cast<uint8_t>(game_.architecture().currentPosition() + 1);
            view.floorCount = static_cast<uint8_t>(game_.architecture().floorCount());
            view.turn = game_.turnNumber();
            view.actions = game_.runner().remainingNetActions();
            view.maxActions = game_.runner().maxNetActions();
            view.runnerHp = game_.runner().hp();
            view.runnerMaxHp = game_.runner().maxHp();
            buildPlayerStatus();
            view.playerStatus = playerStatus_;
            view.architectureCompleted = game_.architectureCompleted();
            view.demonActive = game_.demon().active;
            view.demonRez = game_.demon().currentRez;
            view.demonMaxRez = game_.demon().maxRez();
            view.demonType = demonPresentationRendererType(game_.demon().definition);
            view.demonName = game_.demon().definition != nullptr ? game_.demon().definition->displayName : nullptr;
            view.visual = FloorVisual::Empty;
            view.title = floorName(*floor);
            view.status = "ONLINE";
            if (enemy != nullptr)
            {
                view.visual = FloorVisual::EnemyNetrunner;
                view.title = enemy->definition != nullptr ? enemy->definition->name : "ENEMY RUNNER";
                view.status = enemy->onFire ? "STATUS: ON FIRE" : "ENCOUNTER: RUNNER";
                view.value = enemy->runner.hp();
                view.maxValue = enemy->runner.maxHp();
            }
            else if (focus != nullptr)
            {
                const BlackIceDefinition* definition = focus->definition();
                const BlackIceType type = definition != nullptr ? definition->type : BlackIceType::Ice01;
                view.iceVisual = definition != nullptr ? definition->visualId : IceVisualId::Hound01;
                switch (type)
                {
                    case BlackIceType::Ice02: view.visual = FloorVisual::Ice02; break;
                    case BlackIceType::Ice03: view.visual = FloorVisual::Ice03; break;
                    case BlackIceType::Ice04: view.visual = FloorVisual::Ice04; break;
                    case BlackIceType::Ice05: view.visual = FloorVisual::Ice05; break;
                    case BlackIceType::Ice06: view.visual = FloorVisual::Ice06; break;
                    case BlackIceType::Ice07: view.visual = FloorVisual::Ice07; break;
                    case BlackIceType::Ice08: view.visual = FloorVisual::Ice08; break;
                    case BlackIceType::Ice09: view.visual = FloorVisual::Ice09; break;
                    case BlackIceType::Ice10: view.visual = FloorVisual::Ice10; break;
                    case BlackIceType::Ice11: view.visual = FloorVisual::Ice11; break;
                    case BlackIceType::Ice12: view.visual = FloorVisual::Ice12; break;
                    default: view.visual = FloorVisual::Ice01; break;
                }
                view.title = definition != nullptr ? definition->displayName : "BLACK ICE";
                view.status = !focus->active() ? "STATUS: DEREZZED" :
                    focus->pursuing() ? "STATUS: ENGAGED" : "STATUS: SLID";
                view.value = focus->currentRez();
                view.maxValue = focus->maxRez();
                if (definition != nullptr)
                {
                    view.perception = definition->perception;
                    view.speed = definition->speed;
                    view.attack = definition->attack;
                    view.defense = definition->defense;
                }
            }
            else if (deployedPlayerIce != nullptr &&
                     !(floorEntityCount_ > 0 && selectedFloorEntity_ < floorEntityCount_))
            {
                const BlackIceDefinition* definition = deployedPlayerIce->definition;
                view.iceVisual = definition != nullptr ? definition->visualId : IceVisualId::Hound01;
                view.visual = definition != nullptr ? playerIceFloorVisual(definition->visualId) : FloorVisual::Ice01;
                view.title = definition != nullptr ? definition->displayName : "PLAYER ICE";
                view.status = "STATUS: PLAYER ICE";
                view.value = deployedPlayerIce->instance.currentRez();
                view.maxValue = deployedPlayerIce->instance.maxRez();
            }
            else if (floorEntityCount_ > 0 && selectedFloorEntity_ < floorEntityCount_ &&
                     floorEntities_[selectedFloorEntity_].kind == FloorEntityKind::Trophy)
            {
                view.visual = FloorVisual::Trophy;
                view.title = "TROPHY";
                view.status = game_.virusPlaced() ? "VIRUS PLANTED" : "ARCHITECTURE END";
            }
            else if (floor->type == FloorType::Password) {
                view.visual = FloorVisual::Password;
                view.title = "ENCRYPTED ACCESS";
                view.status = floor->resolved ? "ACCESS" : "ACCESS LOCKED";
                view.securityClass = static_cast<uint8_t>(floor->securityTier);
            }
            else if (floor->type == FloorType::File) {
                view.visual = FloorVisual::File;
                view.title = floor->identified && floor->fileName != nullptr ? floor->fileName : "DATA NODE";
                view.status = floor->downloaded ? "DOWNLOADED" : floor->identified ? "IDENTIFIED" : "STATUS: UNKNOWN";
                view.metadataType = floor->fileType;
                view.metadataValue = floor->fileValue;
            }
            else if (floor->type == FloorType::ControlNode) {
                view.visual = FloorVisual::ControlNode;
                view.title = "CONTROL NODE";
                view.status = floor->controlOwner == ControlOwner::Runner ? "STATUS: OWNED" :
                    floor->controlOwner == ControlOwner::Demon ? "STATUS: DEMON CONTROL" : "STATUS: HOSTILE";
            }
            else if (floor->type == FloorType::BlackICE)
            {
                // Scenario Black ICE is spawn metadata only. If no runtime ICE
                // occupies this floor, keep the floor visually empty instead
                // of manufacturing a stale DEREZZED actor from the spawn slot.
                BlackIceInstance* ice = focusedIce();
                if (ice != nullptr)
                {
                    const BlackIceDefinition* definition = ice->definition();
                    const BlackIceType type = definition != nullptr ? definition->type : BlackIceType::Ice01;
                    view.iceVisual = definition != nullptr ? definition->visualId : IceVisualId::Hound01;
                    view.visual = blackIceFloorVisual(type);
                    view.title = definition != nullptr ? definition->displayName : "BLACK ICE";
                    view.status = !ice->active() ? "STATUS: DEREZZED" :
                        ice->pursuing() ? "STATUS: ENGAGED" : "STATUS: SLID";
                    view.value = ice->currentRez();
                    view.maxValue = ice->maxRez();
                }
                else { view.visual = FloorVisual::Empty; view.title = floorName(*floor); view.status = "STATUS: CLEAR"; }
            }
            view.actionLabels = floorActionLabels_; view.actionCount = floorActionCount_; view.selected = selected_;
            view.chaseTotal = rosterCount;
            view.targetSummary = targetSummary_;
            view.targetIndex = targetIndex;
            view.targetCount = rosterCount;
            if (traceFloor2)
            {
                Serial.printf("[F2] 21 after renderer input build visual=%u title=%s actions=%u\n",
                    static_cast<unsigned>(view.visual), view.title != nullptr ? view.title : "<null>",
                    static_cast<unsigned>(view.actionCount));
                logFloor2Memory(21);
                Serial.println("[F2] 22 before gameplay render");
            }
#if NETRUN_DIAG_F2_STATIC_RENDER
            if (traceFloor2)
            {
                static const char* diagnosticLines[] = {"FLOOR 2 OK", "RENDER ISOLATED"};
                display_.showInfo("F2 DIAGNOSTIC", diagnosticLines, 2, "ENTER", FeedbackTone::Info);
                Serial.println("[F2] 29 after isolated render");
                break;
            }
#endif
            display_.showFloor(view);
            if (traceFloor2) Serial.println("[F2] 29 after gameplay render");
            break;
        }
        case Screen::Programs:
        {
            const size_t count = game_.cyberdeck().programCount();
            const char* names[Cyberdeck::SLOT_COUNT];
            const char* states[Cyberdeck::SLOT_COUNT];
            for (size_t i = 0; i < count; ++i) {
                const Program* p = game_.cyberdeck().programAt(i);
                names[i] = p->name(); states[i] = programStatus(*p);
            }
            display_.showPrograms(names, states, count, selected_, true); break;
        }
        case Screen::ProgramAction:
        {
            const Program* p = game_.cyberdeck().programAt(selectedProgram_);
            const ProgramDefinition* definition = p != nullptr ? programDefinition(p->id()) : nullptr;
            if (p->status() == ProgramStatus::Destroyed)
            {
                static const char* menu[] = {"BACK"};
                display_.showProgramAction(p->name(), "DESTROY",
                    definition != nullptr ? definition->shortRole : "",
                    definition != nullptr ? definition->shortEffect1 : "",
                    definition != nullptr ? definition->shortEffect2 : "", menu, 1, selected_);
            }
            else
            {
                const char* menu[] = {p->status() == ProgramStatus::Inactive ? "ACTIVATE" : "DEACTIVATE", "BACK"};
                display_.showProgramAction(p->name(), programStatus(*p),
                    definition != nullptr ? definition->shortRole : "",
                    definition != nullptr ? definition->shortEffect1 : "",
                    definition != nullptr ? definition->shortEffect2 : "", menu, 2, selected_);
            }
            break;
        }
        case Screen::PlayerIce:
        {
            const size_t count = game_.playerBlackIceCount();
            for (size_t i = 0; i < count; ++i)
            {
                const PlayerBlackIceRuntime* ice = game_.playerBlackIceAt(i);
                const BlackIceDefinition* definition = ice != nullptr ? ice->definition : nullptr;
                snprintf(configLabels_[i], sizeof(configLabels_[0]), "%s %s", definition != nullptr ? definition->displayName : "BLACK ICE",
                    ice != nullptr && ice->instance.active() ? "ACTIVE" : "INACTIVE");
                configItems_[i] = configLabels_[i];
            }
            configItems_[count] = "BACK";
            display_.showMenu("BLACK ICE", "PLAYER ICE RUNTIME", configItems_, count + 1, selected_); break;
        }
        case Screen::PlayerIceAction:
        {
            const PlayerBlackIceRuntime* ice = game_.playerBlackIceAt(selectedPlayerIce_);
            const BlackIceDefinition* definition = ice != nullptr ? ice->definition : nullptr;
            if (ice == nullptr || definition == nullptr) { screen_ = Screen::PlayerIce; selected_ = 0; redraw(); break; }
            snprintf(buffers[0], sizeof(buffers[0]), "REZ %d/%d", ice->instance.currentRez(), ice->instance.maxRez());
            snprintf(buffers[1], sizeof(buffers[1]), "%s", ice->instance.active() ? "ACTIVE" : "INACTIVE");
            snprintf(buffers[2], sizeof(buffers[2]), "%s", definition->shortRole);
            snprintf(buffers[3], sizeof(buffers[3]), "%s", definition->shortEffect1);
            snprintf(buffers[4], sizeof(buffers[4]), "%s", definition->shortEffect2);
            const EnemyNetrunnerRuntime* resolvedTarget =
                game_.findPlayerBlackIcePotentialTarget(*ice);
            const char* targetName = resolvedTarget != nullptr && resolvedTarget->definition != nullptr
                ? resolvedTarget->definition->name : nullptr;
            const char* finalTargetLabel = targetName != nullptr ? targetName : "NO TARGET";
            snprintf(buffers[5], sizeof(buffers[5]), "%s",
                finalTargetLabel);
            lines[0] = buffers[0]; lines[1] = buffers[1]; lines[2] = buffers[2]; lines[3] = buffers[3]; lines[4] = buffers[4]; lines[5] = buffers[5];
            display_.showInfo(definition->displayName, lines, 6, ice->instance.active() ? "ENTER: DEACTIVATE" : "ENTER: ACTIVATE", FeedbackTone::Info); break;
        }
        case Screen::PlayerIceTarget:
        {
            for (size_t i = 0; i < playerIceTargetCount_; ++i)
            {
                const EnemyNetrunnerRuntime* enemy = game_.enemyNetrunnerByRuntimeId(playerIceTargetIds_[i]);
                items[i] = enemy != nullptr && enemy->definition != nullptr ? enemy->definition->name : "ENEMY";
            }
            items[playerIceTargetCount_] = "BACK";
            display_.showMenu("SELECT TARGET", "LOCAL ENEMY", items, playerIceTargetCount_ + 1, selected_); break;
        }
        case Screen::EnemyAttack:
        {
            enemyMenuCount_ = 0;
            for (size_t index = 0; index < game_.cyberdeck().programCount() && enemyMenuCount_ < 9; ++index)
            {
                const Program* p = game_.cyberdeck().programAt(index);
                if (p != nullptr && p->type() == ProgramType::Attacker && p->usable() &&
                    p->id() != ProgramId::Sword && p->id() != ProgramId::Banhammer)
                {
                    enemyMenuSlots_[enemyMenuCount_] = index;
                    items[enemyMenuCount_++] = p->name();
                }
            }
            items[enemyMenuCount_++] = "BACK";
            display_.showMenu("ATTACK ENEMY", "SELECT REZZED PROGRAM", items, enemyMenuCount_, selected_); break;
        }
        case Screen::EnemyProgramTarget:
        {
            EnemyNetrunnerRuntime* enemy = game_.floorEnemyNetrunner();
            enemyMenuCount_ = 0;
            if (enemy != nullptr) for (size_t index = 0; index < enemy->cyberdeck.programCount() && enemyMenuCount_ < 9; ++index)
            {
                const Program* p = enemy->cyberdeck.programAt(index);
                if (p != nullptr && p->usable())
                {
                    enemyMenuSlots_[enemyMenuCount_] = index;
                    items[enemyMenuCount_++] = p->name();
                }
            }
            items[enemyMenuCount_++] = "BACK";
            display_.showMenu("TARGET PROGRAM", "ENEMY REZZED PROGRAM", items, enemyMenuCount_, selected_); break;
        }
        case Screen::EnemyProgramAttack:
        {
            enemyMenuCount_ = 0;
            for (size_t index = 0; index < game_.cyberdeck().programCount() && enemyMenuCount_ < 9; ++index)
            {
                const Program* p = game_.cyberdeck().programAt(index);
                if (p != nullptr && p->usable() &&
                    (p->id() == ProgramId::Sword || p->id() == ProgramId::Banhammer))
                {
                    enemyMenuSlots_[enemyMenuCount_] = index;
                    items[enemyMenuCount_++] = p->name();
                }
            }
            items[enemyMenuCount_++] = "BACK";
            display_.showMenu("ATTACK PROGRAM", "SELECT ATTACKER", items, enemyMenuCount_, selected_); break;
        }
        case Screen::Move:
        {
            const Architecture& architecture = game_.architecture();
            const size_t choices = architecture.connectionCount(architecture.currentPosition());
            for (size_t index = 0; index < choices && index < 9; ++index)
            {
                // Labels describe navigation context only; contents of an
                // unvisited neighbor remain undisclosed.
                movementChoiceLabelFor(game_, index, buffers[index], sizeof(buffers[index]));
                items[index] = buffers[index];
            }
            items[choices] = "BACK";
            display_.showMenu("MOVE", "MOVEMENT COST: 0", items, choices + 1, selected_); break;
        }
        case Screen::Map:
            buildArchitectureMapView(game_, mapView_);
            display_.showArchitectureMap(mapView_, mapPanX_, mapPanY_); break;
        case Screen::Result:
            for (size_t i = 0; i < resultLineCount_; ++i) lines[i] = resultLines_[i];
            display_.showInfo(resultTitle_, lines, resultLineCount_, "ENTER",
                resultToneExplicit_ ? resultTone_ :
                resultLineCount_ > 2 && (strstr(resultLines_[2], "HIT") || strstr(resultLines_[2], "MISS") ||
                    strstr(resultLines_[3], "FAIL") || strstr(resultLines_[3], "DENIED"))
                    ? FeedbackTone::Danger : FeedbackTone::Info,
                resultInfoLayout_); break;
        case Screen::JackOut:
        {
            static const char* menu[] = {"YES", "NO"};
            display_.showMenu("JACK OUT?", "SAFE DISCONNECT", menu, 2, selected_); break;
        }
        case Screen::UnsafeJackOutPrompt:
        {
            static const char* menu[] = {"YES", "NO"};
            display_.showMenu("UNSAFE JACK OUT?", "SAFE JACK BLOCKED", menu, 2, selected_); break;
        }
        case Screen::Objective:
        {
            static const char* text[] = {"CONTROL NODE ACQUIRED", "JACK OUT WHEN READY"};
            display_.showInfo("OBJECTIVE COMPLETE", text, 2, "ENTER", FeedbackTone::Success); break;
        }
        case Screen::RunSummary:
        {
            snprintf(buffers[0], 40, "TARGET // %.28s", game_.architecture().name());
            const char* exitResult = runEndCause_ == RunEndCause::SafeJackOut ? "SAFE JACK OUT" :
                runEndCause_ == RunEndCause::UnsafeJackOut ? "UNSAFE JACK OUT" : "RUNNER DOWN";
            const char* finalStatus = runEndCause_ == RunEndCause::SafeJackOut ? "RUN CLOSED" :
                runEndCause_ == RunEndCause::UnsafeJackOut ? "LINK SEVERED" : "CONNECTION LOST";
            snprintf(buffers[1], 40, "EXIT      %s", exitResult);
            snprintf(buffers[2], 40, "OBJECTIVE %s", game_.virusPlaced() ? "VIRUS PLACED" : "NOT COMPLETED");
            snprintf(buffers[3], 40, "HP        %u / %u", game_.runner().hp(), game_.runner().maxHp());
            if (game_.cloakUsed()) snprintf(buffers[4], 40, "CLOAK     %d", game_.cloakValue());
            else snprintf(buffers[4], 40, "CLOAK     NONE");
            size_t visited = 0;
            for (size_t index = 0; index < game_.architecture().floorCount(); ++index)
            {
                const Floor* floor = game_.architecture().floorAt(index);
                if (floor != nullptr && floor->visited) ++visited;
            }
            snprintf(buffers[5], 40, "VISITED   %u / %u",
                static_cast<unsigned>(visited), static_cast<unsigned>(game_.architecture().floorCount()));
            snprintf(buffers[6], 40, "%s", finalStatus);
            for (size_t index = 0; index < 7; ++index) lines[index] = buffers[index];
            const FeedbackTone tone = runEndCause_ == RunEndCause::SafeJackOut
                ? FeedbackTone::Success : FeedbackTone::Danger;
            if (!runSummaryRevealed_)
            {
                display_.animateRunSummary("RUN SUMMARY", lines, 7, "ENTER / BACK", tone);
                runSummaryRevealed_ = true;
            }
            else display_.showInfo("RUN SUMMARY", lines, 7, "ENTER / BACK", tone);
            break;
        }
        case Screen::RunComplete:
            snprintf(buffers[0], 40, "FILE: %s", completedFile_ ? "DOWNLOADED" : "SKIPPED");
            snprintf(buffers[1], 40, "CONTROL: %s", completedControl_ ? "COMPLETE" : "SKIPPED");
            snprintf(buffers[2], 40, "TURNS: %lu", static_cast<unsigned long>(completedTurns_));
            snprintf(buffers[3], 40, "CLOAK: %s", game_.cloakUsed() ? "" : "NONE");
            if (game_.cloakUsed()) snprintf(buffers[3], 40, "CLOAK: %d", game_.cloakValue());
            lines[0] = buffers[0]; lines[1] = buffers[1]; lines[2] = buffers[2]; lines[3] = buffers[3]; lines[4] = "> RETURN";
            display_.showInfo("RUN COMPLETE", lines, 5, "ENTER", FeedbackTone::Success); break;
        case Screen::ArchitectureComplete:
            snprintf(buffers[0], 40, "%s", game_.architecture().name());
            snprintf(buffers[1], 40, "FLOORS CLEARED %u/%u",
                static_cast<unsigned>(game_.architecture().currentPosition() + 1),
                static_cast<unsigned>(game_.architecture().floorCount()));
            snprintf(buffers[2], 40, "HP %u/%u", game_.runner().hp(), game_.runner().maxHp());
            snprintf(buffers[3], 40, "VIRUS %s", game_.virusPlaced() ? "PLACED" : "NOT PLACED");
            snprintf(buffers[4], 40, "CLOAK %s", game_.cloakUsed() ? "" : "NONE");
            if (game_.cloakUsed()) snprintf(buffers[4], 40, "CLOAK %d", game_.cloakValue());
            lines[0] = buffers[0]; lines[1] = buffers[1]; lines[2] = buffers[2]; lines[3] = buffers[3]; lines[4] = buffers[4];
            lines[5] = "> RETURN TO MENU";
            display_.showInfo("NETRUN COMPLETE", lines, 6, "ENTER / BACK", FeedbackTone::Success); break;
        case Screen::VirusPrompt:
        {
            static const char* menu[] = {"YES", "NO"};
            display_.showMenu("PLACE VIRUS?", "ARCHITECTURE END", menu, 2, selected_); break;
        }
        case Screen::RunnerDown:
            snprintf(buffers[0], 40, "HP 0 / %u", game_.runner().maxHp());
            static const char* downLines[] = {"NETRUN TERMINATED", "> RETURN TO MENU"};
            lines[0] = buffers[0]; lines[1] = downLines[0]; lines[2] = downLines[1];
            display_.showInfo("RUNNER DOWN", lines, 3, "ENTER / BACK", FeedbackTone::Danger); break;
        case Screen::ScenarioError:
            lines[0] = scenarioErrorFile_;
            if (scenarioErrorFloor_ >= 0) {
                snprintf(buffers[1], 40, "FLOOR %d", scenarioErrorFloor_);
                lines[1] = buffers[1]; lines[2] = scenarioErrorMessage_;
                display_.showInfo("SCENARIO ERROR", lines, 3, "> BACK", FeedbackTone::Danger);
            } else {
                lines[1] = scenarioErrorMessage_;
                display_.showInfo("SCENARIO ERROR", lines, 2, "> BACK", FeedbackTone::Danger);
            }
            break;
    }
}
