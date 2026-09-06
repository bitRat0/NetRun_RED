#include "Dice.h"

#include <esp_system.h>

int Dice::rollD10()
{
    return rollDie(10);
}

int Dice::rollD6(uint8_t count)
{
    int total = 0;
    for (uint8_t die = 0; die < count; ++die)
    {
        total += rollDie(6);
    }
    return total;
}

uint8_t Dice::rollIndex(uint8_t count)
{
    if (count == 0) return 0;
    const uint32_t bound = count;
    const uint32_t threshold = static_cast<uint32_t>(-bound) % bound;
    uint32_t value = 0;
    do
    {
        value = esp_random();
    } while (value < threshold);
    return static_cast<uint8_t>(value % bound);
}

int Dice::rollDie(uint8_t sides)
{
    return static_cast<int>(esp_random() % sides) + 1;
}
