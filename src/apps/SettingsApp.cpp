/**
 * @file SettingsApp.cpp
 * @brief Fast, Compact Scrollable Settings, Wi-Fi Station & Factory Reset Implementation
 *
 * Performance optimizations:
 *   - All Flash/LittleFS, Wi-Fi, and Region strings are cached in RAM (m_cached*) so
 *     scrolling performs zero blocking LittleFS.totalBytes() or WiFi driver calls.
 *   - Compact cards with concise Font 1 text and 84px sprite bands for 60 FPS scrolling.
 */

#include "SettingsApp.h"
#include "core/AppRegistry.h"
#include "core/Logger.h"
#include "core/TimeService.h"
#include "core/OtaRecovery.h"
#include "hal/StorageHAL.h"
#include "ui/UIManager.h"
#include "ui/KeyboardView.h"
#include <WiFi.h>

namespace apps {

static constexpr uint16_t COLOR_BG      = 0x0821;
static constexpr uint16_t COLOR_HEADER  = 0x10A2;
static constexpr uint16_t COLOR_CARD    = 0x18E3;
static constexpr uint16_t COLOR_DIVIDER = 0x3186;
static constexpr uint16_t COLOR_ACCENT  = 0x07E0;
static constexpr uint16_t COLOR_CYAN    = 0x07FF;
static constexpr uint16_t COLOR_AMBER   = 0xFFE0;
static constexpr uint16_t COLOR_DANGER  = 0xF800;
static constexpr uint16_t COLOR_WARNING = 0xFD20;
static constexpr uint16_t COLOR_WHITE   = 0xFFFF;
static constexpr uint16_t COLOR_MUTED   = 0x9CD3;

SettingsApp::SettingsApp()
    : m_needsFullRedraw(true)
    , m_needsScrollRedraw(true)
    , m_scrollY(0)
    , m_isScrolling(false)
    , m_touchStartY(0)
    , m_scrollStartY(0)
    , m_lastTouchX(0)
    , m_lastTouchY(0)
    , m_lastScrollRenderMs(0)
    , m_brightness(CONFIG_DEFAULT_BRIGHTNESS)
    , m_isDraggingSlider(false)
    , m_ledState(0)
    , m_soundEnabled(true)
    , m_pinEnabled(false)
    , m_wifiEnabled(false)
    , m_scanCount(0)
    , m_logInfoEnabled(true)
    , m_logWarnEnabled(true)
    , m_devModeEnabled(false)
    , m_devTapCount(0)
    , m_logsJustDeleted(false)
    , m_sleepTimeoutSec(CONFIG_DEFAULT_SLEEP_TIMEOUT)
    , m_confirmReset(false)
    , m_wasPressed(false)
    , m_cachedRamKb(180)
    , m_cachedFlashKb(1408)
    , m_cachedWifiConn(false) {
    strncpy(m_pinCode, "1234", sizeof(m_pinCode));
    m_wifiSsid[0] = '\0';
    m_wifiPass[0] = '\0';
    m_cachedWifiLine[0] = '\0';
    m_cachedRegionLine[0] = '\0';
    memset(m_scanSsids, 0, sizeof(m_scanSsids));
    m_scanRssi[0] = m_scanRssi[1] = 0;
    m_scanOpen[0] = m_scanOpen[1] = false;
}

void SettingsApp::refreshCachedStatus() {
    m_cachedRamKb = ESP.getFreeHeap() / 1024;
    if (m_cachedFlashKb == 0) {
        size_t totalB = StorageHAL::getStorageTotalBytes();
        m_cachedFlashKb = (totalB > 0) ? (uint32_t)(totalB / 1024) : 1408;
    }

    m_cachedWifiConn = m_wifiEnabled && (WiFi.status() == WL_CONNECTED);
    if (m_cachedWifiConn) {
        IPAddress ip = WiFi.localIP();
        snprintf(m_cachedWifiLine, sizeof(m_cachedWifiLine), "OK: %.12s (%u.%u.%u.%u)",
                 WiFi.SSID().c_str(), ip[0], ip[1], ip[2], ip[3]);
    } else if (m_wifiEnabled) {
        snprintf(m_cachedWifiLine, sizeof(m_cachedWifiLine), "SSID: %.20s",
                 m_wifiSsid[0] ? m_wifiSsid : "(none)");
    } else {
        snprintf(m_cachedWifiLine, sizeof(m_cachedWifiLine), "Wi-Fi Radio OFF [DEV]");
    }

    char regSum[28];
    core::TimeService::formatRegionSummary(regSum, sizeof(regSum));
    snprintf(m_cachedRegionLine, sizeof(m_cachedRegionLine), "Reg: %.18s [%s] (Edit)",
             regSum, core::TimeService::isSynced() ? "NTP" : "LOC");
}

void SettingsApp::onCreate() {
    m_state = core::AppState::CREATED;
    Config::getInstance().begin();
    size_t totalB = StorageHAL::getStorageTotalBytes();
    m_cachedFlashKb = (totalB > 0) ? (uint32_t)(totalB / 1024) : 1408;
    onResume();
}

void SettingsApp::onResume() {
    m_state = core::AppState::RUNNING;
    m_brightness      = Config::getInstance().getBrightness();
    m_soundEnabled    = Config::getInstance().isSoundEnabled();
    m_pinEnabled      = Config::getInstance().isPinEnabled();
    strncpy(m_pinCode, Config::getInstance().getPinCode(), sizeof(m_pinCode) - 1);
    m_pinCode[sizeof(m_pinCode) - 1] = '\0';
    m_wifiEnabled     = Config::getInstance().isWifiEnabled();
    strncpy(m_wifiSsid, Config::getInstance().getWifiSsid(), sizeof(m_wifiSsid) - 1);
    m_wifiSsid[sizeof(m_wifiSsid) - 1] = '\0';
    strncpy(m_wifiPass, Config::getInstance().getWifiPass(), sizeof(m_wifiPass) - 1);
    m_wifiPass[sizeof(m_wifiPass) - 1] = '\0';
    m_logInfoEnabled  = Config::getInstance().isLogInfoEnabled();
    m_logWarnEnabled  = Config::getInstance().isLogWarnEnabled();
    m_devModeEnabled  = Config::getInstance().isDevModeEnabled();
    m_sleepTimeoutSec = Config::getInstance().getSleepTimeoutSec();
    m_logsJustDeleted = false;
    m_confirmReset    = false;
    m_isScrolling     = false;
    m_isDraggingSlider = false;
    m_wasPressed      = false;
    refreshCachedStatus();
    m_needsFullRedraw = true;
    m_needsScrollRedraw = true;
}

void SettingsApp::scrollToAbout() {
    m_scrollY = MAX_SCROLL_Y;
    m_needsFullRedraw = true;
    m_needsScrollRedraw = true;
}

void SettingsApp::onPause() {
    m_state = core::AppState::PAUSED;
    Config::getInstance().setBrightness(m_brightness, false);
    Config::getInstance().setSoundEnabled(m_soundEnabled, false);
    Config::getInstance().setPinEnabled(m_pinEnabled, false);
    Config::getInstance().setPinCode(m_pinCode, false);
    Config::getInstance().setWifiEnabled(m_wifiEnabled, false);
    Config::getInstance().setWifiSsid(m_wifiSsid, false);
    Config::getInstance().setWifiPass(m_wifiPass, false);
    Config::getInstance().setLogInfoEnabled(m_logInfoEnabled, false);
    Config::getInstance().setLogWarnEnabled(m_logWarnEnabled, false);
    Config::getInstance().setDevModeEnabled(m_devModeEnabled, false);
    Config::getInstance().setSleepTimeoutSec(m_sleepTimeoutSec, false);
    Config::getInstance().save();
}

void SettingsApp::onDestroy() {
    if (Config::getInstance().isDirty()) {
        Config::getInstance().save();
    }
    m_state = core::AppState::DESTROYED;
}

void SettingsApp::performFactoryResetAll() {
    hal::beep(80);
    TFT_eSPI& d = hal::getDisplay();
    d.fillRect(16, 110, 208, 70, 0x3800);
    d.drawRoundRect(16, 110, 208, 70, 6, COLOR_WHITE);
    d.setTextDatum(MC_DATUM);
    d.setTextFont(2);
    d.setTextColor(COLOR_WHITE, 0x3800);
    d.drawString("FACTORY RESETTING...", 120, 134);
    d.setTextFont(1);
    d.setTextColor(COLOR_AMBER, 0x3800);
    d.drawString("Wiping NVS, Wi-Fi, PIN, Logs & Apps", 120, 158);
    d.setTextDatum(TL_DATUM);

    // 1. Wipe Wi-Fi state & disconnect radio
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);

    // 2. Wipe NVS Preferences (clears PIN, Wi-Fi, Region, Wallpaper, DevMode, Brightness)
    Config::getInstance().resetDefaults();

    // 3. Clear kernel.log on Flash and SD
    core::Logger::clearLog();

    // 4. Remove user-installed scripts in /apps/ and restore factory default apps
    std::vector<String> appFiles;
    if (StorageHAL::listDirectory("/apps", appFiles, nullptr)) {
        for (size_t i = 0; i < appFiles.size(); i++) {
            StorageHAL::deleteFile(appFiles[i].c_str());
        }
    }
    StorageHAL::forceProvisionDefaultApps();

    delay(400);
    ESP.restart();
}

void SettingsApp::connectToWifi() {
    if (m_wifiSsid[0] == '\0') {
        ui::UIManager::showToast("Tap SSID to enter network", 1300);
        return;
    }
    m_wifiEnabled = true;
    Config::getInstance().setWifiEnabled(true, false);
    Config::getInstance().setWifiSsid(m_wifiSsid, false);
    Config::getInstance().setWifiPass(m_wifiPass, true);

    ui::UIManager::showToast("Connecting Wi-Fi...", 1100);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    delay(20);
    if (m_wifiPass[0] != '\0') {
        WiFi.begin(m_wifiSsid, m_wifiPass);
    } else {
        WiFi.begin(m_wifiSsid);
    }

    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs < 4000)) {
        delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
        hal::beep(45);
        core::TimeService::syncNtpNow(2000);
        ui::UIManager::showToast("Wi-Fi & NTP Connected!", 1800);
    } else {
        ui::UIManager::showToast("Connecting in background", 1400);
    }
    refreshCachedStatus();
    m_needsFullRedraw = true;
    m_needsScrollRedraw = true;
}

void SettingsApp::scanWifiNetworks() {
    m_wifiEnabled = true;
    Config::getInstance().setWifiEnabled(true, true);
    ui::UIManager::showToast("Scanning Wi-Fi...", 1000);
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, false);
    m_scanCount = 0;
    if (n > 0) {
        for (int i = 0; i < n && m_scanCount < 2; i++) {
            String s = WiFi.SSID(i);
            if (s.length() == 0) continue;
            strncpy(m_scanSsids[m_scanCount], s.c_str(), 32);
            m_scanSsids[m_scanCount][32] = '\0';
            m_scanRssi[m_scanCount] = WiFi.RSSI(i);
            m_scanOpen[m_scanCount] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
            m_scanCount++;
        }
        char msg[36];
        snprintf(msg, sizeof(msg), "Found %d network(s)", n);
        ui::UIManager::showToast(msg, 1300);
    } else {
        ui::UIManager::showToast("No networks found", 1300);
    }
    WiFi.scanDelete();
    refreshCachedStatus();
    m_needsScrollRedraw = true;
}

void SettingsApp::drawHeader(TFT_eSPI &display, int vx, int vy, int vw) {
    display.fillRect(vx, vy, vw, HEADER_H, COLOR_HEADER);
    display.drawFastHLine(vx, vy + HEADER_H - 1, vw, COLOR_DIVIDER);

    display.setTextDatum(TL_DATUM);
    display.setTextFont(2);
    display.setTextColor(COLOR_WHITE, COLOR_HEADER);
    display.drawString("Settings", vx + 6, vy + 4);

    display.setTextFont(1);
    display.setTextColor(COLOR_AMBER, COLOR_HEADER);
    display.drawString("[DEV]", vx + 70, vy + 8);

    display.fillRoundRect(vx + 154, vy + 2, 38, 19, 3, 0x2166);
    display.drawRoundRect(vx + 154, vy + 2, 38, 19, 3, COLOR_CYAN);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_CYAN, 0x2166);
    display.drawString("UP^", vx + 173, vy + 11);

    display.fillRoundRect(vx + 196, vy + 2, 38, 19, 3, 0x2166);
    display.drawRoundRect(vx + 196, vy + 2, 38, 19, 3, COLOR_CYAN);
    display.drawString("DNv", vx + 215, vy + 11);
    display.setTextDatum(TL_DATUM);
}

void SettingsApp::renderCardsToCanvas(TFT_eSPI &canvas, int cardX, int baseY, int cardW, int bandH) {
    char buf[48];
    canvas.setTextFont(1);

    // =========================================================================
    // CARD 1: BRIGHTNESS (y: 4..56, h: 52)
    // =========================================================================
    int c1Y = baseY + 4;
    if (c1Y + 52 >= 0 && c1Y < bandH) {
        canvas.fillRoundRect(cardX, c1Y, cardW, 52, 5, COLOR_CARD);
        canvas.drawRoundRect(cardX, c1Y, cardW, 52, 5, COLOR_DIVIDER);

        canvas.setTextDatum(TL_DATUM);
        canvas.setTextColor(COLOR_CYAN, COLOR_CARD);
        snprintf(buf, sizeof(buf), "Brightness: %u%%", m_brightness);
        canvas.drawString(buf, cardX + 8, c1Y + 6);

        canvas.fillRoundRect(cardX + 8, c1Y + 20, 28, 24, 3, 0x18C3);
        canvas.drawRoundRect(cardX + 8, c1Y + 20, 28, 24, 3, COLOR_DIVIDER);
        canvas.setTextDatum(MC_DATUM);
        canvas.setTextColor(COLOR_WHITE, 0x18C3);
        canvas.drawString("-", cardX + 22, c1Y + 32);

        int trackX = cardX + 42;
        int trackY = c1Y + 28;
        int trackW = 140;
        canvas.fillRect(trackX, trackY, trackW, 8, 0x2166);
        int fillW = (int)((uint32_t)(m_brightness - 10) * trackW / 90);
        if (fillW > 0) {
            canvas.fillRect(trackX, trackY, fillW, 8, COLOR_ACCENT);
        }
        canvas.fillCircle(trackX + fillW, trackY + 4, 5, COLOR_WHITE);

        canvas.fillRoundRect(cardX + 190, c1Y + 20, 28, 24, 3, 0x18C3);
        canvas.drawRoundRect(cardX + 190, c1Y + 20, 28, 24, 3, COLOR_DIVIDER);
        canvas.drawString("+", cardX + 204, c1Y + 32);
    }

    // =========================================================================
    // CARD 2: SOUND & AUTO SLEEP (y: 62..118, h: 56)
    // =========================================================================
    int c2Y = baseY + 62;
    if (c2Y + 56 >= 0 && c2Y < bandH) {
        canvas.fillRoundRect(cardX, c2Y, cardW, 56, 5, COLOR_CARD);
        canvas.drawRoundRect(cardX, c2Y, cardW, 56, 5, COLOR_DIVIDER);

        canvas.setTextDatum(TL_DATUM);
        canvas.setTextColor(COLOR_WHITE, COLOR_CARD);
        canvas.drawString("Sound", cardX + 8, c2Y + 9);

        uint16_t sndBg = m_soundEnabled ? 0x04D3 : 0x2166;
        canvas.fillRoundRect(cardX + 148, c2Y + 4, 70, 20, 4, sndBg);
        canvas.setTextDatum(MC_DATUM);
        canvas.setTextColor(COLOR_WHITE, sndBg);
        canvas.drawString(m_soundEnabled ? "ON" : "OFF", cardX + 183, c2Y + 14);

        canvas.drawFastHLine(cardX + 8, c2Y + 28, cardW - 16, COLOR_DIVIDER);

        canvas.setTextDatum(TL_DATUM);
        canvas.setTextColor(COLOR_WHITE, COLOR_CARD);
        canvas.drawString("Sleep Timeout", cardX + 8, c2Y + 37);

        canvas.fillRoundRect(cardX + 148, c2Y + 32, 70, 20, 4, 0x18C3);
        canvas.drawRoundRect(cardX + 148, c2Y + 32, 70, 20, 4, COLOR_CYAN);
        canvas.setTextDatum(MC_DATUM);
        canvas.setTextColor(COLOR_CYAN, 0x18C3);
        const char* tStr = "1m";
        if (m_sleepTimeoutSec == 0) tStr = "Off";
        else if (m_sleepTimeoutSec <= 15) tStr = "15s";
        else if (m_sleepTimeoutSec <= 30) tStr = "30s";
        else if (m_sleepTimeoutSec <= 60) tStr = "1m";
        else tStr = "5m";
        canvas.drawString(tStr, cardX + 183, c2Y + 42);
    }

    // =========================================================================
    // CARD 3: RGB LED (y: 124..170, h: 46)
    // =========================================================================
    int c3Y = baseY + 124;
    if (c3Y + 46 >= 0 && c3Y < bandH) {
        canvas.fillRoundRect(cardX, c3Y, cardW, 46, 5, COLOR_CARD);
        canvas.drawRoundRect(cardX, c3Y, cardW, 46, 5, COLOR_DIVIDER);

        canvas.setTextDatum(TL_DATUM);
        canvas.setTextColor(COLOR_CYAN, COLOR_CARD);
        canvas.drawString("RGB LED", cardX + 8, c3Y + 5);

        const char* ledLabels[4] = { "OFF", "RED", "GRN", "BLU" };
        uint16_t ledColors[4]    = { 0x4208, 0xF800, 0x07E0, 0x001F };
        for (int i = 0; i < 4; i++) {
            int bx = cardX + 8 + i * 54;
            int by = c3Y + 18;
            bool active = (m_ledState == i);
            uint16_t bg = active ? ledColors[i] : 0x18C3;
            canvas.fillRoundRect(bx, by, 48, 22, 3, bg);
            canvas.drawRoundRect(bx, by, 48, 22, 3, active ? COLOR_WHITE : COLOR_DIVIDER);
            canvas.setTextDatum(MC_DATUM);
            canvas.setTextColor(active && i == 2 ? TFT_BLACK : COLOR_WHITE, bg);
            canvas.drawString(ledLabels[i], bx + 24, by + 11);
        }
    }

    // =========================================================================
    // CARD 4: SECURITY & PIN LOCK (y: 176..258, h: 82)
    // =========================================================================
    int c4Y = baseY + 176;
    if (c4Y + 82 >= 0 && c4Y < bandH) {
        canvas.fillRoundRect(cardX, c4Y, cardW, 82, 5, COLOR_CARD);
        canvas.drawRoundRect(cardX, c4Y, cardW, 82, 5, COLOR_DIVIDER);

        canvas.setTextDatum(TL_DATUM);
        canvas.setTextColor(COLOR_CYAN, COLOR_CARD);
        canvas.drawString("Security PIN (BOOT=Lock)", cardX + 8, c4Y + 7);

        uint16_t pinBg = m_pinEnabled ? 0x04D3 : 0x2166;
        canvas.fillRoundRect(cardX + 154, c4Y + 4, 64, 18, 4, pinBg);
        canvas.setTextDatum(MC_DATUM);
        canvas.setTextColor(COLOR_WHITE, pinBg);
        canvas.drawString(m_pinEnabled ? "PIN: ON" : "PIN: OFF", cardX + 186, c4Y + 13);

        for (int i = 0; i < 4; i++) {
            int dx = cardX + 8 + i * 54;
            int dy = c4Y + 26;
            canvas.fillRoundRect(dx, dy, 48, 22, 3, 0x10A2);
            canvas.drawRoundRect(dx, dy, 48, 22, 3, m_pinEnabled ? COLOR_CYAN : COLOR_DIVIDER);
            char dStr[4] = { m_pinCode[i], '\0', 0, 0 };
            canvas.setTextColor(m_pinEnabled ? COLOR_AMBER : COLOR_MUTED, 0x10A2);
            canvas.drawString(dStr, dx + 24, dy + 11);
        }

        canvas.fillRoundRect(cardX + 8, c4Y + 54, 210, 22, 4, 0x04D3);
        canvas.setTextColor(COLOR_WHITE, 0x04D3);
        canvas.drawString("LOCK SCREEN NOW", cardX + 113, c4Y + 65);
    }

    // =========================================================================
    // CARD 5: WI-FI & NTP REGION (y: 264..376, h: 112)
    // =========================================================================
    int c5Y = baseY + 264;
    if (c5Y + 112 >= 0 && c5Y < bandH) {
        canvas.fillRoundRect(cardX, c5Y, cardW, 112, 5, COLOR_CARD);
        canvas.drawRoundRect(cardX, c5Y, cardW, 112, 5, m_cachedWifiConn ? COLOR_ACCENT : COLOR_DIVIDER);

        canvas.setTextDatum(TL_DATUM);
        canvas.setTextColor(COLOR_CYAN, COLOR_CARD);
        canvas.drawString("Wi-Fi & NTP", cardX + 8, c5Y + 7);

        uint16_t wfBg = m_wifiEnabled ? (m_cachedWifiConn ? 0x04D3 : 0x1A6B) : 0x2166;
        canvas.fillRoundRect(cardX + 154, c5Y + 4, 64, 18, 4, wfBg);
        canvas.setTextDatum(MC_DATUM);
        canvas.setTextColor(COLOR_WHITE, wfBg);
        canvas.drawString(m_wifiEnabled ? "WIFI: ON" : "WIFI: OFF", cardX + 186, c5Y + 13);

        canvas.setTextDatum(TL_DATUM);
        canvas.setTextColor(m_cachedWifiConn ? COLOR_ACCENT : COLOR_AMBER, COLOR_CARD);
        canvas.drawString(m_cachedWifiLine, cardX + 8, c5Y + 25);

        // [SCAN] [SSID] [PASS] (y = c5Y + 38, h = 20)
        canvas.fillRoundRect(cardX + 8, c5Y + 38, 66, 20, 3, 0x18C3);
        canvas.drawRoundRect(cardX + 8, c5Y + 38, 66, 20, 3, COLOR_CYAN);
        canvas.setTextDatum(MC_DATUM);
        canvas.setTextColor(COLOR_CYAN, 0x18C3);
        canvas.drawString("SCAN", cardX + 41, c5Y + 48);

        canvas.fillRoundRect(cardX + 80, c5Y + 38, 66, 20, 3, 0x18C3);
        canvas.drawRoundRect(cardX + 80, c5Y + 38, 66, 20, 3, COLOR_AMBER);
        canvas.setTextColor(COLOR_AMBER, 0x18C3);
        canvas.drawString("SSID KB", cardX + 113, c5Y + 48);

        canvas.fillRoundRect(cardX + 152, c5Y + 38, 66, 20, 3, 0x18C3);
        canvas.drawRoundRect(cardX + 152, c5Y + 38, 66, 20, 3, COLOR_ACCENT);
        canvas.setTextColor(COLOR_ACCENT, 0x18C3);
        canvas.drawString("PASS KB", cardX + 185, c5Y + 48);

        if (m_scanCount > 0) {
            for (uint8_t s = 0; s < m_scanCount && s < 2; s++) {
                int ry = c5Y + 62 + s * 23;
                canvas.fillRoundRect(cardX + 8, ry, 210, 20, 3, 0x10A2);
                canvas.setTextDatum(TL_DATUM);
                canvas.setTextColor(COLOR_WHITE, 0x10A2);
                snprintf(buf, sizeof(buf), "%.18s (%lddBm)", m_scanSsids[s], (long)m_scanRssi[s]);
                canvas.drawString(buf, cardX + 12, ry + 6);
                canvas.setTextDatum(TR_DATUM);
                canvas.setTextColor(COLOR_CYAN, 0x10A2);
                canvas.drawString("JOIN", cardX + 214, ry + 6);
            }
        } else {
            canvas.fillRoundRect(cardX + 8, c5Y + 62, 210, 22, 4, m_cachedWifiConn ? 0x3800 : 0x04D3);
            canvas.setTextDatum(MC_DATUM);
            canvas.setTextColor(COLOR_WHITE, m_cachedWifiConn ? 0x3800 : 0x04D3);
            canvas.drawString(m_cachedWifiConn ? "DISCONNECT WI-FI" : "CONNECT WI-FI", cardX + 113, c5Y + 73);

            // Region button (c5Y + 88, h = 20)
            canvas.fillRoundRect(cardX + 8, c5Y + 88, 210, 20, 3, 0x10A2);
            canvas.setTextColor(COLOR_AMBER, 0x10A2);
            canvas.drawString(m_cachedRegionLine, cardX + 113, c5Y + 98);
        }
    }

    // =========================================================================
    // CARD 6: DEVELOPER OPTIONS (y: 382..446, h: 64)
    // =========================================================================
    int c6Y = baseY + 382;
    if (c6Y + 64 >= 0 && c6Y < bandH) {
        canvas.fillRoundRect(cardX, c6Y, cardW, 64, 5, COLOR_CARD);
        canvas.drawRoundRect(cardX, c6Y, cardW, 64, 5, m_devModeEnabled ? COLOR_AMBER : COLOR_DIVIDER);

        canvas.setTextDatum(TL_DATUM);
        canvas.setTextColor(COLOR_AMBER, COLOR_CARD);
        canvas.drawString(m_devModeEnabled ? "Dev Options [UNLOCKED]" : "Dev Options [LOCKED]", cardX + 8, c6Y + 6);

        if (!m_devModeEnabled) {
            canvas.setTextColor(COLOR_MUTED, COLOR_CARD);
            canvas.drawString("Tap Build Number 5x below to unlock", cardX + 8, c6Y + 24);
            canvas.drawString("ERR/FATAL logs always active.", cardX + 8, c6Y + 40);
        } else {
            uint16_t iBg = m_logInfoEnabled ? 0x04D3 : 0x2166;
            canvas.fillRoundRect(cardX + 8, c6Y + 20, 64, 18, 3, iBg);
            canvas.setTextDatum(MC_DATUM);
            canvas.setTextColor(COLOR_WHITE, iBg);
            canvas.drawString(m_logInfoEnabled ? "INFO:ON" : "INFO:OFF", cardX + 40, c6Y + 29);

            uint16_t wBg = m_logWarnEnabled ? 0x04D3 : 0x2166;
            canvas.fillRoundRect(cardX + 78, c6Y + 20, 64, 18, 3, wBg);
            canvas.setTextColor(COLOR_WHITE, wBg);
            canvas.drawString(m_logWarnEnabled ? "WARN:ON" : "WARN:OFF", cardX + 110, c6Y + 29);

            uint16_t dBg = m_logsJustDeleted ? 0x04D3 : 0x7800;
            canvas.fillRoundRect(cardX + 148, c6Y + 20, 70, 18, 3, dBg);
            canvas.setTextColor(COLOR_WHITE, dBg);
            canvas.drawString(m_logsJustDeleted ? "CLEARED" : "DEL LOGS", cardX + 183, c6Y + 29);

            canvas.setTextDatum(TL_DATUM);
            canvas.setTextColor(COLOR_MUTED, COLOR_CARD);
            canvas.drawString("ERR/FATAL locked ON -> /kernel.log", cardX + 8, c6Y + 45);
        }
    }

    // =========================================================================
    // CARD 7: ABOUT, OTA UPDATE & FACTORY RESET (y: 452..578, h: 126)
    // =========================================================================
    int c7Y = baseY + 452;
    if (c7Y + 126 >= 0 && c7Y < bandH) {
        canvas.fillRoundRect(cardX, c7Y, cardW, 126, 5, COLOR_CARD);
        canvas.drawRoundRect(cardX, c7Y, cardW, 126, 5, COLOR_CYAN);

        canvas.setTextDatum(TL_DATUM);
        canvas.setTextColor(COLOR_CYAN, COLOR_CARD);
        canvas.drawString("About CYD OS & OTA Recovery", cardX + 8, c7Y + 6);

        snprintf(buf, sizeof(buf), "ESP32 240MHz | RAM:%luK | FLS:%luK",
                 (unsigned long)m_cachedRamKb, (unsigned long)m_cachedFlashKb);
        canvas.setTextColor(COLOR_MUTED, COLOR_CARD);
        canvas.drawString(buf, cardX + 8, c7Y + 19);

        // Build Number Box (c7Y + 32, h = 24)
        uint16_t bldBg = m_devModeEnabled ? 0x0290 : 0x18C3;
        canvas.fillRoundRect(cardX + 8, c7Y + 32, 210, 24, 4, bldBg);
        canvas.drawRoundRect(cardX + 8, c7Y + 32, 210, 24, 4, m_devModeEnabled ? COLOR_ACCENT : COLOR_CYAN);
        canvas.setTextDatum(MC_DATUM);
        canvas.setTextColor(COLOR_WHITE, bldBg);
        if (m_devModeEnabled) {
            canvas.drawString("Build: CYD-DEV-2.0 [DEV UNLOCKED]", cardX + 113, c7Y + 44);
        } else if (m_devTapCount == 0) {
            canvas.drawString("Build: CYD-DEV-2.0 (Tap 5x for Dev)", cardX + 113, c7Y + 44);
        } else {
            snprintf(buf, sizeof(buf), "Build: CYD-DEV-2.0 (%u taps left)", (unsigned)(5 - m_devTapCount));
            canvas.drawString(buf, cardX + 113, c7Y + 44);
        }

        // OTA Firmware Update Button (c7Y + 62, h = 26)
        canvas.fillRoundRect(cardX + 8, c7Y + 62, 210, 26, 4, 0x04D3);
        canvas.drawRoundRect(cardX + 8, c7Y + 62, 210, 26, 4, COLOR_ACCENT);
        canvas.setTextColor(COLOR_WHITE, 0x04D3);
        canvas.drawString("OTA FIRMWARE UPDATE (GITHUB)", cardX + 113, c7Y + 75);

        // Factory Reset All Button (c7Y + 94, h = 26)
        uint16_t rstBg = m_confirmReset ? COLOR_WARNING : COLOR_DANGER;
        canvas.fillRoundRect(cardX + 8, c7Y + 94, 210, 26, 4, rstBg);
        canvas.drawRoundRect(cardX + 8, c7Y + 94, 210, 26, 4, COLOR_WHITE);
        canvas.setTextColor(m_confirmReset ? TFT_BLACK : COLOR_WHITE, rstBg);
        canvas.drawString(m_confirmReset ? "TAP AGAIN: WIPE & REBOOT" : "FACTORY RESET ALL", cardX + 113, c7Y + 107);
    }
}

void SettingsApp::drawScrollContent(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int scrollAreaY = vy + HEADER_H;
    int scrollAreaH = vh - HEADER_H;
    int cardW = vw - 14;

    // 84px band height = only 3 sprite flushes for the entire 250px viewport!
    static constexpr int BAND_H = 84;
    TFT_eSprite band(&display);
    band.setColorDepth(16);
    bool spriteOk = (band.createSprite(vw, BAND_H) != nullptr);

    int thumbH = (scrollAreaH * scrollAreaH) / CONTENT_H;
    if (thumbH < 24) thumbH = 24;
    int thumbTravel = scrollAreaH - thumbH;
    int thumbRelY = (MAX_SCROLL_Y > 0) ? (m_scrollY * thumbTravel / MAX_SCROLL_Y) : 0;

    for (int offsetY = 0; offsetY < scrollAreaH; offsetY += BAND_H) {
        int curBandH = (offsetY + BAND_H <= scrollAreaH) ? BAND_H : (scrollAreaH - offsetY);

        if (spriteOk) {
            band.fillSprite(COLOR_BG);
            int baseY = -m_scrollY - offsetY;
            renderCardsToCanvas(band, 6, baseY, cardW, curBandH);

            int sbX = vw - 5;
            band.fillRect(sbX, 0, 3, curBandH, 0x10A2);
            band.fillRoundRect(sbX, thumbRelY - offsetY, 3, thumbH, 1, COLOR_CYAN);

            band.pushSprite(vx, scrollAreaY + offsetY, 0, 0, vw, curBandH);
        } else {
            display.setViewport(vx, scrollAreaY + offsetY, vw, curBandH, false);
            display.fillRect(vx, scrollAreaY + offsetY, vw, curBandH, COLOR_BG);
            int baseY = scrollAreaY - m_scrollY;
            renderCardsToCanvas(display, vx + 6, baseY, cardW, scrollAreaH);
            display.resetViewport();
        }
    }

    if (spriteOk) {
        band.deleteSprite();
    }
    display.setTextDatum(TL_DATUM);
}

void SettingsApp::onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    if (m_needsFullRedraw) {
        drawHeader(display, vx, vy, vw);
        drawScrollContent(display, vx, vy, vw, vh);
        m_needsFullRedraw = false;
        m_needsScrollRedraw = false;
        return;
    }
    if (m_needsScrollRedraw) {
        drawScrollContent(display, vx, vy, vw, vh);
        m_needsScrollRedraw = false;
    }
}

void SettingsApp::handleTapAt(uint16_t x, uint16_t y) {
    // 1. Header UP^ / DNv
    if (y < HEADER_H) {
        if (x >= 150 && x <= 194) {
            m_scrollY -= 80;
            if (m_scrollY < 0) m_scrollY = 0;
            hal::beep(15);
            m_needsScrollRedraw = true;
        } else if (x >= 195 && x <= 238) {
            m_scrollY += 80;
            if (m_scrollY > MAX_SCROLL_Y) m_scrollY = MAX_SCROLL_Y;
            hal::beep(15);
            m_needsScrollRedraw = true;
        }
        return;
    }

    int contentY = (int)y - HEADER_H + m_scrollY;
    int cardX = 6;

    // CARD 1: BRIGHTNESS (4..56)
    if (contentY >= 4 && contentY <= 56) {
        if (x <= (uint16_t)(cardX + 40)) {
            int nextB = (int)m_brightness - 10;
            if (nextB < 10) nextB = 10;
            m_brightness = (uint8_t)nextB;
        } else if (x >= (uint16_t)(cardX + 186)) {
            int nextB = (int)m_brightness + 10;
            if (nextB > 100) nextB = 100;
            m_brightness = (uint8_t)nextB;
        } else {
            int rel = (int)x - (cardX + 42);
            int pct = 10 + (rel * 90) / 140;
            if (pct < 10) pct = 10;
            if (pct > 100) pct = 100;
            m_brightness = (uint8_t)pct;
        }
        hal::setBacklight(m_brightness);
        Config::getInstance().setBrightness(m_brightness, false);
        hal::beep(15);
        m_needsScrollRedraw = true;
        return;
    }

    // CARD 2: SOUND & SLEEP (62..118)
    if (contentY >= 62 && contentY <= 118) {
        if (contentY < 90) {
            m_soundEnabled = !m_soundEnabled;
            Config::getInstance().setSoundEnabled(m_soundEnabled, true);
            if (m_soundEnabled) hal::beep(20);
        } else {
            if (m_sleepTimeoutSec == 15) m_sleepTimeoutSec = 30;
            else if (m_sleepTimeoutSec == 30) m_sleepTimeoutSec = 60;
            else if (m_sleepTimeoutSec == 60) m_sleepTimeoutSec = 300;
            else if (m_sleepTimeoutSec == 300) m_sleepTimeoutSec = 0;
            else m_sleepTimeoutSec = 15;
            Config::getInstance().setSleepTimeoutSec(m_sleepTimeoutSec, true);
            hal::beep(20);
        }
        m_needsScrollRedraw = true;
        return;
    }

    // CARD 3: RGB LED (124..170)
    if (contentY >= 124 && contentY <= 170) {
        for (int i = 0; i < 4; i++) {
            int bx = cardX + 8 + i * 54;
            if ((int)x >= bx && (int)x <= (bx + 48)) {
                m_ledState = (uint8_t)i;
                switch (m_ledState) {
                    case 0: hal::turnOffLED(); break;
                    case 1: hal::setRGBLED(255, 0, 0); break;
                    case 2: hal::setRGBLED(0, 255, 0); break;
                    case 3: hal::setRGBLED(0, 0, 255); break;
                }
                hal::beep(20);
                m_needsScrollRedraw = true;
                return;
            }
        }
    }

    // CARD 4: SECURITY & PIN (176..258)
    if (contentY >= 176 && contentY <= 258) {
        if (contentY < 200 && x >= (uint16_t)(cardX + 140)) {
            m_pinEnabled = !m_pinEnabled;
            Config::getInstance().setPinEnabled(m_pinEnabled, true);
            hal::beep(25);
            m_needsScrollRedraw = true;
            return;
        }
        if (contentY >= 200 && contentY < 228) {
            for (int i = 0; i < 4; i++) {
                int dx = cardX + 8 + i * 54;
                if ((int)x >= dx && (int)x <= dx + 48) {
                    char c = m_pinCode[i];
                    m_pinCode[i] = (c >= '9' || c < '0') ? '0' : (char)(c + 1);
                    m_pinCode[4] = '\0';
                    Config::getInstance().setPinCode(m_pinCode, true);
                    hal::beep(20);
                    m_needsScrollRedraw = true;
                    return;
                }
            }
        }
        if (contentY >= 228) {
            hal::beep(30);
            ui::UIManager::lockScreen();
            return;
        }
    }

    // CARD 5: WI-FI & NTP REGION (264..376)
    if (contentY >= 264 && contentY <= 376) {
        if (contentY < 288 && x >= (uint16_t)(cardX + 140)) {
            m_wifiEnabled = !m_wifiEnabled;
            Config::getInstance().setWifiEnabled(m_wifiEnabled, true);
            hal::beep(20);
            if (m_wifiEnabled) {
                WiFi.mode(WIFI_STA);
                if (m_wifiSsid[0] != '\0') connectToWifi();
            } else {
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
            }
            refreshCachedStatus();
            m_needsScrollRedraw = true;
            return;
        }

        if (contentY >= 300 && contentY <= 324) {
            if ((int)x <= cardX + 76) {
                hal::beep(25);
                scanWifiNetworks();
                return;
            }
            if ((int)x <= cardX + 148) {
                hal::beep(25);
                char newSsid[33] = {0};
                if (ui::KeyboardView::prompt("Wi-Fi SSID", m_wifiSsid, newSsid, sizeof(newSsid))) {
                    strncpy(m_wifiSsid, newSsid, sizeof(m_wifiSsid) - 1);
                    m_wifiSsid[sizeof(m_wifiSsid) - 1] = '\0';
                    Config::getInstance().setWifiSsid(m_wifiSsid, true);
                    m_scanCount = 0;
                }
                refreshCachedStatus();
                m_needsFullRedraw = true;
                m_needsScrollRedraw = true;
                return;
            }
            hal::beep(25);
            char newPass[64] = {0};
            if (ui::KeyboardView::prompt("Wi-Fi Password", m_wifiPass, newPass, sizeof(newPass))) {
                strncpy(m_wifiPass, newPass, sizeof(m_wifiPass) - 1);
                m_wifiPass[sizeof(m_wifiPass) - 1] = '\0';
                Config::getInstance().setWifiPass(m_wifiPass, true);
                connectToWifi();
            }
            refreshCachedStatus();
            m_needsFullRedraw = true;
            m_needsScrollRedraw = true;
            return;
        }

        if (contentY > 324) {
            if (m_scanCount > 0) {
                int idx = (contentY >= 349 && m_scanCount > 1) ? 1 : 0;
                hal::beep(25);
                strncpy(m_wifiSsid, m_scanSsids[idx], sizeof(m_wifiSsid) - 1);
                m_wifiSsid[sizeof(m_wifiSsid) - 1] = '\0';
                Config::getInstance().setWifiSsid(m_wifiSsid, true);
                if (!m_scanOpen[idx]) {
                    char promptTitle[40];
                    snprintf(promptTitle, sizeof(promptTitle), "Pass: %.20s", m_wifiSsid);
                    char newPass[64] = {0};
                    if (ui::KeyboardView::prompt(promptTitle, m_wifiPass, newPass, sizeof(newPass))) {
                        strncpy(m_wifiPass, newPass, sizeof(m_wifiPass) - 1);
                        m_wifiPass[sizeof(m_wifiPass) - 1] = '\0';
                        Config::getInstance().setWifiPass(m_wifiPass, true);
                    } else {
                        refreshCachedStatus();
                        m_needsFullRedraw = true;
                        m_needsScrollRedraw = true;
                        return;
                    }
                } else {
                    m_wifiPass[0] = '\0';
                    Config::getInstance().setWifiPass("", true);
                }
                m_scanCount = 0;
                connectToWifi();
                return;
            } else if (contentY < 350) {
                hal::beep(25);
                if (m_cachedWifiConn) {
                    WiFi.disconnect(false);
                    refreshCachedStatus();
                    m_needsScrollRedraw = true;
                } else {
                    connectToWifi();
                }
                return;
            } else {
                hal::beep(25);
                char newReg[24] = {0};
                const char* curReg = Config::getInstance().hasRegion() ? Config::getInstance().getRegion() : "Czechia";
                if (ui::KeyboardView::prompt("Region (Czechia, USA, Albania)", curReg, newReg, sizeof(newReg))) {
                    if (newReg[0] != '\0') {
                        Config::getInstance().setRegion(newReg, true);
                        core::TimeService::applySavedRegion();
                        core::TimeService::syncNtpNow(1800);
                    }
                }
                refreshCachedStatus();
                m_needsFullRedraw = true;
                m_needsScrollRedraw = true;
                return;
            }
        }
    }

    // CARD 6: DEV OPTIONS (382..446)
    if (contentY >= 382 && contentY <= 446) {
        if (!m_devModeEnabled) {
            hal::beep(15);
            ui::UIManager::showToast("Tap Build Number 5x below", 1200);
            return;
        }
        if ((int)x <= cardX + 74) {
            m_logInfoEnabled = !m_logInfoEnabled;
            Config::getInstance().setLogInfoEnabled(m_logInfoEnabled, true);
        } else if ((int)x <= cardX + 144) {
            m_logWarnEnabled = !m_logWarnEnabled;
            Config::getInstance().setLogWarnEnabled(m_logWarnEnabled, true);
        } else {
            core::Logger::clearLog();
            m_logsJustDeleted = true;
            ui::UIManager::showToast("Cleared kernel.log", 1200);
        }
        hal::beep(20);
        m_needsScrollRedraw = true;
        return;
    }

    // CARD 7: ABOUT, OTA UPDATE & FACTORY RESET ALL (452..580)
    if (contentY >= 452 && contentY <= 582) {
        if (contentY < 510) {
            // Build Number Box
            if (m_devModeEnabled) {
                hal::beep(15);
                ui::UIManager::showToast("Dev Mode already unlocked!", 1100);
                return;
            }
            m_devTapCount++;
            hal::beep(25);
            if (m_devTapCount >= 5) {
                m_devModeEnabled = true;
                Config::getInstance().setDevModeEnabled(true, true);
                hal::beep(60);
                ui::UIManager::showToast("Developer Mode UNLOCKED!", 1600);
            }
            m_needsScrollRedraw = true;
            return;
        } else if (contentY < 542) {
            // OTA Firmware Update (GitHub Releases Recovery Mode)
            core::OtaRecovery::triggerRebootToOtaRecovery();
            return;
        } else {
            // Factory Reset All Button (contentY >= 542)
            if (!m_confirmReset) {
                m_confirmReset = true;
                hal::beep(35);
                ui::UIManager::showToast("Tap again to Factory Reset All", 1500);
                m_needsScrollRedraw = true;
            } else {
                performFactoryResetAll();
            }
            return;
        }
    }
}

void SettingsApp::onTouch(uint16_t x, uint16_t y, bool pressed) {
    if (pressed && !m_wasPressed) {
        m_wasPressed   = true;
        m_isScrolling  = false;
        m_touchStartY  = (int)y;
        m_scrollStartY = m_scrollY;
        m_lastTouchX   = x;
        m_lastTouchY   = y;

        int contentY = (int)y - HEADER_H + m_scrollY;
        if (y >= HEADER_H && contentY >= 16 && contentY <= 52 && x >= 42 && x <= 190) {
            m_isDraggingSlider = true;
            int rel = (int)x - 48;
            int pct = 10 + (rel * 90) / 140;
            if (pct < 10) pct = 10;
            if (pct > 100) pct = 100;
            if (m_brightness != (uint8_t)pct) {
                m_brightness = (uint8_t)pct;
                hal::setBacklight(m_brightness);
                m_needsScrollRedraw = true;
            }
        }
    } else if (pressed && m_wasPressed) {
        m_lastTouchX = x;
        m_lastTouchY = y;

        if (m_isDraggingSlider) {
            int rel = (int)x - 48;
            int pct = 10 + (rel * 90) / 140;
            if (pct < 10) pct = 10;
            if (pct > 100) pct = 100;
            if (m_brightness != (uint8_t)pct) {
                m_brightness = (uint8_t)pct;
                hal::setBacklight(m_brightness);
                m_needsScrollRedraw = true;
            }
            return;
        }

        int dy = (int)y - m_touchStartY;
        if (!m_isScrolling && y >= HEADER_H && abs(dy) > 7) {
            m_isScrolling = true;
        }
        if (m_isScrolling) {
            int newScroll = m_scrollStartY - dy;
            if (newScroll < 0) newScroll = 0;
            if (newScroll > MAX_SCROLL_Y) newScroll = MAX_SCROLL_Y;
            uint32_t now = millis();
            if (abs(newScroll - m_scrollY) >= 3 && (now - m_lastScrollRenderMs >= 20)) {
                m_scrollY = newScroll;
                m_lastScrollRenderMs = now;
                m_needsScrollRedraw = true;
            }
        }
    } else if (!pressed && m_wasPressed) {
        m_wasPressed = false;
        if (m_isDraggingSlider) {
            m_isDraggingSlider = false;
            Config::getInstance().setBrightness(m_brightness, true);
            return;
        }
        if (!m_isScrolling) {
            handleTapAt(m_lastTouchX, m_lastTouchY);
        } else {
            m_needsScrollRedraw = true;
        }
        m_isScrolling = false;
    }
}

} // namespace apps
