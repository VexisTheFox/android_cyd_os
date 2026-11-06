/**
 * @file SettingsApp.h
 * @brief Fast, Compact Android Material Dark Scrollable Settings & About App (Development Build)
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "core/App.h"
#include "core/Config.h"
#include "hal/BoardHAL.h"
#include "hal/DisplayHAL.h"

namespace apps {

class SettingsApp : public core::App {
public:
    SettingsApp();
    virtual ~SettingsApp() = default;

    const char* getName() const override { return "Settings"; }
    const char* getIcon() const override { return "ICON_SETTINGS"; }
    const char* getCategory() const override { return "System"; }

    void onCreate() override;
    void onResume() override;
    void onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) override;
    void onTouch(uint16_t x, uint16_t y, bool pressed) override;
    void onPause() override;
    void onDestroy() override;

    void scrollToAbout();

private:
    static constexpr int HEADER_H     = 24;
    static constexpr int CONTENT_H    = 586;
    static constexpr int VISIBLE_H    = 274 - HEADER_H; // 250 px
    static constexpr int MAX_SCROLL_Y = CONTENT_H - VISIBLE_H; // 302 px

    bool     m_needsFullRedraw;
    bool     m_needsScrollRedraw;
    int      m_scrollY;
    bool     m_isScrolling;
    int      m_touchStartY;
    int      m_scrollStartY;
    uint16_t m_lastTouchX;
    uint16_t m_lastTouchY;
    uint32_t m_lastScrollRenderMs;

    uint8_t  m_brightness;
    bool     m_isDraggingSlider;
    uint8_t  m_ledState;
    bool     m_soundEnabled;
    bool     m_pinEnabled;
    char     m_pinCode[8];
    bool     m_wifiEnabled;
    char     m_wifiSsid[33];
    char     m_wifiPass[64];
    uint8_t  m_scanCount;
    char     m_scanSsids[2][33];
    int32_t  m_scanRssi[2];
    bool     m_scanOpen[2];
    bool     m_logInfoEnabled;
    bool     m_logWarnEnabled;
    bool     m_devModeEnabled;
    uint8_t  m_devTapCount;
    bool     m_logsJustDeleted;
    uint16_t m_sleepTimeoutSec;
    bool     m_confirmReset;
    bool     m_wasPressed;

    // Cached telemetry strings so scrolling does ZERO Flash/WiFi/NVS queries per frame!
    uint32_t m_cachedRamKb;
    uint32_t m_cachedFlashKb;
    bool     m_cachedWifiConn;
    char     m_cachedWifiLine[40];
    char     m_cachedRegionLine[40];

    void refreshCachedStatus();
    void drawHeader(TFT_eSPI &display, int vx, int vy, int vw);
    void drawScrollContent(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void renderCardsToCanvas(TFT_eSPI &canvas, int cardX, int baseY, int cardW, int bandH);
    void handleTapAt(uint16_t x, uint16_t y);
    void connectToWifi();
    void scanWifiNetworks();
    void performFactoryResetAll();
};

} // namespace apps
