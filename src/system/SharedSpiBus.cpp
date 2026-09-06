#include "SharedSpiBus.h"

#include <Arduino.h>
#include <SPI.h>

namespace
{
constexpr int SCK_PIN = 40;
constexpr int MISO_PIN = 39;
constexpr int MOSI_PIN = 14;
constexpr int SD_CS_PIN = 12;
constexpr int EXTERNAL_DISPLAY_CS_PIN = 5;
bool initialized = false;

void deselectAll()
{
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    pinMode(EXTERNAL_DISPLAY_CS_PIN, OUTPUT);
    digitalWrite(EXTERNAL_DISPLAY_CS_PIN, HIGH);
}
}

void SharedSpiBus::begin()
{
    if (initialized) return;

    deselectAll();
    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SD_CS_PIN);
    initialized = true;
    Serial.printf("[SPI] shared bus SCK=%d MISO=%d MOSI=%d\n", SCK_PIN, MISO_PIN, MOSI_PIN);
    Serial.printf("[SPI] SD CS=%d EXT CS=%d\n", SD_CS_PIN, EXTERNAL_DISPLAY_CS_PIN);
}

void SharedSpiBus::prepareForSd()
{
    deselectAll();
}

void SharedSpiBus::prepareForExternalDisplay()
{
    deselectAll();
}
