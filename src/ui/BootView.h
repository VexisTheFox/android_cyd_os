/**
 * @file BootView.h
 * @brief Stable Release Boot Splash Screen with Custom Fursona Logo & "Powered by Android" Branding
 */

#ifndef UI_BOOT_VIEW_H
#define UI_BOOT_VIEW_H

#include <Arduino.h>
#include <TFT_eSPI.h>

namespace ui {

class BootView {
public:
    // UI Layout Geometry Constants (Portrait 240x320)
    static constexpr int32_t SCREEN_WIDTH        = 240;
    static constexpr int32_t SCREEN_HEIGHT       = 320;

    static constexpr int32_t PROGRESS_BAR_X      = 28;
    static constexpr int32_t PROGRESS_BAR_Y      = 192;
    static constexpr int32_t PROGRESS_BAR_W      = 184;
    static constexpr int32_t PROGRESS_BAR_H      = 12;

    static constexpr int32_t PROGRESS_FILL_X     = 30;
    static constexpr int32_t PROGRESS_FILL_Y     = 194;
    static constexpr int32_t PROGRESS_FILL_MAX_W = 180;
    static constexpr int32_t PROGRESS_FILL_H     = 8;

    static constexpr int32_t STATUS_TEXT_X       = 20;
    static constexpr int32_t STATUS_TEXT_Y       = 212;

    // Color Palette Constants (16-bit RGB565)
    static constexpr uint16_t COLOR_BG            = 0x0000;
    static constexpr uint16_t COLOR_ANDROID_GREEN = 0x3EE0;
    static constexpr uint16_t COLOR_TITLE         = 0x3EE0;
    static constexpr uint16_t COLOR_SUBTITLE      = 0xFFFF;
    static constexpr uint16_t COLOR_FRAME         = 0x4A69;
    static constexpr uint16_t COLOR_STATUS_TEXT   = 0x9CD3;
    static constexpr uint16_t COLOR_FOOTER_TEXT   = 0xAD55;

    // Fast boot splash timing
    static constexpr uint32_t DEFAULT_MIN_SPLASH_MS = 1500;

    explicit BootView(TFT_eSPI &display);

    void render();
    void setProgress(int percent, const char* statusText = nullptr);
    void update();
    void reset();

    bool isComplete() const { return m_isComplete; }
    int getProgress() const { return m_progress; }
    const char* getStatusText() const { return m_statusText; }

    void setMinDuration(uint32_t durationMs) { m_minDurationMs = durationMs; }
    uint32_t getMinDuration() const { return m_minDurationMs; }

private:
    void drawFursonaLogo(int32_t topX, int32_t topY);
    void drawPoweredByAndroidFooter();
    void drawProgressOnly();

    TFT_eSPI &m_display;
    int m_progress;
    char m_statusText[64];
    bool m_isComplete;
    bool m_hasRenderedInitial;
    uint32_t m_startTimeMs;
    uint32_t m_minDurationMs;
    int32_t m_lastDrawnFillW;
};

} // namespace ui

#endif // UI_BOOT_VIEW_H
