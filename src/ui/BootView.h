/**
 * @file BootView.h
 * @brief Boot Splash Screen & Android Bugdroid Logo Animation for CYD Android OS Shell
 *
 * Target Hardware: ESP32 Cheap Yellow Display (ESP32-2432S028, 320x240 LCD)
 * Milestone: M2 - Android UI Shell & Navigation (Part A: Boot Splash & Logo)
 * Authoritative Specifications: PROJECT.md (§ Architecture, § Feature Inventory F01..F03)
 */

#ifndef UI_BOOT_VIEW_H
#define UI_BOOT_VIEW_H

#include <Arduino.h>
#include <TFT_eSPI.h>

namespace ui {

/**
 * @class BootView
 * @brief Renders the Android boot splash screen with vector Bugdroid logo,
 * branding typography, and animated initialization progress bar.
 */
class BootView {
public:
    // UI Layout Geometry Constants (Portrait 240x320)
    static constexpr int32_t SCREEN_WIDTH        = 240;
    static constexpr int32_t SCREEN_HEIGHT       = 320;

    static constexpr int32_t PROGRESS_BAR_X      = 20;
    static constexpr int32_t PROGRESS_BAR_Y      = 190;
    static constexpr int32_t PROGRESS_BAR_W      = 200;
    static constexpr int32_t PROGRESS_BAR_H      = 14;

    static constexpr int32_t PROGRESS_FILL_X     = 22;
    static constexpr int32_t PROGRESS_FILL_Y     = 192;
    static constexpr int32_t PROGRESS_FILL_MAX_W = 196;
    static constexpr int32_t PROGRESS_FILL_H     = 10;

    static constexpr int32_t STATUS_TEXT_X       = 20;
    static constexpr int32_t STATUS_TEXT_Y       = 215;

    // Color Palette Constants (16-bit RGB565)
    static constexpr uint16_t COLOR_BG            = 0x0000; // TFT_BLACK
    static constexpr uint16_t COLOR_ANDROID_GREEN = 0x07E0; // Android Green (TFT_GREEN / #3DDC84)
    static constexpr uint16_t COLOR_TITLE         = 0x07E0; // "ANDROID OS" Title
    static constexpr uint16_t COLOR_SUBTITLE      = 0xFFFF; // "ESP32 CYD Edition" Subtitle
    static constexpr uint16_t COLOR_FRAME         = 0x7BEF; // Progress bar frame outline
    static constexpr uint16_t COLOR_STATUS_TEXT   = 0x7BEF; // Subsystem status text
    static constexpr uint16_t COLOR_FOOTER_TEXT   = 0x4208; // Dark slate hardware specs

    // Default animation timing
    static constexpr uint32_t DEFAULT_MIN_SPLASH_MS = 1800; // 1.8 seconds nominal boot time

    /**
     * @brief Construct a new BootView instance.
     * @param display Reference to initialized TFT_eSPI driver
     */
    explicit BootView(TFT_eSPI &display);

    /**
     * @brief Perform full screen initial render of Boot Splash.
     * Clears screen to black, draws Android Bugdroid vector robot,
     * renders branding text, draws progress bar frame, and renders initial status.
     */
    void render();

    /**
     * @brief Update boot initialization progress and subsystem status label.
     * Clamps percentage to [0..100] and performs high-speed partial redraw
     * to eliminate display flicker.
     *
     * @param percent Initialization progress percentage (0..100)
     * @param statusText Subsystem status message (optional, e.g. "Mounting LittleFS...")
     */
    void setProgress(int percent, const char* statusText = nullptr);

    /**
     * @brief Non-blocking tick update for autonomous animation and transition timing.
     */
    void update();

    /**
     * @brief Reset boot view state to initial 0% progress.
     */
    void reset();

    /**
     * @brief Check if boot splash sequence is complete and ready to transition.
     * @return true if progress is 100% and minimum splash duration has elapsed
     */
    bool isComplete() const { return m_isComplete; }

    /**
     * @brief Get current progress percentage (0..100).
     */
    int getProgress() const { return m_progress; }

    /**
     * @brief Get current subsystem status message.
     */
    const char* getStatusText() const { return m_statusText; }

    /**
     * @brief Configure minimum splash duration in milliseconds.
     * @param durationMs Milliseconds (e.g. 1500..2500)
     */
    void setMinDuration(uint32_t durationMs) { m_minDurationMs = durationMs; }

    /**
     * @brief Get minimum splash duration in milliseconds.
     */
    uint32_t getMinDuration() const { return m_minDurationMs; }

private:
    /**
     * @brief Render vector Bugdroid logo using geometric primitives.
     * @param centerX Horizontal center coordinate (160)
     * @param centerY Vertical center coordinate (28)
     * @param color 16-bit RGB565 color
     */
    void drawBugdroidLogo(int32_t centerX, int32_t centerY, uint16_t color);

    /**
     * @brief Redraw only the inner progress fill bar and status text without full screen refresh.
     */
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
