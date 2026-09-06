#pragma once

#include <stdint.h>

// Neutral visual identities for the existing hand-authored ICE sprites.
// These IDs intentionally do not expose the current public/content names.
enum class IceVisualId : uint8_t
{
    Hound01,
    Bird01,
    Serpent01,
    Octopus01,
    Wraith01,
    Hunter01,
    Scorp01,
    Rat01,
    Winged01,
    Feline01,
    Skull01,
    BigGuy01,
    Count
};

// Presentation-only attack styles. These are independent of content names,
// gameplay effects and sprite visual IDs.
enum class HostileAttackStyle : uint8_t
{
    Lunge,
    Burst,
    Slash,
    Pulse,
    Count
};

constexpr bool isValidIceVisualId(IceVisualId id)
{
    return static_cast<uint8_t>(id) < static_cast<uint8_t>(IceVisualId::Count);
}


enum class DemonVisualId : uint8_t
{
    Orb01,
    Sentinel01,
    Crown01,
    Count
};

constexpr bool isValidDemonVisualId(DemonVisualId id)
{
    return static_cast<uint8_t>(id) < static_cast<uint8_t>(DemonVisualId::Count);
}
constexpr bool isValidHostileAttackStyle(HostileAttackStyle style)
{
    return static_cast<uint8_t>(style) < static_cast<uint8_t>(HostileAttackStyle::Count);
}
