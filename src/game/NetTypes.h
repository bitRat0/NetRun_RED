#pragma once
#include <stddef.h>
#include <stdint.h>

struct EnemyNetrunnerDefinition;
struct BlackIceDefinition;

constexpr size_t MAX_ARCHITECTURE_FLOORS = 16;
constexpr size_t MAX_CONNECTIONS_PER_FLOOR = 4;
constexpr size_t MAX_BLACK_ICE_PER_FLOOR = 3;
constexpr size_t MAX_ACTIVE_BLACK_ICE = 8;

enum class ProgramType : uint8_t { Attacker, Defender, Booster };
// ProgramType remains the runtime class used by existing combat code. ProgramId
// identifies a catalog entry and is what persistent deck configuration stores.
enum class ProgramId : uint8_t {
    None,
    Sword,
    Banhammer,
    Armor,
    Flak,
    Shield,
    Eraser,
    SeeYa,
    SpeedyGonzalvez,
    Worm,
    DeckKRASH,
    Hellbolt,
    Nervescrub,
    PoisonFlatline,
    Superglue,
    Vrizzbolt,
    Count
};
enum class CyberdeckQuality : uint8_t { Poor, Standard, Excellent };
enum class HardwareId : uint8_t { BackupDrive, DnaLock, HardenedCircuitry, InsulatedWiring, KrashBarrier, RangeUpgrade, Count };
enum class ProgramStatus : uint8_t { Inactive, Rezzed, Derezzed, Destroyed };
enum class ProgramDamageMode : uint8_t { DerezzAtZero, DestroyAtZero };
enum class FloorType : uint8_t { Empty, Password, File, ControlNode, BlackICE };
enum class ControlOwner : uint8_t { None, Runner, Demon };
enum class DemonType : uint8_t { None = 0, Demon01 = 1, Demon02 = 2, Demon03 = 3, Count = 4 };
enum class SecurityTier : uint8_t { Low, Medium, High };
enum class BlackIceType : uint8_t {
    Ice01 = 0, Ice02 = 1, Ice03 = 2, Ice04 = 3, Ice05 = 4, Ice06 = 5,
    Ice07 = 6, Ice08 = 7, Ice09 = 8, Ice10 = 9, Ice11 = 10, Ice12 = 11,
    Count = 12
};
static_assert(static_cast<uint8_t>(DemonType::None) == 0 &&
    static_cast<uint8_t>(DemonType::Demon01) == 1 &&
    static_cast<uint8_t>(DemonType::Demon02) == 2 &&
    static_cast<uint8_t>(DemonType::Demon03) == 3 &&
    static_cast<uint8_t>(DemonType::Count) == 4, "DemonType ordinals changed");
static_assert(static_cast<uint8_t>(BlackIceType::Ice01) == 0 &&
    static_cast<uint8_t>(BlackIceType::Ice02) == 1 &&
    static_cast<uint8_t>(BlackIceType::Ice03) == 2 &&
    static_cast<uint8_t>(BlackIceType::Ice04) == 3 &&
    static_cast<uint8_t>(BlackIceType::Ice05) == 4 &&
    static_cast<uint8_t>(BlackIceType::Ice06) == 5 &&
    static_cast<uint8_t>(BlackIceType::Ice07) == 6 &&
    static_cast<uint8_t>(BlackIceType::Ice08) == 7 &&
    static_cast<uint8_t>(BlackIceType::Ice09) == 8 &&
    static_cast<uint8_t>(BlackIceType::Ice10) == 9 &&
    static_cast<uint8_t>(BlackIceType::Ice11) == 10 &&
    static_cast<uint8_t>(BlackIceType::Ice12) == 11 &&
    static_cast<uint8_t>(BlackIceType::Count) == 12, "BlackIceType ordinals changed");
enum class RunState : uint8_t { Idle, Ready, JackedIn, JackedOut, Completed, RunnerDown };
enum class TurnState : uint8_t { Inactive, Active, Ended };
enum class TurnPhase : uint8_t { Player, Demon, PlayerIce, Enemy, Ice };

struct Floor
{
    Floor() = default;
    Floor(uint8_t floorId, FloorType floorType, uint8_t difficultyValue,
          const char* floorContentId)
        : id(floorId), type(floorType), dv(difficultyValue),
          contentId(floorContentId)
    {
    }

    uint8_t id = 0;
    FloorType type = FloorType::Empty;
    uint8_t dv = 0;
    const char* contentId = nullptr;
    SecurityTier securityTier = SecurityTier::Low;
    const char* fileName = nullptr;
    const char* fileType = nullptr;
    uint32_t fileValue = 0;
    BlackIceType blackIceType = BlackIceType::Ice01;
    BlackIceType blackIceTypes[MAX_BLACK_ICE_PER_FLOOR] = {
        BlackIceType::Ice01, BlackIceType::Ice01, BlackIceType::Ice01};
    uint8_t blackIceCount = 0;
    const char* description = nullptr;
    const char* controlName = nullptr;
    const char* controlDescription = nullptr;
    bool discovered = false;
    bool visited = false;
    bool resolved = false;
    bool identified = false;
    bool controlled = false;
    ControlOwner controlOwner = ControlOwner::None;
    bool blackIceTriggered = false;
    bool downloaded = false;
    const EnemyNetrunnerDefinition* enemyNetrunnerDefinition = nullptr;
    uint8_t connections[MAX_CONNECTIONS_PER_FLOOR] = {};
    uint8_t connectionCount = 0;
    const BlackIceDefinition* blackIceDefinition = nullptr;
    const BlackIceDefinition* blackIceDefinitions[MAX_BLACK_ICE_PER_FLOOR] = {};
};
