#pragma once

#include <stdint.h>

#ifndef ENABLE_EXTERNAL_DISPLAY
#define ENABLE_EXTERNAL_DISPLAY 0
#endif

class ExternalDisplay
{
public:
    bool begin();
    bool enabled() const { return enabled_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }
    void mirrorFromInternal();
    void updateAmbient();
    void startAmbient();

private:
    bool enabled_ = false;
#if ENABLE_EXTERNAL_DISPLAY
    enum class AmbientState : uint8_t { Idle, Running };
    static constexpr int AMBIENT_TOP = 210;
    static constexpr int AMBIENT_HEIGHT = 30;
    static constexpr uint32_t AMBIENT_DURATION_MS = 5000;
    static constexpr uint32_t AMBIENT_FRAME_MS = 55;
    AmbientState ambientState_ = AmbientState::Idle;
    uint32_t nextAmbientAt_ = 0;
    uint32_t ambientStartedAt_ = 0;
    uint32_t ambientLastFrameAt_ = 0;
    uint8_t ambientVariant_ = 0;
    void scheduleNextAmbient(uint32_t now);
    void drawAmbientFrame(uint32_t elapsed);
    void drawAmbientDonut(int x, int y, uint8_t biteStage, uint8_t phase);
    void drawAmbientMouse(int x, int y, uint8_t phase);
    void clearAmbientArea();
    void ambientPixel(int x, int y, uint16_t color);
    void ambientFillRect(int x, int y, int width, int height, uint16_t color);
    void ambientDrawRect(int x, int y, int width, int height, uint16_t color);
    void ambientHLine(int x, int y, int width, uint16_t color);
    void ambientVLine(int x, int y, int height, uint16_t color);
    uint16_t ambientFrame_[320 * AMBIENT_HEIGHT] = {};
    uint16_t sourceLine_[240] = {};
    uint16_t scaledLine_[320] = {};
#endif
};
