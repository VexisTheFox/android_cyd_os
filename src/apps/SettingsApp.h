/**
 * @file SettingsApp.h
 * @brief Android Category-Based Full-Screen Settings & System Configuration App
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "core/App.h"
#include "core/Config.h"
#include "hal/BoardHAL.h"
#include "hal/DisplayHAL.h"

namespace apps {

enum class SettingsPage : uint8_t {
    MAIN_MENU     = 0,
    WIFI          = 1,
    DISPLAY_SOUND = 2,
    SECURITY      = 3,
    REGION_TIME   = 4,
    SYSTEM_LOGS   = 5,
    UPDATE_RESET  = 6,
    ABOUT         = 7
};

class SettingsApp : public core::App {
public:
    SettingsApp();
    virtual ~SettingsApp() = default;

    const char* getName() const override { return "Settings"; }
    const char* getIcon() const override { return "ICON_SETTINGS"; }
    const char* getCategory() const override { return "System"; }

    void onCreate() override;
    void onResume() override;
    void onUpdate() override;
    void onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) override;
    void onTouch(uint16_t x, uint16_t y, bool pressed) override;
    void onPause() override;
    void onDestroy() override;
    bool onBackPressed() override;

    void scrollToAbout();

private:
    static constexpr int HEADER_H = 26;
    static constexpr int ABOUT_CONTENT_H = 720;

    SettingsPage m_currentPage;
    bool         m_needsFullRedraw;
    bool         m_wasPressed;
    bool         m_isDraggingSlider;
    bool         m_isDraggingScroll;
    uint16_t     m_lastTouchX;
    uint16_t     m_lastTouchY;
    int16_t      m_touchStartY;
    int16_t      m_scrollStartY;
    int16_t      m_aboutScrollY;
    uint32_t     m_lastClockTickMs;

    uint8_t      m_brightness;
    uint8_t      m_ledState;
    bool         m_soundEnabled;
    bool         m_pinEnabled;
    char         m_pinCode[8];
    bool         m_wifiEnabled;
    char         m_wifiSsid[33];
    char         m_wifiPass[64];
    uint8_t      m_scanCount;
    char         m_scanSsids[3][33];
    int32_t      m_scanRssi[3];
    bool         m_scanOpen[3];
    bool         m_logInfoEnabled;
    bool         m_logWarnEnabled;
    bool         m_devModeEnabled;
    bool         m_logsJustDeleted;
    uint16_t     m_sleepTimeoutSec;
    bool         m_confirmReset;

    // Cached telemetry
    uint32_t     m_cachedRamKb;
    uint32_t     m_cachedMaxBlockKb;
    uint32_t     m_cachedFlashUsedKb;
    uint32_t     m_cachedFlashTotalKb;
    bool         m_cachedWifiConn;
    char         m_cachedWifiLine[44];
    char         m_cachedRegionLine[40];

    void refreshCachedStatus();
    void openPage(SettingsPage page);

    void drawHeader(TFT_eSPI &display, int vx, int vy, int vw);
    void drawMainMenu(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void drawWifiPage(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void drawDisplaySoundPage(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void drawSecurityPage(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void drawRegionTimePage(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void drawSystemLogsPage(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void drawUpdateResetPage(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void drawAboutPage(TFT_eSPI &display, int vx, int vy, int vw, int vh);

    void handleTapAt(uint16_t x, uint16_t y);
    void handleMainMenuTap(uint16_t x, uint16_t y);
    void handleWifiTap(uint16_t x, uint16_t y);
    void handleDisplaySoundTap(uint16_t x, uint16_t y);
    void handleSecurityTap(uint16_t x, uint16_t y);
    void handleRegionTimeTap(uint16_t x, uint16_t y);
    void handleSystemLogsTap(uint16_t x, uint16_t y);
    void handleUpdateResetTap(uint16_t x, uint16_t y);
    void handleAboutTap(uint16_t x, uint16_t y);

    void connectToWifi();
    void scanWifiNetworks();
    void performFactoryResetAll();
};

} // namespace apps
