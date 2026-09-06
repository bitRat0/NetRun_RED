#include "ExternalDisplay.h"

#if ENABLE_EXTERNAL_DISPLAY

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <M5Cardputer.h>
#include <SPI.h>
#include <esp_system.h>

#include "system/SharedSpiBus.h"

namespace
{
constexpr int EXTERNAL_CS = 5;
constexpr int EXTERNAL_RST = 3;
constexpr int EXTERNAL_DC = 6;
constexpr int SOURCE_WIDTH = 240;
constexpr int SOURCE_HEIGHT = 135;
constexpr int TARGET_WIDTH = 320;
constexpr int TARGET_HEIGHT = 240;
constexpr int TARGET_IMAGE_HEIGHT = 180;
constexpr int TARGET_TOP = (TARGET_HEIGHT - TARGET_IMAGE_HEIGHT) / 2;

// M5GFX readRect(uint16_t*) writes swap565_t values. Adafruit_SPITFT's
// drawRGBBitmap() expects the normal little-endian RGB565 value in memory and
// performs the SPI byte ordering itself on ESP32.
uint16_t normalRgb565(uint16_t m5gfxPixel)
{
    return static_cast<uint16_t>((m5gfxPixel << 8) | (m5gfxPixel >> 8));
}

Adafruit_ILI9341 externalPanel(&SPI, EXTERNAL_DC, EXTERNAL_CS, EXTERNAL_RST);
}

bool ExternalDisplay::begin()
{
    // The shared bus has already been initialized with the Cardputer SD MISO
    // pin. Adafruit_ILI9341 may call SPI.begin(), which is then a harmless
    // no-op instead of replacing the MISO assignment with -1.
    SharedSpiBus::prepareForExternalDisplay();
    externalPanel.begin();
    // Keep the shared-bus device deselected while SD.begin()/SD.open() run.
    digitalWrite(EXTERNAL_CS, HIGH);
    externalPanel.setRotation(3);
    externalPanel.fillScreen(ILI9341_BLACK);
    enabled_ = true;
    scheduleNextAmbient(millis());
    return enabled_;
}

void ExternalDisplay::scheduleNextAmbient(uint32_t now)
{
    nextAmbientAt_ = now + 55000UL + (esp_random() % 10001UL);
}

void ExternalDisplay::clearAmbientArea()
{
    SharedSpiBus::prepareForExternalDisplay();
    externalPanel.fillRect(0, AMBIENT_TOP, 320, AMBIENT_HEIGHT, ILI9341_BLACK);
}

void ExternalDisplay::ambientPixel(int x, int y, uint16_t color)
{
    if (x < 0 || x >= 320 || y < AMBIENT_TOP || y >= AMBIENT_TOP + AMBIENT_HEIGHT) return;
    ambientFrame_[(y - AMBIENT_TOP) * 320 + x] = color;
}

void ExternalDisplay::ambientFillRect(int x, int y, int width, int height, uint16_t color)
{
    for (int py = y; py < y + height; ++py)
        for (int px = x; px < x + width; ++px)
            ambientPixel(px, py, color);
}

void ExternalDisplay::ambientHLine(int x, int y, int width, uint16_t color)
{
    for (int px = x; px < x + width; ++px) ambientPixel(px, y, color);
}

void ExternalDisplay::ambientVLine(int x, int y, int height, uint16_t color)
{
    for (int py = y; py < y + height; ++py) ambientPixel(x, py, color);
}

void ExternalDisplay::ambientDrawRect(int x, int y, int width, int height, uint16_t color)
{
    ambientHLine(x, y, width, color);
    ambientHLine(x, y + height - 1, width, color);
    ambientVLine(x, y, height, color);
    ambientVLine(x + width - 1, y, height, color);
}

void ExternalDisplay::drawAmbientDonut(int x, int y, uint8_t biteStage, uint8_t phase)
{
    const uint16_t dough = 0xFD20;
    const uint16_t glaze = phase & 1U ? ILI9341_RED : ILI9341_MAGENTA;

    // Compact pixel donut. The final three stages remove the glaze and body
    // in small bites instead of introducing a second sprite system.
    if (biteStage == 0)
    {
        ambientFillRect(x + 2, y + 2, 12, 8, dough);
        ambientDrawRect(x + 1, y + 1, 14, 10, ILI9341_WHITE);
        ambientFillRect(x + 5, y + 4, 6, 4, ILI9341_BLACK);
        ambientPixel(x + 4, y + 2, glaze);
        ambientPixel(x + 8, y + 1, glaze);
        ambientPixel(x + 12, y + 3, ILI9341_GREEN);
        ambientPixel(x + 3, y + 8, ILI9341_RED);
    }
    else if (biteStage == 1)
    {
        ambientFillRect(x + 3, y + 3, 10, 7, dough);
        ambientDrawRect(x + 2, y + 2, 12, 9, ILI9341_WHITE);
        ambientFillRect(x + 6, y + 5, 4, 3, ILI9341_BLACK);
        ambientPixel(x + 5, y + 3, glaze);
        ambientPixel(x + 10, y + 4, ILI9341_GREEN);
    }
    else if (biteStage == 2)
    {
        ambientFillRect(x + 5, y + 4, 7, 5, dough);
        ambientDrawRect(x + 4, y + 3, 9, 7, glaze);
        ambientFillRect(x + 7, y + 5, 3, 2, ILI9341_BLACK);
    }
    else
    {
        ambientFillRect(x + 7, y + 5, 3, 3, glaze);
    }
}

void ExternalDisplay::drawAmbientMouse(int x, int y, uint8_t phase)
{
    const uint16_t shell = ILI9341_RED;
    const uint16_t accent = phase & 1U ? ILI9341_MAGENTA : ILI9341_YELLOW;

    // Compact angular ICE-like mouse: 16x15 including ears, tail and legs.
    ambientFillRect(x + 3, y + 4, 10, 8, shell);
    ambientFillRect(x + 4, y + 1, 3, 4, shell);
    ambientFillRect(x + 10, y + 1, 3, 4, shell);
    ambientDrawRect(x + 2, y + 3, 12, 9, accent);
    ambientPixel(x + 11, y + 5, ILI9341_GREEN);
    ambientHLine(x - 3, y + 7, 5, ILI9341_MAGENTA);
    ambientPixel(x - 5, y + 6, ILI9341_RED);
    ambientVLine(x + 5, y + 12, phase & 1U ? 3 : 2, shell);
    ambientVLine(x + 11, y + 12, phase & 1U ? 2 : 3, shell);
}

void ExternalDisplay::drawAmbientFrame(uint32_t elapsed)
{
    constexpr int WIDTH = 320;
    constexpr int DONUT_WIDTH = 16;
    constexpr int MOUSE_WIDTH = 16;
    const uint32_t clamped = elapsed > AMBIENT_DURATION_MS ? AMBIENT_DURATION_MS : elapsed;

    // Rebuild only the reserved 320x30 strip in RAM. The mirror at y=30..209
    // is never touched by ambient rendering, and no visible clear is issued.
    for (size_t index = 0; index < sizeof(ambientFrame_) / sizeof(ambientFrame_[0]); ++index)
        ambientFrame_[index] = ILI9341_BLACK;

    int donutX;
    int mouseX;
    uint8_t phase = static_cast<uint8_t>((clamped / AMBIENT_FRAME_MS) & 1U);
    const uint32_t chaseTime = clamped < 3500 ? clamped : 3500;
    if (ambientVariant_ == 1)
    {
        const uint32_t accelerated = chaseTime < 1800 ? chaseTime : 1800 + (chaseTime - 1800) * 6UL / 5UL;
        donutX = -DONUT_WIDTH + static_cast<int>(accelerated * 300UL / 3500UL);
        mouseX = -MOUSE_WIDTH + (chaseTime < 700 ? 0 : static_cast<int>((chaseTime - 700) * 310UL / 2800UL));
    }
    else
    {
        donutX = -DONUT_WIDTH + static_cast<int>(chaseTime * 300UL / 3500UL);
        mouseX = -MOUSE_WIDTH + (chaseTime < 900 ? 0 : static_cast<int>((chaseTime - 900) * 310UL / 2600UL));
    }

    // Freeze both actors at the point where the chase ends. The donut already
    // stays at the final chase position; keep the mouse there through all
    // three bite frames as well.
    const int catchX = mouseX;
    if (clamped >= 3500)
    {
        if (clamped < 4300) mouseX = catchX;
        else
            // Only after Bite 3 has removed the donut does the mouse run out.
            mouseX = catchX + static_cast<int>((clamped - 4300) * 70UL / 700UL);
    }
    const int donutHop = ambientVariant_ == 2 && clamped > 900 && clamped < 1500 ?
        static_cast<int>((clamped - 900) / 100) % 2 * -3 : 0;
    const uint8_t biteStage = clamped >= 3700 && clamped < 4300 ?
        static_cast<uint8_t>(1 + (clamped - 3700) / 200) : 0;
    if (clamped < 4300)
        drawAmbientDonut(donutX, AMBIENT_TOP + 10 + donutHop, biteStage, phase);
    drawAmbientMouse(mouseX, AMBIENT_TOP + 7, phase);

    // A restrained ground trace reinforces the cybernetic, pixel-art style.
    ambientHLine(0, 237, WIDTH, 0x2104);
    ambientPixel((donutX + 6) & 319, 238, ILI9341_MAGENTA);
    SharedSpiBus::prepareForExternalDisplay();
    externalPanel.drawRGBBitmap(0, AMBIENT_TOP, ambientFrame_, WIDTH, AMBIENT_HEIGHT);
}

void ExternalDisplay::startAmbient()
{
    if (!enabled_ || ambientState_ == AmbientState::Running) return;
    const uint32_t now = millis();
    ambientState_ = AmbientState::Running;
    ambientStartedAt_ = now;
    ambientLastFrameAt_ = now;
    ambientVariant_ = static_cast<uint8_t>(esp_random() % 3U);
    drawAmbientFrame(0);
}

void ExternalDisplay::updateAmbient()
{
    if (!enabled_) return;

    const uint32_t now = millis();
    if (ambientState_ == AmbientState::Idle)
    {
        if (static_cast<int32_t>(now - nextAmbientAt_) < 0) return;
        startAmbient();
        return;
    }

    if (now - ambientStartedAt_ >= AMBIENT_DURATION_MS)
    {
        clearAmbientArea();
        ambientState_ = AmbientState::Idle;
        scheduleNextAmbient(now);
        return;
    }

    if (now - ambientLastFrameAt_ >= AMBIENT_FRAME_MS)
    {
        ambientLastFrameAt_ = now;
        drawAmbientFrame(now - ambientStartedAt_);
    }
}

void ExternalDisplay::mirrorFromInternal()
{
    if (!enabled_) return;

    SharedSpiBus::prepareForExternalDisplay();

    // Read and scale one source row at a time. This deliberately avoids a
    // persistent 320x240 framebuffer; only 240 + 320 RGB565 pixels exist.
    for (int sourceY = 0; sourceY < SOURCE_HEIGHT; ++sourceY)
    {
        M5Cardputer.Display.readRect(0, sourceY, SOURCE_WIDTH, 1, sourceLine_);
        for (int targetX = 0; targetX < TARGET_WIDTH; ++targetX)
            scaledLine_[targetX] = normalRgb565(
                sourceLine_[(targetX * SOURCE_WIDTH) / TARGET_WIDTH]);

        const int firstTargetY = (sourceY * TARGET_IMAGE_HEIGHT) / SOURCE_HEIGHT;
        const int nextTargetY = ((sourceY + 1) * TARGET_IMAGE_HEIGHT) / SOURCE_HEIGHT;
        for (int targetY = firstTargetY; targetY < nextTargetY; ++targetY)
            externalPanel.drawRGBBitmap(0, TARGET_TOP + targetY, scaledLine_, TARGET_WIDTH, 1);
    }
}

#else

bool ExternalDisplay::begin() { return false; }
void ExternalDisplay::mirrorFromInternal() {}
void ExternalDisplay::updateAmbient() {}
void ExternalDisplay::startAmbient() {}

#endif
