#pragma once

#include <stdint.h>

class Dice
{
public:
    virtual ~Dice() = default;

    virtual int rollD10();
    virtual int rollD6(uint8_t count);
    virtual uint8_t rollIndex(uint8_t count);

private:
    int rollDie(uint8_t sides);
};
