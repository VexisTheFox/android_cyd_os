/**
 * @file StatusBar.h
 * @brief Top Status Bar for CYD Android OS Shell (ESP32 Cheap Yellow Display)
 *
 * Dimensions: 320x20 pixels (Y = 0..19).
 * Dark Android Material theme background (0x10A2) with subtle accent divider (0x07E0).
 * Displays live system telemetry:
 *  - Clock: Left-aligned time display ("12:00", uptime-derived or RTC)
 *  - RAM: Center free heap gauge ("RAM: %luK")
 *  - Storage: Storage mount indicator ("SD:OK" or "LFS:OK")
 *  - Battery: Graphical battery icon with percentage level ("100%")
 *
 * Supports offscreen 16-bit sprite double-buffering (12.8 KB) for 100% flicker-free updates,
 * with automatic fallback to dirty-rect direct TFT rendering if RAM is constrained.
 */

#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <Arduino.h>
#include <TFT_eSPI.h>

namespace ui {

// Status Bar Geometry Constants (conforms to hal::STATUS_BAR_HEIGHT)
constexpr int32_t STATUS_BAR_X          = 0;
constexpr int32_t STATUS_BAR_Y          = 0;
constexpr int32_t STATUS_BAR_WIDTH      = 240;
constexpr int32_t STATUS_BAR_HEIGHT     = 20;

// Color Palette (Android Dark Theme & Accent)
constexpr uint16_t STATUS_BG_COLOR      = 0x10A2; // Dark tech slate
constexpr uint16_t STATUS_LINE_COLOR    = 0x07E0; // Android accent green (accent line at Y=19)
constexpr uint16_t STATUS_CLOCK_COLOR   = 0xFFFF; // Bright white
constexpr uint16_t STATUS_RAM_COLOR     = 0x07FF; // Tech cyan
constexpr uint16_t STATUS_STORAGE_COLOR = 0xFFE0; // Amber yellow
constexpr uint16_t STATUS_BATTERY_COLOR = 0x07E0; // Battery green

class StatusBar {
public:
    StatusBar();
    ~StatusBar();

    /**
     * @brief Initialize StatusBar display reference and optional offscreen sprite buffer.
     * @param display Pointer to TFT_eSPI instance (defaults to hal::getDisplay() if nullptr)
     * @param useSprite If true, attempts to allocate 12.8KB offscreen sprite for zero-flicker blits
     */
    void init(TFT_eSPI* display = nullptr, bool useSprite = true);

    /**
     * @brief Update telemetry states manually.
     * @param clock Time string (e.g. "12:00", "14:35")
     * @param heapKb Free heap in KB (e.g. 192)
     * @param sdMounted MicroSD card mount status
     * @param batteryPercent Battery level (0..100)
     */
    void update(const char* clock, uint32_t heapKb, bool sdMounted, uint8_t batteryPercent = 100);

    /**
     * @brief Auto-sample hardware telemetry (ESP heap, uptime clock, storage status).
     * Typically invoked once every 1000ms.
     */
    void pollTelemetry();

    /**
     * @brief Render the status bar to display or sprite buffer.
     * @param forceRedraw If true, redraws regardless of dirty state
     */
    void render(bool forceRedraw = false);

    // Individual state setters
    void setClock(const char* clockStr);
    void setHeapKb(uint32_t heapKb);
    void setSDMounted(bool mounted);
    void setBatteryPercent(uint8_t percent);
    void setVisible(bool visible) { m_visible = visible; }

    // State getters
    const char* getClock() const { return m_clock; }
    uint32_t getHeapKb() const { return m_heapKb; }
    bool isSDMounted() const { return m_sdMounted; }
    uint8_t getBatteryPercent() const { return m_batteryPercent; }
    bool isVisible() const { return m_visible; }
    bool isDirty() const { return m_dirty; }

private:
    TFT_eSPI*    m_display;
    TFT_eSprite* m_sprite;
    bool         m_useSprite;
    bool         m_spriteAllocated;
    bool         m_visible;
    bool         m_dirty;
    bool         m_customClockPinned;

    char         m_clock[16];
    uint32_t     m_heapKb;
    bool         m_sdMounted;
    uint8_t      m_batteryPercent;

    uint32_t     m_lastTelemetryPollMs;

    void renderToTarget(TFT_eSPI& target, int16_t originX, int16_t originY);
    void drawBatteryGlyph(TFT_eSPI& target, int16_t x, int16_t y, uint8_t percent);
};

} // namespace ui

#endif // STATUS_BAR_H
