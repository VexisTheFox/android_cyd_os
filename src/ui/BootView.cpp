/**
 * @file BootView.cpp
 * @brief Stable Release Boot Splash Screen with Custom Fursona Logo & "Powered by Android" Footer
 */

#include "BootView.h"
#include "FursonaLogo.h"
#include "core/Version.h"
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
    strncpy(m_statusText, "Starting cydOS...", sizeof(m_statusText) - 1);
    m_statusText[sizeof(m_statusText) - 1] = '\0';
}

void BootView::reset() {
    m_progress = 0;
    m_isComplete = false;
    m_hasRenderedInitial = false;
    m_startTimeMs = millis();
    m_lastDrawnFillW = 0;
    strncpy(m_statusText, "Starting cydOS...", sizeof(m_statusText) - 1);
    m_statusText[sizeof(m_statusText) - 1] = '\0';
}

void BootView::drawFursonaLogo(int32_t topX, int32_t topY) {
    bool prevSwap = m_display.getSwapBytes();
    m_display.setSwapBytes(true);
    m_display.pushImage(topX, topY, FURSONA_LOGO_W, FURSONA_LOGO_H, FURSONA_LOGO_RGB565);
    m_display.setSwapBytes(prevSwap);
}

void BootView::drawPoweredByAndroidFooter() {
    // "Powered by" small label above "Android" + mini green Bugdroid head
    m_display.setTextDatum(TC_DATUM);
    m_display.setTextFont(1);
    m_display.setTextColor(COLOR_FOOTER_TEXT, COLOR_BG);
    m_display.drawString("Powered by", SCREEN_WIDTH / 2, 280);

    // Mini Android Bugdroid head dome at (85, 303)
    int32_t cx = 85;
    int32_t cy = 304;
    m_display.drawLine(cx - 4, cy - 6, cx - 7, cy - 10, COLOR_ANDROID_GREEN);
    m_display.drawLine(cx + 4, cy - 6, cx + 7, cy - 10, COLOR_ANDROID_GREEN);
    m_display.fillCircle(cx, cy, 7, COLOR_ANDROID_GREEN);
    m_display.fillRect(cx - 8, cy + 1, 17, 8, COLOR_BG);
    m_display.fillCircle(cx - 3, cy - 3, 1, COLOR_BG);
    m_display.fillCircle(cx + 3, cy - 3, 1, COLOR_BG);

    // "Android" wordmark next to Bugdroid emblem
    m_display.setTextDatum(TL_DATUM);
    m_display.setTextFont(2);
    m_display.setTextColor(COLOR_SUBTITLE, COLOR_BG);
    m_display.drawString("Android", 98, 292);
}

void BootView::render() {
    if (m_startTimeMs == 0) {
        m_startTimeMs = millis();
    }

    m_display.fillScreen(COLOR_BG);

    // 1. Custom Fursona Boot Logo (108x142 RGB565 PROGMEM centered at Y=8..149)
    int32_t logoX = (SCREEN_WIDTH - FURSONA_LOGO_W) / 2;
    drawFursonaLogo(logoX, 8);

    // 2. Stable Release OS Title ("cydOS V1.0")
    m_display.setTextDatum(TC_DATUM);
    m_display.setTextColor(COLOR_TITLE, COLOR_BG);
    m_display.setTextFont(4);
    m_display.drawString(CYDOS_FULL_NAME, SCREEN_WIDTH / 2, 158);
    m_display.setTextDatum(TL_DATUM);

    // 3. Progress Bar Frame & Fill
    m_display.drawRoundRect(PROGRESS_BAR_X, PROGRESS_BAR_Y, PROGRESS_BAR_W, PROGRESS_BAR_H, 4, COLOR_FRAME);
    int32_t fillW = (PROGRESS_FILL_MAX_W * m_progress) / 100;
    if (fillW > 0) {
        m_display.fillRect(PROGRESS_FILL_X, PROGRESS_FILL_Y, fillW, PROGRESS_FILL_H, COLOR_ANDROID_GREEN);
    }
    m_lastDrawnFillW = fillW;

    // 4. Subsystem Status Message (centered)
    m_display.setTextDatum(TC_DATUM);
    m_display.setTextColor(COLOR_STATUS_TEXT, COLOR_BG);
    m_display.setTextFont(1);
    m_display.drawString(m_statusText, SCREEN_WIDTH / 2, STATUS_TEXT_Y);
    m_display.setTextDatum(TL_DATUM);

    // 5. Bottom "Powered by Android" Footer
    drawPoweredByAndroidFooter();

    m_hasRenderedInitial = true;
}

void BootView::setProgress(int percent, const char* statusText) {
    int clamped = percent;
    if (clamped < 0) clamped = 0;
    if (clamped > 100) clamped = 100;
    m_progress = clamped;

    if (statusText != nullptr && statusText[0] != '\0') {
        strncpy(m_statusText, statusText, sizeof(m_statusText) - 1);
        m_statusText[sizeof(m_statusText) - 1] = '\0';
    }

    if (m_progress >= 100) {
        m_isComplete = true;
    }

    if (!m_hasRenderedInitial) {
        render();
    } else {
        drawProgressOnly();
    }
}

void BootView::drawProgressOnly() {
    int32_t fillW = (PROGRESS_FILL_MAX_W * m_progress) / 100;
    if (fillW > m_lastDrawnFillW) {
        m_display.fillRect(PROGRESS_FILL_X + m_lastDrawnFillW, PROGRESS_FILL_Y,
                           fillW - m_lastDrawnFillW, PROGRESS_FILL_H, COLOR_ANDROID_GREEN);
    } else if (fillW < m_lastDrawnFillW) {
        m_display.fillRect(PROGRESS_FILL_X + fillW, PROGRESS_FILL_Y,
                           m_lastDrawnFillW - fillW, PROGRESS_FILL_H, COLOR_BG);
    }
    m_lastDrawnFillW = fillW;

    m_display.fillRect(10, STATUS_TEXT_Y - 1, SCREEN_WIDTH - 20, 12, COLOR_BG);
    m_display.setTextDatum(TC_DATUM);
    m_display.setTextColor(COLOR_STATUS_TEXT, COLOR_BG);
    m_display.setTextFont(1);
    m_display.drawString(m_statusText, SCREEN_WIDTH / 2, STATUS_TEXT_Y);
    m_display.setTextDatum(TL_DATUM);
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
