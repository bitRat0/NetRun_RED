#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr size_t DIGITAL_ROLL_FAKE_FRAME_COUNT = 5;

struct DigitalRollSequence
{
    uint8_t fakeValues[DIGITAL_ROLL_FAKE_FRAME_COUNT] = {};
    uint8_t finalValue = 1;
};

// This is presentation-only randomness. It never accesses the injected gameplay Dice.
DigitalRollSequence makeDigitalRollSequence(uint8_t finalValue);
