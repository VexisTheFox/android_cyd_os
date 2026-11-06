/**
 * @file BootView.cpp
 * @brief Boot Splash Screen & Android Bugdroid Logo Animation for CYD Android OS Shell
 *
 * Target Hardware: ESP32 Cheap Yellow Display (ESP32-2432S028, 320x240 LCD)
 * Milestone: M2 - Android UI Shell & Navigation (Part A: Boot Splash & Logo)
 * Authoritative Specifications: PROJECT.md (§ Architecture, § Feature Inventory F01..F03)
 */

#include "BootView.h"
#include <cstring>

namespace ui {

BootView::BootView(TFT_eSPI &display)
    : m_display(display)
    , m_progress(0)
    , m_isComplete(false)
    , m_hasRenderedInitial(false)
    , m_startTimeMs(0)
    , m_minDurationMs(DEFAULT_MIN_SPLASH_MS)
    , m_lastDrawnFillW(0)
{
    strncpy(m_statusText, "Initializing hardware...", sizeof(m_statusText) - 1);
    m_statusText[sizeof(m_statusText) - 1] = '\0';
}

void BootView::reset() {
    m_progress = 0;
    m_isComplete = false;
    m_hasRenderedInitial = false;
    m_startTimeMs = millis();
    m_lastDrawnFillW = 0;
    strncpy(m_statusText, "Initializing hardware...", sizeof(m_statusText) - 1);
    m_statusText[sizeof(m_statusText) - 1] = '\0';
}

void BootView::drawBugdroidLogo(int32_t centerX, int32_t centerY, uint16_t color) {
    // 1. Antennae (2px thick angled segments pointing outward)
    // Left antenna
    m_display.drawLine(centerX - 10, centerY - 14, centerX - 17, centerY - 24, color);
    m_display.drawLine(centerX - 9,  centerY - 14, centerX - 16, centerY - 24, color);

    // Right antenna
    m_display.drawLine(centerX + 10, centerY - 14, centerX + 17, centerY - 24, color);
    m_display.drawLine(centerX + 9,  centerY - 14, centerX + 16, centerY - 24, color);

    // 2. Head Dome (Filled circle with lower half clipped)
    int32_t headRadius = 18;
    int32_t headY = centerY - 2;
    m_display.fillCircle(centerX, headY, headRadius, color);
    m_display.fillRect(centerX - headRadius - 1, headY + 1, (headRadius * 2) + 2, headRadius + 2, COLOR_BG);

    // 3. Eyes (Small cutout circles in background black)
    int32_t eyeY = headY - 6;
    int32_t eyeSpacing = 7;
    int32_t eyeRadius = 2;
    m_display.fillCircle(centerX - eyeSpacing, eyeY, eyeRadius, COLOR_BG);
    m_display.fillCircle(centerX + eyeSpacing, eyeY, eyeRadius, COLOR_BG);

    // 4. Torso / Body (Rounded rectangle, 2px collar gap below head)
    int32_t bodyW = 36;
    int32_t bodyH = 22;
    int32_t bodyX = centerX - (bodyW / 2);
    int32_t bodyY = headY + 3;
    m_display.fillRoundRect(bodyX, bodyY, bodyW, bodyH, 4, color);

    // 5. Left & Right Appendages / Arms (Pill shaped)
    int32_t armW = 4;
    int32_t armH = 16;
    int32_t armGap = 3;
    m_display.fillRoundRect(bodyX - armGap - armW, bodyY, armW, armH, 2, color);
    m_display.fillRoundRect(bodyX + bodyW + armGap, bodyY, armW, armH, 2, color);
}

void BootView::render() {
    if (m_startTimeMs == 0) {
        m_startTimeMs = millis();
    }

    // F01.02: Clear full display screen to black
    m_display.fillScreen(COLOR_BG);

    // Render graphical Android Bugdroid vector robot
    drawBugdroidLogo(SCREEN_WIDTH / 2, 65, COLOR_ANDROID_GREEN);

    // F01.01 & F01.03: "ANDROID OS" in Android green (0x07E0) using Font 4
    m_display.setTextDatum(TC_DATUM);
    m_display.setTextColor(COLOR_TITLE, COLOR_BG);
    m_display.setTextFont(4);
    m_display.drawString("ANDROID OS", SCREEN_WIDTH / 2, 105);

    // F01.01: "ESP32 CYD Edition" Subtitle in white using Font 2
    m_display.setTextColor(COLOR_SUBTITLE, COLOR_BG);
    m_display.setTextFont(2);
    m_display.drawString("ESP32 CYD Edition", SCREEN_WIDTH / 2, 136);

    // Development Build Badge
    m_display.setTextColor(0xFFE0, COLOR_BG);
    m_display.setTextFont(1);
    m_display.drawString("[ DEVELOPMENT BUILD - v2.0-ENG ]", SCREEN_WIDTH / 2, 158);
    m_display.setTextDatum(TL_DATUM);

    // F01.04: Progress bar outer frame outline
    m_display.drawRect(PROGRESS_BAR_X, PROGRESS_BAR_Y, PROGRESS_BAR_W, PROGRESS_BAR_H, COLOR_FRAME);

    // F02.02: Progress bar inner fill
    int32_t fillW = (int32_t)((float)PROGRESS_FILL_MAX_W * ((float)m_progress / 100.0f));
    if (fillW > 0) {
        m_display.fillRect(PROGRESS_FILL_X, PROGRESS_FILL_Y, fillW, PROGRESS_FILL_H, COLOR_ANDROID_GREEN);
    }
    m_lastDrawnFillW = fillW;

    // F01.05: Subsystem status message string
    m_display.setTextColor(COLOR_STATUS_TEXT, COLOR_BG);
    m_display.setTextFont(2);
    m_display.drawString(m_statusText, STATUS_TEXT_X, STATUS_TEXT_Y);

    // Subdued hardware information line at bottom
    m_display.setTextColor(COLOR_FOOTER_TEXT, COLOR_BG);
    m_display.setTextFont(1);
    m_display.setTextDatum(TC_DATUM);
    m_display.drawString("DEVELOPMENT BUILD | CYD-2432S028 | FreeRTOS", SCREEN_WIDTH / 2, 295);
    m_display.setTextDatum(TL_DATUM); // Reset text datum

    m_hasRenderedInitial = true;
}

void BootView::setProgress(int percent, const char* statusText) {
    // B02.01 & B02.02: Clamp percentage to [0..100]
    int clamped = percent;
    if (clamped < 0) clamped = 0;
    if (clamped > 100) clamped = 100;
    m_progress = clamped;

    // Update status text if non-null and non-empty
    if (statusText != nullptr && statusText[0] != '\0') {
        strncpy(m_statusText, statusText, sizeof(m_statusText) - 1);
        m_statusText[sizeof(m_statusText) - 1] = '\0';
    }

    // F02.03: 100% completion flag
    if (m_progress >= 100) {
        m_isComplete = true;
    }

    // High performance redraw
    if (!m_hasRenderedInitial) {
        render();
    } else {
        drawProgressOnly();
    }
}

void BootView::drawProgressOnly() {
    // 1. Partial fill bar update without full screen flicker
    int32_t fillW = (int32_t)((float)PROGRESS_FILL_MAX_W * ((float)m_progress / 100.0f));
    if (fillW > m_lastDrawnFillW) {
        m_display.fillRect(PROGRESS_FILL_X + m_lastDrawnFillW, PROGRESS_FILL_Y,
                           fillW - m_lastDrawnFillW, PROGRESS_FILL_H, COLOR_ANDROID_GREEN);
    } else if (fillW < m_lastDrawnFillW) {
        m_display.fillRect(PROGRESS_FILL_X + fillW, PROGRESS_FILL_Y,
                           m_lastDrawnFillW - fillW, PROGRESS_FILL_H, COLOR_BG);
    }
    m_lastDrawnFillW = fillW;

    // 2. Erase only status text line bounding box and redraw
    m_display.fillRect(STATUS_TEXT_X, STATUS_TEXT_Y, PROGRESS_BAR_W, 18, COLOR_BG);
    m_display.setTextColor(COLOR_STATUS_TEXT, COLOR_BG);
    m_display.setTextFont(2);
    m_display.drawString(m_statusText, STATUS_TEXT_X, STATUS_TEXT_Y);
}

void BootView::update() {
    if (m_startTimeMs == 0) {
        m_startTimeMs = millis();
    }

    uint32_t elapsed = millis() - m_startTimeMs;
    if (m_progress >= 100 && elapsed >= m_minDurationMs) {
        m_isComplete = true;
    }
}

} // namespace ui
