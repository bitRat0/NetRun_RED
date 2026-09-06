#pragma once

#include <stddef.h>
#include <stdint.h>

#include "game/VisualIds.h"
#include "ExternalDisplay.h"
#include "game/NetTypes.h"

enum class FloorVisual : uint8_t { Empty, Password, File, ControlNode, Trophy, EnemyNetrunner, Ice01, Ice02, Ice03, Ice04, Ice05, Ice06, Ice07, Ice08, Ice09, Ice10, Ice11, Ice12 };
enum class FeedbackTone : uint8_t { Neutral, Info, Success, Warning, Danger, Inactive };
enum class InfoLayout : uint8_t { Automatic, Combat };
enum class PlayerActionVisual : uint8_t {
    None, Zap, Sword, Banhammer, Slide,
    Hellbolt, Vrizzbolt, Nervescrub, Superglue, PoisonFlatline, DeckKrash
};
enum class HostileActorVisual : uint8_t { None, BlackIce, EnemyNetrunner, Demon };
enum class BlackIcePresentationSide : uint8_t { Hostile, Player };

constexpr size_t MAX_ARCHITECTURE_MAP_NODES = 16;
constexpr size_t MAX_ARCHITECTURE_MAP_EDGES = 48;
enum class ArchitectureMapNodeStatus : uint8_t { Unknown, Completed, Actionable, Hostile };

struct ArchitectureMapNode
{
    uint8_t floorIndex = 0;
    int16_t x = 0;
    int16_t y = 0;
    char symbol = '?';
    uint8_t floorNumber = 0;
    bool discovered = false;
    bool visited = false;
    bool currentPlayer = false;
    bool playerIce = false;
    ArchitectureMapNodeStatus status = ArchitectureMapNodeStatus::Unknown;
};

struct ArchitectureMapEdge { uint8_t from = 0; uint8_t to = 0; };

struct ArchitectureMapView
{
    const char* architecture = nullptr;
    ArchitectureMapNode nodes[MAX_ARCHITECTURE_MAP_NODES];
    ArchitectureMapEdge edges[MAX_ARCHITECTURE_MAP_EDGES];
    size_t nodeCount = 0;
    size_t edgeCount = 0;
    bool hasPlayerIce = false;
};

struct FloorView
{
    const char* architecture;
    uint8_t floor;
    uint8_t floorCount;
    uint32_t turn;
    uint8_t actions;
    uint8_t maxActions;
    FloorVisual visual;
    const char* title;
    const char* status;
    const char* metadataType = nullptr;
    uint32_t metadataValue = 0;
    uint8_t securityClass = 0;
    int value = 0;
    int maxValue = 0;
    const char* const* actionLabels = nullptr;
    size_t actionCount = 0;
    size_t selected = 0;
    size_t chaseTotal = 0;
    const char* targetSummary = nullptr;
    const char* playerStatus = nullptr;
    bool architectureCompleted = false;
    size_t targetIndex = 0;
    size_t targetCount = 0;
    uint8_t perception = 0;
    uint8_t speed = 0;
    uint8_t attack = 0;
    uint8_t defense = 0;
    IceVisualId iceVisual = IceVisualId::Hound01;
    uint8_t runnerHp = 0;
    uint8_t runnerMaxHp = 0;
    bool demonActive = false;
    int demonRez = 0;
    int demonMaxRez = 0;
    uint8_t demonType = 0;
    const char* demonName = nullptr;
};

class DisplayManager
{
public:
    static constexpr size_t MENU_FIRST_AUTO = static_cast<size_t>(-1);
    // Main entities share one stable logical bounding box.
    static constexpr int MAIN_SPRITE_CANVAS = 48;
    void begin();
    void showMenu(const char* title, const char* subtitle,
                  const char* const items[], size_t itemCount, size_t selected,
                  bool marqueeTitle = false, size_t firstItem = MENU_FIRST_AUTO);
    void showInfo(const char* title, const char* const lines[], size_t lineCount,
                  const char* footer = "ENTER / FN BACK",
                  FeedbackTone tone = FeedbackTone::Neutral,
                  InfoLayout layout = InfoLayout::Automatic);
    void showFloor(const FloorView& view);
    void showArchitectureMap(const ArchitectureMapView& view, int16_t panX, int16_t panY);
    void showPrograms(const char* const names[], const char* const states[],
                      size_t slotCount, size_t selected, bool includeBack);
    void showProgramAction(const char* title, const char* status, const char* role,
                           const char* effect1, const char* effect2,
                           const char* const items[], size_t count, size_t selected);
    void animateJackIn(const char* targetName);
    void animateRunSummary(const char* title, const char* const lines[], size_t lineCount,
                           const char* footer, FeedbackTone tone);
    void animateDigitalRoll(const char* checkName, uint8_t finalRoll);
    void animateTransition(FeedbackTone tone);
    void animateEncounterIntro(const char* iceName);
    void animateEnemyPresence(const char* handle);
    void animateHostileAppearance(FloorVisual visual, const char* label, uint8_t demonType = 0,
                                  IceVisualId iceVisual = IceVisualId::Count);
    void animateHostileAction(HostileActorVisual actor, FloorVisual visual = FloorVisual::Empty,
                              uint8_t demonType = 0, bool hit = false,
                              ProgramId program = ProgramId::None,
                              IceVisualId iceVisual = IceVisualId::Count,
                              HostileAttackStyle style = HostileAttackStyle::Pulse,
                              BlackIcePresentationSide side = BlackIcePresentationSide::Hostile);
    void animateFireDamage();
    void animateTransfer(const char* label);
    void animateDataTransfer();
    void animateDataTransferFailure();
    void animateCloak(bool success);
    void animateControlSuccess();
    void animateControlFailure();
    void animateControlReclaim(const char* demonName, const char* nodeName);
    void animatePasswordUnlock();
    void animatePasswordFailure();
    void animateEyeDee(bool success);
    void animatePathfinder();
    void holdSystemAnimationFinalFrame();
    void animateFloorMovement(uint8_t destinationFloor, int8_t direction);
    void animateFloorMovementFailure();
    void animateConnectionLoss();
    void animatePlayerAction(PlayerActionVisual visual, FeedbackTone tone = FeedbackTone::Info);
    void setPresentationSuppressed(bool suppressed) { presentationSuppressed_ = suppressed; }
    bool presentationSuppressed() const { return presentationSuppressed_; }
    void invalidateFloorCache() { floorCacheValid_ = false; }
    void updateMarquee();
    void updateAmbient();
    void startAmbient();
    static bool hasIceVisualRenderer(IceVisualId visualId);
    static bool hasHostileAttackStyle(HostileAttackStyle style);

private:
    // Floor geometry is shared by the renderer and the action-overlay path.
    // The animation region stays left of the action-panel frame and above the
    // status/HP lines, so it cannot affect navigational UI.
    static constexpr int FLOOR_ACTION_PANEL_X = 157;
    static constexpr int FLOOR_ACTION_PANEL_Y = 18;
    static constexpr int FLOOR_ACTION_PANEL_WIDTH = 80;
    static constexpr int FLOOR_ACTION_PANEL_HEIGHT = 108;
    static constexpr int FLOOR_ANIMATION_X = 6;
    static constexpr int FLOOR_ANIMATION_Y = 30;
    static constexpr int FLOOR_ANIMATION_WIDTH = FLOOR_ACTION_PANEL_X - FLOOR_ANIMATION_X - 6;
    static constexpr int FLOOR_ANIMATION_HEIGHT = 60;
    static constexpr int FLOOR_ACTOR_SPRITE_X = 37;
    static constexpr int FLOOR_ACTOR_SPRITE_Y = 32;
    static constexpr int FLOOR_ACTOR_CENTER_X = FLOOR_ACTOR_SPRITE_X + MAIN_SPRITE_CANVAS / 2;
    static constexpr int FLOOR_ACTOR_CENTER_Y = FLOOR_ACTOR_SPRITE_Y + MAIN_SPRITE_CANVAS / 2;
    // Enemy runner bust: red helmet/body spans x=42..85 and y=34..72.
    static constexpr int FLOOR_ENEMY_RUNNER_CENTER_X = 64;
    static constexpr int FLOOR_ENEMY_RUNNER_CENTER_Y = 53;

    struct MarqueeState
    {
        bool active = false;
        char text[64] = {};
        const char* region = "";
        uint32_t startMs = 0;
        uint32_t lastFrameMs = 0;
        int32_t textWidth = 0;
        int32_t viewWidth = 0;
        int32_t x = 0;
        int32_t y = 0;
        int32_t height = 0;
        int32_t lastOffset = -1;
        uint16_t color = 0xFFFF;
        uint8_t textSize = 1;
    };

    void configureMarquee(MarqueeState& state, const char* region, const char* text,
                          int32_t x, int32_t y, int32_t width, int32_t height,
                          uint8_t textSize, uint16_t color);
    void drawStaticClippedText(const char* text, int32_t x, int32_t y, int32_t width,
                               int32_t height, uint8_t textSize, uint16_t color);
    void drawMarqueeFrame(const MarqueeState& state);
    bool updateMarqueeState(MarqueeState& state, uint32_t now);
    void present();
    void waitForFrameBudget(uint32_t frameStart, uint32_t frameDurationMs);

    class PresentGuard
    {
    public:
        explicit PresentGuard(DisplayManager& display) : display_(display) {}
        ~PresentGuard() { display_.present(); }
    private:
        DisplayManager& display_;
    };

    void clear();
    void drawMenu(const char* const items[], size_t itemCount, size_t selected, int y,
                  int rowHeight = 10, size_t firstItem = MENU_FIRST_AUTO);
    void drawHud(const FloorView& view);
    void drawNetActions(int x, int y, uint8_t current, uint8_t maximum);
    void drawBar(int x, int y, int width, int value, int maximum, uint16_t color);
    void drawPasswordDoor(int x, int y, uint8_t securityClass, bool open);
    void drawDatabaseNode(int x, int y, bool downloaded);
    void drawControlNode(int x, int y, bool owned);
    void drawDemonActor(int x, int y, uint8_t type, bool active);
    void drawEnemyNetrunner(int x, int y);
    void drawHostileSprite(FloorVisual visual, int x, int y, FeedbackTone tone, uint8_t demonType,
                           IceVisualId iceVisual = IceVisualId::Count);
    void drawIceVisual(IceVisualId visualId, int x, int y, FeedbackTone tone);
    void drawHound01(int x, int y, FeedbackTone tone);
    void drawBird01(int x, int y, FeedbackTone tone);
    void drawSerpent01(int x, int y, FeedbackTone tone);
    void drawOctopus01(int x, int y, FeedbackTone tone);
    void drawWraith01(int x, int y, FeedbackTone tone);
    void drawHunter01(int x, int y, FeedbackTone tone);
    void drawScorp01(int x, int y, FeedbackTone tone);
    void drawRat01(int x, int y, FeedbackTone tone);
    void drawWinged01(int x, int y, FeedbackTone tone);
    void drawFeline01(int x, int y, FeedbackTone tone);
    void drawSkull01(int x, int y, FeedbackTone tone);
    void drawBigGuy01(int x, int y, FeedbackTone tone);
    bool menuCacheValid_ = false;
    size_t cachedMenuCount_ = 0;
    size_t cachedMenuSelected_ = 0;
    char cachedMenuTitle_[32] = {};
    char cachedMenuSubtitle_[40] = {};
    bool programCacheValid_ = false;
    size_t cachedProgramCount_ = 0;
    size_t cachedProgramSelected_ = 0;
    bool floorCacheValid_ = false;
    bool presentationSuppressed_ = false;
    uint8_t cachedFloor_ = 0;
    uint32_t cachedTurn_ = 0;
    FloorVisual cachedFloorVisual_ = FloorVisual::Empty;
    IceVisualId cachedIceVisual_ = IceVisualId::Hound01;
    uint8_t cachedFloorActions_ = 0;
    uint8_t cachedFloorRunnerHp_ = 0;
    bool cachedFloorArchitectureCompleted_ = false;
    int cachedFloorValue_ = 0;
    int cachedFloorMaxValue_ = 0;
    bool cachedDemonActive_ = false;
    int cachedDemonRez_ = 0;
    uint8_t cachedDemonType_ = 0;
    uint16_t floorAnimationBackground_[FLOOR_ANIMATION_WIDTH * FLOOR_ANIMATION_HEIGHT] = {};
    size_t cachedFloorActionCount_ = 0;
    size_t cachedFloorSelected_ = 0;
    char cachedFloorTitle_[32] = {};
    char cachedFloorStatus_[32] = {};
    char cachedPlayerStatus_[64] = {};
    MarqueeState marquee_;
    MarqueeState floorHudMarquee_;
    MarqueeState playerStatusMarquee_;
    ExternalDisplay externalDisplay_;
};
