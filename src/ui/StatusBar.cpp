/**
 * @file StatusBar.cpp
 * @brief Top Status Bar implementation for CYD Android OS Shell
 */

#include "StatusBar.h"
#include "hal/DisplayHAL.h"
#include "hal/StorageHAL.h"
#include "core/Config.h"
#include "core/TimeService.h"
#include <WiFi.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace ui {

StatusBar::StatusBar()
    : m_display(nullptr),
      m_sprite(nullptr),
      m_useSprite(true),
      m_spriteAllocated(false),
      m_visible(true),
      m_dirty(true),
      m_customClockPinned(false),
      m_heapKb(192),
      m_sdMounted(false),
      m_batteryPercent(100),
      m_lastTelemetryPollMs(0) {
    strncpy(m_clock, "12:00", sizeof(m_clock) - 1);
    m_clock[sizeof(m_clock) - 1] = '\0';
}

StatusBar::~StatusBar() {
    if (m_sprite) {
        m_sprite->deleteSprite();
        delete m_sprite;
        m_sprite = nullptr;
    }
}

void StatusBar::init(TFT_eSPI* display, bool useSprite) {
    m_display = (display != nullptr) ? display : &hal::getDisplay();
    m_useSprite = useSprite;

    if (m_useSprite && !m_spriteAllocated) {
        m_sprite = new TFT_eSprite(m_display);
        m_sprite->setColorDepth(16);
        void* ptr = m_sprite->createSprite(STATUS_BAR_WIDTH, STATUS_BAR_HEIGHT);
        if (ptr != nullptr) {
            m_spriteAllocated = true;
        } else {
            // RAM allocation failed; fall back cleanly to direct TFT drawing
            delete m_sprite;
            m_sprite = nullptr;
            m_spriteAllocated = false;
        }
    }

    m_dirty = true;
}

void StatusBar::update(const char* clock, uint32_t heapKb, bool sdMounted, uint8_t batteryPercent) {
    if (clock != nullptr && strcmp(m_clock, clock) != 0) {
        strncpy(m_clock, clock, sizeof(m_clock) - 1);
        m_clock[sizeof(m_clock) - 1] = '\0';
        m_customClockPinned = true;
        m_dirty = true;
    }

    if (m_heapKb != heapKb) {
        m_heapKb = heapKb;
        m_dirty = true;
    }

    if (m_sdMounted != sdMounted) {
        m_sdMounted = sdMounted;
        m_dirty = true;
    }

    if (m_batteryPercent != batteryPercent) {
        m_batteryPercent = (batteryPercent > 100) ? 100 : batteryPercent;
        m_dirty = true;
    }
}

void StatusBar::pollTelemetry() {
    uint32_t now = millis();
    m_lastTelemetryPollMs = now;

    // 1. Live ESP32 Free Heap
    uint32_t currentHeapKb = esp_get_free_heap_size() / 1024;
    if (abs((long)currentHeapKb - (long)m_heapKb) >= 1) {
        m_heapKb = currentHeapKb;
        m_dirty = true;
    }

    // 2. Storage Subsystem Mount Status
    bool currentSD = StorageHAL::isSDMounted();
    if (m_sdMounted != currentSD) {
        m_sdMounted = currentSD;
        m_dirty = true;
    }

    // 3. Dynamic NTP Local Time Clock (or Uptime Clock before Wi-Fi NTP sync)
    core::TimeService::poll();
    if (!m_customClockPinned) {
        char autoClock[16];
        core::TimeService::formatHHMM(autoClock, sizeof(autoClock));
        if (strcmp(m_clock, autoClock) != 0) {
            strncpy(m_clock, autoClock, sizeof(m_clock) - 1);
            m_clock[sizeof(m_clock) - 1] = '\0';
            m_dirty = true;
        }
    }
}

void StatusBar::render(bool forceRedraw) {
    if (!m_visible) {
        return;
    }

    if (!forceRedraw && !m_dirty) {
        return;
    }

    if (m_display == nullptr) {
        m_display = &hal::getDisplay();
    }

    int16_t curW = m_display->width();
    if (m_useSprite && m_sprite != nullptr && m_spriteAllocated && m_sprite->width() != curW) {
        m_sprite->deleteSprite();
        m_spriteAllocated = (m_sprite->createSprite(curW, STATUS_BAR_HEIGHT) != nullptr);
    }

    if (m_useSprite && m_spriteAllocated && m_sprite != nullptr) {
        // Offscreen double-buffer rendering: zero flicker, zero tearing
        renderToTarget(*m_sprite, 0, 0);
        m_sprite->pushSprite(STATUS_BAR_X, STATUS_BAR_Y);
    } else {
        // Direct-to-display rendering (fallback or low-memory mode)
        renderToTarget(*m_display, STATUS_BAR_X, STATUS_BAR_Y);
    }

    m_dirty = false;
}

void StatusBar::renderToTarget(TFT_eSPI& target, int16_t originX, int16_t originY) {
    int16_t barW = (m_display != nullptr) ? m_display->width() : STATUS_BAR_WIDTH;

    // 1. Background fill across current screen width (240 or 320) x 20
    target.fillRect(originX, originY, barW, STATUS_BAR_HEIGHT, STATUS_BG_COLOR);

    // 2. Accent divider line at bottom of status bar (Y=19)
    target.drawFastHLine(originX, originY + STATUS_BAR_HEIGHT - 1, barW, STATUS_LINE_COLOR);

    target.setTextDatum(TL_DATUM);
    target.setTextFont(2);

    const char* fsStr = m_sdMounted ? "SD:OK" : (StorageHAL::isLittleFSMounted() ? "LFS:OK" : "NO:FS");
    char ramBuf[24];
    char batBuf[10];
    snprintf(batBuf, sizeof(batBuf), "%u%%", m_batteryPercent);

    if (barW >= 300) {
        // Landscape 320px layout
        target.setTextColor(STATUS_CLOCK_COLOR, STATUS_BG_COLOR);
        target.drawString(m_clock, originX + 6, originY + 2);

        target.setTextColor(0xFFE0, STATUS_BG_COLOR);
        target.setTextColor(0x3EE0, STATUS_BG_COLOR);
        target.drawString("cydOS", originX + 54, originY + 2);

        snprintf(ramBuf, sizeof(ramBuf), "RAM: %luK", (unsigned long)m_heapKb);
        target.setTextColor(STATUS_RAM_COLOR, STATUS_BG_COLOR);
        target.drawString(ramBuf, originX + 130, originY + 2);

        target.setTextColor(STATUS_STORAGE_COLOR, STATUS_BG_COLOR);
        target.drawString(fsStr, originX + 220, originY + 2);

        drawBatteryGlyph(target, originX + 266, originY + 5, m_batteryPercent);
        target.setTextColor(STATUS_BATTERY_COLOR, STATUS_BG_COLOR);
        target.drawString(batBuf, originX + 280, originY + 2);
    } else {
        // Portrait 240px layout
        target.setTextColor(STATUS_CLOCK_COLOR, STATUS_BG_COLOR);
        target.drawString(m_clock, originX + 4, originY + 2);

        target.setTextColor(0xFFE0, STATUS_BG_COLOR);
        target.setTextColor(0x3EE0, STATUS_BG_COLOR);
        target.drawString("V1", originX + 46, originY + 2);

        snprintf(ramBuf, sizeof(ramBuf), "%luK", (unsigned long)m_heapKb);
        target.setTextColor(STATUS_RAM_COLOR, STATUS_BG_COLOR);
        target.drawString(ramBuf, originX + 76, originY + 2);

        // Live 802.11 Wi-Fi indicator glyph (X=118): Green=Connected, Cyan=Enabled, Gray=Off
        uint16_t wfCol = 0x4208;
        if (Config::getInstance().isWifiEnabled()) {
            wfCol = (WiFi.status() == WL_CONNECTED) ? 0x07E0 : 0x07FF;
        }
        int16_t wx = originX + 118, wy = originY + 14;
        target.fillCircle(wx, wy, 1, wfCol);
        target.drawLine(wx - 3, wy - 3, wx, wy - 5, wfCol);
        target.drawLine(wx, wy - 5, wx + 3, wy - 3, wfCol);
        target.drawLine(wx - 5, wy - 6, wx, wy - 9, wfCol);
        target.drawLine(wx, wy - 9, wx + 5, wy - 6, wfCol);

        target.setTextColor(STATUS_STORAGE_COLOR, STATUS_BG_COLOR);
        target.drawString(fsStr, originX + 132, originY + 2);

        drawBatteryGlyph(target, originX + 186, originY + 5, m_batteryPercent);
        target.setTextColor(STATUS_BATTERY_COLOR, STATUS_BG_COLOR);
        target.drawString(batBuf, originX + 202, originY + 2);
    }
}

void StatusBar::drawBatteryGlyph(TFT_eSPI& target, int16_t x, int16_t y, uint8_t percent) {
    // Outer body: 11x10 outline
    target.drawRect(x, y, 11, 10, TFT_WHITE);
    // Positive terminal nub: 2x4 on right
    target.fillRect(x + 11, y + 3, 2, 4, TFT_WHITE);

    // Fill level: 7px max width
    uint8_t fillW = (percent * 7) / 100;
    if (fillW > 7) fillW = 7;

    uint16_t fillColor = (percent > 20) ? STATUS_BATTERY_COLOR : TFT_RED;
    if (fillW > 0) {
        target.fillRect(x + 2, y + 2, fillW, 6, fillColor);
    }
}

void StatusBar::setClock(const char* clockStr) {
    if (clockStr != nullptr) {
        strncpy(m_clock, clockStr, sizeof(m_clock) - 1);
        m_clock[sizeof(m_clock) - 1] = '\0';
        m_customClockPinned = true;
        m_dirty = true;
    }
}

void StatusBar::setHeapKb(uint32_t heapKb) {
    if (m_heapKb != heapKb) {
        m_heapKb = heapKb;
        m_dirty = true;
    }
}

void StatusBar::setSDMounted(bool mounted) {
    if (m_sdMounted != mounted) {
        m_sdMounted = mounted;
        m_dirty = true;
    }
}

void StatusBar::setBatteryPercent(uint8_t percent) {
    uint8_t clamped = (percent > 100) ? 100 : percent;
    if (m_batteryPercent != clamped) {
        m_batteryPercent = clamped;
        m_dirty = true;
    }
}

} // namespace ui
