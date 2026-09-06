#include "DigitalRollAnimation.h"

#include <esp_system.h>

DigitalRollSequence makeDigitalRollSequence(uint8_t finalValue)
{
    DigitalRollSequence sequence;
    for (size_t index = 0; index < DIGITAL_ROLL_FAKE_FRAME_COUNT; ++index)
        sequence.fakeValues[index] = static_cast<uint8_t>(esp_random() % 10) + 1;

    // NetCheckResult supplies a valid d10 result. Keep this defensive clamp local to rendering.
    sequence.finalValue = finalValue < 1 ? 1 : finalValue > 10 ? 10 : finalValue;
    return sequence;
}
