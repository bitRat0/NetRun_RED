#include "BuiltInArchitectures.h"
#include "game/EnemyNetrunner.h"

namespace
{
const FloorDefinition MILITECH_FLOORS[] = {
    {1, FloorType::Password, 6, SecurityTier::Low, "PASSWORD",
     nullptr, nullptr, 0, BlackIceType::Ice01, "ENCRYPTED ACCESS", nullptr, nullptr,
     {}, 0, nullptr, {2}, 1},
    {2, FloorType::File, 0, SecurityTier::Low, "DATA_FILE",
     "SECURITY_LOG.DAT", "SECURITY LOG", 350, BlackIceType::Ice01,
     "SECURITY LOG", nullptr, nullptr, {}, 0, nullptr, {1, 3}, 2},
    {3, FloorType::BlackICE, 0, SecurityTier::Low, "TRACEJACKAL",
     nullptr, nullptr, 0, BlackIceType::Ice01, "TRACEJACKAL", nullptr, nullptr,
     {}, 0, nullptr, {2, 4}, 2},
    {4, FloorType::ControlNode, 6, SecurityTier::Low, "CONTROL_NODE",
     nullptr, nullptr, 0, BlackIceType::Ice01, "FACILITY CAMERAS", "FACILITY CAMERAS", nullptr,
     {}, 0, nullptr, {3, 5}, 2},
    {5, FloorType::BlackICE, 0, SecurityTier::Low, "GHOSTPULSE",
     nullptr, nullptr, 0, BlackIceType::Ice05, "GHOSTPULSE", nullptr, nullptr,
     {}, 0, nullptr, {4, 6}, 2},
    {6, FloorType::BlackICE, 0, SecurityTier::Low, "CARRIONBYTE",
     nullptr, nullptr, 0, BlackIceType::Ice02, "CARRIONBYTE", nullptr, nullptr,
     {}, 0, nullptr, {5, 7}, 2},
    {7, FloorType::Empty, 0, SecurityTier::Low, "ENEMY_NETRUNNER",
     nullptr, nullptr, 0, BlackIceType::Ice01, "ENEMY NETRUNNER", nullptr, nullptr,
     {}, 0, &zerDefinition(), {6, 8}, 2},
    {8, FloorType::ControlNode, 6, SecurityTier::Low, "FINAL_OBJECTIVE",
     nullptr, nullptr, 0, BlackIceType::Ice01, "SECURITY CORE",
     "SECURITY CORE", "FINAL OBJECTIVE", {}, 0, nullptr, {7}, 1}
};

const ArchitectureDefinition MILITECH_TEST_NET = {
    "militech_test_net",
    "DEMO NET",
    "Linear Schema 1 showcase architecture for the Interface 4 runner.",
    MILITECH_FLOORS,
    sizeof(MILITECH_FLOORS) / sizeof(MILITECH_FLOORS[0])
};

const FloorDefinition NETRUNNER_COMBAT_FLOORS[] = {
    {1, FloorType::Password, 6, SecurityTier::Low, "ENTRY", nullptr, nullptr, 0,
     BlackIceType::Ice01, "ENTRY PASSWORD", nullptr, nullptr},
    {2, FloorType::File, 6, SecurityTier::Low, "TEST_DATA", "COMBAT_LOG.DAT", "LOG FILE", 250,
     BlackIceType::Ice01, "ENEMY OPERATIONS", nullptr, nullptr, {}, 0, &nullbyteDefinition()},
    {3, FloorType::BlackICE, 0, SecurityTier::Low, "GHOSTPULSE", nullptr, nullptr, 0,
     BlackIceType::Ice05, "GHOSTPULSE GUARD", nullptr, nullptr},
    {4, FloorType::ControlNode, 6, SecurityTier::Low, "EXIT_CONTROL", nullptr, nullptr, 0,
     BlackIceType::Ice01, nullptr, "TEST CONTROL", nullptr}
};

const ArchitectureDefinition NETRUNNER_COMBAT_TEST = {
    "netrunner_combat_test", "NETRUNNER COMBAT TEST",
    "Built-in Enemy Netrunner smoke-test architecture.", NETRUNNER_COMBAT_FLOORS,
    sizeof(NETRUNNER_COMBAT_FLOORS) / sizeof(NETRUNNER_COMBAT_FLOORS[0]), DemonType::Demon01
};

// 10 -> 20 -> (30, 40) -> 50 -> 70, with 60 as a Hub dead end. This provides
// a linear entry, a real choice, merge, dead end, and return paths.
const FloorDefinition BRANCHING_TEST_FLOORS[] = {
    {10, FloorType::Empty, 0, SecurityTier::Low, "ENTRY", nullptr, nullptr, 0,
     BlackIceType::Ice01, "ENTRY", nullptr, nullptr, {}, 0, nullptr, {20}, 1},
    {20, FloorType::Empty, 0, SecurityTier::Low, "HUB", nullptr, nullptr, 0,
     BlackIceType::Ice01, "HUB", nullptr, nullptr, {}, 0, nullptr, {10, 30, 40, 60}, 4},
    {30, FloorType::File, 6, SecurityTier::Low, "PATH_A", "PATH_A.DAT", "LOG", 100,
     BlackIceType::Ice01, "PATH A", nullptr, nullptr, {}, 0, nullptr, {20, 50}, 2},
    {40, FloorType::Password, 6, SecurityTier::Low, "PATH_B", nullptr, nullptr, 0,
     BlackIceType::Ice01, "PATH B", nullptr, nullptr, {}, 0, nullptr, {20, 50}, 2},
    {50, FloorType::ControlNode, 6, SecurityTier::Low, "MERGE", nullptr, nullptr, 0,
     BlackIceType::Ice01, "MERGE", "BRANCH CONTROL", nullptr, {}, 0, &nullbyteDefinition(), {30, 40, 70}, 3},
    {60, FloorType::File, 6, SecurityTier::Low, "DEAD_END", "VAULT.DAT", "DATA", 500,
     BlackIceType::Ice01, "DEAD END", nullptr, nullptr, {}, 0, nullptr, {20}, 1},
    {70, FloorType::ControlNode, 6, SecurityTier::Low, "EXIT", nullptr, nullptr, 0,
     BlackIceType::Ice01, "EXIT", "FINAL CONTROL", nullptr, {}, 0, nullptr, {50}, 1}
};

const ArchitectureDefinition BRANCHING_TEST_NET = {
    "branching_test_net", "BRANCHING TEST NET", "V1 graph/pathfinding test architecture.",
    BRANCHING_TEST_FLOORS, sizeof(BRANCHING_TEST_FLOORS) / sizeof(BRANCHING_TEST_FLOORS[0])
};

// 1 -> 2 -> (3 -> 4, 5) -> 6 -> 7, with 8 as a hub dead end.
// Branch A exercises a multi-ICE floor and a File; Branch B exercises a
// Control Node with an Enemy Netrunner. Both paths merge before the final
// Control Node. The highest-tier Demon keeps Control plus multiple Zaps visible.
const FloorDefinition V1_INTEGRATION_TEST_FLOORS[] = {
    {1, FloorType::Password, 6, SecurityTier::Medium, "INTEGRATION_ENTRY",
     nullptr, nullptr, 0, BlackIceType::Ice01, "ENTRY PASSWORD", nullptr, nullptr,
     {}, 0, nullptr, {2}, 1},
    {2, FloorType::Empty, 0, SecurityTier::Low, "INTEGRATION_HUB",
     nullptr, nullptr, 0, BlackIceType::Ice01, "BRANCH HUB", nullptr, nullptr,
     {}, 0, nullptr, {1, 3, 5, 8}, 4},
    {3, FloorType::BlackICE, 0, SecurityTier::Medium, "INTEGRATION_MULTI_ICE",
     nullptr, nullptr, 0, BlackIceType::Ice01, "MULTI ICE",
     nullptr, nullptr, {BlackIceType::Ice01, BlackIceType::Ice05, BlackIceType::Ice07}, 3,
     nullptr, {2, 4}, 2},
    {4, FloorType::File, 6, SecurityTier::Low, "INTEGRATION_BRANCH_A_FILE",
     "INTEGRATION_A.DAT", "LOG", 250, BlackIceType::Ice01, "BRANCH A FILE", nullptr, nullptr,
     {}, 0, nullptr, {3, 6}, 2},
    {5, FloorType::ControlNode, 6, SecurityTier::Low, "INTEGRATION_ENEMY_NODE",
     nullptr, nullptr, 0, BlackIceType::Ice01, "ENEMY CONTROL NODE", "ENEMY NODE", "NULLBYTE",
     {}, 0, &nullbyteDefinition(), {2, 6}, 2},
    {6, FloorType::BlackICE, 0, SecurityTier::High, "INTEGRATION_MERGE_ICE",
     nullptr, nullptr, 0, BlackIceType::Ice09, "MERGE ICE",
     nullptr, nullptr, {}, 0, nullptr, {4, 5, 7}, 3},
    {7, FloorType::ControlNode, 6, SecurityTier::High, "INTEGRATION_FINAL_CONTROL",
     nullptr, nullptr, 0, BlackIceType::Ice01, "FINAL CONTROL", "INTEGRATION CONTROL", "FINAL OBJECTIVE",
     {}, 0, nullptr, {6}, 1},
    {8, FloorType::BlackICE, 0, SecurityTier::Medium, "INTEGRATION_DEAD_END",
     nullptr, nullptr, 0, BlackIceType::Ice06, "DEAD END ICE",
     nullptr, nullptr, {}, 0, nullptr, {2}, 1}
};

const ArchitectureDefinition V1_INTEGRATION_TEST_NET = {
    "v1_integration_test_net", "V1 INTEGRATION TEST NET",
    "V1 stabilization integration architecture.", V1_INTEGRATION_TEST_FLOORS,
    sizeof(V1_INTEGRATION_TEST_FLOORS) / sizeof(V1_INTEGRATION_TEST_FLOORS[0]), DemonType::Demon03
};

const FloorDefinition ENEMY_FLOOR1_TEST_FLOORS[] = {
    {1, FloorType::File, 0, SecurityTier::Low, "COMBAT_SANDBOX", "TEST_BEACON.DAT", "TEST DATA", 100,
     BlackIceType::Ice01, "COMBAT SANDBOX", nullptr, nullptr, {}, 0, &nullbyteDefinition(), {2}, 1},
    {2, FloorType::ControlNode, 4, SecurityTier::Low, "TEST_CONTROL", nullptr, nullptr, 0,
     BlackIceType::Ice01, "TEST CONTROL", "SANDBOX CONTROL", nullptr, {}, 0, nullptr, {1, 3}, 2},
    {3, FloorType::File, 4, SecurityTier::Low, "TEST_EXIT", "EXIT.DAT", "TEST DATA", 100,
     BlackIceType::Ice01, "TEST EXIT", nullptr, nullptr, {}, 0, nullptr, {2}, 1}
};

const ArchitectureDefinition ENEMY_FLOOR1_TEST_NET = {
    "enemy_netrunner_duel", "ENEMY NETRUNNER DUEL",
    "Compact player-versus-enemy Netrunner duel.", ENEMY_FLOOR1_TEST_FLOORS,
    sizeof(ENEMY_FLOOR1_TEST_FLOORS) / sizeof(ENEMY_FLOOR1_TEST_FLOORS[0])
};

// Hardware review route for the 4A sprite pass. The hub keeps the visual
// groups close together; no review-only rules or runtime entities are needed.
CyberdeckConfig makeSpriteReviewNullbyteDeck()
{
    CyberdeckConfig deck;
    deck.quality = CyberdeckQuality::Standard;
    deck.programs[0] = ProgramId::Hellbolt;
    deck.programs[1] = ProgramId::Vrizzbolt;
    deck.programs[2] = ProgramId::Nervescrub;
    deck.programs[3] = ProgramId::Armor;
    deck.programs[4] = ProgramId::Sword;
    deck.programCount = 5;
    return deck;
}

const EnemyNetrunnerDefinition SPRITE_REVIEW_NULLBYTE = {
    "nullbyte", "NULLBYTE", 4, 30, 3,
    makeSpriteReviewNullbyteDeck(),
    EnemyAiArchetype::AntiPersonnel, true, EnemyBehaviorId::Baseline
};

// Keep the review route's contract explicit: Floor 10 is the Nullbyte spawn
// floor and must be part of the built-in definition, not an implicit tail.
const FloorDefinition SPRITE_REVIEW_FLOORS[10] = {
    {1, FloorType::Empty, 0, SecurityTier::Low, "REVIEW_HUB", nullptr, nullptr, 0,
     BlackIceType::Ice01, "SPRITE REVIEW HUB", nullptr, nullptr, {}, 0, nullptr, {2, 6, 7, 8}, 4},
    {2, FloorType::Password, 4, SecurityTier::Low, "REVIEW_PASSWORD", nullptr, nullptr, 0,
     BlackIceType::Ice01, "PASSWORD REVIEW", nullptr, nullptr, {}, 0, nullptr, {1, 3}, 2},
    {3, FloorType::File, 4, SecurityTier::Low, "REVIEW_DATA", "SPRITE_DATA.DAT", "ASSET DATA", 1,
     BlackIceType::Ice01, "DATA STORAGE REVIEW", nullptr, nullptr, {}, 0, nullptr, {2, 4}, 2},
    {4, FloorType::ControlNode, 4, SecurityTier::Low, "REVIEW_CONTROL", nullptr, nullptr, 0,
     BlackIceType::Ice01, "CONTROL NODE REVIEW", "REVIEW PANEL", nullptr, {}, 0, nullptr, {3, 5}, 2},
    {5, FloorType::ControlNode, 4, SecurityTier::Low, "REVIEW_FINAL", nullptr, nullptr, 0,
     BlackIceType::Ice01, "COMPLETION REVIEW", "REVIEW OBJECTIVE", nullptr, {}, 0, nullptr, {4}, 1},
    {6, FloorType::BlackICE, 0, SecurityTier::Low, "REVIEW_ANIMALS", nullptr, nullptr, 0,
     BlackIceType::Ice01, "ANIMAL ICE", nullptr, nullptr,
     {BlackIceType::Ice01, BlackIceType::Ice10, BlackIceType::Ice02}, 3, nullptr, {1}, 1},
    {7, FloorType::BlackICE, 0, SecurityTier::Low, "REVIEW_HEAVY", nullptr, nullptr, 0,
     BlackIceType::Ice06, "HEAVY ICE", nullptr, nullptr,
     {BlackIceType::Ice06, BlackIceType::Ice12, BlackIceType::Ice09}, 3, nullptr, {1, 10}, 2},
    {8, FloorType::BlackICE, 0, SecurityTier::Low, "REVIEW_ABSTRACT", nullptr, nullptr, 0,
     BlackIceType::Ice05, "ABSTRACT ICE", nullptr, nullptr,
     {BlackIceType::Ice02, BlackIceType::Ice05, BlackIceType::Ice04}, 3, nullptr, {1, 9}, 2},
    {9, FloorType::BlackICE, 0, SecurityTier::Low, "REVIEW_CREATURES", nullptr, nullptr, 0,
     BlackIceType::Ice03, "CREATURE ICE", nullptr, nullptr,
     {BlackIceType::Ice03, BlackIceType::Ice07, BlackIceType::Ice08}, 3, nullptr, {8}, 1},
    {10, FloorType::File, 4, SecurityTier::Low, "REVIEW_RUNNER", "NULLBYTE_CACHE.DAT", "RUNNER DATA", 1,
     BlackIceType::Ice01, "RUNNER + DATA REVIEW", nullptr, nullptr, {}, 0, &SPRITE_REVIEW_NULLBYTE, {7}, 1}
};

static_assert(sizeof(SPRITE_REVIEW_FLOORS) / sizeof(SPRITE_REVIEW_FLOORS[0]) == 10,
              "SPRITE_REVIEW_FLOORS must contain exactly 10 floors");

const ArchitectureDefinition SPRITE_REVIEW_NET = {
    "sprite_review_net", "SPRITE REVIEW NET",
    "Built-in Cardputer hardware review for NETRUN visuals.", SPRITE_REVIEW_FLOORS,
    sizeof(SPRITE_REVIEW_FLOORS) / sizeof(SPRITE_REVIEW_FLOORS[0]), DemonType::Demon01
};
}

const ArchitectureDefinition& BuiltInArchitectures::militechTestNet()
{
    return MILITECH_TEST_NET;
}

const ArchitectureDefinition& BuiltInArchitectures::netrunnerCombatTest()
{
    return NETRUNNER_COMBAT_TEST;
}

const ArchitectureDefinition& BuiltInArchitectures::branchingTestNet()
{
    return BRANCHING_TEST_NET;
}

const ArchitectureDefinition& BuiltInArchitectures::v1IntegrationTestNet()
{
    return V1_INTEGRATION_TEST_NET;
}

const ArchitectureDefinition& BuiltInArchitectures::enemyFloor1TestNet()
{
    return ENEMY_FLOOR1_TEST_NET;
}

const ArchitectureDefinition& BuiltInArchitectures::spriteReviewNet()
{
    return SPRITE_REVIEW_NET;
}
