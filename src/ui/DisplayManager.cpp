#include "DisplayManager.h"
#include "app/DebugConfig.h"
#include "DigitalRollAnimation.h"
#include "ListViewState.h"
#include <Arduino.h>
#include <M5Cardputer.h>
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint32_t MARQUEE_INITIAL_HOLD_MS = 1500;
constexpr uint32_t MARQUEE_SPEED_PX_PER_SECOND = 22;
constexpr int32_t MARQUEE_GAP_PX = 16;
constexpr uint32_t MARQUEE_FRAME_MS = 45;
constexpr uint32_t SYSTEM_ANIMATION_FINAL_HOLD_MS = 500;

// Bright magenta remains legible on the Cardputer LCD without cyan glare.
constexpr uint16_t UI_MAGENTA = 0xF81F, UI_RED = TFT_RED, UI_GREEN = TFT_GREEN;
constexpr uint16_t UI_YELLOW = TFT_YELLOW, UI_ORANGE = 0xFD20, UI_DIM = TFT_DARKGREY;
uint16_t toneColor(FeedbackTone tone) {
    switch (tone) {
        case FeedbackTone::Info: return UI_MAGENTA;
        case FeedbackTone::Success: return UI_GREEN;
        case FeedbackTone::Warning: return UI_YELLOW;
        case FeedbackTone::Danger: return UI_RED;
        case FeedbackTone::Inactive: return UI_DIM;
        default: return TFT_WHITE;
    }
}

uint16_t spriteInnerColor(FeedbackTone tone)
{
    if (tone == FeedbackTone::Success) return 0x0240;
    if (tone == FeedbackTone::Inactive) return 0x2104;
    return tone == FeedbackTone::Danger ? 0x7800 : TFT_DARKGREY;
}

uint16_t spriteHighlightColor(FeedbackTone tone)
{
    if (tone == FeedbackTone::Success) return 0x07E0;
    if (tone == FeedbackTone::Inactive) return UI_DIM;
    return tone == FeedbackTone::Danger ? UI_MAGENTA : TFT_LIGHTGREY;
}

// Keep titles on one deterministic line so they cannot collide with adjacent
// UI. The helper uses caller-provided fixed buffers and never allocates.
void copyHeaderTitle(const char* source, char* destination, size_t capacity,
                     size_t maxChars = 18)
{
    if (capacity == 0) return;
    const char* value = source != nullptr ? source : "";
    const size_t length = strlen(value);
    const size_t visibleChars = capacity - 1 < maxChars ? capacity - 1 : maxChars;
    if (visibleChars == 0) {
        destination[0] = '\0';
        return;
    }
    if (length <= visibleChars) {
        snprintf(destination, capacity, "%s", value);
        return;
    }
    if (visibleChars <= 3) {
        snprintf(destination, capacity, "%.*s", static_cast<int>(visibleChars), value);
        return;
    }
    snprintf(destination, capacity, "%.*s...", static_cast<int>(visibleChars - 3), value);
}

size_t combatOutcomeLine(const char* const lines[], size_t count)
{
    for (size_t index = 0; index < count; ++index)
    {
        const char* line = lines[index] != nullptr ? lines[index] : "";
        if (strstr(line, "HIT") || strstr(line, "MISS") ||
            strstr(line, "SUCCESS") || strstr(line, "FAIL") ||
            strstr(line, "ACTION REJECTED")) return index;
    }
    return count;
}
}

void DisplayManager::begin()
{
    M5Cardputer.Display.setRotation(1);
    const bool externalDisplayOk = externalDisplay_.begin();
    Serial.printf("[ExtDisplay] begin: %s\n", externalDisplayOk ? "OK" : "FAIL");
    clear();
}

void DisplayManager::clear()
{
    menuCacheValid_ = false;
    programCacheValid_ = false;
    floorCacheValid_ = false;
    marquee_.active = false;
    floorHudMarquee_.active = false;
    playerStatusMarquee_.active = false;
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
}

void DisplayManager::present()
{
    // The internal display is the only renderer. The optional external panel
    // receives exactly the fully rendered frame currently shown internally.
    externalDisplay_.mirrorFromInternal();
}

void DisplayManager::configureMarquee(MarqueeState& state, const char* region,
                                       const char* text, int32_t x, int32_t y,
                                       int32_t width, int32_t height,
                                       uint8_t textSize, uint16_t color)
{
    const char* value = text != nullptr ? text : "";
    const bool same = !strcmp(state.text, value) && state.x == x && state.y == y &&
        state.viewWidth == width && state.height == height && state.textSize == textSize &&
        state.color == color && !strcmp(state.region, region);
    if (same && state.active) return;

    snprintf(state.text, sizeof(state.text), "%s", value);
    state.region = region;
    state.x = x;
    state.y = y;
    state.viewWidth = width;
    state.height = height;
    state.textSize = textSize;
    state.color = color;
    M5Cardputer.Display.setTextSize(textSize);
    state.textWidth = M5Cardputer.Display.textWidth(state.text);
    state.active = state.textWidth > state.viewWidth;
    state.startMs = millis();
    state.lastFrameMs = state.startMs;
    state.lastOffset = 0;
    drawMarqueeFrame(state);
}

void DisplayManager::drawMarqueeFrame(const MarqueeState& state)
{
    M5Cardputer.Display.setTextSize(state.textSize);
    M5Cardputer.Display.setTextColor(state.color, TFT_BLACK);
    M5Cardputer.Display.setClipRect(state.x, state.y, state.viewWidth, state.height);
    M5Cardputer.Display.fillRect(state.x, state.y, state.viewWidth, state.height, TFT_BLACK);
    // M5GFX print() applies its default X-wrap policy and clamps a negative
    // cursor to the clip's left edge. drawString() takes the signed position
    // directly, so the text is clipped rather than snapped back to the start.
    M5Cardputer.Display.drawString(state.text, state.x - state.lastOffset, state.y);
    M5Cardputer.Display.clearClipRect();
}

void DisplayManager::drawStaticClippedText(const char* text, int32_t x, int32_t y,
                                           int32_t width, int32_t height,
                                           uint8_t textSize, uint16_t color)
{
    M5Cardputer.Display.setTextSize(textSize);
    M5Cardputer.Display.setTextColor(color, TFT_BLACK);
    M5Cardputer.Display.setClipRect(x, y, width, height);
    M5Cardputer.Display.fillRect(x, y, width, height, TFT_BLACK);
    M5Cardputer.Display.setCursor(x, y);
    M5Cardputer.Display.print(text != nullptr ? text : "");
    M5Cardputer.Display.clearClipRect();
}

void DisplayManager::updateMarquee()
{
    const uint32_t now = millis();
    const bool mainChanged = updateMarqueeState(marquee_, now);
    const bool hudChanged = updateMarqueeState(floorHudMarquee_, now);
    const bool statusChanged = updateMarqueeState(playerStatusMarquee_, now);
    if (mainChanged || hudChanged || statusChanged) present();
}

bool DisplayManager::updateMarqueeState(MarqueeState& state, uint32_t now)
{
    if (!state.active || state.textWidth <= state.viewWidth ||
        now - state.lastFrameMs < MARQUEE_FRAME_MS) return false;

    const uint32_t scrollDistance = static_cast<uint32_t>(state.textWidth + MARQUEE_GAP_PX);
    const uint32_t scrollDuration = (scrollDistance * 1000U + MARQUEE_SPEED_PX_PER_SECOND - 1U) /
        MARQUEE_SPEED_PX_PER_SECOND;
    const uint32_t cycleDuration = MARQUEE_INITIAL_HOLD_MS + scrollDuration;
    const uint32_t phase = (now - state.startMs) % cycleDuration;
    const int32_t offset = phase < MARQUEE_INITIAL_HOLD_MS ? 0 :
        static_cast<int32_t>((static_cast<uint32_t>(phase - MARQUEE_INITIAL_HOLD_MS) *
            MARQUEE_SPEED_PX_PER_SECOND) / 1000U);
    state.lastFrameMs = now;
    if (offset == state.lastOffset) return false;
    state.lastOffset = offset;
    drawMarqueeFrame(state);
    return true;
}

void DisplayManager::updateAmbient()
{
    externalDisplay_.updateAmbient();
}

void DisplayManager::startAmbient()
{
    externalDisplay_.startAmbient();
}

void DisplayManager::waitForFrameBudget(uint32_t frameStart, uint32_t frameDurationMs)
{
    const uint32_t elapsed = millis() - frameStart;
    if (elapsed < frameDurationMs)
        delay(frameDurationMs - elapsed);
}

void DisplayManager::holdSystemAnimationFinalFrame()
{
    const uint32_t holdStart = millis();
    waitForFrameBudget(holdStart, SYSTEM_ANIMATION_FINAL_HOLD_MS);
}

void DisplayManager::showMenu(const char* title, const char* subtitle,
                               const char* const items[], size_t count, size_t selected,
                               bool marqueeTitle, size_t firstItem)
{
    PresentGuard mirror(*this);
    const bool sameMenu = menuCacheValid_ && count == cachedMenuCount_ &&
        strcmp(cachedMenuTitle_, title != nullptr ? title : "") == 0 &&
        strcmp(cachedMenuSubtitle_, subtitle != nullptr ? subtitle : "") == 0;
    const bool selectionOnly = sameMenu && selected != cachedMenuSelected_;
    const bool contentOnly = sameMenu && selected == cachedMenuSelected_;
    if (selectionOnly)
    {
        // Keep the header and border on the panel. Repainting only the list
        // avoids the visible black full-screen frame on navigation.
        M5Cardputer.Display.fillRect(6, 40, 228, 88, TFT_BLACK);
        drawMenu(items, count, selected, 46, 15, firstItem);
    }
    else if (contentOnly)
    {
        // Text-entry screens update their first row for every character. Only
        // repaint the list area; clearing the whole display causes flicker.
        M5Cardputer.Display.fillRect(6, 40, 228, 88, TFT_BLACK);
        drawMenu(items, count, selected, 46, 15, firstItem);
    }
    else
    {
    clear();
    M5Cardputer.Display.drawRect(3, 3, 234, 129, UI_DIM);
    M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
    M5Cardputer.Display.setTextSize(2);
    if (title != nullptr && strcmp(title, "NETRUN // RED") == 0)
    {
        M5Cardputer.Display.setCursor(8, 7); M5Cardputer.Display.print("NETRUN // ");
        M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
        M5Cardputer.Display.print("RED");
    }
    else if (marqueeTitle)
    {
        // Run-detail geometry: text-size 2 occupies the title row only.
        // Subtitle starts at y=27, so keep both clear and clip well above it.
        configureMarquee(marquee_, "RUN_DETAIL", title, 8, 7, 224, 16, 2, UI_MAGENTA);
    }
    else
    {
        char headerTitle[19] = {};
        copyHeaderTitle(title, headerTitle, sizeof(headerTitle));
        M5Cardputer.Display.setCursor(8, 7); M5Cardputer.Display.print(headerTitle);
    }
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5Cardputer.Display.setCursor(9, 27); M5Cardputer.Display.print(subtitle);
    M5Cardputer.Display.drawLine(7, 39, 233, 39, UI_DIM);
    drawMenu(items, count, selected, 46, 15, firstItem);
    }
    snprintf(cachedMenuTitle_, sizeof(cachedMenuTitle_), "%s", title != nullptr ? title : "");
    snprintf(cachedMenuSubtitle_, sizeof(cachedMenuSubtitle_), "%s", subtitle != nullptr ? subtitle : "");
    cachedMenuCount_ = count;
    cachedMenuSelected_ = selected;
    menuCacheValid_ = true;
}

void DisplayManager::showInfo(const char* title, const char* const lines[],
                              size_t count, const char* footer, FeedbackTone tone,
                              InfoLayout layout)
{
    PresentGuard mirror(*this);
    clear();
    const uint16_t accent = toneColor(tone);
    M5Cardputer.Display.drawRect(3, 3, 234, 129, accent == TFT_WHITE ? UI_DIM : accent);
    M5Cardputer.Display.setTextColor(accent, TFT_BLACK);
    M5Cardputer.Display.setTextSize(2);
    configureMarquee(marquee_, "INFO", title, 8, 7, 224, 20, 2, accent);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.drawLine(7, 29, 233, 29, UI_DIM);
    for (size_t i = 0; i < count && i < 7; ++i)
    {
        uint16_t color = TFT_WHITE;
        if (strstr(lines[i], "SUCCESS") || strstr(lines[i], "GRANTED") ||
            strstr(lines[i], "COMPLETE") || strstr(lines[i], "IDENTIFIED") || strstr(lines[i], "OWNED")) color = UI_GREEN;
        else if (strstr(lines[i], "FAIL") || strstr(lines[i], "HIT") ||
                 strstr(lines[i], "DENIED") || lines[i][0] == '-') color = UI_RED;
        M5Cardputer.Display.setTextColor(color, TFT_BLACK);
        M5Cardputer.Display.setCursor(10, 36 + static_cast<int>(i) * 12);
        M5Cardputer.Display.print(lines[i]);
    }
    M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
    M5Cardputer.Display.setCursor(9, 120);
    M5Cardputer.Display.print(footer);

    // Compact, distinct glyphs keep feedback screens related without making them identical.
    const char* originalTitle = title != nullptr ? title : "";
    const size_t outcomeLine = combatOutcomeLine(lines, count);
    const bool outcomeSuccess = outcomeLine < count &&
        (strstr(lines[outcomeLine], "HIT") || strstr(lines[outcomeLine], "SUCCESS"));
    if (strstr(originalTitle, "OBJECTIVE") || strstr(originalTitle, "RUN COMPLETE")) {
        M5Cardputer.Display.drawCircle(202, 76, 16, UI_GREEN);
        M5Cardputer.Display.drawLine(192, 76, 199, 83, UI_GREEN);
        M5Cardputer.Display.drawLine(199, 83, 213, 67, UI_GREEN);
        M5Cardputer.Display.drawFastHLine(184, 98, 36, UI_MAGENTA);
    } else if (layout == InfoLayout::Combat) {
        const uint16_t c = outcomeSuccess ? UI_RED : UI_DIM;
        M5Cardputer.Display.drawLine(184, 57, 220, 93, c);
        M5Cardputer.Display.drawLine(220, 57, 184, 93, c);
        M5Cardputer.Display.drawRect(190, 63, 24, 24, outcomeSuccess ? UI_RED : UI_MAGENTA);
    } else if (strstr(originalTitle, "BACKDOOR")) {
        const uint16_t c = count >= 4 && strstr(lines[3], "DENIED") ? UI_RED : accent;
        M5Cardputer.Display.drawRect(189, 68, 28, 24, c);
        M5Cardputer.Display.drawArc(203, 68, 10, 13, 180, 360, c);
        M5Cardputer.Display.fillCircle(203, 79, 2, c);
        M5Cardputer.Display.drawFastVLine(203, 81, 6, c);
    } else if (strstr(originalTitle, "EYE-DEE")) {
        for (int level = 0; level < 3; ++level)
            M5Cardputer.Display.drawEllipse(203, 65 + level * 11, 17, 5,
                count >= 4 && strstr(lines[3], "IDENTIFIED") ? UI_GREEN : UI_MAGENTA);
        M5Cardputer.Display.drawFastVLine(186, 65, 25, UI_MAGENTA);
        M5Cardputer.Display.drawFastVLine(220, 65, 25, UI_MAGENTA);
    } else if (!strcmp(originalTitle, "CONTROL")) {
        const uint16_t c = count >= 4 && strstr(lines[3], "ACQUIRED") ? UI_GREEN : UI_RED;
        M5Cardputer.Display.drawRect(190, 61, 27, 34, c);
        for (int y = 67; y < 91; y += 8) {
            M5Cardputer.Display.fillCircle(195, y, 1, c);
            M5Cardputer.Display.drawFastHLine(200, y, 12, UI_MAGENTA);
        }
        M5Cardputer.Display.drawFastVLine(203, 51, 10, c);
    } else if (!strcmp(originalTitle, "SLIDE")) {
        const uint16_t c = outcomeSuccess ? UI_GREEN : UI_MAGENTA;
        M5Cardputer.Display.drawLine(183, 76, 211, 76, c);
        M5Cardputer.Display.fillTriangle(211, 69, 223, 76, 211, 83, c);
        M5Cardputer.Display.drawFastHLine(188, 87, 27, UI_DIM);
    } else if (!strcmp(originalTitle, "RUNNER") && count > 2) {
        int hp = 0, maxHp = 0;
        if (sscanf(lines[2], "HP %d/%d", &hp, &maxHp) == 2) {
            M5Cardputer.Display.setTextColor(UI_GREEN, TFT_BLACK);
            M5Cardputer.Display.setCursor(157, 55); M5Cardputer.Display.print("VITALS");
            drawBar(157, 68, 66, hp, maxHp, UI_GREEN);
        }
    }
}

void DisplayManager::drawHud(const FloorView& view)
{
    M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
    configureMarquee(floorHudMarquee_, "FLOOR_HUD", view.architecture,
                     4, 3, 82, 10, 1, UI_MAGENTA);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.setCursor(91, 3);
    M5Cardputer.Display.printf("F%02u/%02u T%lu", view.floor, view.floorCount,
                              static_cast<unsigned long>(view.turn));
    M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
    M5Cardputer.Display.setCursor(177, 3); M5Cardputer.Display.print("ACT");
    drawNetActions(200, 5, view.actions, view.maxActions);
    if (view.architectureCompleted)
    {
        M5Cardputer.Display.setTextColor(UI_GREEN, TFT_BLACK);
        M5Cardputer.Display.setCursor(145, 3); M5Cardputer.Display.print("VIR");
    }
    M5Cardputer.Display.drawLine(3, 14, 236, 14, UI_MAGENTA);
    M5Cardputer.Display.drawPixel(2, 13, TFT_WHITE);
    M5Cardputer.Display.drawPixel(237, 13, TFT_WHITE);
}

void DisplayManager::drawNetActions(int x, int y, uint8_t current, uint8_t maximum)
{
    for (uint8_t i = 0; i < maximum && i < 5; ++i) {
        const uint16_t color = i < current ? UI_MAGENTA : UI_DIM;
        M5Cardputer.Display.drawCircle(x + i * 8, y, 3, color);
        if (i < current) M5Cardputer.Display.fillCircle(x + i * 8, y, 2, color);
    }
}

void DisplayManager::drawBar(int x, int y, int width, int value, int maximum, uint16_t color)
{
    M5Cardputer.Display.drawRect(x, y, width, 7, UI_DIM);
    if (maximum > 0 && value > 0)
        M5Cardputer.Display.fillRect(x + 1, y + 1, (width - 2) * value / maximum, 5, color);
}

void DisplayManager::drawPasswordDoor(int x, int y, uint8_t level, bool open)
{
    const uint16_t orange = 0xFD20;
    const uint16_t c = open ? UI_GREEN : (level == 0 ? orange : level == 1 ? UI_YELLOW : UI_RED);
    const uint16_t gateRear = open ? 0x0240 : (level == 0 ? 0x7800 : TFT_DARKGREY);
    // Both states retain the same frame; only an open gate exposes the portal.
    M5Cardputer.Display.fillRoundRect(x + 3, y + 5, 40, 37, 6, gateRear);
    M5Cardputer.Display.drawRoundRect(x + 1, y + 3, 40, 37, 6, c);
    if (open)
    {
        M5Cardputer.Display.fillRect(x + 5, y + 9, 11, 27, c);
        M5Cardputer.Display.fillRect(x + 27, y + 9, 11, 27, c);
        M5Cardputer.Display.fillRect(x + 17, y + 10, 10, 25, gateRear);
        M5Cardputer.Display.fillCircle(x + 22, y + 22, 4, UI_GREEN);
        M5Cardputer.Display.drawFastHLine(x + 7, y + 8, 28, TFT_WHITE);
    }
    else
    {
        M5Cardputer.Display.fillRect(x + 6, y + 8, 32, 29, c);
        M5Cardputer.Display.fillRect(x + 10, y + 12, 24, 21, gateRear);
        M5Cardputer.Display.fillCircle(x + 22, y + 21, 5, c);
        M5Cardputer.Display.fillCircle(x + 22, y + 20, 2, TFT_WHITE);
        M5Cardputer.Display.fillRect(x + 20, y + 22, 5, 7, TFT_WHITE);
    }
    return;
    const uint16_t rear = open ? 0x0240 : (level == 0 ? 0x7800 : TFT_DARKGREY);
    // Digital access barrier: rear slab, front gate and central keyhole core.
    M5Cardputer.Display.fillRoundRect(x + 5, y + 7, 38, 36, 6, rear);
    M5Cardputer.Display.fillRoundRect(x + 3, y + 4, 38, 36, 6, c);
    M5Cardputer.Display.fillRect(x + 10, y + 11, 24, 22, rear);
    M5Cardputer.Display.fillCircle(x + 22, y + 21, 6, c);
    M5Cardputer.Display.fillCircle(x + 22, y + 20, 3, TFT_WHITE);
    M5Cardputer.Display.fillRect(x + 20, y + 23, 5, 8, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 9, y + 9, 20, TFT_WHITE);
    return;
    M5Cardputer.Display.drawRect(x, y, MAIN_SPRITE_CANVAS, MAIN_SPRITE_CANVAS, c);
    M5Cardputer.Display.drawRect(x + 6, y + 4, 34, 42, c);
    M5Cardputer.Display.drawFastVLine(x + 31, y + 5, 40, c);
    M5Cardputer.Display.fillCircle(x + 26, y + 25, 2, open ? UI_GREEN : TFT_WHITE);
    if (level > 1) M5Cardputer.Display.drawCircle(x + 23, y + 24, 12, c);
    M5Cardputer.Display.drawFastHLine(x + 10, y + 16, 17, UI_DIM);
    M5Cardputer.Display.drawFastHLine(x + 10, y + 34, 17, UI_DIM);
    M5Cardputer.Display.fillRect(x + 15, y + 17, 18, 17, c);
    M5Cardputer.Display.drawRect(x + 19, y + 20, 10, 11, TFT_BLACK);
    return;
    M5Cardputer.Display.drawRect(x - 2, y - 2, 58, 58, UI_DIM);
    M5Cardputer.Display.drawRect(x, y, 54, 54, c);
    if (level == 0) {
        M5Cardputer.Display.drawRect(x + 5, y + 4, 41, 47, c);
        for (int px = x + 12; px < x + 46; px += 8) {
            M5Cardputer.Display.drawFastVLine(px, y + 5, 45, c);
            M5Cardputer.Display.drawPixel(px + 2, y + 12, TFT_LIGHTGREY);
        }
        M5Cardputer.Display.drawFastHLine(x + 6, y + 17, 39, UI_DIM);
        M5Cardputer.Display.fillCircle(x + 40, y + 29, 2, TFT_WHITE);
        M5Cardputer.Display.drawFastHLine(x + 42, y + 29, 5, UI_MAGENTA);
    } else if (level == 1) {
        M5Cardputer.Display.drawRect(x + 3, y + 3, 48, 48, TFT_LIGHTGREY);
        M5Cardputer.Display.drawRect(x + 7, y + 7, 40, 40, c);
        for (int py = y + 9; py < y + 47; py += 10) {
            M5Cardputer.Display.fillCircle(x + 6, py, 1, TFT_WHITE);
            M5Cardputer.Display.fillCircle(x + 48, py, 1, TFT_WHITE);
        }
        M5Cardputer.Display.drawFastHLine(x + 8, y + 18, 38, UI_DIM);
        M5Cardputer.Display.drawFastHLine(x + 8, y + 36, 38, UI_DIM);
        M5Cardputer.Display.drawRect(x + 34, y + 20, 10, 16, UI_MAGENTA);
        M5Cardputer.Display.fillRect(x + 37, y + 23, 4, 3, UI_GREEN);
        M5Cardputer.Display.drawLine(x + 34, y + 39, x + 26, y + 47, UI_MAGENTA);
    } else {
        M5Cardputer.Display.drawRect(x + 2, y + 2, 50, 50, TFT_LIGHTGREY);
        for (int p = 7; p < 49; p += 14) {
            M5Cardputer.Display.fillCircle(x + p, y + 6, 1, TFT_WHITE);
            M5Cardputer.Display.fillCircle(x + p, y + 48, 1, TFT_WHITE);
        }
        M5Cardputer.Display.drawCircle(x + 27, y + 27, 20, c);
        M5Cardputer.Display.drawCircle(x + 27, y + 27, 16, UI_DIM);
        M5Cardputer.Display.drawCircle(x + 27, y + 27, 7, TFT_WHITE);
        for (int arm = 0; arm < 4; ++arm) {
            if (arm & 1) M5Cardputer.Display.drawFastHLine(x + 8 + arm * 10, y + 27, 18, c);
            else M5Cardputer.Display.drawFastVLine(x + 27, y + 7 + arm * 10, 18, c);
        }
        M5Cardputer.Display.fillCircle(x + 27, y + 27, 3, UI_MAGENTA);
        M5Cardputer.Display.drawRect(x + 43, y + 21, 7, 13, UI_RED);
    }
}

void DisplayManager::drawDatabaseNode(int x, int y, bool downloaded)
{
    constexpr uint16_t kDataRear = 0x780F;
    // A full bank has four loaded plates; an empty bank exposes its ports.
    M5Cardputer.Display.fillRoundRect(x + 10, y + 29, 31, 10, 4, kDataRear);
    M5Cardputer.Display.fillRoundRect(x + 7, y + 22, 31, 10, 4, UI_MAGENTA);
    M5Cardputer.Display.fillRoundRect(x + 10, y + 15, 31, 10, 4, UI_MAGENTA);
    M5Cardputer.Display.fillRoundRect(x + 7, y + 8, 31, 10, 4, UI_MAGENTA);
    if (downloaded)
    {
        M5Cardputer.Display.fillRect(x + 13, y + 12, 20, 21, kDataRear);
        M5Cardputer.Display.drawRect(x + 17, y + 15, 12, 14, UI_GREEN);
        M5Cardputer.Display.fillCircle(x + 23, y + 22, 3, UI_GREEN);
        M5Cardputer.Display.drawFastHLine(x + 12, y + 36, 17, UI_GREEN);
    }
    else
    {
        M5Cardputer.Display.fillRect(x + 14, y + 11, 17, 5, kDataRear);
        M5Cardputer.Display.fillRect(x + 11, y + 18, 17, 5, kDataRear);
        M5Cardputer.Display.fillRect(x + 14, y + 25, 17, 5, kDataRear);
        M5Cardputer.Display.fillRect(x + 11, y + 32, 17, 4, kDataRear);
        M5Cardputer.Display.fillCircle(x + 34, y + 21, 3, TFT_WHITE);
    }
    return;
    constexpr uint16_t rear = 0x780F;
    // Three offset cartridge plates make the stack read as a data module.
    M5Cardputer.Display.fillRoundRect(x + 9, y + 27, 31, 12, 4, rear);
    M5Cardputer.Display.fillRoundRect(x + 6, y + 20, 31, 12, 4, UI_MAGENTA);
    M5Cardputer.Display.fillRoundRect(x + 9, y + 13, 31, 12, 4, UI_MAGENTA);
    M5Cardputer.Display.fillRect(x + 15, y + 17, 17, 5, rear);
    M5Cardputer.Display.fillRect(x + 12, y + 24, 17, 5, rear);
    M5Cardputer.Display.fillRect(x + 18, y + 30, 14, 5, rear);
    M5Cardputer.Display.fillCircle(x + 34, y + 20, 3, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 12, y + 14, 16, TFT_WHITE);
    return;
    const int cx = x + MAIN_SPRITE_CANVAS / 2;
    M5Cardputer.Display.drawEllipse(cx, y + 6, 20, 5, UI_MAGENTA);
    M5Cardputer.Display.drawEllipse(cx, y + 41, 20, 5, UI_MAGENTA);
    M5Cardputer.Display.drawFastVLine(x + 4, y + 6, 35, UI_MAGENTA);
    M5Cardputer.Display.drawFastVLine(x + 44, y + 6, 35, UI_MAGENTA);
    for (int offset = 15; offset < 41; offset += 10) {
        M5Cardputer.Display.drawArc(cx, y + offset, 20, 5, 0, 180, UI_MAGENTA);
        M5Cardputer.Display.drawFastHLine(x + 8, y + offset + 1, 33, UI_DIM);
    }
    M5Cardputer.Display.drawFastVLine(x + 38, y + 12, 24, TFT_WHITE);
    M5Cardputer.Display.fillRect(x + 12, y + 17, 24, 15, UI_MAGENTA);
    M5Cardputer.Display.drawFastHLine(x + 16, y + 23, 16, TFT_BLACK);
    return;
    // One continuous stack reads more clearly than three disconnected rings.
    M5Cardputer.Display.drawEllipse(x + 30, y + 5, 28, 7, UI_MAGENTA);
    M5Cardputer.Display.drawArc(x + 30, y + 4, 24, 5, 180, 350, TFT_LIGHTGREY);
    M5Cardputer.Display.drawFastVLine(x + 2, y + 5, 45, UI_MAGENTA);
    M5Cardputer.Display.drawFastVLine(x + 58, y + 5, 45, UI_MAGENTA);
    for (int level = 0; level < 4; ++level) {
        const int py = y + 14 + level * 12;
        M5Cardputer.Display.drawArc(x + 30, py, 28, 7, 0, 180, UI_MAGENTA);
        M5Cardputer.Display.drawFastHLine(x + 7, py + 2, 46, UI_DIM);
        M5Cardputer.Display.drawFastHLine(x + 9, py - 2, 18, TFT_LIGHTGREY);
        M5Cardputer.Display.drawFastHLine(x + 35, py + 4, 16, 0x780F);
    }
    M5Cardputer.Display.fillCircle(x + 50, y + 10, 1, TFT_WHITE);
    M5Cardputer.Display.fillCircle(x + 50, y + 34, 1, TFT_WHITE);
}

void DisplayManager::drawControlNode(int x, int y, bool owned)
{
    const uint16_t c = owned ? UI_GREEN : TFT_LIGHTGREY;
    const uint16_t controlRear = owned ? 0x0240 : TFT_DARKGREY;
    M5Cardputer.Display.fillRoundRect(x + 8, y + 10, 34, 31, 5, controlRear);
    M5Cardputer.Display.fillRoundRect(x + 5, y + 7, 34, 31, 5, c);
    M5Cardputer.Display.fillRect(x + 11, y + 14, 22, 17, controlRear);
    if (owned)
    {
        M5Cardputer.Display.drawCircle(x + 22, y + 22, 8, UI_GREEN);
        M5Cardputer.Display.fillCircle(x + 22, y + 22, 4, UI_GREEN);
        M5Cardputer.Display.drawLine(x + 12, y + 31, x + 22, y + 22, UI_GREEN);
        M5Cardputer.Display.drawLine(x + 32, y + 31, x + 22, y + 22, UI_GREEN);
        M5Cardputer.Display.drawFastHLine(x + 11, y + 11, 17, TFT_WHITE);
    }
    else
    {
        M5Cardputer.Display.fillCircle(x + 22, y + 22, 7, c);
        M5Cardputer.Display.fillCircle(x + 22, y + 22, 3, UI_MAGENTA);
        M5Cardputer.Display.fillRect(x + 2, y + 18, 5, 10, c);
        M5Cardputer.Display.fillRect(x + 38, y + 15, 5, 12, c);
        M5Cardputer.Display.drawFastHLine(x + 11, y + 11, 17, TFT_WHITE);
    }
    return;
    const uint16_t rear = owned ? 0x0240 : TFT_DARKGREY;
    // A raised terminal frame surrounds a distinct, centered control core.
    M5Cardputer.Display.fillRoundRect(x + 8, y + 10, 34, 31, 5, rear);
    M5Cardputer.Display.fillRoundRect(x + 5, y + 7, 34, 31, 5, c);
    M5Cardputer.Display.fillRect(x + 11, y + 14, 22, 17, rear);
    M5Cardputer.Display.fillCircle(x + 22, y + 22, 7, c);
    M5Cardputer.Display.fillCircle(x + 22, y + 22, 3, owned ? UI_GREEN : UI_MAGENTA);
    M5Cardputer.Display.fillRect(x + 2, y + 18, 5, 10, c);
    M5Cardputer.Display.fillRect(x + 38, y + 15, 5, 12, c);
    M5Cardputer.Display.drawFastHLine(x + 11, y + 11, 17, TFT_WHITE);
    return;
    M5Cardputer.Display.drawFastVLine(x + 12, y + 4, 11, c);
    M5Cardputer.Display.drawFastVLine(x + 35, y + 2, 13, c);
    M5Cardputer.Display.fillCircle(x + 12, y + 3, 2, c);
    M5Cardputer.Display.fillCircle(x + 35, y + 2, 2, c);
    M5Cardputer.Display.drawRect(x + 5, y + 17, 38, 26, c);
    for (int py = y + 23; py < y + 40; py += 7) {
        M5Cardputer.Display.fillCircle(x + 11, py, 2, c);
        M5Cardputer.Display.drawFastHLine(x + 17, py, 20, UI_DIM);
    }
    M5Cardputer.Display.drawRect(x + 29, y + 27, 8, 9, c);
    M5Cardputer.Display.fillCircle(x + 33, y + 31, 2, owned ? UI_GREEN : UI_RED);
    M5Cardputer.Display.fillRect(x + 13, y + 21, 21, 17, c);
    M5Cardputer.Display.drawRect(x + 18, y + 25, 11, 9, TFT_BLACK);
    return;
    M5Cardputer.Display.drawFastVLine(x + 27, y, 12, c);
    M5Cardputer.Display.drawFastVLine(x + 15, y + 5, 10, UI_MAGENTA);
    M5Cardputer.Display.drawFastVLine(x + 40, y + 3, 12, UI_RED);
    M5Cardputer.Display.fillCircle(x + 15, y + 4, 2, UI_MAGENTA);
    M5Cardputer.Display.fillCircle(x + 40, y + 2, 2, UI_RED);
    M5Cardputer.Display.drawLine(x + 5, y + 17, x + 27, y + 10, c);
    M5Cardputer.Display.drawLine(x + 49, y + 17, x + 27, y + 10, c);
    M5Cardputer.Display.drawRect(x + 3, y + 17, 50, 42, c);
    M5Cardputer.Display.drawRect(x + 8, y + 21, 40, 34, UI_MAGENTA);
    for (int py = y + 24; py < y + 53; py += 9) {
        M5Cardputer.Display.fillCircle(x + 13, py, 2, owned ? UI_GREEN : UI_RED);
        M5Cardputer.Display.drawFastHLine(x + 19, py, 23, UI_MAGENTA);
        M5Cardputer.Display.drawPixel(x + 45, py, TFT_WHITE);
    }
    M5Cardputer.Display.drawLine(x + 3, y + 38, x - 3, y + 43, UI_RED);
    M5Cardputer.Display.drawLine(x + 53, y + 34, x + 59, y + 29, UI_MAGENTA);
    M5Cardputer.Display.drawFastHLine(x - 1, y + 59, 60, UI_DIM);
}

void DisplayManager::drawDemonActor(int x, int y, uint8_t type, bool active)
{
    const uint16_t orb = active ? UI_RED : UI_DIM;
    const uint16_t core = active ? UI_MAGENTA : TFT_DARKGREY;
    if (type == 2) { M5Cardputer.Display.fillRect(x - 6, y + 7, 12, 19, orb); M5Cardputer.Display.fillCircle(x, y + 4, 6, core); M5Cardputer.Display.fillRect(x - 7, y - 1, 14, 4, orb); M5Cardputer.Display.drawRect(x - 3, y + 9, 6, 9, TFT_BLACK); M5Cardputer.Display.drawFastHLine(x - 4, y + 21, 8, TFT_WHITE); M5Cardputer.Display.drawLine(x + 8, y + 13, x + 13, y + 22, TFT_WHITE); return; }
    if (type == 3) { M5Cardputer.Display.fillRect(x - 12, y + 6, 24, 19, orb); M5Cardputer.Display.drawRect(x - 9, y, 18, 9, core); M5Cardputer.Display.drawRect(x - 5, y + 11, 10, 7, TFT_BLACK); M5Cardputer.Display.drawFastHLine(x - 8, y + 21, 16, TFT_WHITE); for (int offset = -10; offset <= 10; offset += 5) M5Cardputer.Display.drawLine(x + offset, y + 23, x + offset * 2, y + 30, UI_GREEN); return; }
    // Small hostile orb with two horns: an Architecture actor overlay, not a
    // floor object or a Black ICE sprite.
    M5Cardputer.Display.fillCircle(x, y + 12, 10, orb);
    M5Cardputer.Display.fillCircle(x - 3, y + 9, 4, core);
    M5Cardputer.Display.drawLine(x - 7, y + 5, x - 12, y, orb);
    M5Cardputer.Display.drawLine(x - 12, y, x - 8, y + 9, orb);
    M5Cardputer.Display.drawLine(x + 7, y + 5, x + 12, y, orb);
    M5Cardputer.Display.drawLine(x + 12, y, x + 8, y + 9, orb);
    M5Cardputer.Display.drawPixel(x - 4, y + 11, TFT_WHITE);
    M5Cardputer.Display.drawPixel(x + 4, y + 11, TFT_WHITE);
    M5Cardputer.Display.drawCircle(x, y + 12, 4, TFT_BLACK);
    M5Cardputer.Display.drawPixel(x, y + 12, TFT_WHITE);
}

void DisplayManager::drawHound01(int x, int y, FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    const uint16_t houndRear = spriteInnerColor(tone);
    const uint16_t houndFront = spriteHighlightColor(tone);
    M5Cardputer.Display.fillTriangle(x + 6, y + 16, x + 15, y + 3, x + 20, y + 20, houndRear);
    M5Cardputer.Display.fillTriangle(x + 42, y + 16, x + 33, y + 3, x + 28, y + 20, houndRear);
    M5Cardputer.Display.fillRoundRect(x + 7, y + 14, 34, 25, 7, c);
    M5Cardputer.Display.fillTriangle(x + 10, y + 25, x + 38, y + 25, x + 24, y + 43, c);
    M5Cardputer.Display.fillRoundRect(x + 13, y + 20, 22, 15, 4, houndRear);
    M5Cardputer.Display.fillTriangle(x + 17, y + 29, x + 31, y + 29, x + 24, y + 39, houndRear);
    M5Cardputer.Display.fillRect(x + 17, y + 22, 5, 3, houndFront);
    M5Cardputer.Display.fillRect(x + 27, y + 22, 5, 3, houndFront);
    M5Cardputer.Display.fillTriangle(x + 18, y + 35, x + 22, y + 35, x + 20, y + 42, TFT_WHITE);
    M5Cardputer.Display.fillTriangle(x + 27, y + 35, x + 31, y + 35, x + 29, y + 42, TFT_WHITE);
    return;
    const uint16_t inner = spriteInnerColor(tone);
    const uint16_t highlight = spriteHighlightColor(tone);
    // Shell: ears and an armored skull. Plate: inset muzzle. Focus: paired eyes.
    M5Cardputer.Display.fillTriangle(x + 4, y + 15, x + 15, y + 3, x + 19, y + 20, c);
    M5Cardputer.Display.fillTriangle(x + 44, y + 15, x + 33, y + 3, x + 29, y + 20, c);
    M5Cardputer.Display.fillRoundRect(x + 7, y + 14, 34, 25, 7, c);
    M5Cardputer.Display.fillTriangle(x + 10, y + 25, x + 38, y + 25, x + 24, y + 42, c);
    M5Cardputer.Display.fillRoundRect(x + 13, y + 20, 22, 15, 4, inner);
    M5Cardputer.Display.fillTriangle(x + 17, y + 28, x + 31, y + 28, x + 24, y + 38, inner);
    M5Cardputer.Display.fillRect(x + 17, y + 22, 5, 3, highlight);
    M5Cardputer.Display.fillRect(x + 27, y + 22, 5, 3, highlight);
    M5Cardputer.Display.fillTriangle(x + 18, y + 35, x + 22, y + 35, x + 20, y + 41, TFT_WHITE);
    M5Cardputer.Display.fillTriangle(x + 27, y + 35, x + 31, y + 35, x + 29, y + 41, TFT_WHITE);
    return;
    M5Cardputer.Display.fillTriangle(x + 3, y + 13, x + 15, y + 2, x + 17, y + 19, c);
    M5Cardputer.Display.fillTriangle(x + 45, y + 13, x + 33, y + 2, x + 31, y + 19, c);
    M5Cardputer.Display.drawRoundRect(x + 8, y + 14, 32, 21, 6, c);
    M5Cardputer.Display.drawLine(x + 10, y + 23, x + 24, y + 31, c);
    M5Cardputer.Display.drawLine(x + 38, y + 23, x + 24, y + 31, c);
    M5Cardputer.Display.drawFastHLine(x + 15, y + 32, 19, c);
    M5Cardputer.Display.fillTriangle(x + 18, y + 33, x + 23, y + 33, x + 20, y + 40, TFT_WHITE);
    M5Cardputer.Display.fillTriangle(x + 29, y + 33, x + 34, y + 33, x + 31, y + 40, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 13, y + 20, 8, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 27, y + 20, 8, TFT_WHITE);
    M5Cardputer.Display.drawLine(x + 9, y + 17, x + 4, y + 23, c);
    M5Cardputer.Display.fillTriangle(x + 12, y + 19, x + 36, y + 19, x + 24, y + 34, c);
    M5Cardputer.Display.drawFastHLine(x + 17, y + 24, 14, TFT_BLACK);
    return;
    const int8_t points[][2] = {
        {2,13},{15,0},{24,14},{48,11},{64,0},{76,15},{72,43},
        {60,55},{49,62},{28,62},{15,54},{5,42},{2,13}
    };
    for (size_t i = 1; i < 13; ++i)
        M5Cardputer.Display.drawLine(x + points[i-1][0], y + points[i-1][1], x + points[i][0], y + points[i][1], c);
    // Brow, eyes and split cybernetic face plates.
    M5Cardputer.Display.drawLine(x + 10, y + 22, x + 31, y + 18, c);
    M5Cardputer.Display.drawLine(x + 67, y + 21, x + 47, y + 18, c);
    M5Cardputer.Display.fillTriangle(x + 16, y + 25, x + 31, y + 23, x + 27, y + 30, UI_YELLOW);
    M5Cardputer.Display.fillTriangle(x + 62, y + 25, x + 47, y + 23, x + 51, y + 30, UI_YELLOW);
    M5Cardputer.Display.drawFastVLine(x + 39, y + 15, 17, UI_DIM);
    M5Cardputer.Display.drawLine(x + 30, y + 36, x + 39, y + 31, c);
    M5Cardputer.Display.drawLine(x + 39, y + 31, x + 49, y + 36, c);
    // Long muzzle with visible teeth.
    M5Cardputer.Display.drawLine(x + 22, y + 39, x + 39, y + 48, c);
    M5Cardputer.Display.drawLine(x + 56, y + 39, x + 39, y + 48, c);
    M5Cardputer.Display.drawFastHLine(x + 24, y + 48, 31, c);
    for (int tx = 27; tx <= 51; tx += 8)
        M5Cardputer.Display.fillTriangle(tx, y + 49, tx + 5, y + 49, tx + 2, y + 55, TFT_WHITE);
    // Asymmetric circuits and glitch fragments.
    M5Cardputer.Display.drawLine(x + 7, y + 31, x + 14, y + 34, UI_MAGENTA);
    M5Cardputer.Display.drawFastHLine(x + 1, y + 36, 10, c);
    M5Cardputer.Display.drawFastHLine(x + 67, y + 33, 15, c);
    M5Cardputer.Display.drawPixel(x + 84, y + 29, c);
    M5Cardputer.Display.drawFastVLine(x + 70, y + 39, 9, UI_MAGENTA);
    M5Cardputer.Display.drawCircle(x + 64, y + 18, 6, UI_DIM);
    M5Cardputer.Display.drawLine(x + 64, y + 12, x + 72, y + 6, UI_MAGENTA);
    M5Cardputer.Display.drawLine(x + 9, y + 42, x - 1, y + 49, UI_RED);
    M5Cardputer.Display.drawPixel(x + 79, y + 50, TFT_WHITE);
}

void DisplayManager::drawBird01(int x, int y, FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    const uint16_t ravenRear = spriteInnerColor(tone);
    const uint16_t ravenFront = spriteHighlightColor(tone);
    M5Cardputer.Display.fillTriangle(x + 4, y + 16, x + 23, y + 19, x + 16, y + 36, ravenRear);
    M5Cardputer.Display.fillTriangle(x + 44, y + 16, x + 25, y + 19, x + 32, y + 36, ravenRear);
    M5Cardputer.Display.fillTriangle(x, y + 10, x + 22, y + 17, x + 17, y + 31, c);
    M5Cardputer.Display.fillTriangle(x + 48, y + 10, x + 26, y + 17, x + 31, y + 31, c);
    M5Cardputer.Display.fillCircle(x + 23, y + 20, 9, c);
    M5Cardputer.Display.fillTriangle(x + 28, y + 18, x + 41, y + 22, x + 29, y + 27, c);
    M5Cardputer.Display.fillCircle(x + 23, y + 20, 5, ravenRear);
    M5Cardputer.Display.fillCircle(x + 26, y + 19, 2, ravenFront);
    return;
    const uint16_t ravenShadow = spriteInnerColor(tone);
    const uint16_t ravenLight = spriteHighlightColor(tone);
    // Rear wing plates sit down/right; the front head and beak sit forward.
    M5Cardputer.Display.fillTriangle(x + 5, y + 16, x + 23, y + 19, x + 17, y + 35, ravenShadow);
    M5Cardputer.Display.fillTriangle(x + 43, y + 16, x + 25, y + 19, x + 31, y + 35, ravenShadow);
    M5Cardputer.Display.fillTriangle(x + 1, y + 11, x + 22, y + 17, x + 18, y + 31, c);
    M5Cardputer.Display.fillTriangle(x + 47, y + 11, x + 26, y + 17, x + 30, y + 31, c);
    M5Cardputer.Display.fillCircle(x + 24, y + 21, 9, c);
    M5Cardputer.Display.fillTriangle(x + 28, y + 20, x + 39, y + 23, x + 29, y + 27, c);
    M5Cardputer.Display.fillCircle(x + 24, y + 21, 5, ravenShadow);
    M5Cardputer.Display.fillCircle(x + 26, y + 20, 2, ravenLight);
    return;
    const uint16_t inner = spriteInnerColor(tone);
    const uint16_t highlight = spriteHighlightColor(tone);
    // Closed wing shell around a separate chest/head plate.
    M5Cardputer.Display.fillTriangle(x + 1, y + 12, x + 24, y + 18, x + 20, y + 34, c);
    M5Cardputer.Display.fillTriangle(x + 47, y + 12, x + 24, y + 18, x + 28, y + 34, c);
    M5Cardputer.Display.fillTriangle(x + 17, y + 21, x + 31, y + 21, x + 24, y + 44, c);
    M5Cardputer.Display.fillTriangle(x + 8, y + 14, x + 20, y + 19, x + 18, y + 28, inner);
    M5Cardputer.Display.fillTriangle(x + 40, y + 14, x + 28, y + 19, x + 30, y + 28, inner);
    M5Cardputer.Display.fillTriangle(x + 19, y + 21, x + 29, y + 21, x + 24, y + 33, inner);
    M5Cardputer.Display.fillCircle(x + 24, y + 23, 3, highlight);
    return;
    M5Cardputer.Display.fillTriangle(x + 1, y + 11, x + 23, y + 21, x + 20, y + 29, c);
    M5Cardputer.Display.fillTriangle(x + 47, y + 11, x + 25, y + 21, x + 28, y + 29, c);
    M5Cardputer.Display.fillTriangle(x + 20, y + 18, x + 28, y + 18, x + 24, y + 42, c);
    M5Cardputer.Display.fillTriangle(x + 21, y + 21, x + 31, y + 24, x + 39, y + 22, c);
    M5Cardputer.Display.drawLine(x + 7, y + 14, x + 18, y + 20, TFT_WHITE);
    M5Cardputer.Display.drawLine(x + 41, y + 14, x + 30, y + 20, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 21, y + 29, 7, UI_DIM);
    M5Cardputer.Display.fillTriangle(x + 16, y + 18, x + 32, y + 18, x + 24, y + 32, c);
    M5Cardputer.Display.drawFastHLine(x + 21, y + 23, 8, TFT_BLACK);
    return;
    // Compact angular bird glyph using the existing primitive style.
    M5Cardputer.Display.drawLine(x + 5, y + 23, x + 30, y + 8, c);
    M5Cardputer.Display.drawLine(x + 30, y + 8, x + 43, y + 20, c);
    M5Cardputer.Display.drawLine(x + 43, y + 20, x + 67, y + 13, c);
    M5Cardputer.Display.drawLine(x + 67, y + 13, x + 53, y + 36, c);
    M5Cardputer.Display.drawLine(x + 53, y + 36, x + 34, y + 45, c);
    M5Cardputer.Display.drawLine(x + 34, y + 45, x + 14, y + 36, c);
    M5Cardputer.Display.drawLine(x + 14, y + 36, x + 5, y + 23, c);
    M5Cardputer.Display.drawLine(x + 30, y + 8, x + 29, y + 43, UI_MAGENTA);
    M5Cardputer.Display.drawLine(x + 43, y + 20, x + 53, y + 36, UI_DIM);
    M5Cardputer.Display.fillTriangle(x + 31, y + 20, x + 39, y + 22,
                                    x + 32, y + 26, UI_YELLOW);
    M5Cardputer.Display.fillTriangle(x + 14, y + 36, x + 34, y + 45,
                                    x + 21, y + 57, c);
    M5Cardputer.Display.drawFastHLine(x, y + 61, 74, UI_DIM);
    M5Cardputer.Display.drawPixel(x + 72, y + 18, UI_MAGENTA);
    M5Cardputer.Display.drawFastHLine(x + 60, y + 42, 18, UI_RED);
}

void DisplayManager::drawSerpent01(int x, int y, FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    const uint16_t aspTail = spriteInnerColor(tone);
    const uint16_t aspFocus = spriteHighlightColor(tone);
    M5Cardputer.Display.fillCircle(x + 36, y + 42, 5, aspTail);
    M5Cardputer.Display.fillCircle(x + 28, y + 37, 7, aspTail);
    M5Cardputer.Display.fillCircle(x + 20, y + 30, 7, c);
    M5Cardputer.Display.fillCircle(x + 18, y + 21, 7, c);
    M5Cardputer.Display.fillCircle(x + 25, y + 14, 7, c);
    M5Cardputer.Display.fillTriangle(x + 25, y + 10, x + 36, y + 3, x + 38, y + 14, c);
    M5Cardputer.Display.fillCircle(x + 32, y + 9, 5, aspTail);
    M5Cardputer.Display.fillCircle(x + 34, y + 8, 2, aspFocus);
    M5Cardputer.Display.drawFastHLine(x + 24, y + 16, 5, aspFocus);
    return;
    const uint16_t aspRear = spriteInnerColor(tone);
    const uint16_t aspFront = spriteHighlightColor(tone);
    M5Cardputer.Display.fillCircle(x + 35, y + 42, 5, aspRear);
    M5Cardputer.Display.fillCircle(x + 28, y + 37, 7, aspRear);
    M5Cardputer.Display.fillCircle(x + 20, y + 30, 7, c);
    M5Cardputer.Display.fillCircle(x + 18, y + 21, 7, c);
    M5Cardputer.Display.fillCircle(x + 25, y + 14, 7, c);
    M5Cardputer.Display.fillTriangle(x + 25, y + 10, x + 36, y + 3, x + 38, y + 14, c);
    M5Cardputer.Display.fillCircle(x + 32, y + 9, 5, aspRear);
    M5Cardputer.Display.fillCircle(x + 34, y + 8, 2, aspFront);
    M5Cardputer.Display.drawFastHLine(x + 24, y + 16, 5, aspFront);
    return;
    const uint16_t aspShadow = spriteInnerColor(tone);
    const uint16_t aspLight = spriteHighlightColor(tone);
    // A continuous S: dark rear tail, red mid-body, then a raised head.
    M5Cardputer.Display.fillCircle(x + 34, y + 41, 6, aspShadow);
    M5Cardputer.Display.fillCircle(x + 27, y + 36, 7, aspShadow);
    M5Cardputer.Display.fillCircle(x + 19, y + 29, 7, c);
    M5Cardputer.Display.fillCircle(x + 18, y + 21, 7, c);
    M5Cardputer.Display.fillCircle(x + 25, y + 14, 7, c);
    M5Cardputer.Display.fillCircle(x + 32, y + 8, 9, c);
    M5Cardputer.Display.fillCircle(x + 32, y + 8, 5, aspShadow);
    M5Cardputer.Display.fillCircle(x + 34, y + 7, 2, aspLight);
    M5Cardputer.Display.drawFastHLine(x + 24, y + 15, 5, aspLight);
    return;
    const uint16_t inner = spriteInnerColor(tone);
    const uint16_t highlight = spriteHighlightColor(tone);
    // A head at upper right leads through a single, unmistakable S curve.
    const int8_t body[][3] = {{32,8,8},{26,14,7},{18,21,7},{19,29,7},{26,36,7},{34,41,6}};
    for (const auto& segment : body)
        M5Cardputer.Display.fillCircle(x + segment[0], y + segment[1], segment[2], c);
    M5Cardputer.Display.fillCircle(x + 32, y + 8, 5, inner);
    M5Cardputer.Display.fillCircle(x + 20, y + 28, 4, inner);
    M5Cardputer.Display.fillCircle(x + 32, y + 8, 2, highlight);
    M5Cardputer.Display.drawFastHLine(x + 25, y + 36, 7, highlight);
    return;
    // A compact S shell made from overlapping body masses, not a single line.
    M5Cardputer.Display.fillCircle(x + 31, y + 10, 9, c);
    M5Cardputer.Display.fillCircle(x + 22, y + 18, 8, c);
    M5Cardputer.Display.fillCircle(x + 28, y + 28, 9, c);
    M5Cardputer.Display.fillCircle(x + 23, y + 38, 8, c);
    M5Cardputer.Display.fillCircle(x + 14, y + 41, 6, c);
    M5Cardputer.Display.fillCircle(x + 31, y + 10, 5, inner);
    M5Cardputer.Display.fillCircle(x + 27, y + 27, 5, inner);
    M5Cardputer.Display.fillCircle(x + 31, y + 10, 2, highlight);
    M5Cardputer.Display.drawFastHLine(x + 18, y + 37, 10, highlight);
    return;
    const int8_t spine[][2] = {{32,3},{19,4},{15,12},{23,18},{31,24},{33,33},{27,42},{16,44},{10,39}};
    for (size_t i = 1; i < sizeof(spine) / sizeof(spine[0]); ++i)
        M5Cardputer.Display.drawLine(x + spine[i - 1][0], y + spine[i - 1][1], x + spine[i][0], y + spine[i][1], c);
    M5Cardputer.Display.drawCircle(x + 31, y + 10, 5, c);
    M5Cardputer.Display.drawFastHLine(x + 27, y + 9, 5, TFT_WHITE);
    M5Cardputer.Display.drawLine(x + 23, y + 18, x + 28, y + 20, UI_DIM);
    M5Cardputer.Display.drawLine(x + 31, y + 24, x + 24, y + 29, UI_DIM);
    M5Cardputer.Display.fillCircle(x + 30, y + 11, 7, c);
    M5Cardputer.Display.fillCircle(x + 30, y + 11, 3, TFT_BLACK);
    M5Cardputer.Display.fillTriangle(x + 22, y + 19, x + 34, y + 19, x + 31, y + 30, c);
    return;
    // Temporary angular cobra glyph in the existing primitive style.
    M5Cardputer.Display.drawCircle(x + 37, y + 12, 10, c);
    M5Cardputer.Display.drawLine(x + 27, y + 10, x + 12, y + 22, c);
    M5Cardputer.Display.drawLine(x + 47, y + 10, x + 62, y + 22, c);
    M5Cardputer.Display.drawLine(x + 12, y + 22, x + 25, y + 35, c);
    M5Cardputer.Display.drawLine(x + 62, y + 22, x + 49, y + 35, c);
    M5Cardputer.Display.fillTriangle(x + 30, y + 10, x + 35, y + 13,
                                    x + 32, y + 16, UI_YELLOW);
    M5Cardputer.Display.fillTriangle(x + 44, y + 10, x + 39, y + 13,
                                    x + 42, y + 16, UI_YELLOW);
    M5Cardputer.Display.drawLine(x + 37, y + 22, x + 37, y + 45, UI_MAGENTA);
    M5Cardputer.Display.drawArc(x + 29, y + 45, 8, 15, 270, 90, c);
    M5Cardputer.Display.drawArc(x + 45, y + 45, 8, 15, 90, 270, c);
    M5Cardputer.Display.drawLine(x + 37, y + 55, x + 31, y + 62, c);
    M5Cardputer.Display.drawLine(x + 37, y + 55, x + 43, y + 62, c);
    M5Cardputer.Display.drawFastHLine(x + 4, y + 64, 66, UI_DIM);
    M5Cardputer.Display.drawFastHLine(x + 57, y + 40, 18, UI_RED);
}

void DisplayManager::drawOctopus01(int x, int y, FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    const uint16_t krakenRear = spriteInnerColor(tone);
    const uint16_t krakenFront = spriteHighlightColor(tone);
    const int8_t rearArms[][4] = {{14,26,3,43},{20,29,12,46},{28,29,29,47},{34,26,45,43}};
    for (const auto& arm : rearArms) {
        M5Cardputer.Display.drawLine(x + arm[0], y + arm[1], x + arm[2], y + arm[3], krakenRear);
        M5Cardputer.Display.drawLine(x + arm[0] + 1, y + arm[1], x + arm[2] + 1, y + arm[3], krakenRear);
    }
    M5Cardputer.Display.fillCircle(x + 26, y + 20, 14, krakenRear);
    M5Cardputer.Display.fillCircle(x + 22, y + 17, 14, c);
    M5Cardputer.Display.fillRoundRect(x + 13, y + 10, 19, 16, 7, krakenRear);
    M5Cardputer.Display.fillCircle(x + 22, y + 17, 6, c);
    M5Cardputer.Display.fillCircle(x + 22, y + 17, 2, krakenFront);
    return;
    const uint16_t inner = spriteInnerColor(tone);
    const uint16_t highlight = spriteHighlightColor(tone);
    M5Cardputer.Display.fillCircle(x + 24, y + 18, 14, c);
    const int8_t shellTentacles[][4] = {{14,27,4,42},{19,30,14,46},{25,31,24,47},{31,27,43,42},{12,22,1,31},{36,22,47,31}};
    for (const auto& arm : shellTentacles) {
        M5Cardputer.Display.drawLine(x + arm[0], y + arm[1], x + arm[2], y + arm[3], c);
        M5Cardputer.Display.drawLine(x + arm[0] + 1, y + arm[1], x + arm[2] + 1, y + arm[3], c);
    }
    M5Cardputer.Display.fillCircle(x + 24, y + 18, 9, inner);
    M5Cardputer.Display.fillCircle(x + 24, y + 18, 5, c);
    M5Cardputer.Display.fillCircle(x + 24, y + 18, 2, highlight);
    return;
    M5Cardputer.Display.fillCircle(x + 24, y + 17, 10, c);
    const int8_t arms[][4] = {{17,24,3,43},{21,26,10,45},{27,26,27,45},{31,24,45,43},{15,20,1,31},{33,20,47,31}};
    for (const auto& arm : arms) M5Cardputer.Display.drawLine(x + arm[0], y + arm[1], x + arm[2], y + arm[3], c);
    M5Cardputer.Display.drawCircle(x + 24, y + 17, 5, TFT_BLACK);
    M5Cardputer.Display.drawCircle(x + 24, y + 17, 2, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 17, y + 25, 15, UI_DIM);
    M5Cardputer.Display.fillCircle(x + 24, y + 17, 8, c);
    M5Cardputer.Display.drawRect(x + 18, y + 12, 12, 10, TFT_BLACK);
    return;
    M5Cardputer.Display.drawCircle(x + 37, y + 22, 13, c);
    M5Cardputer.Display.drawCircle(x + 37, y + 22, 7, UI_MAGENTA);
    M5Cardputer.Display.fillCircle(x + 37, y + 22, 2, UI_YELLOW);
    const int8_t tentacles[][6] = {
        {30,31,14,43,7,61}, {34,34,29,49,20,63}, {40,34,45,49,39,65},
        {44,31,61,43,68,59}, {26,27,8,31,2,48}, {48,27,66,31,74,47}
    };
    for (const auto& t : tentacles)
    {
        M5Cardputer.Display.drawLine(x + t[0], y + t[1], x + t[2], y + t[3], c);
        M5Cardputer.Display.drawLine(x + t[2], y + t[3], x + t[4], y + t[5], c);
    }
    M5Cardputer.Display.drawFastHLine(x + 4, y + 67, 66, UI_DIM);
    M5Cardputer.Display.drawFastHLine(x + 58, y + 13, 16, UI_RED);
}

void DisplayManager::drawWraith01(int x, int y, FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    const uint16_t inner = spriteInnerColor(tone);
    const uint16_t highlight = spriteHighlightColor(tone);
    M5Cardputer.Display.fillCircle(x + 24, y + 25, 12, c);
    M5Cardputer.Display.fillCircle(x + 24, y + 25, 8, inner);
    M5Cardputer.Display.fillCircle(x + 24, y + 25, 4, c);
    M5Cardputer.Display.fillCircle(x + 24, y + 25, 2, highlight);
    M5Cardputer.Display.fillCircle(x + 10, y + 14, 2, c);
    M5Cardputer.Display.fillCircle(x + 38, y + 11, 2, c);
    M5Cardputer.Display.fillCircle(x + 41, y + 37, 2, c);
    return;
    M5Cardputer.Display.fillCircle(x + 24, y + 25, 7, c);
    M5Cardputer.Display.drawPixel(x + 10, y + 14, c);
    M5Cardputer.Display.drawFastHLine(x + 34, y + 11, 5, c);
    M5Cardputer.Display.drawFastVLine(x + 7, y + 33, 5, c);
    M5Cardputer.Display.drawPixel(x + 41, y + 37, c);
    M5Cardputer.Display.drawCircle(x + 24, y + 25, 3, TFT_BLACK);
    M5Cardputer.Display.drawPixel(x + 24, y + 25, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 16, y + 39, 4, UI_DIM);
    M5Cardputer.Display.drawCircle(x + 24, y + 25, 10, c);
    M5Cardputer.Display.drawCircle(x + 24, y + 25, 5, TFT_BLACK);
    return;
    M5Cardputer.Display.drawCircle(x + 37, y + 31, 16, c);
    M5Cardputer.Display.drawCircle(x + 37, y + 31, 11, UI_DIM);
    M5Cardputer.Display.fillCircle(x + 37, y + 31, 5, TFT_WHITE);
    M5Cardputer.Display.fillCircle(x + 37, y + 31, 2, UI_RED);
    M5Cardputer.Display.drawLine(x + 37, y + 7, x + 37, y + 14, UI_YELLOW);
    M5Cardputer.Display.drawLine(x + 17, y + 15, x + 23, y + 21, c);
    M5Cardputer.Display.drawLine(x + 51, y + 19, x + 60, y + 12, UI_YELLOW);
    M5Cardputer.Display.drawLine(x + 15, y + 43, x + 24, y + 39, UI_YELLOW);
    M5Cardputer.Display.drawLine(x + 52, y + 42, x + 62, y + 48, c);
    M5Cardputer.Display.drawFastHLine(x + 7, y + 57, 60, UI_DIM);
}

void DisplayManager::drawHunter01(int x, int y, FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    const uint16_t killerRear = spriteInnerColor(tone);
    const uint16_t killerFront = spriteHighlightColor(tone);
    M5Cardputer.Display.fillRoundRect(x + 11, y + 20, 28, 20, 5, killerRear);
    M5Cardputer.Display.fillRect(x + 35, y + 24, 10, 9, killerRear);
    M5Cardputer.Display.fillRoundRect(x + 15, y + 5, 18, 18, 4, c);
    M5Cardputer.Display.fillRoundRect(x + 12, y + 19, 24, 18, 4, c);
    M5Cardputer.Display.fillRect(x + 18, y + 9, 12, 7, killerRear);
    M5Cardputer.Display.fillRect(x + 16, y + 24, 16, 9, killerRear);
    M5Cardputer.Display.fillRect(x + 21, y + 26, 6, 4, killerFront);
    M5Cardputer.Display.drawLine(x + 38, y + 28, x + 45, y + 20, killerFront);
    return;
    const uint16_t killerShadow = spriteInnerColor(tone);
    const uint16_t killerLight = spriteHighlightColor(tone);
    // Rear shoulder slab and offset weapon sit behind a compact armor avatar.
    M5Cardputer.Display.fillRect(x + 10, y + 20, 27, 19, killerShadow);
    M5Cardputer.Display.fillRect(x + 35, y + 24, 10, 9, killerShadow);
    M5Cardputer.Display.fillRoundRect(x + 15, y + 5, 18, 18, 4, c);
    M5Cardputer.Display.fillRoundRect(x + 12, y + 19, 24, 18, 4, c);
    M5Cardputer.Display.fillRect(x + 18, y + 9, 12, 7, killerShadow);
    M5Cardputer.Display.fillRect(x + 16, y + 24, 16, 9, killerShadow);
    M5Cardputer.Display.fillRect(x + 21, y + 26, 6, 4, killerLight);
    M5Cardputer.Display.drawLine(x + 38, y + 28, x + 45, y + 20, killerLight);
    return;
    const uint16_t inner = spriteInnerColor(tone);
    const uint16_t highlight = spriteHighlightColor(tone);
    M5Cardputer.Display.fillRoundRect(x + 15, y + 5, 18, 17, 4, c);
    M5Cardputer.Display.fillRoundRect(x + 11, y + 20, 26, 19, 4, c);
    M5Cardputer.Display.fillRect(x + 7, y + 26, 8, 8, c);
    M5Cardputer.Display.fillRect(x + 34, y + 25, 10, 7, c);
    M5Cardputer.Display.fillRect(x + 18, y + 9, 12, 7, inner);
    M5Cardputer.Display.fillRect(x + 16, y + 24, 16, 10, inner);
    M5Cardputer.Display.fillRect(x + 21, y + 27, 6, 4, highlight);
    M5Cardputer.Display.drawLine(x + 39, y + 28, x + 45, y + 21, highlight);
    return;
    M5Cardputer.Display.fillRect(x + 18, y + 5, 12, 12, c);
    M5Cardputer.Display.fillRect(x + 20, y + 18, 8, 16, c);
    M5Cardputer.Display.drawLine(x + 20, y + 21, x + 8, y + 33, c);
    M5Cardputer.Display.drawLine(x + 28, y + 21, x + 39, y + 29, c);
    M5Cardputer.Display.drawLine(x + 21, y + 34, x + 14, y + 45, c);
    M5Cardputer.Display.drawLine(x + 27, y + 34, x + 33, y + 45, c);
    M5Cardputer.Display.drawFastHLine(x + 31, y + 26, 12, c);
    M5Cardputer.Display.drawRect(x + 20, y + 8, 8, 4, TFT_BLACK);
    M5Cardputer.Display.drawFastHLine(x + 20, y + 24, 8, UI_DIM);
    M5Cardputer.Display.drawLine(x + 39, y + 29, x + 44, y + 24, TFT_WHITE);
    M5Cardputer.Display.fillRect(x + 16, y + 17, 16, 18, c);
    M5Cardputer.Display.drawRect(x + 19, y + 20, 10, 9, TFT_BLACK);
    return;
    M5Cardputer.Display.drawRect(x + 26, y + 8, 25, 18, c);
    M5Cardputer.Display.drawLine(x + 22, y + 8, x + 55, y + 8, UI_MAGENTA);
    M5Cardputer.Display.fillCircle(x + 33, y + 17, 2, UI_RED);
    M5Cardputer.Display.fillCircle(x + 44, y + 17, 2, UI_RED);
    M5Cardputer.Display.drawLine(x + 38, y + 26, x + 38, y + 51, c);
    M5Cardputer.Display.drawLine(x + 38, y + 31, x + 22, y + 42, c);
    M5Cardputer.Display.drawLine(x + 38, y + 31, x + 53, y + 43, c);
    M5Cardputer.Display.drawLine(x + 38, y + 51, x + 27, y + 64, c);
    M5Cardputer.Display.drawLine(x + 38, y + 51, x + 50, y + 64, c);
    M5Cardputer.Display.drawLine(x + 51, y + 42, x + 70, y + 15, UI_MAGENTA);
    M5Cardputer.Display.drawLine(x + 54, y + 44, x + 73, y + 17, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 8, y + 66, 64, UI_DIM);
}

void DisplayManager::drawScorp01(int x, int y, FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    const uint16_t scorpionRear = spriteInnerColor(tone);
    const uint16_t scorpionFront = spriteHighlightColor(tone);
    M5Cardputer.Display.drawLine(x + 28, y + 30, x + 42, y + 19, scorpionRear);
    M5Cardputer.Display.drawLine(x + 42, y + 19, x + 45, y + 6, scorpionRear);
    M5Cardputer.Display.fillTriangle(x + 41, y + 7, x + 47, y + 6, x + 43, y + 16, scorpionFront);
    M5Cardputer.Display.fillCircle(x + 24, y + 31, 12, scorpionRear);
    M5Cardputer.Display.fillCircle(x + 19, y + 26, 15, c);
    M5Cardputer.Display.drawLine(x + 11, y + 24, x + 3, y + 17, c);
    M5Cardputer.Display.drawLine(x + 26, y + 24, x + 36, y + 17, c);
    M5Cardputer.Display.drawLine(x + 12, y + 34, x + 5, y + 43, c);
    M5Cardputer.Display.drawLine(x + 26, y + 34, x + 35, y + 43, c);
    M5Cardputer.Display.fillRoundRect(x + 11, y + 19, 17, 14, 5, scorpionRear);
    M5Cardputer.Display.fillRoundRect(x + 13, y + 18, 14, 10, 4, c);
    M5Cardputer.Display.fillCircle(x + 22, y + 23, 3, scorpionFront);
    return;
    const uint16_t scorpionShadow = spriteInnerColor(tone);
    const uint16_t scorpionLight = spriteHighlightColor(tone);
    // Rear body and stinger are offset down/right; the front carapace is not concentric.
    M5Cardputer.Display.drawLine(x + 27, y + 30, x + 40, y + 19, scorpionShadow);
    M5Cardputer.Display.drawLine(x + 40, y + 19, x + 43, y + 7, scorpionShadow);
    M5Cardputer.Display.fillTriangle(x + 39, y + 8, x + 46, y + 7, x + 42, y + 16, scorpionShadow);
    M5Cardputer.Display.fillCircle(x + 24, y + 31, 12, scorpionShadow);
    M5Cardputer.Display.fillCircle(x + 19, y + 26, 15, c);
    M5Cardputer.Display.drawLine(x + 11, y + 24, x + 3, y + 17, c);
    M5Cardputer.Display.drawLine(x + 26, y + 24, x + 36, y + 17, c);
    M5Cardputer.Display.drawLine(x + 12, y + 33, x + 5, y + 43, c);
    M5Cardputer.Display.drawLine(x + 26, y + 33, x + 35, y + 43, c);
    M5Cardputer.Display.fillRoundRect(x + 11, y + 19, 17, 14, 5, scorpionShadow);
    M5Cardputer.Display.fillRoundRect(x + 13, y + 18, 14, 11, 4, c);
    M5Cardputer.Display.fillCircle(x + 22, y + 23, 3, scorpionLight);
    return;
    const uint16_t inner = spriteInnerColor(tone);
    const uint16_t highlight = spriteHighlightColor(tone);
    // Shell, inset armor plate, and focus core; legs and stinger attach outside.
    M5Cardputer.Display.fillCircle(x + 21, y + 28, 15, c);
    M5Cardputer.Display.drawLine(x + 13, y + 24, x + 4, y + 17, c);
    M5Cardputer.Display.drawLine(x + 29, y + 24, x + 38, y + 17, c);
    M5Cardputer.Display.drawLine(x + 14, y + 34, x + 6, y + 43, c);
    M5Cardputer.Display.drawLine(x + 28, y + 34, x + 37, y + 43, c);
    M5Cardputer.Display.drawLine(x + 31, y + 27, x + 41, y + 18, c);
    M5Cardputer.Display.drawLine(x + 41, y + 18, x + 43, y + 7, c);
    M5Cardputer.Display.fillTriangle(x + 39, y + 8, x + 46, y + 7, x + 42, y + 15, c);
    M5Cardputer.Display.fillCircle(x + 21, y + 28, 11, inner);
    M5Cardputer.Display.fillCircle(x + 21, y + 28, 7, c);
    M5Cardputer.Display.fillCircle(x + 21, y + 28, 4, inner);
    M5Cardputer.Display.fillCircle(x + 21, y + 28, 2, highlight);
    M5Cardputer.Display.drawFastHLine(x + 14, y + 20, 8, highlight);
    return;
    M5Cardputer.Display.fillCircle(x + 21, y + 28, 8, c);
    M5Cardputer.Display.drawLine(x + 16, y + 22, x + 8, y + 14, c);
    M5Cardputer.Display.drawLine(x + 26, y + 22, x + 35, y + 14, c);
    M5Cardputer.Display.drawLine(x + 19, y + 35, x + 9, y + 43, c);
    M5Cardputer.Display.drawLine(x + 24, y + 35, x + 35, y + 43, c);
    M5Cardputer.Display.drawLine(x + 27, y + 29, x + 38, y + 20, c);
    M5Cardputer.Display.drawLine(x + 38, y + 20, x + 42, y + 8, c);
    M5Cardputer.Display.fillTriangle(x + 39, y + 8, x + 45, y + 7, x + 41, y + 14, c);
    M5Cardputer.Display.drawFastHLine(x + 16, y + 28, 10, UI_DIM);
    M5Cardputer.Display.drawFastVLine(x + 21, y + 24, 8, TFT_WHITE);
    M5Cardputer.Display.fillCircle(x + 21, y + 28, 10, c);
    M5Cardputer.Display.drawRect(x + 17, y + 25, 8, 7, TFT_BLACK);
    return;
    M5Cardputer.Display.drawCircle(x + 35, y + 29, 14, c);
    M5Cardputer.Display.fillCircle(x + 35, y + 29, 4, UI_RED);
    for (int side = -1; side <= 1; side += 2) {
        M5Cardputer.Display.drawLine(x + 28, y + 39, x + 14, y + 51, c);
        M5Cardputer.Display.drawLine(x + 14, y + 51, x + 8, y + 62, c);
        M5Cardputer.Display.drawLine(x + 42, y + 39, x + 56, y + 51, c);
        M5Cardputer.Display.drawLine(x + 56, y + 51, x + 63, y + 62, c);
        (void)side;
        break;
    }
    M5Cardputer.Display.drawLine(x + 35, y + 15, x + 48, y + 5, c);
    M5Cardputer.Display.drawLine(x + 48, y + 5, x + 59, y + 13, c);
    M5Cardputer.Display.drawLine(x + 59, y + 13, x + 55, y + 20, UI_RED);
    M5Cardputer.Display.drawFastHLine(x + 7, y + 67, 61, UI_DIM);
}

void DisplayManager::drawRat01(int x, int y, FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    const uint16_t skunkRear = spriteInnerColor(tone);
    const uint16_t skunkFront = spriteHighlightColor(tone);
    // Tail is a high, separate rear plate; body stays compact and low.
    M5Cardputer.Display.fillTriangle(x + 13, y + 31, x + 2, y + 3, x + 18, y + 10, skunkRear);
    M5Cardputer.Display.fillTriangle(x + 15, y + 29, x + 6, y + 7, x + 20, y + 13, c);
    M5Cardputer.Display.fillEllipse(x + 23, y + 30, 19, 10, c);
    M5Cardputer.Display.fillCircle(x + 38, y + 24, 8, c);
    M5Cardputer.Display.fillEllipse(x + 22, y + 30, 12, 6, skunkRear);
    M5Cardputer.Display.fillRect(x + 19, y + 25, 7, 10, skunkFront);
    M5Cardputer.Display.fillCircle(x + 39, y + 23, 2, skunkFront);
    return;
    const uint16_t inner = spriteInnerColor(tone);
    const uint16_t highlight = spriteHighlightColor(tone);
    M5Cardputer.Display.fillEllipse(x + 23, y + 30, 20, 11, c);
    M5Cardputer.Display.fillCircle(x + 38, y + 24, 8, c);
    M5Cardputer.Display.fillTriangle(x + 11, y + 30, x + 2, y + 5, x + 17, y + 11, c);
    M5Cardputer.Display.fillEllipse(x + 22, y + 30, 13, 6, inner);
    M5Cardputer.Display.fillRect(x + 19, y + 25, 8, 10, c);
    M5Cardputer.Display.fillCircle(x + 38, y + 24, 2, highlight);
    return;
    M5Cardputer.Display.fillEllipse(x + 24, y + 30, 17, 8, c);
    M5Cardputer.Display.fillCircle(x + 38, y + 25, 6, c);
    M5Cardputer.Display.drawLine(x + 10, y + 29, x + 3, y + 7, c);
    M5Cardputer.Display.drawLine(x + 3, y + 7, x + 15, y + 2, c);
    M5Cardputer.Display.drawFastVLine(x + 23, y + 23, 14, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 16, y + 27, 14, UI_DIM);
    M5Cardputer.Display.drawLine(x + 8, y + 18, x + 13, y + 25, c);
    M5Cardputer.Display.fillEllipse(x + 24, y + 30, 19, 10, c);
    M5Cardputer.Display.drawFastVLine(x + 23, y + 23, 14, TFT_BLACK);
    return;
    M5Cardputer.Display.drawEllipse(x + 34, y + 34, 26, 13, c);
    M5Cardputer.Display.fillCircle(x + 57, y + 26, 8, c);
    M5Cardputer.Display.fillCircle(x + 60, y + 24, 2, UI_YELLOW);
    M5Cardputer.Display.drawLine(x + 13, y + 31, x + 2, y + 9, c);
    M5Cardputer.Display.drawLine(x + 2, y + 9, x + 16, y + 4, UI_MAGENTA);
    M5Cardputer.Display.drawFastVLine(x + 32, y + 22, 24, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 9, y + 48, 53, UI_DIM);
}

void DisplayManager::drawWinged01(int x, int y, FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    const uint16_t dragonRear = spriteInnerColor(tone);
    const uint16_t dragonFront = spriteHighlightColor(tone);
    // A mask-like head, rear horn plates, then a projecting lower jaw.
    M5Cardputer.Display.fillTriangle(x + 4, y + 22, x + 21, y + 4, x + 19, y + 34, dragonRear);
    M5Cardputer.Display.fillTriangle(x + 44, y + 22, x + 27, y + 4, x + 29, y + 34, dragonRear);
    M5Cardputer.Display.fillTriangle(x + 11, y + 18, x + 24, y + 2, x + 37, y + 18, c);
    M5Cardputer.Display.fillRoundRect(x + 12, y + 16, 24, 18, 6, c);
    M5Cardputer.Display.fillTriangle(x + 15, y + 30, x + 33, y + 30, x + 24, y + 42, c);
    M5Cardputer.Display.fillTriangle(x + 17, y + 21, x + 24, y + 12, x + 31, y + 21, dragonRear);
    M5Cardputer.Display.fillRect(x + 18, y + 25, 12, 5, dragonRear);
    M5Cardputer.Display.fillCircle(x + 24, y + 19, 2, dragonFront);
    return;
    const uint16_t dragonShadow = spriteInnerColor(tone);
    const uint16_t dragonLight = spriteHighlightColor(tone);
    // Horn/wing plates sit behind a broad dragon mask and protruding jaw.
    M5Cardputer.Display.fillTriangle(x + 5, y + 21, x + 22, y + 5, x + 20, y + 34, dragonShadow);
    M5Cardputer.Display.fillTriangle(x + 43, y + 21, x + 26, y + 5, x + 28, y + 34, dragonShadow);
    M5Cardputer.Display.fillTriangle(x + 10, y + 18, x + 24, y + 4, x + 38, y + 18, c);
    M5Cardputer.Display.fillRoundRect(x + 13, y + 16, 22, 19, 6, c);
    M5Cardputer.Display.fillTriangle(x + 16, y + 31, x + 32, y + 31, x + 24, y + 41, c);
    M5Cardputer.Display.fillTriangle(x + 17, y + 21, x + 24, y + 13, x + 31, y + 21, dragonShadow);
    M5Cardputer.Display.fillRect(x + 18, y + 25, 12, 5, dragonShadow);
    M5Cardputer.Display.fillCircle(x + 24, y + 20, 2, dragonLight);
    return;
    const uint16_t inner = spriteInnerColor(tone);
    const uint16_t highlight = spriteHighlightColor(tone);
    M5Cardputer.Display.fillTriangle(x + 2, y + 19, x + 24, y + 4, x + 22, y + 37, c);
    M5Cardputer.Display.fillTriangle(x + 46, y + 19, x + 24, y + 4, x + 26, y + 37, c);
    M5Cardputer.Display.fillCircle(x + 24, y + 23, 12, c);
    M5Cardputer.Display.fillTriangle(x + 15, y + 29, x + 24, y + 14, x + 33, y + 29, inner);
    M5Cardputer.Display.fillCircle(x + 24, y + 23, 3, highlight);
    M5Cardputer.Display.drawFastHLine(x + 18, y + 31, 13, inner);
    return;
    M5Cardputer.Display.fillTriangle(x + 3, y + 18, x + 24, y + 4, x + 20, y + 37, c);
    M5Cardputer.Display.fillTriangle(x + 45, y + 18, x + 24, y + 4, x + 28, y + 37, c);
    M5Cardputer.Display.drawTriangle(x + 16, y + 27, x + 24, y + 16, x + 32, y + 27, c);
    M5Cardputer.Display.drawFastHLine(x + 19, y + 30, 11, c);
    M5Cardputer.Display.drawLine(x + 15, y + 20, x + 9, y + 30, UI_DIM);
    M5Cardputer.Display.drawLine(x + 33, y + 20, x + 39, y + 30, UI_DIM);
    M5Cardputer.Display.drawFastHLine(x + 21, y + 22, 6, TFT_WHITE);
    M5Cardputer.Display.fillTriangle(x + 10, y + 20, x + 24, y + 7, x + 38, y + 20, c);
    M5Cardputer.Display.drawFastHLine(x + 19, y + 21, 11, TFT_BLACK);
    return;
    M5Cardputer.Display.drawTriangle(x + 10, y + 58, x + 37, y + 4, x + 66, y + 58, c);
    M5Cardputer.Display.drawLine(x + 20, y + 42, x + 4, y + 26, UI_MAGENTA);
    M5Cardputer.Display.drawLine(x + 54, y + 42, x + 70, y + 26, UI_MAGENTA);
    M5Cardputer.Display.fillCircle(x + 30, y + 31, 2, UI_YELLOW);
    M5Cardputer.Display.fillCircle(x + 44, y + 31, 2, UI_YELLOW);
    M5Cardputer.Display.drawFastHLine(x + 20, y + 54, 35, TFT_WHITE);
}

void DisplayManager::drawFeline01(int x, int y, FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    const uint16_t saberRear = spriteInnerColor(tone);
    const uint16_t saberFront = spriteHighlightColor(tone);
    M5Cardputer.Display.fillEllipse(x + 26, y + 26, 21, 16, saberRear);
    M5Cardputer.Display.fillTriangle(x + 7, y + 14, x + 18, y + 10, x + 17, y + 25, saberRear);
    M5Cardputer.Display.fillTriangle(x + 41, y + 14, x + 30, y + 10, x + 31, y + 25, saberRear);
    M5Cardputer.Display.fillEllipse(x + 23, y + 23, 20, 15, c);
    M5Cardputer.Display.fillEllipse(x + 23, y + 24, 14, 10, saberRear);
    M5Cardputer.Display.fillRect(x + 14, y + 20, 6, 3, saberFront);
    M5Cardputer.Display.fillRect(x + 26, y + 20, 6, 3, saberFront);
    M5Cardputer.Display.fillTriangle(x + 15, y + 31, x + 21, y + 31, x + 18, y + 44, TFT_WHITE);
    M5Cardputer.Display.fillTriangle(x + 26, y + 31, x + 32, y + 31, x + 29, y + 44, TFT_WHITE);
    return;
    const uint16_t inner = spriteInnerColor(tone);
    const uint16_t highlight = spriteHighlightColor(tone);
    M5Cardputer.Display.fillEllipse(x + 24, y + 24, 21, 16, c);
    M5Cardputer.Display.fillTriangle(x + 7, y + 14, x + 18, y + 10, x + 17, y + 25, c);
    M5Cardputer.Display.fillTriangle(x + 41, y + 14, x + 30, y + 10, x + 31, y + 25, c);
    M5Cardputer.Display.fillEllipse(x + 24, y + 24, 14, 10, inner);
    M5Cardputer.Display.fillRect(x + 15, y + 20, 6, 3, highlight);
    M5Cardputer.Display.fillRect(x + 27, y + 20, 6, 3, highlight);
    M5Cardputer.Display.fillTriangle(x + 16, y + 31, x + 22, y + 31, x + 19, y + 44, TFT_WHITE);
    M5Cardputer.Display.fillTriangle(x + 27, y + 31, x + 33, y + 31, x + 30, y + 44, TFT_WHITE);
    return;
    M5Cardputer.Display.fillEllipse(x + 24, y + 23, 18, 14, c);
    M5Cardputer.Display.fillTriangle(x + 7, y + 13, x + 18, y + 11, x + 15, y + 23, c);
    M5Cardputer.Display.fillTriangle(x + 41, y + 13, x + 30, y + 11, x + 33, y + 23, c);
    M5Cardputer.Display.fillTriangle(x + 16, y + 30, x + 22, y + 30, x + 19, y + 44, TFT_WHITE);
    M5Cardputer.Display.fillTriangle(x + 27, y + 30, x + 33, y + 30, x + 30, y + 44, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 14, y + 21, 8, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 27, y + 21, 8, TFT_WHITE);
    M5Cardputer.Display.drawLine(x + 8, y + 28, x + 13, y + 34, UI_DIM);
    M5Cardputer.Display.fillEllipse(x + 24, y + 23, 20, 15, c);
    M5Cardputer.Display.drawFastHLine(x + 16, y + 26, 16, TFT_BLACK);
    return;
    M5Cardputer.Display.drawCircle(x + 37, y + 30, 20, c);
    M5Cardputer.Display.fillCircle(x + 29, y + 25, 2, UI_RED);
    M5Cardputer.Display.fillCircle(x + 45, y + 25, 2, UI_RED);
    M5Cardputer.Display.drawFastHLine(x + 20, y + 39, 34, c);
    M5Cardputer.Display.fillTriangle(x + 25, y + 40, x + 31, y + 40, x + 28, y + 56, TFT_WHITE);
    M5Cardputer.Display.fillTriangle(x + 43, y + 40, x + 49, y + 40, x + 46, y + 56, TFT_WHITE);
    M5Cardputer.Display.drawFastHLine(x + 8, y + 65, 59, UI_DIM);
}

void DisplayManager::drawSkull01(int x, int y, FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    const uint16_t inner = spriteInnerColor(tone);
    const uint16_t highlight = spriteHighlightColor(tone);
    M5Cardputer.Display.fillCircle(x + 24, y + 23, 17, c);
    M5Cardputer.Display.fillRect(x + 16, y + 25, 16, 13, c);
    M5Cardputer.Display.fillCircle(x + 24, y + 23, 12, inner);
    M5Cardputer.Display.fillRect(x + 18, y + 18, 4, 6, TFT_BLACK);
    M5Cardputer.Display.fillRect(x + 27, y + 18, 4, 6, TFT_BLACK);
    M5Cardputer.Display.fillCircle(x + 24, y + 29, 3, highlight);
    M5Cardputer.Display.drawFastHLine(x + 18, y + 34, 12, inner);
    return;
    M5Cardputer.Display.fillRect(x + 14, y + 10, 20, 22, c);
    M5Cardputer.Display.fillRect(x + 10, y + 17, 28, 10, c);
    M5Cardputer.Display.fillRect(x + 18, y + 32, 12, 7, c);
    M5Cardputer.Display.fillRect(x + 18, y + 17, 4, 5, TFT_BLACK);
    M5Cardputer.Display.fillRect(x + 27, y + 17, 4, 5, TFT_BLACK);
    M5Cardputer.Display.drawPixel(x + 8, y + 12, c);
    M5Cardputer.Display.drawFastHLine(x + 35, y + 35, 8, c);
    M5Cardputer.Display.drawFastHLine(x + 19, y + 27, 11, TFT_BLACK);
    M5Cardputer.Display.drawLine(x + 14, y + 10, x + 19, y + 5, UI_DIM);
    M5Cardputer.Display.fillCircle(x + 24, y + 23, 15, c);
    M5Cardputer.Display.drawRect(x + 17, y + 16, 14, 12, TFT_BLACK);
    return;
    M5Cardputer.Display.drawCircle(x + 37, y + 29, 19, c);
    M5Cardputer.Display.drawCircle(x + 37, y + 29, 14, UI_DIM);
    M5Cardputer.Display.fillCircle(x + 30, y + 26, 3, UI_RED);
    M5Cardputer.Display.fillCircle(x + 44, y + 26, 3, UI_RED);
    M5Cardputer.Display.drawFastHLine(x + 27, y + 39, 20, c);
    M5Cardputer.Display.drawFastVLine(x + 37, y + 8, 10, UI_YELLOW);
    M5Cardputer.Display.drawFastHLine(x + 10, y + 65, 54, UI_DIM);
}

void DisplayManager::drawBigGuy01(int x, int y, FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    const uint16_t giantRear = spriteInnerColor(tone);
    const uint16_t giantFront = spriteHighlightColor(tone);
    M5Cardputer.Display.fillRect(x + 7, y + 7, 34, 31, giantRear);
    M5Cardputer.Display.fillRect(x + 4, y + 20, 40, 17, giantRear);
    M5Cardputer.Display.fillRect(x + 9, y + 4, 30, 18, c);
    M5Cardputer.Display.fillRect(x + 4, y + 20, 40, 16, c);
    M5Cardputer.Display.fillRect(x + 9, y + 35, 13, 11, c);
    M5Cardputer.Display.fillRect(x + 27, y + 35, 13, 11, c);
    M5Cardputer.Display.fillRect(x + 15, y + 9, 18, 9, giantRear);
    M5Cardputer.Display.fillRect(x + 12, y + 24, 24, 9, giantRear);
    M5Cardputer.Display.fillRect(x + 20, y + 26, 8, 4, giantFront);
    return;
    const uint16_t inner = spriteInnerColor(tone);
    const uint16_t highlight = spriteHighlightColor(tone);
    M5Cardputer.Display.fillRect(x + 9, y + 4, 30, 18, c);
    M5Cardputer.Display.fillRect(x + 4, y + 20, 40, 17, c);
    M5Cardputer.Display.fillRect(x + 8, y + 35, 14, 11, c);
    M5Cardputer.Display.fillRect(x + 26, y + 35, 14, 11, c);
    M5Cardputer.Display.fillRect(x + 15, y + 9, 18, 9, inner);
    M5Cardputer.Display.fillRect(x + 12, y + 24, 24, 9, inner);
    M5Cardputer.Display.fillRect(x + 20, y + 26, 8, 4, highlight);
    return;
    M5Cardputer.Display.fillRect(x + 9, y + 4, 30, 18, c);
    M5Cardputer.Display.fillRect(x + 4, y + 20, 40, 14, c);
    M5Cardputer.Display.fillRect(x + 9, y + 33, 13, 13, c);
    M5Cardputer.Display.fillRect(x + 27, y + 33, 13, 13, c);
    M5Cardputer.Display.drawRect(x + 16, y + 9, 16, 7, TFT_BLACK);
    M5Cardputer.Display.drawFastHLine(x + 10, y + 25, 28, UI_DIM);
    M5Cardputer.Display.drawFastVLine(x + 4, y + 22, 12, c);
    M5Cardputer.Display.fillRect(x + 6, y + 18, 36, 17, c);
    M5Cardputer.Display.drawRect(x + 15, y + 10, 18, 10, TFT_BLACK);
    return;
    M5Cardputer.Display.drawRect(x + 12, y + 8, 50, 38, c);
    M5Cardputer.Display.fillRect(x + 18, y + 46, 16, 18, c);
    M5Cardputer.Display.fillRect(x + 40, y + 46, 16, 18, c);
    M5Cardputer.Display.drawFastHLine(x + 6, y + 66, 62, UI_DIM);
    M5Cardputer.Display.setTextColor(c, TFT_BLACK);
    M5Cardputer.Display.setCursor(x + 23, y + 22); M5Cardputer.Display.print("!!");
}

void DisplayManager::drawEnemyNetrunner(int, int)
{
    M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
    M5Cardputer.Display.fillRoundRect(49, 37, 32, 29, 7, TFT_DARKGREY);
    M5Cardputer.Display.fillRect(45, 62, 40, 11, TFT_DARKGREY);
    M5Cardputer.Display.fillRoundRect(46, 34, 32, 29, 7, UI_RED);
    M5Cardputer.Display.fillRect(42, 60, 40, 11, UI_RED);
    M5Cardputer.Display.drawRoundRect(51, 41, 22, 11, 3, TFT_BLACK);
    M5Cardputer.Display.drawFastHLine(54, 45, 16, TFT_WHITE);
    M5Cardputer.Display.drawFastVLine(74, 43, 8, UI_DIM);
    M5Cardputer.Display.fillCircle(49, 54, 3, UI_MAGENTA);
    M5Cardputer.Display.fillCircle(78, 53, 4, TFT_DARKGREY);
    M5Cardputer.Display.drawPixel(78, 52, UI_MAGENTA);
    M5Cardputer.Display.drawLine(46, 50, 40, 55, UI_RED);
    M5Cardputer.Display.drawFastHLine(78, 57, 7, UI_RED);
    M5Cardputer.Display.drawPixel(87, 57, UI_RED);
}

void DisplayManager::drawHostileSprite(FloorVisual visual, int x, int y,
                                        FeedbackTone tone, uint8_t demonType,
                                        IceVisualId iceVisual)
{
    if (visual == FloorVisual::EnemyNetrunner) { drawEnemyNetrunner(x, y); return; }
    if (isValidIceVisualId(iceVisual)) { drawIceVisual(iceVisual, x, y, tone); return; }
    if (visual == FloorVisual::Ice01) drawHound01(x, y, tone);
    else if (visual == FloorVisual::Ice02) drawBird01(x, y, tone);
    else if (visual == FloorVisual::Ice03) drawSerpent01(x, y, tone);
    else if (visual == FloorVisual::Ice04) drawOctopus01(x, y, tone);
    else if (visual == FloorVisual::Ice05) drawWraith01(x, y, tone);
    else if (visual == FloorVisual::Ice06) drawHunter01(x, y, tone);
    else if (visual == FloorVisual::Ice07) drawScorp01(x, y, tone);
    else if (visual == FloorVisual::Ice08) drawRat01(x, y, tone);
    else if (visual == FloorVisual::Ice09) drawWinged01(x, y, tone);
    else if (visual == FloorVisual::Ice10) drawFeline01(x, y, tone);
    else if (visual == FloorVisual::Ice11) drawSkull01(x, y, tone);
    else if (visual == FloorVisual::Ice12) drawBigGuy01(x, y, tone);
    else drawDemonActor(x + 93, y - 4, demonType, true);
}

bool DisplayManager::hasIceVisualRenderer(IceVisualId visualId)
{
    switch (visualId)
    {
        case IceVisualId::Hound01:
        case IceVisualId::Bird01:
        case IceVisualId::Serpent01:
        case IceVisualId::Octopus01:
        case IceVisualId::Wraith01:
        case IceVisualId::Hunter01:
        case IceVisualId::Scorp01:
        case IceVisualId::Rat01:
        case IceVisualId::Winged01:
        case IceVisualId::Feline01:
        case IceVisualId::Skull01:
        case IceVisualId::BigGuy01:
            return true;
        case IceVisualId::Count:
            return false;
    }
    return false;
}

bool DisplayManager::hasHostileAttackStyle(HostileAttackStyle style)
{
    return isValidHostileAttackStyle(style);
}

void DisplayManager::drawIceVisual(IceVisualId visualId, int x, int y, FeedbackTone tone)
{
    switch (visualId)
    {
        case IceVisualId::Hound01: drawHound01(x, y, tone); break;
        case IceVisualId::Bird01: drawBird01(x, y, tone); break;
        case IceVisualId::Serpent01: drawSerpent01(x, y, tone); break;
        case IceVisualId::Octopus01: drawOctopus01(x, y, tone); break;
        case IceVisualId::Wraith01: drawWraith01(x, y, tone); break;
        case IceVisualId::Hunter01: drawHunter01(x, y, tone); break;
        case IceVisualId::Scorp01: drawScorp01(x, y, tone); break;
        case IceVisualId::Rat01: drawRat01(x, y, tone); break;
        case IceVisualId::Winged01: drawWinged01(x, y, tone); break;
        case IceVisualId::Feline01: drawFeline01(x, y, tone); break;
        case IceVisualId::Skull01: drawSkull01(x, y, tone); break;
        case IceVisualId::BigGuy01: drawBigGuy01(x, y, tone); break;
        case IceVisualId::Count: break;
    }
}

void DisplayManager::showFloor(const FloorView& v)
{
    PresentGuard mirror(*this);
    char floorTitle[24] = {};
    // Leave room for the optional entity counter while keeping the title out
    // of the right-side action panel.
    copyHeaderTitle(v.title, floorTitle, sizeof(floorTitle), 20);
    const bool traceFloor2 = NETRUN_DEBUG_VERBOSE && v.floor == 2 && v.architecture != nullptr &&
        strcmp(v.architecture, "NETRUNNER COMBAT TEST") == 0;
    if (traceFloor2) Serial.println("[F2] 23 DisplayManager enter");
    const bool actionSelectionOnly = floorCacheValid_ && v.floor == cachedFloor_ &&
        v.turn == cachedTurn_ && v.visual == cachedFloorVisual_ && v.actions == cachedFloorActions_ &&
        v.runnerHp == cachedFloorRunnerHp_ && v.value == cachedFloorValue_ &&
        v.architectureCompleted == cachedFloorArchitectureCompleted_ &&
        v.maxValue == cachedFloorMaxValue_ && v.demonActive == cachedDemonActive_ &&
        v.demonRez == cachedDemonRez_ && v.demonType == cachedDemonType_ && v.iceVisual == cachedIceVisual_ && v.actionCount == cachedFloorActionCount_ &&
        v.selected != cachedFloorSelected_ && strcmp(floorTitle, cachedFloorTitle_) == 0 &&
        strcmp(v.status, cachedFloorStatus_) == 0 &&
        strcmp(v.playerStatus != nullptr ? v.playerStatus : "", cachedPlayerStatus_) == 0;
    if (actionSelectionOnly)
    {
        // Up/Down only changes the right-side action list. Preserve the HUD and
        // floor visual rather than flashing a black full-screen frame.
        M5Cardputer.Display.fillRect(FLOOR_ACTION_PANEL_X + 1, FLOOR_ACTION_PANEL_Y + 1,
                                     FLOOR_ACTION_PANEL_WIDTH - 1, FLOOR_ACTION_PANEL_HEIGHT - 1, TFT_BLACK);
        M5Cardputer.Display.drawRect(FLOOR_ACTION_PANEL_X, FLOOR_ACTION_PANEL_Y,
                                     FLOOR_ACTION_PANEL_WIDTH, FLOOR_ACTION_PANEL_HEIGHT, UI_MAGENTA);
        M5Cardputer.Display.drawPixel(155, 20, TFT_WHITE);
        M5Cardputer.Display.drawPixel(238, 124, TFT_WHITE);
        drawMenu(v.actionLabels, v.actionCount, v.selected, 23, 12);
        if (traceFloor2) Serial.println("[F2] 27 after partial action panel render");
        cachedFloorSelected_ = v.selected;
        if (traceFloor2) Serial.println("[F2] 28 DisplayManager exit");
        return;
    }
    clear(); drawHud(v);
    const bool positive = strstr(v.status, "RESOLVED") || strstr(v.status, "IDENTIFIED") ||
                          strstr(v.status, "DOWNLOADED") || strstr(v.status, "OWNED");
    if (v.visual == FloorVisual::Ice01 || v.visual == FloorVisual::Ice02 ||
        v.visual == FloorVisual::Ice03 || v.visual == FloorVisual::Ice04 ||
        v.visual == FloorVisual::Ice05 || v.visual == FloorVisual::Ice06 ||
        v.visual == FloorVisual::Ice07 || v.visual == FloorVisual::Ice08 ||
        v.visual == FloorVisual::Ice09 || v.visual == FloorVisual::Ice10 ||
        v.visual == FloorVisual::Ice11 || v.visual == FloorVisual::Ice12) {
        const bool engaged = strstr(v.status, "ENGAGED");
        const bool slid = strstr(v.status, "SLID");
        const bool playerIce = strstr(v.status, "PLAYER ICE");
        M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
        const int titleWidth = v.targetCount > 0 ? 127 : 151;
        M5Cardputer.Display.setTextSize(1);
        if (M5Cardputer.Display.textWidth(v.title != nullptr ? v.title : "") > titleWidth)
            configureMarquee(marquee_, "FLOOR_TITLE", v.title,
                             6, 18, titleWidth, 10, 1, UI_RED);
        else
            drawStaticClippedText(v.title, 6, 18, titleWidth, 10, 1, UI_RED);
        if (v.targetCount > 0)
        {
            M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
            M5Cardputer.Display.setCursor(6 + titleWidth, 18);
            M5Cardputer.Display.printf("%u/%u", static_cast<unsigned>(v.targetIndex + 1),
                static_cast<unsigned>(v.targetCount));
        }
        const FeedbackTone tone = playerIce ? FeedbackTone::Success :
            (engaged || slid ? FeedbackTone::Danger : FeedbackTone::Inactive);
        constexpr int spriteX = FLOOR_ACTOR_SPRITE_X;
        constexpr int spriteY = FLOOR_ACTOR_SPRITE_Y;
        drawIceVisual(v.iceVisual, spriteX, spriteY, tone);
        M5Cardputer.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        M5Cardputer.Display.setCursor(8, 91); M5Cardputer.Display.print("STATUS:");
        M5Cardputer.Display.setTextColor(engaged ? UI_RED : playerIce ? UI_GREEN : UI_DIM, TFT_BLACK);
        M5Cardputer.Display.setCursor(52, 91);
        M5Cardputer.Display.print(engaged ? "ENGAGED" : slid ? "SLID" : playerIce ? "PLAYER ICE" : "DEREZZED");
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.setCursor(8, 103); M5Cardputer.Display.print("REZ");
        drawBar(30, 104, 83, v.value, v.maxValue, UI_RED);
        M5Cardputer.Display.setCursor(117, 103); M5Cardputer.Display.printf("%d/%d", v.value, v.maxValue);
    } else {
        M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        const int titleWidth = v.targetCount > 0 ? 127 : 151;
        if (M5Cardputer.Display.textWidth(v.title != nullptr ? v.title : "") > titleWidth)
            configureMarquee(marquee_, "FLOOR_TITLE", v.title,
                             7, 18, titleWidth, 10, 1, UI_MAGENTA);
        else
            drawStaticClippedText(v.title, 7, 18, titleWidth, 10, 1, UI_MAGENTA);
        if (v.targetCount > 0)
        {
            M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
            M5Cardputer.Display.setCursor(7 + titleWidth, 18);
            M5Cardputer.Display.printf("%u/%u", static_cast<unsigned>(v.targetIndex + 1),
                static_cast<unsigned>(v.targetCount));
        }
        if (v.visual == FloorVisual::Password) drawPasswordDoor(37, 32, v.securityClass, positive);
        else if (v.visual == FloorVisual::File) drawDatabaseNode(37, 32, positive);
        else if (v.visual == FloorVisual::ControlNode) drawControlNode(37, 32, positive);
        else if (v.visual == FloorVisual::Trophy) {
            M5Cardputer.Display.setTextColor(UI_GREEN, TFT_BLACK);
            // Virus anchor/final node: a beacon core locked into a base frame.
            M5Cardputer.Display.fillRoundRect(47, 39, 27, 31, 6, 0x0240);
            M5Cardputer.Display.fillRoundRect(42, 34, 27, 31, 6, UI_GREEN);
            M5Cardputer.Display.fillRect(48, 40, 15, 19, 0x0240);
            M5Cardputer.Display.fillCircle(56, 49, 8, UI_GREEN);
            M5Cardputer.Display.fillCircle(56, 49, 4, TFT_WHITE);
            M5Cardputer.Display.drawLine(56, 57, 56, 68, UI_GREEN);
            M5Cardputer.Display.drawLine(48, 63, 64, 63, UI_GREEN);
            M5Cardputer.Display.fillRect(45, 69, 24, 6, UI_GREEN);
            M5Cardputer.Display.drawFastHLine(48, 37, 15, TFT_WHITE);
        }
        else if (v.visual == FloorVisual::EnemyNetrunner) {
            if (traceFloor2) Serial.println("[F2] 24 before enemy detail render");
            M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
            // Bust-only hostile avatar: helmet shell, inset visor, interface
            // port and a shoulder block. No whole-body stick figure remains.
            M5Cardputer.Display.fillRoundRect(49, 37, 32, 29, 7, TFT_DARKGREY);
            M5Cardputer.Display.fillRect(45, 62, 40, 11, TFT_DARKGREY);
            M5Cardputer.Display.fillRoundRect(46, 34, 32, 29, 7, UI_RED);
            M5Cardputer.Display.fillRect(42, 60, 40, 11, UI_RED);
            M5Cardputer.Display.drawRoundRect(51, 41, 22, 11, 3, TFT_BLACK);
            M5Cardputer.Display.drawFastHLine(54, 45, 16, TFT_WHITE);
            M5Cardputer.Display.drawFastVLine(74, 43, 8, UI_DIM);
            M5Cardputer.Display.fillCircle(49, 54, 3, UI_MAGENTA);
            M5Cardputer.Display.fillCircle(78, 53, 4, TFT_DARKGREY);
            M5Cardputer.Display.drawPixel(78, 52, UI_MAGENTA);
            M5Cardputer.Display.drawLine(46, 50, 40, 55, UI_RED);
            M5Cardputer.Display.drawFastHLine(78, 57, 7, UI_RED);
            M5Cardputer.Display.drawPixel(87, 57, UI_RED);
            if (traceFloor2) Serial.println("[F2] 25 after enemy detail render");
        }
        M5Cardputer.Display.setTextColor(positive ? UI_GREEN :
            strstr(v.status, "HOSTILE") ? UI_RED : TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.setCursor(8, 93);
        if (v.visual == FloorVisual::EnemyNetrunner) {
            M5Cardputer.Display.printf("ENEMY HP %d/%d", v.value, v.maxValue);
            M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
            M5Cardputer.Display.setCursor(8, 106); M5Cardputer.Display.print(v.status);
        } else if (v.visual == FloorVisual::File && positive) {
            char valueText[16] = {};
            char metadataLine[25] = {};
            snprintf(valueText, sizeof(valueText), "%lueb", static_cast<unsigned long>(v.metadataValue));
            constexpr size_t metadataLimit = sizeof(metadataLine) - 1;
            const size_t valueLength = strlen(valueText);
            const size_t fixedLength = 5 + 1 + 4 + valueLength; // TYPE: + space + VAL:
            const size_t typeLength = fixedLength < metadataLimit ? metadataLimit - fixedLength : 0;
            if (typeLength > 0)
                snprintf(metadataLine, sizeof(metadataLine), "TYPE:%.*s VAL:%s",
                    static_cast<int>(typeLength), v.metadataType != nullptr ? v.metadataType : "", valueText);
            else
                snprintf(metadataLine, sizeof(metadataLine), "VAL:%s", valueText);
            M5Cardputer.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            M5Cardputer.Display.print(metadataLine);
            M5Cardputer.Display.setTextColor(UI_GREEN, TFT_BLACK);
            M5Cardputer.Display.setCursor(8, 106); M5Cardputer.Display.printf("STATUS: %s", v.status);
        } else M5Cardputer.Display.print(v.status);
    }
    if (v.demonMaxRez > 0)
    {
        // Keep every Demon type in the narrow lane between the focused
        // object and the action panel. This prevents the Demon actor from sitting on
        // top of ICE, runner, or floor-object artwork.
        const int demonX = 130;
        const int demonTextX = 113;
        drawDemonActor(demonX, 28, v.demonType, v.demonActive);
        M5Cardputer.Display.setTextColor(v.demonActive ? UI_RED : UI_DIM, TFT_BLACK);
        M5Cardputer.Display.setCursor(demonTextX, 60); M5Cardputer.Display.print(v.demonName != nullptr ? v.demonName : "DEMON");
        M5Cardputer.Display.setCursor(demonTextX, 70);
        if (v.demonActive) M5Cardputer.Display.printf("%d/%d", v.demonRez, v.demonMaxRez);
        else M5Cardputer.Display.print("DEREZZED");
    }
    const uint16_t hpColor = v.runnerMaxHp > 0 && v.runnerHp * 3 <= v.runnerMaxHp ? UI_RED :
        v.runnerMaxHp > 0 && v.runnerHp * 2 <= v.runnerMaxHp ? UI_YELLOW : UI_GREEN;
    M5Cardputer.Display.setTextColor(hpColor, TFT_BLACK);
    // Keep a clear breathing line below the object status (especially ENGAGED)
    // while preserving the compact action panel on the right.
    M5Cardputer.Display.setCursor(8, 119);
    M5Cardputer.Display.printf("HP %u/%u", v.runnerHp, v.runnerMaxHp);
    if (v.playerStatus != nullptr && v.playerStatus[0] != '\0' &&
        (v.targetSummary == nullptr || v.targetSummary[0] == '\0'))
    {
        configureMarquee(playerStatusMarquee_, "PLAYER_STATUS", v.playerStatus,
                         85, 118, 70, 10, 1, UI_YELLOW);
    }
    else
    {
        playerStatusMarquee_.active = false;
    }
    M5Cardputer.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    if (traceFloor2) Serial.println("[F2] 26 before action panel render");
    M5Cardputer.Display.drawRect(FLOOR_ACTION_PANEL_X, FLOOR_ACTION_PANEL_Y,
                                 FLOOR_ACTION_PANEL_WIDTH, FLOOR_ACTION_PANEL_HEIGHT, UI_MAGENTA);
    M5Cardputer.Display.drawPixel(155, 20, TFT_WHITE);
    M5Cardputer.Display.drawPixel(238, 124, TFT_WHITE);
    drawMenu(v.actionLabels, v.actionCount, v.selected, 23, 12);
    if (traceFloor2) Serial.println("[F2] 27 after action panel render");
    cachedFloor_ = v.floor;
    cachedTurn_ = v.turn;
    cachedFloorVisual_ = v.visual;
    cachedFloorActions_ = v.actions;
    cachedFloorRunnerHp_ = v.runnerHp;
    cachedFloorArchitectureCompleted_ = v.architectureCompleted;
    cachedFloorValue_ = v.value;
    cachedFloorMaxValue_ = v.maxValue;
    cachedDemonActive_ = v.demonActive;
    cachedDemonRez_ = v.demonRez;
    cachedDemonType_ = v.demonType;
    cachedIceVisual_ = v.iceVisual;
    cachedFloorActionCount_ = v.actionCount;
    cachedFloorSelected_ = v.selected;
    snprintf(cachedFloorTitle_, sizeof(cachedFloorTitle_), "%s", floorTitle);
    snprintf(cachedFloorStatus_, sizeof(cachedFloorStatus_), "%s", v.status);
    snprintf(cachedPlayerStatus_, sizeof(cachedPlayerStatus_), "%s",
        v.playerStatus != nullptr ? v.playerStatus : "");
    floorCacheValid_ = true;
    if (traceFloor2) Serial.println("[F2] 28 DisplayManager exit");
}

void DisplayManager::showArchitectureMap(const ArchitectureMapView& view, int16_t panX, int16_t panY)
{
    PresentGuard mirror(*this);
    floorCacheValid_ = false;
    clear();
    M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
    M5Cardputer.Display.setCursor(7, 5); M5Cardputer.Display.print("ARCHITECTURE MAP");
    configureMarquee(marquee_, "MAP",
        view.architecture != nullptr ? view.architecture : "NET",
        7, 16, 226, 10, 1, UI_DIM);
    const int16_t mapTop = 25;
    const int16_t mapBottom = 115;
    for (size_t index = 0; index < view.edgeCount; ++index)
    {
        const ArchitectureMapEdge& edge = view.edges[index];
        if (edge.from >= view.nodeCount || edge.to >= view.nodeCount) continue;
        const ArchitectureMapNode& a = view.nodes[edge.from];
        const ArchitectureMapNode& b = view.nodes[edge.to];
        M5Cardputer.Display.drawLine(a.x + panX + 7, a.y + panY + 7,
            b.x + panX + 7, b.y + panY + 7, UI_DIM);
    }
    for (size_t index = 0; index < view.nodeCount; ++index)
    {
        const ArchitectureMapNode& node = view.nodes[index];
        const int16_t x = node.x + panX;
        const int16_t y = node.y + panY;
        if (x < -14 || x > 240 || y < mapTop - 14 || y > mapBottom) continue;
        const uint16_t color = node.status == ArchitectureMapNodeStatus::Unknown ? TFT_DARKGREY :
            node.status == ArchitectureMapNodeStatus::Hostile ? UI_RED :
            node.status == ArchitectureMapNodeStatus::Actionable ? UI_YELLOW : UI_ORANGE;
        M5Cardputer.Display.drawRect(x, y, 14, 14, color);
        if (node.currentPlayer) M5Cardputer.Display.drawRect(x - 2, y - 2, 18, 18, UI_GREEN);
        M5Cardputer.Display.setTextColor(color, TFT_BLACK);
        M5Cardputer.Display.setCursor(x + (node.floorNumber > 9 ? 2 : 4), y + 3);
        if (node.visited) M5Cardputer.Display.print(node.floorNumber);
        else if (node.discovered)
        {
            M5Cardputer.Display.setCursor(x + 1, y + 3); M5Cardputer.Display.print(node.symbol);
            M5Cardputer.Display.print(node.floorNumber);
        }
        else M5Cardputer.Display.print('?');
        if (node.playerIce)
        {
            M5Cardputer.Display.setTextColor(UI_GREEN, TFT_BLACK);
            M5Cardputer.Display.setCursor(x + 10, y - 4); M5Cardputer.Display.print('+');
        }
    }
    M5Cardputer.Display.setTextColor(UI_DIM, TFT_BLACK);
    M5Cardputer.Display.setCursor(7, 121); M5Cardputer.Display.print("ARROWS PAN  BACK CLOSE");
    if (view.hasPlayerIce)
    {
        M5Cardputer.Display.setTextColor(UI_GREEN, TFT_BLACK);
        M5Cardputer.Display.setCursor(185, 16); M5Cardputer.Display.print("+ P-ICE");
    }
}

void DisplayManager::drawMenu(const char* const items[], size_t count,
                              size_t selected, int y, int rowHeight, size_t firstItem)
{
    M5Cardputer.Display.setTextSize(1);
    const size_t visibleRows = y == 23 ? 8 : y >= 30 ? 5 : count;
    ListViewState list;
    list.selected = selected;
    list.ensureVisible(count, visibleRows);
    const bool windowedItems = firstItem != MENU_FIRST_AUTO;
    const size_t first = windowedItems ? firstItem : list.scrollOffset;
    for (size_t row = 0; row < visibleRows && first + row < count; ++row)
    {
        const size_t i = first + row;
        const int rowY = y + static_cast<int>(row) * rowHeight;
        if (rowY > 122) break;
        const bool active = i == selected;
        M5Cardputer.Display.setTextColor(active ? UI_MAGENTA : TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.setCursor(y < 30 ? 160 : 9, rowY);
        M5Cardputer.Display.print(active ? ">" : " ");
        // The floor action panel is only 77px wide after the selection marker.
        // Print a bounded prefix so TFT_eSPI cannot wrap into the play field.
        const size_t localIndex = windowedItems ? i - firstItem : i;
        const char* item = items[localIndex] != nullptr ? items[localIndex] : "";
        const size_t maxChars = y < 30 ? 11 : 36;
        char label[37] = {};
        snprintf(label, sizeof(label), "%.*s", static_cast<int>(maxChars), item);
        M5Cardputer.Display.print(label);
    }
    if (count > visibleRows)
    {
        M5Cardputer.Display.setTextColor(UI_DIM, TFT_BLACK);
        if (first > 0) { M5Cardputer.Display.setCursor(224, y - 9); M5Cardputer.Display.print("^"); }
        if (first + visibleRows < count) { M5Cardputer.Display.setCursor(224, 120); M5Cardputer.Display.print("v"); }
    }
}

void DisplayManager::showProgramAction(const char* title, const char* status, const char* role,
                                       const char* effect1, const char* effect2,
                                       const char* const items[], size_t count, size_t selected)
{
    PresentGuard mirror(*this);
    clear();
    M5Cardputer.Display.drawRect(3, 3, 234, 129, UI_DIM);
    M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(8, 7); M5Cardputer.Display.print(title);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5Cardputer.Display.setCursor(9, 28); M5Cardputer.Display.print(status);
    M5Cardputer.Display.drawLine(7, 39, 233, 39, UI_DIM);
    M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
    M5Cardputer.Display.setCursor(10, 48); M5Cardputer.Display.print(role);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.setCursor(10, 60); M5Cardputer.Display.print(effect1);
    M5Cardputer.Display.setCursor(10, 72); M5Cardputer.Display.print(effect2);
    drawMenu(items, count, selected, 92, 12);
}

void DisplayManager::showPrograms(const char* const names[], const char* const states[],
                                  size_t slotCount, size_t selected, bool includeBack)
{
    PresentGuard mirror(*this);
    const size_t count = slotCount + (includeBack ? 1 : 0);
    const bool selectionOnly = programCacheValid_ && count == cachedProgramCount_ &&
        selected != cachedProgramSelected_;
    if (selectionOnly)
    {
        M5Cardputer.Display.fillRect(6, 27, 228, 101, TFT_BLACK);
    }
    else
    {
        clear();
        M5Cardputer.Display.drawRect(3, 3, 234, 129, UI_DIM);
        M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
        M5Cardputer.Display.setTextSize(2); M5Cardputer.Display.setCursor(7, 5); M5Cardputer.Display.print("CYBERDECK");
        M5Cardputer.Display.setTextSize(1); M5Cardputer.Display.drawLine(5, 25, 235, 25, UI_MAGENTA);
        M5Cardputer.Display.fillCircle(218, 14, 3, UI_GREEN);
        M5Cardputer.Display.drawFastHLine(224, 14, 7, UI_MAGENTA);
    }
    constexpr size_t visibleRows = 7;
    ListViewState list;
    list.selected = selected;
    list.ensureVisible(count, visibleRows);
    for (size_t row = 0; row < visibleRows && list.scrollOffset + row < count; ++row) {
        const size_t i = list.scrollOffset + row;
        const int y = 31 + static_cast<int>(row) * 13;
        const bool active = i == selected;
        M5Cardputer.Display.drawRect(7, y - 2, 226, 12, active ? UI_MAGENTA : UI_DIM);
        M5Cardputer.Display.drawFastVLine(14, y, 6, active ? TFT_WHITE : UI_MAGENTA);
        M5Cardputer.Display.setTextColor(active ? UI_MAGENTA : TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.setCursor(21, y); M5Cardputer.Display.print(active ? "> " : "  ");
        if (i == slotCount) { M5Cardputer.Display.print("BACK"); continue; }
        M5Cardputer.Display.print(names[i]);
        const char* state = states[i];
        const uint16_t c = !strcmp(state, "ON") ? UI_GREEN : !strcmp(state, "DEREZ") ? UI_YELLOW :
                           !strcmp(state, "DESTROY") ? UI_RED : UI_DIM;
        M5Cardputer.Display.setTextColor(c, TFT_BLACK);
        M5Cardputer.Display.setCursor(174, y); M5Cardputer.Display.printf("[%s]", state);
        if (state[0] != '\0') M5Cardputer.Display.fillCircle(224, y + 3, 2, c);
    }
    if (count > visibleRows)
    {
        M5Cardputer.Display.setTextColor(UI_DIM, TFT_BLACK);
        if (list.scrollOffset > 0) { M5Cardputer.Display.setCursor(224, 28); M5Cardputer.Display.print("^"); }
        if (list.scrollOffset + visibleRows < count) { M5Cardputer.Display.setCursor(224, 120); M5Cardputer.Display.print("v"); }
    }
    cachedProgramCount_ = count;
    cachedProgramSelected_ = selected;
    programCacheValid_ = true;
}

void DisplayManager::animateTransition(FeedbackTone tone)
{
    const uint16_t c = toneColor(tone);
    for (int x = 0; x < 240; x += 30) {
        const uint32_t frameStart = millis();
        uint16_t backgroundColumn[135];
        M5Cardputer.Display.readRect(x, 0, 1, 135, backgroundColumn);
        M5Cardputer.Display.drawFastVLine(x, 0, 135, c);
        present();
        waitForFrameBudget(frameStart, 18);
        M5Cardputer.Display.pushImage(x, 0, 1, 135, backgroundColumn);
    }
    // The last restored column is also part of the final internal frame.
    // Present it so the optional external panel cannot retain the overlay.
    present();
}

void DisplayManager::animateCloak(bool success)
{
    constexpr uint8_t frameCount = 8;
    constexpr uint32_t frameMs = 90;
    const uint16_t accent = success ? UI_GREEN : UI_RED;

    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setTextColor(accent, TFT_BLACK);
        M5Cardputer.Display.setCursor(12, 12);
        M5Cardputer.Display.print("CLOAK // SIGNAL MASK");
        M5Cardputer.Display.drawRect(8, 27, 224, 82, UI_DIM);

        // Abstract signature fragments only: no map, floor, or entity data.
        const uint8_t phase = success ? frame : static_cast<uint8_t>(frame / 2);
        for (uint8_t band = 0; band < 7; ++band)
        {
            const int y = 37 + band * 10;
            const int gap = 16 + ((phase + band * 3) % 5) * 7;
            const int left = 18 + ((phase * 11 + band * 5) % 18);
            const int right = 214 - ((phase * 7 + band * 3) % 18);
            M5Cardputer.Display.drawFastHLine(left, y, gap, accent);
            if (right > left + gap + 8)
                M5Cardputer.Display.drawFastHLine(left + gap + 8, y, right - left - gap - 8,
                                                  (band + frame) & 1 ? UI_MAGENTA : UI_DIM);
        }
        const int centerX = 120 + static_cast<int>((frame & 1) ? 5 : -5);
        M5Cardputer.Display.drawFastVLine(centerX, 44, 48, accent);
        M5Cardputer.Display.drawFastVLine(centerX - 3, 52, 32, UI_MAGENTA);
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.setCursor(24, 119);
        M5Cardputer.Display.print(success ? "TRACE SUPPRESSED" : "SIGNATURE REACQUIRED");
        present();
        waitForFrameBudget(frameStart, frameMs);
    }
}

void DisplayManager::animateDigitalRoll(const char* name, uint8_t finalRoll)
{
    const DigitalRollSequence sequence = makeDigitalRollSequence(finalRoll);
    for (uint8_t value : sequence.fakeValues) {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
        M5Cardputer.Display.setTextSize(2); M5Cardputer.Display.setCursor(8, 8); M5Cardputer.Display.print(name);
        M5Cardputer.Display.setTextSize(1); M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.setCursor(88, 48); M5Cardputer.Display.print("ROLLING...");
        M5Cardputer.Display.drawRect(91, 68, 58, 34, UI_MAGENTA);
        M5Cardputer.Display.setTextSize(2); M5Cardputer.Display.setCursor(111, 77); M5Cardputer.Display.print(value);
        present();
        waitForFrameBudget(frameStart, 60);
    }

    const uint32_t frameStart = millis();
    clear();
    M5Cardputer.Display.setTextColor(UI_GREEN, TFT_BLACK);
    M5Cardputer.Display.setTextSize(2); M5Cardputer.Display.setCursor(8, 8); M5Cardputer.Display.print(name);
    M5Cardputer.Display.setTextSize(1); M5Cardputer.Display.setCursor(82, 48); M5Cardputer.Display.print("ROLL LOCKED");
    M5Cardputer.Display.drawRect(91, 68, 58, 34, UI_GREEN);
    M5Cardputer.Display.setTextSize(2); M5Cardputer.Display.setCursor(111, 77);
    M5Cardputer.Display.print(sequence.finalValue);
    present();
    waitForFrameBudget(frameStart, 160);
}

void DisplayManager::animateJackIn(const char* targetName)
{
    // Keep the target line fixed-size: scenario names are content data and
    // this path must not allocate while the run transition is on screen.
    char targetLine[40] = {};
    snprintf(targetLine, sizeof(targetLine), "TARGET // %.29s", targetName != nullptr ? targetName : "UNKNOWN");

    struct JackFrame { const char* first; const char* second; uint16_t accent; uint32_t duration; };
    static const JackFrame frames[] = {
        {"> RESOLVING ARCHITECTURE", "", UI_MAGENTA, 360},
        {"> RESOLVING ARCHITECTURE", "> LINK ESTABLISHED", UI_MAGENTA, 420},
        {"> NEURAL HANDSHAKE", "> INTERFACE ........ OK", UI_GREEN, 420}
    };

    for (const JackFrame& frame : frames)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.drawRect(7, 7, 226, 121, UI_DIM);
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(15, 17); M5Cardputer.Display.print(targetLine);
        M5Cardputer.Display.drawFastHLine(15, 31, 210, UI_DIM);
        M5Cardputer.Display.setTextColor(frame.accent, TFT_BLACK);
        M5Cardputer.Display.setCursor(15, 49); M5Cardputer.Display.print(frame.first);
        if (frame.second[0] != '\0')
        {
            M5Cardputer.Display.setCursor(15, 64); M5Cardputer.Display.print(frame.second);
        }
        for (uint8_t index = 0; index < 12; ++index)
        {
            const int x = 15 + index * 17;
            const uint16_t color = index <= (&frame - frames) * 4 ? frame.accent : UI_DIM;
            M5Cardputer.Display.drawFastHLine(x, 96, 12, color);
            M5Cardputer.Display.drawPixel(x + 3, 103, color);
        }
        M5Cardputer.Display.setTextColor(UI_DIM, TFT_BLACK);
        M5Cardputer.Display.setCursor(15, 113); M5Cardputer.Display.print("SECURE CHANNEL / 2400 BAUD");
        present();
        waitForFrameBudget(frameStart, frame.duration);
    }

    const uint32_t frameStart = millis();
    clear();
    M5Cardputer.Display.drawRect(7, 7, 226, 121, UI_GREEN);
    M5Cardputer.Display.setTextColor(UI_GREEN, TFT_BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(47, 51); M5Cardputer.Display.print("JACKED IN");
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(78, 82); M5Cardputer.Display.print("ARCHITECTURE ONLINE");
    present();
    waitForFrameBudget(frameStart, 480);
}

void DisplayManager::animateRunSummary(const char* title, const char* const lines[],
                                        size_t lineCount, const char* footer, FeedbackTone tone)
{
    const size_t revealCount = lineCount > 7 ? 7 : lineCount;
    for (size_t visible = 1; visible <= revealCount; ++visible)
    {
        showInfo(title, lines, visible, visible == revealCount ? footer : "", tone);
        if (visible < revealCount) delay(240);
    }
}

void DisplayManager::animateEncounterIntro(const char* iceName)
{
    for (uint8_t frame = 0; frame < 3; ++frame) {
        const uint32_t frameStart = millis();
        clear();
        const int offset = frame == 1 ? 4 : 0;
        M5Cardputer.Display.drawRect(4, 4, 232, 127, UI_RED);
        M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setCursor(34 + offset, 38); M5Cardputer.Display.print("BLACK ICE");
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(69 - offset, 68);
        M5Cardputer.Display.printf("%s DETECTED", iceName != nullptr ? iceName : "ICE");
        for (int y = 91 + frame * 3; y < 112; y += 7)
            M5Cardputer.Display.drawFastHLine(21 + frame * 8, y, 198 - frame * 16, frame == 1 ? UI_MAGENTA : UI_RED);
        present();
        waitForFrameBudget(frameStart, 120);
    }
}

void DisplayManager::animateEnemyPresence(const char* handle)
{
    const uint32_t frameStart = millis();
    clear();
    M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
    for (int y = 18; y < 112; y += 9) M5Cardputer.Display.drawFastHLine(12, y, 216, UI_DIM);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(19, 30); M5Cardputer.Display.print("HOSTILE SIGNAL");
    M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
    M5Cardputer.Display.setCursor(55, 63); M5Cardputer.Display.print(handle != nullptr ? handle : "NETRUNNER");
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(62, 94); M5Cardputer.Display.print("NETRUNNER DETECTED");
    present();
    waitForFrameBudget(frameStart, 420);
}

void DisplayManager::animateHostileAppearance(FloorVisual visual, const char* label, uint8_t demonType,
                                               IceVisualId iceVisual)
{
    constexpr uint8_t frameCount = 6;
    constexpr uint32_t revealFrameMs = 70;
    constexpr uint32_t fullRevealHoldMs = 450;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("HOSTILE // MATERIALIZE");
        M5Cardputer.Display.drawRect(7, 23, 226, 91, UI_DIM);
        // The final frame must cover the complete logical stage. The old
        // 53px endpoint clipped the right side of large ICE and the bust.
        const int revealWidth = frame == frameCount - 1
            ? 226 : 18 + static_cast<int>(frame) * 34;
        M5Cardputer.Display.setClipRect(8, 24, revealWidth, 89);
        drawHostileSprite(visual, FLOOR_ACTOR_SPRITE_X, FLOOR_ACTOR_SPRITE_Y,
                          FeedbackTone::Danger, demonType, iceVisual);
        M5Cardputer.Display.clearClipRect();
        M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
        M5Cardputer.Display.setCursor(12, 119); M5Cardputer.Display.print(label != nullptr ? label : "HOSTILE ACTOR");
        if ((frame & 1) == 0)
            M5Cardputer.Display.drawFastHLine(12, 106, 200 - frame * 12, UI_RED);
        present();
        waitForFrameBudget(frameStart, revealFrameMs);
    }
    const uint32_t holdStart = millis();
    clear();
    M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("HOSTILE // MATERIALIZE");
    M5Cardputer.Display.drawRect(7, 23, 226, 91, UI_DIM);
    drawHostileSprite(visual, FLOOR_ACTOR_SPRITE_X, FLOOR_ACTOR_SPRITE_Y,
                      FeedbackTone::Danger, demonType, iceVisual);
    M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
    M5Cardputer.Display.setCursor(12, 119); M5Cardputer.Display.print(label != nullptr ? label : "HOSTILE ACTOR");
    present();
    waitForFrameBudget(holdStart, fullRevealHoldMs);
}

void DisplayManager::animateHostileAction(HostileActorVisual actor, FloorVisual visual,
                                           uint8_t demonType, bool hit, ProgramId program,
                                           IceVisualId iceVisual, HostileAttackStyle style,
                                           BlackIcePresentationSide side)
{
    constexpr uint8_t frameCount = 8;
    const bool playerBlackIce = actor == HostileActorVisual::BlackIce &&
        side == BlackIcePresentationSide::Player;
    const uint16_t attackPrimary = playerBlackIce ? UI_MAGENTA : UI_RED;
    const uint16_t attackSecondary = playerBlackIce ? 0xFDBF : UI_MAGENTA;
    const uint16_t impactColor = playerBlackIce ? 0xFDBF : UI_RED;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        // Hostile attacks own a clean presentation stage. Recompose it from
        // black on every frame so Floor objects and prior attack pixels never
        // survive into this action.
        clear();
        M5Cardputer.Display.setTextColor(playerBlackIce ? UI_MAGENTA : UI_RED, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12);
        M5Cardputer.Display.print(playerBlackIce ? "PLAYER // ICE ATTACK" : "HOSTILE // ATTACK");
        const int impulse = actor == HostileActorVisual::EnemyNetrunner
            ? (frame == 2 || frame == 3 ? 2 : frame == 5 ? -1 : 0)
            : style == HostileAttackStyle::Lunge
            ? (frame < 3 ? static_cast<int>(frame) * 5 : frame < 5 ? 15 - static_cast<int>(frame) * 3 : 0)
            : style == HostileAttackStyle::Slash ? (frame == 2 || frame == 3 ? 3 : 0)
            : frame == 2 || frame == 3 ? 2 : 0;
        drawHostileSprite(visual, FLOOR_ACTOR_SPRITE_X + impulse, FLOOR_ACTOR_SPRITE_Y,
                          playerBlackIce ? FeedbackTone::Info : FeedbackTone::Danger,
                          demonType, iceVisual);
        const int sourceX = actor == HostileActorVisual::Demon ? 150 : 88;
        const int sourceY = actor == HostileActorVisual::Demon ? 52 : 58;
        if (actor == HostileActorVisual::EnemyNetrunner)
        {
            // Progress reaches the runner-side target zone by frame 6.
            const int effectX = 90 + static_cast<int>(frame) * 15;
            const int effectY = 58;
            switch (program)
            {
                case ProgramId::Hellbolt:
                    for (int burst = 0; burst < 3; ++burst)
                    {
                        const int x = effectX + burst * 7;
                        const int y = effectY + ((frame + burst) & 1 ? -5 : 5);
                        const int height = 9 + static_cast<int>((frame + burst) % 3) * 3;
                        M5Cardputer.Display.fillCircle(x, y + 3, 5, UI_RED);
                        M5Cardputer.Display.fillTriangle(x - 5, y + 5, x + 5, y + 5,
                            x + ((frame + burst) & 1 ? 2 : -2), y - height, UI_RED);
                        M5Cardputer.Display.fillTriangle(x - 2, y + 3, x + 3, y + 3,
                            x + 1, y - height / 2, UI_YELLOW);
                        M5Cardputer.Display.drawPixel(x, y - height - 2, TFT_WHITE);
                    }
                    M5Cardputer.Display.drawPixel(effectX - 7, effectY - 8, UI_YELLOW);
                    M5Cardputer.Display.drawPixel(effectX + 25, effectY + 12, UI_RED);
                    break;
                case ProgramId::Vrizzbolt:
                    for (int segment = 0; segment < 3; ++segment)
                    {
                        const int x = effectX + segment * 12;
                        const int y = effectY + ((segment + frame) & 1 ? -9 : 9);
                        M5Cardputer.Display.drawLine(x, y, x + 6, y + (segment & 1 ? 10 : -10), TFT_CYAN);
                        M5Cardputer.Display.drawLine(x + 6, y + (segment & 1 ? 10 : -10), x + 12, y, UI_MAGENTA);
                    }
                    break;
                case ProgramId::Nervescrub:
                    for (int band = 0; band < 3; ++band)
                    {
                        const int x = effectX + 12 + band * 4;
                        const int y = effectY + (band - 1) * 11;
                        M5Cardputer.Display.drawRect(x - 10, y - 4, 22, 9, band == 1 ? TFT_WHITE : UI_MAGENTA);
                    }
                    M5Cardputer.Display.drawFastHLine(effectX + 26, effectY - 13, 12, UI_MAGENTA);
                    M5Cardputer.Display.drawFastHLine(effectX + 29, effectY + 10, 9, TFT_WHITE);
                    break;
                case ProgramId::Superglue:
                {
                    const int squareCenterX = frame >= 6 ? 185 : effectX;
                    const int nodeOffset = frame >= 4 ? 6 : 8;
                    const int nodeRadius = frame >= 6 ? 5 : 4;
                    for (int row = -1; row <= 1; row += 2)
                    {
                        for (int column = -1; column <= 1; column += 2)
                        {
                            const int x = squareCenterX + column * nodeOffset;
                            const int y = effectY + row * nodeOffset;
                            M5Cardputer.Display.fillRoundRect(x - nodeRadius, y - nodeRadius,
                                nodeRadius * 2 + 1, nodeRadius * 2 + 1, 3, UI_YELLOW);
                            M5Cardputer.Display.drawPixel(x, y, TFT_WHITE);
                        }
                    }
                    if (frame >= 6)
                        M5Cardputer.Display.drawRect(squareCenterX - 14, effectY - 14, 29, 29, UI_YELLOW);
                    break;
                }
                case ProgramId::PoisonFlatline:
                    for (int spike = 0; spike < 4; ++spike)
                    {
                        const int x = effectX + spike * 8;
                        M5Cardputer.Display.fillTriangle(x, effectY, x + 8, effectY - 5,
                            x + 5, effectY + 6, UI_GREEN);
                    }
                    M5Cardputer.Display.drawFastHLine(effectX + 27, effectY, 13, UI_GREEN);
                    M5Cardputer.Display.drawPixel(effectX + 34, effectY - 8, TFT_WHITE);
                    break;
                case ProgramId::DeckKRASH:
                    for (int block = 0; block < 5; ++block)
                    {
                        const int x = effectX + (block % 3) * 11;
                        const int y = effectY + (block / 3) * 12 - 8;
                        if (((frame + block) & 1) == 0) M5Cardputer.Display.fillRect(x, y, 7, 7, UI_MAGENTA);
                        else M5Cardputer.Display.drawRect(x + 3, y - 3, 8, 8, UI_RED);
                    }
                    M5Cardputer.Display.drawFastHLine(effectX + 23, effectY + 13, 15, TFT_WHITE);
                    break;
                default:
                    M5Cardputer.Display.drawCircle(effectX + 12, effectY, 4 + static_cast<int>(frame) * 2, UI_MAGENTA);
                    if (frame >= 2) M5Cardputer.Display.drawCircle(effectX + 12, effectY, 2, UI_RED);
                    break;
            }
        }
        else if (style == HostileAttackStyle::Lunge)
        {
            M5Cardputer.Display.drawFastHLine(sourceX, sourceY, 18 + frame * 8, attackPrimary);
            M5Cardputer.Display.drawFastHLine(sourceX + 5, sourceY - 5, 10 + frame * 5, attackSecondary);
            M5Cardputer.Display.drawFastHLine(sourceX + 10, sourceY + 5, 8 + frame * 4, TFT_WHITE);
        }
        else if (style == HostileAttackStyle::Burst)
        {
            for (uint8_t streak = 0; streak < 3; ++streak)
            {
                if (frame < streak) continue;
                const int head = sourceX + (static_cast<int>(frame) - streak) * 14;
                const int y = sourceY - 10 + streak * 10;
                M5Cardputer.Display.drawFastHLine(head - 17, y, 13, streak == 1 ? TFT_WHITE : attackSecondary);
                M5Cardputer.Display.drawPixel(head, y, attackPrimary);
            }
        }
        else if (style == HostileAttackStyle::Slash)
        {
            const int reach = 12 + static_cast<int>(frame) * 8;
            M5Cardputer.Display.drawLine(sourceX, sourceY - 15, sourceX + reach, sourceY + 11, TFT_WHITE);
            M5Cardputer.Display.drawLine(sourceX, sourceY + 11, sourceX + reach, sourceY - 15, attackSecondary);
        }
        else
        {
            const int radius = 4 + static_cast<int>(frame) * 3;
            M5Cardputer.Display.drawCircle(sourceX + static_cast<int>(frame) * 8, sourceY, radius, attackSecondary);
            if (frame >= 2) M5Cardputer.Display.drawCircle(sourceX + static_cast<int>(frame) * 8, sourceY, radius - 3, attackPrimary);
        }
        const bool enemyProgramAttack = actor == HostileActorVisual::EnemyNetrunner;
        const bool targetReached = enemyProgramAttack ? frame >= 6 : frame >= 4;
        if (targetReached && hit && (!enemyProgramAttack || frame <= 7) &&
            (enemyProgramAttack || frame <= 5))
        {
            const int targetX = 185;
            const int targetY = 62;
            M5Cardputer.Display.drawCircle(targetX, targetY, frame == 4 ? 5 : 10, impactColor);
            M5Cardputer.Display.drawFastHLine(targetX - 15, targetY, 31, TFT_WHITE);
            M5Cardputer.Display.drawFastVLine(targetX, targetY - 15, 31, attackPrimary);
            M5Cardputer.Display.drawLine(targetX - 12, targetY - 12, targetX + 12, targetY + 12, impactColor);
            M5Cardputer.Display.drawLine(targetX + 12, targetY - 12, targetX - 12, targetY + 12, impactColor);
        }
        present();
        waitForFrameBudget(frameStart, 45);
    }
}

void DisplayManager::animateFireDamage()
{
    constexpr uint8_t frameCount = 6;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(6, 18); M5Cardputer.Display.print("FIRE");
        // Keep FIRE in the same left hostile-stage zone as the actor sprites.
        const int centerX = 72;
        const int centerY = 72;
        const int flicker = static_cast<int>(frame & 1) * 3;
        M5Cardputer.Display.fillRoundRect(centerX - 25, centerY - 3, 51, 10, 4, UI_RED);
        M5Cardputer.Display.fillCircle(centerX - 13 + flicker, centerY - 8, 11, UI_RED);
        M5Cardputer.Display.fillCircle(centerX + flicker, centerY - 13, 14, UI_RED);
        M5Cardputer.Display.fillCircle(centerX + 13 + flicker, centerY - 7, 10, UI_RED);
        for (int flame = 0; flame < 4; ++flame)
        {
            const int x = centerX - 18 + flame * 12 + flicker;
            const int height = 18 + static_cast<int>((frame + flame) % 3) * 6;
            M5Cardputer.Display.fillTriangle(x - 6, centerY - 3, x + 6, centerY - 3,
                x + ((frame + flame) & 1 ? 2 : -2), centerY - height, UI_RED);
        }
        M5Cardputer.Display.fillRoundRect(centerX - 14, centerY - 10, 28, 12, 5, UI_YELLOW);
        M5Cardputer.Display.fillCircle(centerX, centerY - 9 - static_cast<int>(frame & 1) * 2, 6, TFT_WHITE);
        M5Cardputer.Display.drawPixel(centerX - 22 + static_cast<int>(frame) * 3, centerY - 25, UI_YELLOW);
        M5Cardputer.Display.drawPixel(centerX + 20 - static_cast<int>(frame) * 2, centerY - 18, UI_RED);
        M5Cardputer.Display.drawPixel(centerX + 27, centerY - 31 + static_cast<int>(frame & 1) * 2, UI_YELLOW);
        present();
        waitForFrameBudget(frameStart, 40);
    }
}

void DisplayManager::animateTransfer(const char* label)
{
    static const char* bars[] = {"[>---------]", "[###-------]", "[######----]", "[##########]"};
    for (uint8_t frame = 0; frame < 4; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(UI_MAGENTA, TFT_BLACK);
        M5Cardputer.Display.setTextSize(2);
        M5Cardputer.Display.setCursor(20, 35); M5Cardputer.Display.print(label != nullptr ? label : "TRANSFERRING");
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(72, 75); M5Cardputer.Display.print(bars[frame]);
        present();
        waitForFrameBudget(frameStart, 55);
    }
}

void DisplayManager::animateDataTransfer()
{
    constexpr uint8_t frameCount = 6;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(UI_GREEN, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("DATA");
        M5Cardputer.Display.drawRect(27, 55, 28, 22, UI_DIM);
        M5Cardputer.Display.drawRect(185, 55, 28, 22, UI_GREEN);
        for (uint8_t packet = 0; packet < 3; ++packet)
        {
            const int x = 64 + static_cast<int>(frame) * 22 + packet * 8;
            const int y = 51 + ((frame + packet) & 1) * 17;
            M5Cardputer.Display.fillRect(x, y, 7, 7, packet == 1 ? TFT_WHITE : TFT_CYAN);
        }
        M5Cardputer.Display.drawFastHLine(61, 65, 120, UI_DIM);
        M5Cardputer.Display.drawFastHLine(61, 65, 20 + static_cast<int>(frame) * 20, UI_GREEN);
        M5Cardputer.Display.drawRect(61, 91, 118, 8, UI_DIM);
        M5Cardputer.Display.fillRect(63, 93, 18 + static_cast<int>(frame) * 20, 4, UI_GREEN);
        present();
        waitForFrameBudget(frameStart, 70);
    }
    holdSystemAnimationFinalFrame();
}

void DisplayManager::animateDataTransferFailure()
{
    constexpr uint8_t frameCount = 5;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(UI_YELLOW, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("DOWNLOAD");
        M5Cardputer.Display.drawRect(27, 55, 28, 22, UI_DIM);
        M5Cardputer.Display.drawRect(185, 55, 28, 22, UI_DIM);
        const int progress = frame < 3 ? 20 + static_cast<int>(frame) * 22 : 64 - static_cast<int>(frame - 3) * 10;
        for (uint8_t packet = 0; packet < 2; ++packet)
        {
            const int x = 64 + static_cast<int>(frame < 3 ? frame : 2) * 22 + packet * 9;
            M5Cardputer.Display.fillRect(x, 57 + packet * 13, 7, 6, packet == 0 ? UI_YELLOW : TFT_WHITE);
        }
        M5Cardputer.Display.drawRect(61, 91, 118, 8, UI_DIM);
        M5Cardputer.Display.fillRect(63, 93, progress, 4, UI_YELLOW);
        if (frame >= 3)
            M5Cardputer.Display.drawFastHLine(63 + progress, 93, 8, UI_DIM);
        present();
        waitForFrameBudget(frameStart, 70);
    }
    holdSystemAnimationFinalFrame();
}

void DisplayManager::animateControlSuccess()
{
    constexpr uint8_t frameCount = 6;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(UI_GREEN, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("CONTROL");
        const int length = 8 + static_cast<int>(frame) * 12;
        const int cx = 120;
        const int cy = 68;
        M5Cardputer.Display.fillCircle(cx, cy, frame >= 4 ? 7 : 5, UI_GREEN);
        M5Cardputer.Display.drawCircle(cx, cy, 11 + frame * 2, TFT_CYAN);
        M5Cardputer.Display.drawFastHLine(cx - length, cy, length - 5, UI_GREEN);
        M5Cardputer.Display.drawFastHLine(cx + 5, cy, length - 5, UI_GREEN);
        M5Cardputer.Display.drawFastVLine(cx, cy - length / 2, length / 2 - 5, UI_GREEN);
        M5Cardputer.Display.drawFastVLine(cx, cy + 5, length / 2 - 5, UI_GREEN);
        if (frame >= 3)
        {
            M5Cardputer.Display.drawRect(cx - length - 5, cy - 5, 6, 10, TFT_CYAN);
            M5Cardputer.Display.drawRect(cx + length - 1, cy - 5, 6, 10, TFT_CYAN);
        }
        present();
        waitForFrameBudget(frameStart, 60);
    }
    holdSystemAnimationFinalFrame();
}

void DisplayManager::animateControlFailure()
{
    constexpr uint8_t frameCount = 6;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(UI_YELLOW, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("CONTROL");
        const int length = frame < 3 ? 8 + static_cast<int>(frame) * 13 : 34 - static_cast<int>(frame - 3) * 10;
        const int cx = 120;
        const int cy = 68;
        M5Cardputer.Display.fillCircle(cx, cy, frame == 5 ? 7 : 5, frame == 5 ? UI_YELLOW : TFT_WHITE);
        M5Cardputer.Display.drawFastHLine(cx - length, cy, length - 5, UI_YELLOW);
        M5Cardputer.Display.drawFastHLine(cx + 5, cy, length - 5, UI_YELLOW);
        M5Cardputer.Display.drawFastVLine(cx, cy - length / 2, length / 2 - 5, UI_YELLOW);
        M5Cardputer.Display.drawFastVLine(cx, cy + 5, length / 2 - 5, UI_YELLOW);
        present();
        waitForFrameBudget(frameStart, 60);
    }
    holdSystemAnimationFinalFrame();
}

void DisplayManager::animateControlReclaim(const char* demonName, const char* nodeName)
{
    const char* actor = demonName != nullptr && demonName[0] != '\0' ? demonName : "DEMON";
    const char* label = nodeName != nullptr && nodeName[0] != '\0' ? nodeName : "CONTROL NODE";
    constexpr uint8_t frameCount = 6;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        const uint16_t accent = frame == frameCount - 1 ? UI_RED : (frame & 1 ? UI_ORANGE : UI_MAGENTA);
        M5Cardputer.Display.setTextColor(accent, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("CONTROL OVERRIDE");
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.setCursor(8, 31); M5Cardputer.Display.print("NODE // ");
        M5Cardputer.Display.print(label);
        const int width = 16 + static_cast<int>(frame) * 14;
        const int cx = 120;
        const int cy = 72;
        M5Cardputer.Display.drawRect(cx - width, cy - 22, width * 2, 44, accent);
        M5Cardputer.Display.drawFastHLine(cx - width + 4, cy, width * 2 - 8, accent);
        M5Cardputer.Display.drawFastVLine(cx, cy - 18, 36, accent);
        if (frame >= 2)
        {
            M5Cardputer.Display.drawFastHLine(cx - width - 8, cy - 12, 8, UI_ORANGE);
            M5Cardputer.Display.drawFastHLine(cx + width, cy + 12, 8, UI_ORANGE);
        }
        if (frame == frameCount - 1)
        {
            M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
            M5Cardputer.Display.setCursor(8, 113); M5Cardputer.Display.print(actor);
            M5Cardputer.Display.print(" TOOK CONTROL");
        }
        present();
        waitForFrameBudget(frameStart, 70);
    }
    holdSystemAnimationFinalFrame();
}

void DisplayManager::animatePasswordUnlock()
{
    constexpr uint8_t frameCount = 6;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(TFT_CYAN, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("ENCRYPTED ACCESS");
        const int left = 78;
        const int top = 43;
        const int width = 84;
        const int height = 45;
        const uint16_t color = frame >= 5 ? UI_GREEN : TFT_CYAN;
        M5Cardputer.Display.drawRect(left, top, width, height, color);
        M5Cardputer.Display.drawFastHLine(left + 12, top + 15, 60, color);
        M5Cardputer.Display.drawFastHLine(left + 12, top + 30, 60, color);
        for (uint8_t block = 0; block < 5; ++block)
        {
            const int x = left + 15 + block * 12;
            const int y = 49 + ((block + frame) % 3) * 12;
            if (frame < 4) M5Cardputer.Display.fillRect(x, y, 7, 5, block & 1 ? UI_MAGENTA : TFT_CYAN);
        }
        if (frame >= 3)
        {
            const int gap = frame >= 5 ? 25 : 10;
            M5Cardputer.Display.fillRect(left + width / 2 - gap / 2, top - 2, gap, 4, TFT_BLACK);
            M5Cardputer.Display.fillRect(left + width / 2 - gap / 2, top + height - 2, gap, 4, TFT_BLACK);
            M5Cardputer.Display.drawFastVLine(left + width / 2, top + 7, height - 14, frame >= 5 ? UI_GREEN : TFT_WHITE);
        }
        present();
        waitForFrameBudget(frameStart, 65);
    }
    holdSystemAnimationFinalFrame();
}

void DisplayManager::animatePasswordFailure()
{
    constexpr uint8_t frameCount = 6;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(UI_YELLOW, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("ENCRYPTED ACCESS");
        const int left = 78;
        const int top = 43;
        M5Cardputer.Display.drawRect(left, top, 84, 45, UI_YELLOW);
        M5Cardputer.Display.drawFastHLine(left + 12, top + 15, 60, UI_YELLOW);
        M5Cardputer.Display.drawFastHLine(left + 12, top + 30, 60, UI_YELLOW);
        for (uint8_t block = 0; block < 5; ++block)
        {
            const int x = left + 15 + block * 12 + ((frame & 1) ? 3 : 0);
            const int y = 49 + ((block + frame) % 3) * 12;
            M5Cardputer.Display.fillRect(x, y, 7, 5, block & 1 ? TFT_WHITE : UI_YELLOW);
        }
        if (frame >= 4)
        {
            M5Cardputer.Display.drawRect(left + 29, top + 12, 26, 21, UI_YELLOW);
            M5Cardputer.Display.drawFastHLine(left + 35, top + 22, 14, TFT_WHITE);
        }
        present();
        waitForFrameBudget(frameStart, 65);
    }
    holdSystemAnimationFinalFrame();
}

void DisplayManager::animateEyeDee(bool success)
{
    constexpr uint8_t frameCount = 6;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        const uint16_t accent = success && frame == frameCount - 1 ? UI_GREEN : TFT_CYAN;
        M5Cardputer.Display.setTextColor(accent, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("EYE-DEE");
        const int left = 75;
        const int top = 43;
        const int right = 165;
        const int bottom = 91;
        const int scanY = top + 5 + static_cast<int>(frame) * 8;
        M5Cardputer.Display.drawFastHLine(left, scanY, right - left, frame == 5 ? UI_GREEN : TFT_CYAN);
        M5Cardputer.Display.drawFastHLine(left, scanY + 1, right - left, UI_DIM);
        M5Cardputer.Display.drawFastHLine(left, top, 14, accent);
        M5Cardputer.Display.drawFastVLine(left, top, 10, accent);
        M5Cardputer.Display.drawFastHLine(right - 14, top, 14, accent);
        M5Cardputer.Display.drawFastVLine(right, top, 10, accent);
        M5Cardputer.Display.drawFastHLine(left, bottom, 14, accent);
        M5Cardputer.Display.drawFastVLine(left, bottom - 10, 10, accent);
        M5Cardputer.Display.drawFastHLine(right - 14, bottom, 14, accent);
        M5Cardputer.Display.drawFastVLine(right, bottom - 10, 10, accent);
        for (uint8_t block = 0; block < 4; ++block)
        {
            const int x = 88 + block * 17;
            const int y = 55 + ((block + frame) % 3) * 8;
            M5Cardputer.Display.fillRect(x, y, 9, 4, success && frame == 5 ? UI_GREEN : UI_MAGENTA);
        }
        if (!success && frame >= 4)
        {
            M5Cardputer.Display.drawFastHLine(91, 68, 58, UI_YELLOW);
            M5Cardputer.Display.drawPixel(98, 62, UI_YELLOW);
            M5Cardputer.Display.drawPixel(137, 76, UI_YELLOW);
        }
        present();
        waitForFrameBudget(frameStart, 60);
    }
    holdSystemAnimationFinalFrame();
}

void DisplayManager::animatePathfinder()
{
    constexpr uint8_t frameCount = 7;
    constexpr int centerX = 120;
    constexpr int centerY = 68;
    const int branchEnds[3][2] = {{55, 43}, {185, 43}, {185, 93}};

    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(TFT_CYAN, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("PATHFINDER");

        const bool branchesActive = frame >= 2;
        const bool nodesActive = frame >= 4;
        const bool resolved = frame == frameCount - 1;
        const uint16_t routeColor = resolved ? UI_GREEN : TFT_CYAN;
        const int pulseRadius = 8 + static_cast<int>(frame) * 3;

        M5Cardputer.Display.drawCircle(centerX, centerY, pulseRadius, frame & 1 ? UI_DIM : TFT_CYAN);
        M5Cardputer.Display.fillRect(centerX - 4, centerY - 4, 9, 9, resolved ? UI_GREEN : TFT_WHITE);
        if (branchesActive)
        {
            for (uint8_t branch = 0; branch < 3; ++branch)
            {
                const int endX = branchEnds[branch][0];
                const int endY = branchEnds[branch][1];
                const int reach = frame < 4 ? 35 + static_cast<int>(frame - 2) * 22 : 79;
                const int x = centerX + (endX - centerX) * reach / 79;
                const int y = centerY + (endY - centerY) * reach / 79;
                M5Cardputer.Display.drawLine(centerX, centerY, x, y, routeColor);
                if (frame >= 3)
                    M5Cardputer.Display.drawRect(x - 3, y - 3, 7, 7, branch == 1 ? TFT_WHITE : TFT_CYAN);
            }
        }
        if (nodesActive)
        {
            for (uint8_t node = 0; node < 4; ++node)
            {
                const int x = 43 + node * 51 + ((frame + node) & 1 ? 2 : 0);
                const int y = node & 1 ? 101 : 35;
                M5Cardputer.Display.fillRect(x, y, 4, 4, resolved ? UI_GREEN : UI_DIM);
            }
        }
        if (resolved)
        {
            M5Cardputer.Display.setTextColor(UI_GREEN, TFT_BLACK);
            M5Cardputer.Display.setCursor(78, 116); M5Cardputer.Display.print("PATH RESOLVED");
        }
        present();
        waitForFrameBudget(frameStart, 75);
    }
    holdSystemAnimationFinalFrame();
}

void DisplayManager::animateFloorMovement(uint8_t destinationFloor, int8_t direction)
{
    constexpr uint8_t frameCount = 8;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(TFT_CYAN, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("ROUTE");
        char destination[8] = {};
        snprintf(destination, sizeof(destination), "F%02u", static_cast<unsigned>(destinationFloor));
        M5Cardputer.Display.setCursor(205, 12); M5Cardputer.Display.print(destination);
        const int travel = static_cast<int>(frame) * 18;
        for (uint8_t rail = 0; rail < 3; ++rail)
        {
            const int y = 49 + rail * 17;
            const int offset = direction < 0 ? 216 - travel :
                direction > 0 ? travel - 18 : travel / 2;
            for (uint8_t segment = 0; segment < 4; ++segment)
            {
                const int x = (offset + segment * 62 + 240) % 240;
                M5Cardputer.Display.drawFastHLine(x, y, 25, rail == 1 ? UI_GREEN : UI_DIM);
            }
        }
        const int nodeX = direction < 0 ? 30 + (7 - frame) * 20 :
            direction > 0 ? 30 + frame * 20 : 100 + static_cast<int>(frame & 1) * 20;
        M5Cardputer.Display.fillCircle(nodeX, 83, 3, TFT_WHITE);
        M5Cardputer.Display.drawFastHLine(nodeX - 15, 83, 31, TFT_CYAN);
        M5Cardputer.Display.drawFastVLine(nodeX, 76, 15, TFT_CYAN);
        present();
        waitForFrameBudget(frameStart, 28);
    }
}

void DisplayManager::animateFloorMovementFailure()
{
    constexpr uint8_t frameCount = 7;
    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        clear();
        M5Cardputer.Display.setTextColor(UI_YELLOW, TFT_BLACK);
        M5Cardputer.Display.setTextSize(1);
        M5Cardputer.Display.setCursor(8, 12); M5Cardputer.Display.print("MOVE");
        const int travel = static_cast<int>(frame) * 17;
        for (uint8_t rail = 0; rail < 3; ++rail)
        {
            const int y = 49 + rail * 17;
            M5Cardputer.Display.drawFastHLine(27 + travel, y, 34, rail == 1 ? UI_YELLOW : UI_DIM);
            M5Cardputer.Display.drawFastHLine(90 + travel / 2, y, 22, UI_DIM);
        }
        const int barrierX = 170;
        M5Cardputer.Display.drawFastVLine(barrierX, 42, 66, frame >= 4 ? UI_YELLOW : UI_DIM);
        M5Cardputer.Display.drawFastVLine(barrierX + 3, 48, 54, frame >= 5 ? TFT_WHITE : UI_YELLOW);
        if (frame >= 5)
        {
            M5Cardputer.Display.drawFastHLine(27, 99, 125 - frame * 8, UI_DIM);
            M5Cardputer.Display.drawPixel(157 - frame * 3, 49, UI_YELLOW);
        }
        present();
        waitForFrameBudget(frameStart, 28);
    }
}

void DisplayManager::animateConnectionLoss()
{
    clear();
    M5Cardputer.Display.setTextColor(UI_RED, TFT_BLACK);
    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(38, 50); M5Cardputer.Display.print("CONNECTION LOST");
    for (uint8_t line = 0; line < 3; ++line)
    {
        const uint32_t frameStart = millis();
        M5Cardputer.Display.drawFastHLine(12 + line * 9, 30 + line * 19, 216 - line * 18, UI_RED);
        present();
        waitForFrameBudget(frameStart, 35);
    }
    delay(1000);
}

void DisplayManager::animatePlayerAction(PlayerActionVisual visual, FeedbackTone tone)
{
    // This is presentation-only. The combat result, including hit/miss, has
    // already been calculated; the animation deliberately does not encode it.
    if (presentationSuppressed_) return;
    (void)tone;
    constexpr uint8_t frameCount = 6;
    const uint32_t frameMs = visual == PlayerActionVisual::Sword ? 40 : 45;

    // Capture only the actor region. Replaying this snapshot before every
    // frame keeps the live Floor UI intact without a fullscreen clear or a
    // general framebuffer.
    M5Cardputer.Display.readRect(FLOOR_ANIMATION_X, FLOOR_ANIMATION_Y,
                                 FLOOR_ANIMATION_WIDTH, FLOOR_ANIMATION_HEIGHT,
                                 floorAnimationBackground_);

    for (uint8_t frame = 0; frame < frameCount; ++frame)
    {
        const uint32_t frameStart = millis();
        if (frame > 0)
            M5Cardputer.Display.pushImage(FLOOR_ANIMATION_X, FLOOR_ANIMATION_Y,
                                          FLOOR_ANIMATION_WIDTH, FLOOR_ANIMATION_HEIGHT,
                                          floorAnimationBackground_);
        M5Cardputer.Display.setClipRect(FLOOR_ANIMATION_X, FLOOR_ANIMATION_Y,
                                        FLOOR_ANIMATION_WIDTH, FLOOR_ANIMATION_HEIGHT);

        switch (visual)
        {
            case PlayerActionVisual::Zap:
            {
                const uint16_t magenta = UI_MAGENTA;
                if (frame == 0)
                {
                    M5Cardputer.Display.fillCircle(20, 62, 3, TFT_WHITE);
                    M5Cardputer.Display.drawFastHLine(13, 62, 14, magenta);
                }
                else
                {
                    const int head = 20 + static_cast<int>(frame) * 24;
                    const int points[][2] = {{20, 62}, {head - 25, 51}, {head - 16, 73},
                                             {head - 7, 47}, {head, 62}};
                    for (size_t i = 1; i < 5; ++i)
                        M5Cardputer.Display.drawLine(points[i - 1][0], points[i - 1][1],
                                                     points[i][0], points[i][1], magenta);
                    M5Cardputer.Display.drawPixel(head - 1, 61, TFT_WHITE);
                    M5Cardputer.Display.drawPixel(head, 62, TFT_WHITE);
                    if (frame >= 4)
                    {
                        M5Cardputer.Display.drawCircle(head, 62, 7, magenta);
                        M5Cardputer.Display.drawFastHLine(head - 11, 62, 23, TFT_WHITE);
                    }
                }
                break;
            }
            case PlayerActionVisual::Sword:
            {
                // One dominant cut grows from the lower-left to the upper-right.
                const int startX = 18;
                const int startY = 84;
                const int endX = 28 + static_cast<int>(frame) * 23;
                const int endY = 80 - static_cast<int>(frame) * 9;
                M5Cardputer.Display.drawLine(startX, startY, endX, endY, TFT_WHITE);
                if (frame >= 4)
                {
                    // Brief, weaker afterimage and a few digital fragments.
                    M5Cardputer.Display.drawLine(startX + 3, startY, endX + 2, endY + 2, UI_MAGENTA);
                    M5Cardputer.Display.drawPixel(endX + 4, endY, TFT_WHITE);
                    M5Cardputer.Display.drawPixel(endX - 5, endY + 6, UI_MAGENTA);
                    M5Cardputer.Display.drawPixel(endX + 8, endY + 5, UI_MAGENTA);
                }
                break;
            }
            case PlayerActionVisual::Banhammer:
            {
                // Every Black ICE visual uses the shared 48x48 actor canvas
                // anchored at FLOOR_ACTOR_SPRITE_X/Y. Strike its true visual
                // center, rather than the center of the broader overlay.
                constexpr int targetX = FLOOR_ACTOR_CENTER_X;
                constexpr int targetY = FLOOR_ACTOR_CENTER_Y;
                const int strikeBottom = FLOOR_ANIMATION_Y + 8 + static_cast<int>(frame) * 7;
                M5Cardputer.Display.drawFastVLine(targetX, FLOOR_ANIMATION_Y,
                                                   strikeBottom - FLOOR_ANIMATION_Y, UI_MAGENTA);
                M5Cardputer.Display.drawFastVLine(targetX + 3, FLOOR_ANIMATION_Y + 4,
                                                   strikeBottom - FLOOR_ANIMATION_Y - 4, TFT_WHITE);
                if (frame >= 3)
                {
                    M5Cardputer.Display.drawRect(targetX - 13, targetY - 4, 27, 12, UI_MAGENTA);
                    M5Cardputer.Display.drawFastHLine(targetX - 29, targetY + 2, 59, TFT_WHITE);
                    M5Cardputer.Display.drawRect(targetX - 37, targetY - 15, 5, 5, UI_MAGENTA);
                    M5Cardputer.Display.drawRect(targetX + 36, targetY + 8, 5, 5, UI_MAGENTA);
                    M5Cardputer.Display.drawLine(targetX - 25, targetY - 14,
                                                 targetX - 35, targetY - 23, UI_MAGENTA);
                    M5Cardputer.Display.drawLine(targetX + 25, targetY + 11,
                                                 targetX + 35, targetY + 20, UI_MAGENTA);
                }
                break;
            }
            case PlayerActionVisual::Slide:
            {
                // Presence -> duplicate -> phase shift -> trailing fragments.
                // The 42px signal spans 70% of the available actor height and
                // shifts 60px across the left gameplay region without aiming at ICE.
                constexpr int signalTop = 39;
                constexpr int signalHeight = 42;
                const int leadX = 32 + static_cast<int>(frame) * 12;
                const int ghostCount = frame == 0 ? 0 : frame == 1 ? 1 :
                    frame <= 3 ? 2 : frame == 4 ? 1 : 0;
                for (int ghost = ghostCount; ghost >= 0; --ghost)
                {
                    const int x = leadX - ghost * 12;
                    const uint16_t color = ghost == 0 ? TFT_WHITE : UI_MAGENTA;
                    M5Cardputer.Display.drawRect(x - 10, signalTop, 21, signalHeight, color);
                    M5Cardputer.Display.drawFastVLine(x, signalTop + 4, signalHeight - 8, color);
                    M5Cardputer.Display.drawFastHLine(x - 14, signalTop + 7, 29, color);
                    M5Cardputer.Display.drawFastHLine(x - 14, signalTop + signalHeight - 8, 29, color);
                }
                if (frame >= 3)
                {
                    const int fragmentX = 28 + static_cast<int>(frame - 3) * 10;
                    M5Cardputer.Display.drawFastHLine(fragmentX, 48, 18, UI_MAGENTA);
                    M5Cardputer.Display.drawFastHLine(fragmentX + 22, 61, 13, UI_MAGENTA);
                    M5Cardputer.Display.drawFastHLine(fragmentX - 5, 76, 10, UI_MAGENTA);
                    M5Cardputer.Display.drawRect(fragmentX + 39, 54, 5, 5, UI_MAGENTA);
                    M5Cardputer.Display.drawPixel(fragmentX + 49, 70, TFT_WHITE);
                }
                break;
            }
            case PlayerActionVisual::Hellbolt:
            {
                constexpr int targetX = FLOOR_ENEMY_RUNNER_CENTER_X;
                constexpr int targetY = FLOOR_ENEMY_RUNNER_CENTER_Y;
                const int boltX = 12 + static_cast<int>(frame) * 10;
                const int boltY = 80 - static_cast<int>(frame) * 5;
                M5Cardputer.Display.fillCircle(boltX, boltY, frame < 2 ? 3 : 5, UI_RED);
                M5Cardputer.Display.drawPixel(boltX, boltY, TFT_WHITE);
                if (frame >= 1)
                {
                    M5Cardputer.Display.drawFastHLine(boltX - 20, boltY + 4, 16, UI_RED);
                    M5Cardputer.Display.drawFastHLine(boltX - 12, boltY - 4, 9, UI_MAGENTA);
                    M5Cardputer.Display.drawPixel(boltX - 23, boltY + 8, UI_MAGENTA);
                }
                if (frame >= 4)
                {
                    M5Cardputer.Display.drawCircle(targetX, targetY, 11, UI_RED);
                    M5Cardputer.Display.fillCircle(targetX, targetY, 4, TFT_WHITE);
                    M5Cardputer.Display.drawPixel(targetX + 12, targetY - 7, UI_RED);
                    M5Cardputer.Display.drawPixel(targetX - 11, targetY + 8, UI_MAGENTA);
                }
                break;
            }
            case PlayerActionVisual::Vrizzbolt:
            {
                constexpr int targetX = FLOOR_ENEMY_RUNNER_CENTER_X;
                constexpr int targetY = FLOOR_ENEMY_RUNNER_CENTER_Y;
                constexpr int sourceX = 12;
                constexpr int sourceY = 80;
                const int headX = sourceX + static_cast<int>(frame) * 10;
                int previousTopY = sourceY;
                int previousBottomY = sourceY;
                for (int step = 0; step <= 5; ++step)
                {
                    const int x = sourceX + ((headX - sourceX) * step) / 5;
                    const int baselineY = sourceY + ((targetY - sourceY) * step) / 5;
                    const int offset = ((step + frame) & 1) == 0 ? 13 : -13;
                    const int topY = baselineY + offset;
                    const int bottomY = baselineY - offset;
                    if (step > 0)
                    {
                        M5Cardputer.Display.drawLine(sourceX + ((headX - sourceX) * (step - 1)) / 5,
                                                     previousTopY, x, topY, UI_MAGENTA);
                        M5Cardputer.Display.drawLine(sourceX + ((headX - sourceX) * (step - 1)) / 5,
                                                     previousBottomY, x, bottomY, TFT_CYAN);
                    }
                    previousTopY = topY;
                    previousBottomY = bottomY;
                }
                if (frame >= 4)
                {
                    M5Cardputer.Display.fillCircle(targetX, targetY, 4, TFT_WHITE);
                    M5Cardputer.Display.drawPixel(targetX + 10, targetY - 11, UI_MAGENTA);
                    M5Cardputer.Display.drawPixel(targetX - 10, targetY + 11, TFT_CYAN);
                }
                break;
            }
            case PlayerActionVisual::Nervescrub:
            {
                constexpr int targetX = FLOOR_ENEMY_RUNNER_CENTER_X;
                constexpr int targetY = FLOOR_ENEMY_RUNNER_CENTER_Y;
                const int orbX = 12 + static_cast<int>(frame) * 10;
                const int orbY = 80 - static_cast<int>(frame) * 5;
                M5Cardputer.Display.drawCircle(orbX, orbY, 8, TFT_LIGHTGREY);
                M5Cardputer.Display.fillCircle(orbX, orbY, 3, TFT_WHITE);
                M5Cardputer.Display.drawPixel(orbX - 11, orbY + 6, UI_MAGENTA);
                M5Cardputer.Display.drawPixel(orbX + 10, orbY - 7, UI_MAGENTA);
                if (frame >= 4)
                {
                    M5Cardputer.Display.drawCircle(targetX, targetY, 12, UI_MAGENTA);
                    M5Cardputer.Display.drawFastHLine(targetX - 15, targetY, 31, TFT_WHITE);
                    M5Cardputer.Display.drawFastVLine(targetX, targetY - 12, 25, UI_MAGENTA);
                }
                break;
            }
            case PlayerActionVisual::Superglue:
            {
                constexpr int targetX = FLOOR_ENEMY_RUNNER_CENTER_X;
                constexpr int targetY = FLOOR_ENEMY_RUNNER_CENTER_Y;
                const int blobX = 12 + static_cast<int>(frame) * 10;
                const int blobY = 78 - static_cast<int>(frame) * 5 + static_cast<int>(frame & 1) * 4;
                const int blobW = 15 + static_cast<int>(frame & 1) * 7;
                M5Cardputer.Display.fillRoundRect(blobX - blobW / 2, blobY - 7, blobW, 15, 5, UI_RED);
                M5Cardputer.Display.drawFastHLine(blobX - 12, blobY + 9, 11, UI_RED);
                if (frame >= 4)
                {
                    M5Cardputer.Display.fillRect(targetX - 16, targetY - 6, 33, 14, UI_RED);
                    M5Cardputer.Display.drawFastVLine(targetX + 5, targetY - 13, 27, UI_RED);
                    M5Cardputer.Display.drawPixel(targetX - 20, targetY + 8, UI_RED);
                    M5Cardputer.Display.drawPixel(targetX + 20, targetY - 9, UI_RED);
                }
                break;
            }
            case PlayerActionVisual::PoisonFlatline:
            {
                constexpr int sourceX = 12;
                constexpr int sourceY = 80;
                constexpr int targetX = FLOOR_ENEMY_RUNNER_CENTER_X;
                constexpr int targetY = FLOOR_ENEMY_RUNNER_CENTER_Y;
                const int beamEndX = sourceX + ((targetX - sourceX) * static_cast<int>(frame + 1)) / 5;
                const int beamEndY = sourceY + ((targetY - sourceY) * static_cast<int>(frame + 1)) / 5;
                M5Cardputer.Display.fillCircle(sourceX, sourceY, 2, UI_GREEN);
                if (frame < 5)
                {
                    M5Cardputer.Display.drawLine(sourceX, sourceY, beamEndX, beamEndY, UI_GREEN);
                    if (frame >= 2) M5Cardputer.Display.drawLine(sourceX, sourceY + 1, beamEndX, beamEndY + 1, TFT_WHITE);
                }
                if (frame >= 4)
                {
                    M5Cardputer.Display.drawFastHLine(targetX - 13, targetY, 27, UI_GREEN);
                    M5Cardputer.Display.drawFastVLine(targetX, targetY - 10, 21, UI_GREEN);
                    M5Cardputer.Display.drawPixel(targetX + 11, targetY - 9, UI_GREEN);
                    M5Cardputer.Display.drawPixel(targetX - 10, targetY + 10, UI_GREEN);
                }
                break;
            }
            case PlayerActionVisual::DeckKrash:
            {
                constexpr int payloadX[] = {12, 23, 35, 47, 57, FLOOR_ENEMY_RUNNER_CENTER_X};
                constexpr int payloadY[] = {83, 72, 61, 53, 49, FLOOR_ENEMY_RUNNER_CENTER_Y};
                const int x = payloadX[frame];
                const int y = payloadY[frame];
                M5Cardputer.Display.fillRect(x - 6, y - 5, 13, 11, UI_MAGENTA);
                M5Cardputer.Display.drawRect(x - 7, y - 6, 15, 13, TFT_WHITE);
                M5Cardputer.Display.drawPixel(x - 9, y - 8, UI_RED);
                if (frame >= 4)
                {
                    constexpr int targetX = FLOOR_ENEMY_RUNNER_CENTER_X;
                    constexpr int targetY = FLOOR_ENEMY_RUNNER_CENTER_Y;
                    M5Cardputer.Display.drawRect(targetX - 14, targetY - 10, 29, 21, UI_MAGENTA);
                    M5Cardputer.Display.drawFastHLine(targetX - 20, targetY + 1, 41, TFT_WHITE);
                    M5Cardputer.Display.drawFastVLine(targetX, targetY - 15, 31, UI_RED);
                    M5Cardputer.Display.drawPixel(targetX + 18, targetY - 12, UI_MAGENTA);
                }
                break;
            }
            case PlayerActionVisual::None:
            default:
                break;
        }

        M5Cardputer.Display.clearClipRect();
        present();
        waitForFrameBudget(frameStart, frameMs);
    }

    // Leave the internal display clean even before the following Result redraw.
    M5Cardputer.Display.pushImage(FLOOR_ANIMATION_X, FLOOR_ANIMATION_Y,
                                  FLOOR_ANIMATION_WIDTH, FLOOR_ANIMATION_HEIGHT,
                                  floorAnimationBackground_);
}
