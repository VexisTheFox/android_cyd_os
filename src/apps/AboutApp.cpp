/**
 * @file AboutApp.cpp
 * @brief Implementation of Feature F21 About System Application
 */

#include "AboutApp.h"
#include "hal/BoardHAL.h"

namespace apps {

static constexpr uint16_t COLOR_BG        = 0x0821; // Deep Tech Slate (test_f21_05)
static constexpr uint16_t COLOR_BUGDROID  = 0x3EE0; // Android Green
static constexpr uint16_t COLOR_TEXT_MAIN = 0xFFFF; // Crisp White
static constexpr uint16_t COLOR_TEXT_MUTED= 0xCE79; // Slate Gray
static constexpr uint16_t COLOR_ACCENT    = 0x07FF; // Tech Cyan
static constexpr uint16_t COLOR_DIVIDER   = 0x324B; // Dark Divider Line

AboutApp::AboutApp()
    : m_needsFullRedraw(true)
    , m_lastTelemetryPoll(0)
    , m_cachedUptimeSec(0)
    , m_cachedFreeHeapKb(0)
    , m_cachedMinHeapKb(0)
    , m_cachedSdMounted(false)
    , m_isTouched(false) {
}

void AboutApp::onCreate() {
    core::App::onCreate();
    m_needsFullRedraw = true;
    m_lastTelemetryPoll = 0;
    m_cachedUptimeSec = millis() / 1000;
    m_cachedFreeHeapKb = ESP.getFreeHeap() / 1024;
    m_cachedMinHeapKb = esp_get_minimum_free_heap_size() / 1024;
    m_cachedSdMounted = StorageHAL::isSDMounted();
    m_isTouched = false;
}

void AboutApp::onResume() {
    core::App::onResume();
    m_needsFullRedraw = true;
    m_isTouched = false;
}

void AboutApp::onUpdate() {
    // Only update telemetry when requested by user touch
}

void AboutApp::onUpdate(uint32_t deltaMs) {
    (void)deltaMs;
    // Only update telemetry when requested by user touch
}

void AboutApp::onTouch(uint16_t x, uint16_t y, bool pressed) {
    core::App::onTouch(x, y, pressed);
    if (pressed && !m_isTouched) {
        m_isTouched = true;
        // User tapped screen: force immediate telemetry resample and audio confirmation
        m_needsFullRedraw = true;
        hal::beep(20);
    } else if (!pressed) {
        m_isTouched = false;
    }
}

void AboutApp::onPause() {
    core::App::onPause();
    m_isTouched = false;
}

void AboutApp::onDestroy() {
    core::App::onDestroy();
    m_isTouched = false;
}

void AboutApp::drawBugdroidMascot(TFT_eSPI &display, int16_t cx, int16_t cy) {
    // Android Mascot Bugdroid Head
    display.fillCircle(cx, cy, 10, COLOR_BUGDROID);
    display.fillRect(cx - 10, cy, 20, 10, COLOR_BG); // Cut lower half
    // Eyes
    display.fillCircle(cx - 4, cy - 3, 1, COLOR_BG);
    display.fillCircle(cx + 4, cy - 3, 1, COLOR_BG);
    // Antennae
    display.drawLine(cx - 4, cy - 8, cx - 8, cy - 13, COLOR_BUGDROID);
    display.drawLine(cx + 4, cy - 8, cx + 8, cy - 13, COLOR_BUGDROID);
}

void AboutApp::onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    if (m_needsFullRedraw) {
        // 1. Clear Viewport
        display.fillRect(vx, vy, vw, vh, COLOR_BG);

        // 2. Header: Bugdroid Icon and Title
        drawBugdroidMascot(display, vx + 22, vy + 18);

        display.setTextDatum(TL_DATUM);
        display.setTextColor(COLOR_TEXT_MAIN, COLOR_BG);
        display.setTextFont(2);
        display.drawString("cydOS V1.0 (Stable)", vx + 38, vy + 6);

        display.setTextColor(COLOR_BUGDROID, COLOR_BG);
        display.setTextFont(1);
        display.drawString("v1.0.0 (ESP32-2432S028)", vx + 40, vy + 24);

        // 3. Top Divider
        display.drawFastHLine(vx + 8, vy + 36, vw - 16, COLOR_DIVIDER);

        // 4. Static System Specifications (240px Portrait layout)
        display.setTextFont(1);
        display.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);

        display.drawString("Board: ESP32-2432S028 (CYD 2 USB)", vx + 10, vy + 46);
        display.drawString("SoC: ESP32-D0WDQ6-V3 @ 240 MHz", vx + 10, vy + 62);
        display.drawString("Flash: 4 MB | SRAM: 320 KB", vx + 10, vy + 78);

        // 5. Bottom Static Specs
        display.drawString("LCD: 240x320 ST7789 (55 MHz)", vx + 10, vy + 150);
        display.drawString("Touch: XPT2046 Resistive SPI", vx + 10, vy + 166);

        // 6. Bottom Divider
        display.drawFastHLine(vx + 8, vy + 184, vw - 16, COLOR_DIVIDER);

        // 7. Interactive Footer Hint
        display.setTextColor(COLOR_ACCENT, COLOR_BG);
        display.setTextDatum(MC_DATUM);
        display.drawString("Tap screen to refresh telemetry", vx + (vw / 2), vy + 202);

        display.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
        display.drawString("Press [<] Back or [O] Home to exit", vx + (vw / 2), vy + 220);

        // Render Telemetry only on initial load or user touch
        renderDynamicTelemetry(display, vx, vy, vw, vh);

        m_needsFullRedraw = false;
    }
}

void AboutApp::renderDynamicTelemetry(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    (void)vh;
    display.setTextDatum(TL_DATUM);
    display.setTextFont(1);
    display.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);

    char buf[80];

    // Heap Line (Y = vy + 96)
    display.fillRect(vx + 10, vy + 96, vw - 20, 10, COLOR_BG);
    snprintf(buf, sizeof(buf), "Heap: %lu KB Free (Min: %lu KB)",
             (unsigned long)(ESP.getFreeHeap() / 1024),
             (unsigned long)(esp_get_minimum_free_heap_size() / 1024));
    display.drawString(buf, vx + 10, vy + 96);

    // Storage Line (Y = vy + 112)
    display.fillRect(vx + 10, vy + 112, vw - 20, 10, COLOR_BG);
    snprintf(buf, sizeof(buf), "LFS: %lu/%lu KB | SD: %s",
             (unsigned long)(StorageHAL::getLittleFSUsedBytes() / 1024),
             (unsigned long)(StorageHAL::getLittleFSTotalBytes() / 1024),
             StorageHAL::isSDMounted() ? "Mounted" : "None");
    display.drawString(buf, vx + 10, vy + 112);

    // Uptime Line (Y = vy + 128)
    display.fillRect(vx + 10, vy + 128, vw - 20, 10, COLOR_BG);
    uint32_t uptimeSec = millis() / 1000;
    uint32_t days = uptimeSec / 86400;
    uint32_t hours = (uptimeSec % 86400) / 3600;
    uint32_t mins = (uptimeSec % 3600) / 60;
    uint32_t secs = uptimeSec % 60;
    snprintf(buf, sizeof(buf), "Uptime: %lud %02luh %02lum %02lus",
             (unsigned long)days, (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    display.drawString(buf, vx + 10, vy + 128);
}

} // namespace apps
