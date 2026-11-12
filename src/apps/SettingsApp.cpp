/**
 * @file SettingsApp.cpp
 * @brief Implementation of Android Category-Based Full-Screen Settings App
 */

#include "SettingsApp.h"
#include "core/Version.h"
#include "core/Logger.h"
#include "core/TimeService.h"
#include "core/OtaRecovery.h"
#include "hal/StorageHAL.h"
#include "hal/TouchHAL.h"
#include "ui/UIManager.h"
#include "ui/KeyboardView.h"
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace apps {

static constexpr uint16_t COLOR_BG        = 0x0821;
static constexpr uint16_t COLOR_HEADER    = 0x10A2;
static constexpr uint16_t COLOR_CARD      = 0x18E3;
static constexpr uint16_t COLOR_CARD_ALT  = 0x2124;
static constexpr uint16_t COLOR_DIVIDER   = 0x3186;
static constexpr uint16_t COLOR_CYAN      = 0x07FF;
static constexpr uint16_t COLOR_GREEN     = 0x07E0;
static constexpr uint16_t COLOR_ANDR_GRN  = 0x3EE0;
static constexpr uint16_t COLOR_AMBER     = 0xFFE0;
static constexpr uint16_t COLOR_RED       = 0xF800;
static constexpr uint16_t COLOR_WHITE     = 0xFFFF;
static constexpr uint16_t COLOR_MUTED     = 0x9CD3;

SettingsApp::SettingsApp()
    : m_currentPage(SettingsPage::MAIN_MENU)
    , m_needsFullRedraw(true)
    , m_wasPressed(false)
    , m_isDraggingSlider(false)
    , m_isDraggingScroll(false)
    , m_lastTouchX(0)
    , m_lastTouchY(0)
    , m_touchStartY(0)
    , m_scrollStartY(0)
    , m_aboutScrollY(0)
    , m_lastClockTickMs(0)
    , m_brightness(80)
    , m_ledState(0)
    , m_soundEnabled(true)
    , m_pinEnabled(false)
    , m_wifiEnabled(false)
    , m_scanCount(0)
    , m_logInfoEnabled(true)
    , m_logWarnEnabled(true)
    , m_devModeEnabled(false)
    , m_logsJustDeleted(false)
    , m_sleepTimeoutSec(30)
    , m_confirmReset(false)
    , m_cachedRamKb(100)
    , m_cachedMaxBlockKb(40)
    , m_cachedFlashUsedKb(0)
    , m_cachedFlashTotalKb(320)
    , m_cachedWifiConn(false) {
    strcpy(m_pinCode, "1234");
    memset(m_wifiSsid, 0, sizeof(m_wifiSsid));
    memset(m_wifiPass, 0, sizeof(m_wifiPass));
    memset(m_scanSsids, 0, sizeof(m_scanSsids));
    memset(m_scanRssi, 0, sizeof(m_scanRssi));
    memset(m_scanOpen, 0, sizeof(m_scanOpen));
    strcpy(m_cachedWifiLine, "Wi-Fi Off");
    strcpy(m_cachedRegionLine, "Czechia (UTC+1)");
}

void SettingsApp::refreshCachedStatus() {
    m_cachedRamKb        = (uint32_t)(ESP.getFreeHeap() / 1024);
    m_cachedMaxBlockKb   = (uint32_t)(ESP.getMaxAllocHeap() / 1024);
    m_cachedFlashUsedKb  = (uint32_t)(StorageHAL::getStorageUsedBytes() / 1024);
    size_t totalB        = StorageHAL::getStorageTotalBytes();
    m_cachedFlashTotalKb = (totalB > 0) ? (uint32_t)(totalB / 1024) : 320;
    m_cachedWifiConn     = (WiFi.status() == WL_CONNECTED);

    if (m_cachedWifiConn) {
        snprintf(m_cachedWifiLine, sizeof(m_cachedWifiLine), "Connected: %.12s", WiFi.SSID().c_str());
    } else if (m_wifiEnabled && m_wifiSsid[0] != '\0') {
        snprintf(m_cachedWifiLine, sizeof(m_cachedWifiLine), "Saved: %.16s", m_wifiSsid);
    } else if (m_wifiEnabled) {
        strncpy(m_cachedWifiLine, "Wi-Fi Enabled (Idle)", sizeof(m_cachedWifiLine) - 1);
    } else {
        strncpy(m_cachedWifiLine, "Wi-Fi Off", sizeof(m_cachedWifiLine) - 1);
    }
    m_cachedWifiLine[sizeof(m_cachedWifiLine) - 1] = '\0';

    core::TimeService::formatRegionSummary(m_cachedRegionLine, sizeof(m_cachedRegionLine));
}

void SettingsApp::onCreate() {
    m_state = core::AppState::CREATED;
    Config::getInstance().begin();
    onResume();
}

void SettingsApp::onResume() {
    m_state            = core::AppState::RUNNING;
    m_brightness       = Config::getInstance().getBrightness();
    m_soundEnabled     = Config::getInstance().isSoundEnabled();
    m_pinEnabled       = Config::getInstance().isPinEnabled();
    strncpy(m_pinCode, Config::getInstance().getPinCode(), sizeof(m_pinCode) - 1);
    m_pinCode[sizeof(m_pinCode) - 1] = '\0';
    m_wifiEnabled      = Config::getInstance().isWifiEnabled();
    strncpy(m_wifiSsid, Config::getInstance().getWifiSsid(), sizeof(m_wifiSsid) - 1);
    m_wifiSsid[sizeof(m_wifiSsid) - 1] = '\0';
    strncpy(m_wifiPass, Config::getInstance().getWifiPass(), sizeof(m_wifiPass) - 1);
    m_wifiPass[sizeof(m_wifiPass) - 1] = '\0';
    m_logInfoEnabled   = Config::getInstance().isLogInfoEnabled();
    m_logWarnEnabled   = Config::getInstance().isLogWarnEnabled();
    m_devModeEnabled   = Config::getInstance().isDevModeEnabled();
    m_sleepTimeoutSec  = Config::getInstance().getSleepTimeoutSec();
    m_logsJustDeleted  = false;
    m_confirmReset     = false;
    m_isDraggingSlider = false;
    m_isDraggingScroll = false;
    m_wasPressed       = false;
    refreshCachedStatus();
    m_needsFullRedraw  = true;
}

void SettingsApp::onUpdate() {
    // Live clock update once per second ONLY on Region & Time page
    // About page intentionally does NOT auto-refresh every second to prevent screen blinking!
    if (m_currentPage == SettingsPage::REGION_TIME) {
        uint32_t now = millis();
        if (now - m_lastClockTickMs >= 1000) {
            m_lastClockTickMs = now;
            m_needsFullRedraw = true;
        }
    }
}

void SettingsApp::scrollToAbout() {
    refreshCachedStatus();
    m_aboutScrollY = 0;
    m_currentPage = SettingsPage::ABOUT;
    m_needsFullRedraw = true;
}

bool SettingsApp::onBackPressed() {
    if (m_currentPage != SettingsPage::MAIN_MENU) {
        m_confirmReset = false;
        m_aboutScrollY = 0;
        refreshCachedStatus();
        m_currentPage = SettingsPage::MAIN_MENU;
        m_needsFullRedraw = true;
        return true; // Handled internally: return to Settings category list
    }
    return false; // On main menu: allow UIManager to exit to Home Launcher
}

void SettingsApp::openPage(SettingsPage page) {
    m_confirmReset = false;
    m_logsJustDeleted = false;
    m_aboutScrollY = 0;
    refreshCachedStatus();
    m_currentPage = page;
    m_needsFullRedraw = true;
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

    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    Config::getInstance().resetDefaults();
    core::Logger::clearLog();

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
        ui::UIManager::showToast("Tap SSID box to enter Wi-Fi", 1400);
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
        ui::UIManager::showToast("Connecting in background...", 1400);
    }
    refreshCachedStatus();
    m_needsFullRedraw = true;
}

void SettingsApp::scanWifiNetworks() {
    m_wifiEnabled = true;
    Config::getInstance().setWifiEnabled(true, true);
    ui::UIManager::showToast("Scanning Wi-Fi...", 1000);
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, false);
    m_scanCount = 0;
    if (n > 0) {
        for (int i = 0; i < n && m_scanCount < 3; i++) {
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
    m_needsFullRedraw = true;
}

// ============================================================================
// RENDERING: HEADER & FULL-SCREEN CATEGORY PAGES
// ============================================================================

void SettingsApp::drawHeader(TFT_eSPI &display, int vx, int vy, int vw) {
    display.fillRect(vx, vy, vw, HEADER_H, COLOR_HEADER);
    display.drawFastHLine(vx, vy + HEADER_H - 1, vw, COLOR_CYAN);

    if (m_currentPage == SettingsPage::MAIN_MENU) {
        display.setTextDatum(TL_DATUM);
        display.setTextFont(2);
        display.setTextColor(COLOR_WHITE, COLOR_HEADER);
        display.drawString("Settings", vx + 8, vy + 5);

        display.setTextFont(1);
        display.setTextDatum(TR_DATUM);
        display.setTextColor(COLOR_ANDR_GRN, COLOR_HEADER);
        display.drawString(CYDOS_FULL_NAME, vx + vw - 8, vy + 9);
    } else {
        // Back button pill on left
        display.fillRoundRect(vx + 4, vy + 3, 56, 20, 4, 0x2166);
        display.drawRoundRect(vx + 4, vy + 3, 56, 20, 4, COLOR_CYAN);
        display.setTextFont(1);
        display.setTextDatum(MC_DATUM);
        display.setTextColor(COLOR_CYAN, 0x2166);
        display.drawString("< BACK", vx + 32, vy + 13);

        const char* pageTitle = "Settings";
        switch (m_currentPage) {
            case SettingsPage::WIFI:          pageTitle = "Wi-Fi & Network"; break;
            case SettingsPage::DISPLAY_SOUND: pageTitle = "Display & Sound"; break;
            case SettingsPage::SECURITY:      pageTitle = "Security & PIN";  break;
            case SettingsPage::REGION_TIME:   pageTitle = "Region & Time";   break;
            case SettingsPage::SYSTEM_LOGS:   pageTitle = "System & Logs";   break;
            case SettingsPage::UPDATE_RESET:  pageTitle = "Update & Reset";  break;
            case SettingsPage::ABOUT:         pageTitle = "About cydOS";     break;
            default: break;
        }
        display.setTextFont(2);
        display.setTextDatum(TL_DATUM);
        display.setTextColor(COLOR_WHITE, COLOR_HEADER);
        display.drawString(pageTitle, vx + 68, vy + 5);
    }
    display.setTextDatum(TL_DATUM);
}

void SettingsApp::drawMainMenu(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int bodyY = vy + HEADER_H;
    int bodyH = vh - HEADER_H;
    display.fillRect(vx, bodyY, vw, bodyH, COLOR_BG);

    // Keep all subtitles strictly under 25 chars (~150 px) so they NEVER overflow card boundaries!
    char subDisplay[32];
    if (m_sleepTimeoutSec == 0) {
        snprintf(subDisplay, sizeof(subDisplay), "Bright %u%% | Sleep Never", m_brightness);
    } else {
        snprintf(subDisplay, sizeof(subDisplay), "Bright %u%% | Sleep %us", m_brightness, (unsigned)m_sleepTimeoutSec);
    }

    char subSec[32];
    if (m_pinEnabled) {
        snprintf(subSec, sizeof(subSec), "PIN Protected (%s)", m_pinCode);
    } else {
        strncpy(subSec, "Swipe / Tap to Unlock", sizeof(subSec) - 1);
        subSec[sizeof(subSec) - 1] = '\0';
    }

    char subSys[32];
    snprintf(subSys, sizeof(subSys), "RAM %luKB | Flash %luKB",
             (unsigned long)m_cachedRamKb, (unsigned long)m_cachedFlashUsedKb);

    struct CatRow {
        const char* title;
        const char* sub;
        uint16_t    badgeCol;
        const char* glyph;
    };

    CatRow rows[7] = {
        { "Wi-Fi & Network",     m_cachedWifiLine,                0x0276, "W" },
        { "Display & Sound",     subDisplay,                      0x6815, "D" },
        { "Security & PIN Lock", subSec,                          0x04D3, "S" },
        { "Region & Date/Time",  m_cachedRegionLine,              0x2C9F, "T" },
        { "System & Logs",       subSys,                          0x9175, "M" },
        { "OTA Update & Reset",  "Firmware Update & Reset",       0xD8A7, "U" },
        { "About cydOS",         "Version " CYDOS_VERSION_STR " & Device Info", 0x04A0, "i" }
    };

    const int rowH = 33;
    const int gapY = 2;
    int curY = bodyY + 3;

    for (int i = 0; i < 7; i++) {
        display.fillRoundRect(vx + 6, curY, vw - 12, rowH, 5, COLOR_CARD);
        display.drawRoundRect(vx + 6, curY, vw - 12, rowH, 5,
                              (i == 6) ? COLOR_ANDR_GRN : COLOR_DIVIDER);

        // Colored category icon badge
        int bx = vx + 11;
        int by = curY + 4;
        display.fillRoundRect(bx, by, 25, 25, 5, rows[i].badgeCol);
        display.drawRoundRect(bx, by, 25, 25, 5, COLOR_WHITE);
        display.setTextFont(2);
        display.setTextDatum(MC_DATUM);
        display.setTextColor(COLOR_WHITE, rows[i].badgeCol);
        display.drawString(rows[i].glyph, bx + 12, by + 12);

        // Category Title
        display.setTextDatum(TL_DATUM);
        display.setTextFont(2);
        display.setTextColor(COLOR_WHITE, COLOR_CARD);
        display.drawString(rows[i].title, vx + 42, curY + 2);

        // Subtitle (concise & guaranteed within 168 px)
        display.setTextFont(1);
        display.setTextColor(COLOR_MUTED, COLOR_CARD);
        display.drawString(rows[i].sub, vx + 42, curY + 21);

        // Right arrow chevron '>'
        display.setTextFont(2);
        display.setTextDatum(MC_DATUM);
        display.setTextColor(COLOR_CYAN, COLOR_CARD);
        display.drawString(">", vx + vw - 15, curY + 16);

        curY += rowH + gapY;
    }
    display.setTextDatum(TL_DATUM);
}

void SettingsApp::drawWifiPage(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int bodyY = vy + HEADER_H;
    int bodyH = vh - HEADER_H;
    display.fillRect(vx, bodyY, vw, bodyH, COLOR_BG);
    int cardX = vx + 6;
    int cardW = vw - 12;

    // 1. Radio & Status Card
    int y1 = bodyY + 4;
    display.fillRoundRect(cardX, y1, cardW, 44, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y1, cardW, 44, 6, COLOR_DIVIDER);
    display.setTextFont(2);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_WHITE, COLOR_CARD);
    display.drawString("Wi-Fi Radio", cardX + 8, y1 + 5);

    uint16_t togCol = m_wifiEnabled ? COLOR_GREEN : 0x4208;
    display.fillRoundRect(cardX + cardW - 64, y1 + 5, 56, 20, 10, togCol);
    display.setTextFont(1);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(m_wifiEnabled ? TFT_BLACK : COLOR_WHITE, togCol);
    display.drawString(m_wifiEnabled ? "ENABLED" : "OFF", cardX + cardW - 36, y1 + 15);

    display.setTextDatum(TL_DATUM);
    display.setTextColor(m_cachedWifiConn ? COLOR_GREEN : COLOR_AMBER, COLOR_CARD);
    display.drawString(m_cachedWifiLine, cardX + 8, y1 + 28);

    // 2. Credentials Card
    int y2 = bodyY + 52;
    display.fillRoundRect(cardX, y2, cardW, 62, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y2, cardW, 62, 6, COLOR_DIVIDER);

    // SSID Input Box
    display.fillRoundRect(cardX + 6, y2 + 5, cardW - 12, 24, 4, 0x0821);
    display.drawRoundRect(cardX + 6, y2 + 5, cardW - 12, 24, 4, COLOR_CYAN);
    char ssidStr[40];
    snprintf(ssidStr, sizeof(ssidStr), "SSID: %.18s", (m_wifiSsid[0] != '\0') ? m_wifiSsid : "<Tap to type>");
    display.setTextColor(COLOR_WHITE, 0x0821);
    display.drawString(ssidStr, cardX + 10, y2 + 13);

    // Password Input Box
    display.fillRoundRect(cardX + 6, y2 + 33, cardW - 12, 24, 4, 0x0821);
    display.drawRoundRect(cardX + 6, y2 + 33, cardW - 12, 24, 4, COLOR_DIVIDER);
    char passStr[40];
    if (m_wifiPass[0] == '\0') {
        strcpy(passStr, "PASS: <Open / Tap to enter>");
    } else {
        size_t len = strlen(m_wifiPass);
        if (len > 12) len = 12;
        char stars[14];
        memset(stars, '*', len);
        stars[len] = '\0';
        snprintf(passStr, sizeof(passStr), "PASS: %s (%u chars)", stars, (unsigned)strlen(m_wifiPass));
    }
    display.setTextColor(COLOR_MUTED, 0x0821);
    display.drawString(passStr, cardX + 10, y2 + 41);

    // 3. Action Buttons Row
    int y3 = bodyY + 118;
    display.fillRoundRect(cardX, y3, 110, 28, 5, 0x04D3);
    display.drawRoundRect(cardX, y3, 110, 28, 5, COLOR_GREEN);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_WHITE, 0x04D3);
    display.drawString("CONNECT WI-FI", cardX + 55, y3 + 14);

    display.fillRoundRect(cardX + 116, y3, cardW - 116, 28, 5, 0x2166);
    display.drawRoundRect(cardX + 116, y3, cardW - 116, 28, 5, COLOR_CYAN);
    display.setTextColor(COLOR_CYAN, 0x2166);
    display.drawString("SCAN NETWORKS", cardX + 116 + (cardW - 116) / 2, y3 + 14);

    // 4. Scanned Networks List Card
    int y4 = bodyY + 150;
    display.fillRoundRect(cardX, y4, cardW, 94, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y4, cardW, 94, 6, COLOR_DIVIDER);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("AVAILABLE NETWORKS (TAP TO SELECT):", cardX + 8, y4 + 5);

    if (m_scanCount == 0) {
        display.setTextDatum(MC_DATUM);
        display.setTextColor(COLOR_MUTED, COLOR_CARD);
        display.drawString("Tap [SCAN NETWORKS] to discover APs", vx + vw / 2, y4 + 52);
    } else {
        for (uint8_t i = 0; i < m_scanCount && i < 3; i++) {
            int ry = y4 + 18 + i * 24;
            display.fillRoundRect(cardX + 6, ry, cardW - 12, 21, 4, 0x10A2);
            display.drawRoundRect(cardX + 6, ry, cardW - 12, 21, 4, COLOR_DIVIDER);
            char netLine[36];
            snprintf(netLine, sizeof(netLine), "%u. %.14s (%lddBm)",
                     (unsigned)(i + 1), m_scanSsids[i], (long)m_scanRssi[i]);
            display.setTextDatum(TL_DATUM);
            display.setTextColor(COLOR_WHITE, 0x10A2);
            display.drawString(netLine, cardX + 10, ry + 6);
        }
    }
    display.setTextDatum(TL_DATUM);
}

void SettingsApp::drawDisplaySoundPage(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int bodyY = vy + HEADER_H;
    int bodyH = vh - HEADER_H;
    display.fillRect(vx, bodyY, vw, bodyH, COLOR_BG);
    int cardX = vx + 6;
    int cardW = vw - 12;

    // 1. Brightness Card
    int y1 = bodyY + 4;
    display.fillRoundRect(cardX, y1, cardW, 52, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y1, cardW, 52, 6, COLOR_DIVIDER);
    display.setTextFont(1);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("SCREEN BRIGHTNESS", cardX + 8, y1 + 5);

    char bVal[16];
    snprintf(bVal, sizeof(bVal), "%u%%", m_brightness);
    display.setTextDatum(TR_DATUM);
    display.setTextColor(COLOR_AMBER, COLOR_CARD);
    display.drawString(bVal, cardX + cardW - 8, y1 + 5);

    // [-] Button
    display.fillRoundRect(cardX + 8, y1 + 19, 34, 26, 5, 0x10A2);
    display.drawRoundRect(cardX + 8, y1 + 19, 34, 26, 5, COLOR_DIVIDER);
    display.setTextFont(2);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_WHITE, 0x10A2);
    display.drawString("-", cardX + 25, y1 + 32);

    // Slider Bar
    int barX = cardX + 48;
    int barW = cardW - 96;
    display.fillRoundRect(barX, y1 + 25, barW, 14, 4, 0x0821);
    display.drawRoundRect(barX, y1 + 25, barW, 14, 4, COLOR_DIVIDER);
    int fillW = (m_brightness * (barW - 4)) / 100;
    if (fillW > 0) {
        display.fillRoundRect(barX + 2, y1 + 27, fillW, 10, 3, COLOR_CYAN);
    }

    // [+] Button
    display.fillRoundRect(cardX + cardW - 42, y1 + 19, 34, 26, 5, 0x10A2);
    display.drawRoundRect(cardX + cardW - 42, y1 + 19, 34, 26, 5, COLOR_DIVIDER);
    display.setTextColor(COLOR_WHITE, 0x10A2);
    display.drawString("+", cardX + cardW - 25, y1 + 32);

    // 2. Auto-Sleep Timeout Card
    int y2 = bodyY + 60;
    display.fillRoundRect(cardX, y2, cardW, 50, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y2, cardW, 50, 6, COLOR_DIVIDER);
    display.setTextFont(1);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("AUTO-SLEEP (BOOT = WAKE/SLEEP)", cardX + 8, y2 + 5);

    const uint16_t presets[5] = { 15, 30, 60, 120, 0 };
    const char* labels[5]     = { "15s", "30s", "1m", "2m", "OFF" };
    for (int i = 0; i < 5; i++) {
        int bx = cardX + 8 + i * 43;
        bool active = (m_sleepTimeoutSec == presets[i]);
        uint16_t bg = active ? COLOR_GREEN : 0x10A2;
        uint16_t fg = active ? TFT_BLACK : COLOR_WHITE;
        display.fillRoundRect(bx, y2 + 20, 39, 23, 4, bg);
        display.drawRoundRect(bx, y2 + 20, 39, 23, 4, active ? COLOR_WHITE : COLOR_DIVIDER);
        display.setTextDatum(MC_DATUM);
        display.setTextColor(fg, bg);
        display.drawString(labels[i], bx + 19, y2 + 31);
    }

    // 3. System Touch Sound Card
    int y3 = bodyY + 114;
    display.fillRoundRect(cardX, y3, cardW, 42, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y3, cardW, 42, 6, COLOR_DIVIDER);
    display.setTextDatum(TL_DATUM);
    display.setTextFont(2);
    display.setTextColor(COLOR_WHITE, COLOR_CARD);
    display.drawString("System Touch Audio", cardX + 8, y3 + 4);
    display.setTextFont(1);
    display.setTextColor(COLOR_MUTED, COLOR_CARD);
    display.drawString("Piezo clicks & alert tones", cardX + 8, y3 + 24);

    uint16_t sndCol = m_soundEnabled ? COLOR_GREEN : 0x4208;
    display.fillRoundRect(cardX + cardW - 68, y3 + 9, 60, 24, 12, sndCol);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(m_soundEnabled ? TFT_BLACK : COLOR_WHITE, sndCol);
    display.drawString(m_soundEnabled ? "SOUND ON" : "MUTED", cardX + cardW - 38, y3 + 21);

    // 4. Onboard RGB LED Control Card
    int y4 = bodyY + 160;
    display.fillRoundRect(cardX, y4, cardW, 48, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y4, cardW, 48, 6, COLOR_DIVIDER);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("ONBOARD RGB STATUS LED", cardX + 8, y4 + 5);

    const char* ledNames[5] = { "OFF", "RED", "GRN", "BLU", "CYN" };
    const uint16_t ledCols[5] = { 0x2124, 0x9000, 0x04A0, 0x0276, 0x04D3 };
    for (int i = 0; i < 5; i++) {
        int bx = cardX + 8 + i * 43;
        bool sel = (m_ledState == i);
        display.fillRoundRect(bx, y4 + 19, 39, 23, 4, ledCols[i]);
        display.drawRoundRect(bx, y4 + 19, 39, 23, 4, sel ? COLOR_AMBER : COLOR_DIVIDER);
        display.setTextDatum(MC_DATUM);
        display.setTextColor(COLOR_WHITE, ledCols[i]);
        display.drawString(ledNames[i], bx + 19, y4 + 30);
    }

    // 5. Touchscreen Calibration Button
    int y5 = bodyY + 212;
    display.fillRoundRect(cardX, y5, cardW, 32, 6, 0x2166);
    display.drawRoundRect(cardX, y5, cardW, 32, 6, COLOR_CYAN);
    display.setTextFont(2);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_CYAN, 0x2166);
    display.drawString("CALIBRATE TOUCHSCREEN", vx + vw / 2, y5 + 16);
    display.setTextFont(1);
    display.setTextDatum(TL_DATUM);
}

void SettingsApp::drawSecurityPage(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int bodyY = vy + HEADER_H;
    int bodyH = vh - HEADER_H;
    display.fillRect(vx, bodyY, vw, bodyH, COLOR_BG);
    int cardX = vx + 6;
    int cardW = vw - 12;

    // 1. PIN Lock Switch Card
    int y1 = bodyY + 8;
    display.fillRoundRect(cardX, y1, cardW, 58, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y1, cardW, 58, 6, COLOR_DIVIDER);
    display.setTextFont(2);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_WHITE, COLOR_CARD);
    display.drawString("4-Digit PIN Lock", cardX + 10, y1 + 8);
    display.setTextFont(1);
    display.setTextColor(COLOR_MUTED, COLOR_CARD);
    display.drawString(m_pinEnabled ? "PIN required on boot & wake" : "Swipe up or tap to unlock",
                       cardX + 10, y1 + 32);

    uint16_t pCol = m_pinEnabled ? COLOR_GREEN : 0x4208;
    display.fillRoundRect(cardX + cardW - 68, y1 + 16, 60, 26, 12, pCol);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(m_pinEnabled ? TFT_BLACK : COLOR_WHITE, pCol);
    display.drawString(m_pinEnabled ? "ENABLED" : "OFF", cardX + cardW - 38, y1 + 29);

    // 2. PIN Code Configuration Card
    int y2 = bodyY + 74;
    display.fillRoundRect(cardX, y2, cardW, 74, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y2, cardW, 74, 6, COLOR_DIVIDER);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("ACTIVE SECURITY PIN CODE", cardX + 10, y2 + 8);

    char pinInfo[32];
    snprintf(pinInfo, sizeof(pinInfo), "Current PIN:  %s", m_pinCode);
    display.setTextFont(2);
    display.setTextColor(COLOR_AMBER, COLOR_CARD);
    display.drawString(pinInfo, cardX + 10, y2 + 22);

    display.fillRoundRect(cardX + 10, y2 + 44, cardW - 20, 24, 5, 0x2166);
    display.drawRoundRect(cardX + 10, y2 + 44, cardW - 20, 24, 5, COLOR_CYAN);
    display.setTextFont(1);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_WHITE, 0x2166);
    display.drawString("CHANGE 4-DIGIT PIN (KEYBOARD)", cardX + cardW / 2, y2 + 56);

    // 3. Quick Lock & Sleep Actions Card
    int y3 = bodyY + 156;
    display.fillRoundRect(cardX, y3, cardW, 82, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y3, cardW, 82, 6, COLOR_DIVIDER);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("QUICK LOCK & POWER ACTIONS", cardX + 10, y3 + 8);

    display.fillRoundRect(cardX + 10, y3 + 24, cardW - 20, 24, 5, 0x04D3);
    display.drawRoundRect(cardX + 10, y3 + 24, cardW - 20, 24, 5, COLOR_GREEN);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_WHITE, 0x04D3);
    display.drawString("LOCK SCREEN NOW", cardX + cardW / 2, y3 + 36);

    display.fillRoundRect(cardX + 10, y3 + 52, cardW - 20, 24, 5, 0x2C9F);
    display.drawRoundRect(cardX + 10, y3 + 52, cardW - 20, 24, 5, COLOR_CYAN);
    display.setTextColor(COLOR_WHITE, 0x2C9F);
    display.drawString("TURN SCREEN OFF (SLEEP)", cardX + cardW / 2, y3 + 64);

    display.setTextDatum(TL_DATUM);
}

void SettingsApp::drawRegionTimePage(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int bodyY = vy + HEADER_H;
    int bodyH = vh - HEADER_H;
    display.fillRect(vx, bodyY, vw, bodyH, COLOR_BG);
    int cardX = vx + 6;
    int cardW = vw - 12;

    // 1. Live Clock Card
    int y1 = bodyY + 8;
    display.fillRoundRect(cardX, y1, cardW, 68, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y1, cardW, 68, 6, COLOR_DIVIDER);

    char timeBuf[16];
    core::TimeService::formatHHMMSS(timeBuf, sizeof(timeBuf));
    display.setTextFont(4);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_WHITE, COLOR_CARD);
    display.drawString(timeBuf, vx + vw / 2, y1 + 26);

    display.setTextFont(1);
    bool synced = core::TimeService::isSynced();
    display.setTextColor(synced ? COLOR_GREEN : COLOR_AMBER, COLOR_CARD);
    display.drawString(synced ? "NTP Status: Synchronized (Wi-Fi)" : "NTP Status: Unsynced (Local Uptime)",
                       vx + vw / 2, y1 + 54);

    // 2. Region & Timezone Card
    int y2 = bodyY + 84;
    display.fillRoundRect(cardX, y2, cardW, 76, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y2, cardW, 76, 6, COLOR_DIVIDER);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("COUNTRY / TIMEZONE REGION", cardX + 10, y2 + 8);

    display.setTextFont(2);
    display.setTextColor(COLOR_WHITE, COLOR_CARD);
    display.drawString(m_cachedRegionLine, cardX + 10, y2 + 22);

    display.fillRoundRect(cardX + 10, y2 + 46, cardW - 20, 24, 5, 0x2166);
    display.drawRoundRect(cardX + 10, y2 + 46, cardW - 20, 24, 5, COLOR_CYAN);
    display.setTextFont(1);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_WHITE, 0x2166);
    display.drawString("CHANGE REGION (CZECHIA, USA...)", cardX + cardW / 2, y2 + 58);

    // 3. Sync NTP Now Card
    int y3 = bodyY + 168;
    display.fillRoundRect(cardX, y3, cardW, 64, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y3, cardW, 64, 6, COLOR_DIVIDER);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_MUTED, COLOR_CARD);
    display.drawString("Fetches UTC time from pool.ntp.org", cardX + 10, y3 + 10);

    display.fillRoundRect(cardX + 10, y3 + 28, cardW - 20, 28, 5, 0x04D3);
    display.drawRoundRect(cardX + 10, y3 + 28, cardW - 20, 28, 5, COLOR_GREEN);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_WHITE, 0x04D3);
    display.drawString("SYNC NTP CLOCK NOW", cardX + cardW / 2, y3 + 42);

    display.setTextDatum(TL_DATUM);
}

void SettingsApp::drawSystemLogsPage(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int bodyY = vy + HEADER_H;
    int bodyH = vh - HEADER_H;
    display.fillRect(vx, bodyY, vw, bodyH, COLOR_BG);
    int cardX = vx + 6;
    int cardW = vw - 12;

    // 1. Hardware & Memory Telemetry Card
    int y1 = bodyY + 6;
    display.fillRoundRect(cardX, y1, cardW, 64, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y1, cardW, 64, 6, COLOR_DIVIDER);
    display.setTextFont(1);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("MEMORY & FILESYSTEM TELEMETRY", cardX + 8, y1 + 6);

    char line1[36];
    snprintf(line1, sizeof(line1), "Free RAM: %luKB (Max: %luKB)",
             (unsigned long)m_cachedRamKb, (unsigned long)m_cachedMaxBlockKb);
    display.setTextColor(COLOR_WHITE, COLOR_CARD);
    display.drawString(line1, cardX + 8, y1 + 22);

    char line2[36];
    snprintf(line2, sizeof(line2), "Flash: %lu/%luKB | SD: %s",
             (unsigned long)m_cachedFlashUsedKb, (unsigned long)m_cachedFlashTotalKb,
             StorageHAL::isSDMounted() ? "YES" : "NO");
    display.setTextColor(COLOR_MUTED, COLOR_CARD);
    display.drawString(line2, cardX + 8, y1 + 36);

    display.setTextColor(COLOR_GREEN, COLOR_CARD);
    display.drawString("Lazy App & C-Module Loader: ACTIVE", cardX + 8, y1 + 50);

    // 2. Diagnostics Mode Card
    int y2 = bodyY + 76;
    display.fillRoundRect(cardX, y2, cardW, 44, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y2, cardW, 44, 6, COLOR_DIVIDER);
    display.setTextFont(2);
    display.setTextColor(COLOR_WHITE, COLOR_CARD);
    display.drawString("Verbose Diagnostics", cardX + 8, y2 + 5);
    display.setTextFont(1);
    display.setTextColor(COLOR_MUTED, COLOR_CARD);
    display.drawString("Extended Lua VM & kernel logging", cardX + 8, y2 + 25);

    uint16_t devCol = m_devModeEnabled ? COLOR_GREEN : 0x4208;
    display.fillRoundRect(cardX + cardW - 64, y2 + 10, 56, 24, 12, devCol);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(m_devModeEnabled ? TFT_BLACK : COLOR_WHITE, devCol);
    display.drawString(m_devModeEnabled ? "ON" : "OFF", cardX + cardW - 36, y2 + 22);

    // 3. Kernel Log Filters & Actions Card
    int y3 = bodyY + 126;
    display.fillRoundRect(cardX, y3, cardW, 114, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y3, cardW, 114, 6, COLOR_DIVIDER);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("KERNEL LOGGER (/logs/kernel.log)", cardX + 8, y3 + 6);

    uint16_t infoBg = m_logInfoEnabled ? COLOR_GREEN : 0x4208;
    display.fillRoundRect(cardX + 8, y3 + 22, 66, 24, 4, infoBg);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(m_logInfoEnabled ? TFT_BLACK : COLOR_WHITE, infoBg);
    display.drawString(m_logInfoEnabled ? "INFO: ON" : "INFO: OFF", cardX + 41, y3 + 34);

    uint16_t warnBg = m_logWarnEnabled ? COLOR_AMBER : 0x4208;
    display.fillRoundRect(cardX + 80, y3 + 22, 66, 24, 4, warnBg);
    display.setTextColor(m_logWarnEnabled ? TFT_BLACK : COLOR_WHITE, warnBg);
    display.drawString(m_logWarnEnabled ? "WARN: ON" : "WARN: OFF", cardX + 113, y3 + 34);

    display.fillRoundRect(cardX + 152, y3 + 22, 68, 24, 4, 0x3800);
    display.drawRoundRect(cardX + 152, y3 + 22, 68, 24, 4, COLOR_RED);
    display.setTextColor(COLOR_WHITE, 0x3800);
    display.drawString("ERR: LOCKED", cardX + 186, y3 + 34);

    display.fillRoundRect(cardX + 8, y3 + 54, 102, 26, 5, 0x2166);
    display.drawRoundRect(cardX + 8, y3 + 54, 102, 26, 5, COLOR_CYAN);
    display.setTextColor(COLOR_WHITE, 0x2166);
    display.drawString("EXPORT TO SD", cardX + 59, y3 + 67);

    uint16_t delCol = m_logsJustDeleted ? 0x04A0 : 0x7800;
    display.fillRoundRect(cardX + 116, y3 + 54, 104, 26, 5, delCol);
    display.drawRoundRect(cardX + 116, y3 + 54, 104, 26, 5, COLOR_WHITE);
    display.setTextColor(COLOR_WHITE, delCol);
    display.drawString(m_logsJustDeleted ? "LOGS CLEARED!" : "DELETE LOGS", cardX + 168, y3 + 67);

    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_MUTED, COLOR_CARD);
    display.drawString("Hold BOOT on Home for Power Menu", cardX + 8, y3 + 92);
}

void SettingsApp::drawUpdateResetPage(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int bodyY = vy + HEADER_H;
    int bodyH = vh - HEADER_H;
    display.fillRect(vx, bodyY, vw, bodyH, COLOR_BG);
    int cardX = vx + 6;
    int cardW = vw - 12;

    // 1. Current Release Summary Card
    int y1 = bodyY + 6;
    display.fillRoundRect(cardX, y1, cardW, 66, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y1, cardW, 66, 6, COLOR_DIVIDER);
    display.setTextFont(2);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_ANDR_GRN, COLOR_CARD);
    display.drawString(CYDOS_FULL_NAME " (Stable)", cardX + 8, y1 + 5);

    display.setTextFont(1);
    display.setTextColor(COLOR_WHITE, COLOR_CARD);
    display.drawString("Board: ESP32-2432S028 (CYD 2-USB)", cardX + 8, y1 + 25);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("Repo: " CYDOS_GITHUB_REPO, cardX + 8, y1 + 39);
    display.setTextColor(COLOR_MUTED, COLOR_CARD);
    display.drawString("Recovery: Hold [BOOT] on startup", cardX + 8, y1 + 52);

    // 2. GitHub OTA Firmware Update Card
    int y2 = bodyY + 78;
    display.fillRoundRect(cardX, y2, cardW, 78, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y2, cardW, 78, 6, COLOR_DIVIDER);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("CLOUD OTA FIRMWARE UPDATER", cardX + 8, y2 + 6);
    display.setTextColor(COLOR_MUTED, COLOR_CARD);
    display.drawString("Reboots into Minimal-RAM Recovery & flashes", cardX + 8, y2 + 20);
    display.drawString("latest stable release from GitHub over Wi-Fi.", cardX + 8, y2 + 32);

    display.fillRoundRect(cardX + 8, y2 + 46, cardW - 16, 26, 5, 0x0352);
    display.drawRoundRect(cardX + 8, y2 + 46, cardW - 16, 26, 5, COLOR_CYAN);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_WHITE, 0x0352);
    display.drawString("INSTALL OTA UPDATE (RECOVERY)", cardX + cardW / 2, y2 + 59);

    // 3. Factory Reset Card
    int y3 = bodyY + 162;
    display.fillRoundRect(cardX, y3, cardW, 78, 6, COLOR_CARD);
    display.drawRoundRect(cardX, y3, cardW, 78, 6, COLOR_DIVIDER);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(COLOR_RED, COLOR_CARD);
    display.drawString("DANGER ZONE: FACTORY RESET", cardX + 8, y3 + 6);
    display.setTextColor(COLOR_MUTED, COLOR_CARD);
    display.drawString("Wipes NVS settings, Wi-Fi, PIN, logs &", cardX + 8, y3 + 20);
    display.drawString("restores default bundled apps.", cardX + 8, y3 + 32);

    uint16_t rstBg = m_confirmReset ? COLOR_AMBER : 0x9000;
    display.fillRoundRect(cardX + 8, y3 + 46, cardW - 16, 26, 5, rstBg);
    display.drawRoundRect(cardX + 8, y3 + 46, cardW - 16, 26, 5, COLOR_WHITE);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(m_confirmReset ? TFT_BLACK : COLOR_WHITE, rstBg);
    display.drawString(m_confirmReset ? "TAP AGAIN: CONFIRM WIPE & REBOOT" : "FACTORY RESET ALL",
                       cardX + cardW / 2, y3 + 59);

    display.setTextDatum(TL_DATUM);
}

// ============================================================================
// SCROLLABLE ABOUT PAGE WITH LARGE TEXT & TELEMETRY
// ============================================================================

void SettingsApp::drawAboutPage(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int bodyY = vy + HEADER_H;
    int bodyH = vh - HEADER_H;
    int maxScroll = ABOUT_CONTENT_H - bodyH;
    if (maxScroll < 0) maxScroll = 0;
    if (m_aboutScrollY > maxScroll) m_aboutScrollY = maxScroll;
    if (m_aboutScrollY < 0) m_aboutScrollY = 0;

    display.setViewport(vx, bodyY, vw, bodyH);
    display.fillRect(0, 0, vw, bodyH, COLOR_BG);

    int cardX = 6;
    int cardW = vw - 12;

    // Helper lambda to draw a neat Label (Font 1) + Big Value (Font 2) row
    auto drawBigRow = [&](int y, const char* label, const char* val, uint16_t valColor) {
        display.setTextDatum(TL_DATUM);
        display.setTextFont(1);
        display.setTextColor(COLOR_MUTED, COLOR_CARD);
        display.drawString(label, cardX + 10, y);

        display.setTextFont(2);
        display.setTextColor(valColor, COLOR_CARD);
        display.drawString(val, cardX + 10, y + 11);
    };

    // ------------------------------------------------------------------------
    // CARD 1: OS Information & Software Identity (Large Text)
    // ------------------------------------------------------------------------
    int c1Y = 6 - m_aboutScrollY;
    int c1H = 200;
    display.fillRoundRect(cardX, c1Y, cardW, c1H, 6, COLOR_CARD);
    display.drawRoundRect(cardX, c1Y, cardW, c1H, 6, COLOR_ANDR_GRN);

    // Title & Channel Badge
    display.setTextDatum(TL_DATUM);
    display.setTextFont(4);
    display.setTextColor(COLOR_ANDR_GRN, COLOR_CARD);
    display.drawString("cydOS", cardX + 10, c1Y + 6);

    display.fillRoundRect(cardX + cardW - 74, c1Y + 8, 66, 18, 9, COLOR_ANDR_GRN);
    display.setTextFont(1);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(TFT_BLACK, COLOR_ANDR_GRN);
    display.drawString("STABLE", cardX + cardW - 41, c1Y + 17);

    display.drawFastHLine(cardX + 8, c1Y + 36, cardW - 16, COLOR_DIVIDER);

    drawBigRow(c1Y + 42,  "SYSTEM VERSION", CYDOS_FULL_NAME, COLOR_WHITE);
    drawBigRow(c1Y + 74,  "BUILD CHANNEL",  CYDOS_BUILD_CHANNEL " (OTA Active)", COLOR_GREEN);
    drawBigRow(c1Y + 106, "OS ARCHITECTURE","Android Compatible UI Shell", COLOR_WHITE);
    drawBigRow(c1Y + 138, "AUTHOR / OWNER", CYDOS_AUTHOR, COLOR_AMBER);
    drawBigRow(c1Y + 170, "LUA ENVIRONMENT","Lua 5.4.6 VM (Lazy Loaded)", COLOR_WHITE);

    // ------------------------------------------------------------------------
    // CARD 2: Device & Hardware Specifications (Large Text)
    // ------------------------------------------------------------------------
    int c2Y = c1Y + c1H + 8;
    int c2H = 232;
    display.fillRoundRect(cardX, c2Y, cardW, c2H, 6, COLOR_CARD);
    display.drawRoundRect(cardX, c2Y, cardW, c2H, 6, COLOR_DIVIDER);

    display.setTextDatum(TL_DATUM);
    display.setTextFont(2);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("Device Specifications", cardX + 10, c2Y + 6);
    display.drawFastHLine(cardX + 8, c2Y + 24, cardW - 16, COLOR_DIVIDER);

    drawBigRow(c2Y + 30,  "DEVICE MODEL",     "ESP32-2432S028", COLOR_WHITE);
    drawBigRow(c2Y + 62,  "HARDWARE VARIANT", "CYD 2-USB (Dual Type-C)", COLOR_WHITE);
    drawBigRow(c2Y + 94,  "MAIN PROCESSOR",   "Xtensa Dual-Core LX6", COLOR_WHITE);
    drawBigRow(c2Y + 126, "CPU FREQUENCY",    "240 MHz (Max Performance)", COLOR_GREEN);
    drawBigRow(c2Y + 158, "DISPLAY PANEL",    "2.8\" ST7789 TFT 240x320", COLOR_WHITE);
    drawBigRow(c2Y + 190, "DIGITIZER & AUDIO","XPT2046 SPI Touch | PWM Piezo", COLOR_WHITE);

    // ------------------------------------------------------------------------
    // CARD 3: Storage, Partition & System Status (Large Text)
    // ------------------------------------------------------------------------
    int c3Y = c2Y + c2H + 8;
    int c3H = 200;
    display.fillRoundRect(cardX, c3Y, cardW, c3H, 6, COLOR_CARD);
    display.drawRoundRect(cardX, c3Y, cardW, c3H, 6, COLOR_DIVIDER);

    display.setTextDatum(TL_DATUM);
    display.setTextFont(2);
    display.setTextColor(COLOR_CYAN, COLOR_CARD);
    display.drawString("System & Memory Status", cardX + 10, c3Y + 6);
    display.drawFastHLine(cardX + 8, c3Y + 24, cardW - 16, COLOR_DIVIDER);

    const esp_partition_t* running = esp_ota_get_running_partition();
    char partStr[36];
    if (running) {
        snprintf(partStr, sizeof(partStr), "Slot: %s (0x%lX)", running->label, (unsigned long)running->address);
    } else {
        strcpy(partStr, "Slot: app0 (0x10000)");
    }
    drawBigRow(c3Y + 30, "ACTIVE OTA PARTITION", partStr, COLOR_GREEN);

    char ramStr[36];
    snprintf(ramStr, sizeof(ramStr), "%lu KB Free (Max: %lu KB)",
             (unsigned long)m_cachedRamKb, (unsigned long)m_cachedMaxBlockKb);
    drawBigRow(c3Y + 62, "SYSTEM SRAM MEMORY", ramStr, COLOR_WHITE);

    char flashStr[36];
    snprintf(flashStr, sizeof(flashStr), "%lu KB Used / %lu KB Total",
             (unsigned long)m_cachedFlashUsedKb, (unsigned long)m_cachedFlashTotalKb);
    drawBigRow(c3Y + 94, "INTERNAL FLASH (LITTLEFS)", flashStr, COLOR_WHITE);

    drawBigRow(c3Y + 126, "MICROSD STORAGE", StorageHAL::isSDMounted() ? "Mounted (/sd)" : "Not Inserted",
               StorageHAL::isSDMounted() ? COLOR_GREEN : COLOR_MUTED);

    char netStr[36];
    if (m_cachedWifiConn) {
        snprintf(netStr, sizeof(netStr), "Connected: %.16s", WiFi.localIP().toString().c_str());
    } else {
        strcpy(netStr, "Wi-Fi Disconnected");
    }
    drawBigRow(c3Y + 158, "NETWORK IP ADDRESS", netStr, m_cachedWifiConn ? COLOR_GREEN : COLOR_AMBER);

    // ------------------------------------------------------------------------
    // CARD 4: System Updates Action Button
    // ------------------------------------------------------------------------
    int c4Y = c3Y + c3H + 8;
    int c4H = 70;
    display.fillRoundRect(cardX, c4Y, cardW, c4H, 6, COLOR_CARD);
    display.drawRoundRect(cardX, c4Y, cardW, c4H, 6, COLOR_DIVIDER);

    display.setTextDatum(TL_DATUM);
    display.setTextFont(1);
    display.setTextColor(COLOR_MUTED, COLOR_CARD);
    display.drawString("Check for new cydOS updates on GitHub:", cardX + 10, c4Y + 8);

    display.fillRoundRect(cardX + 8, c4Y + 24, cardW - 16, 36, 6, 0x0352);
    display.drawRoundRect(cardX + 8, c4Y + 24, cardW - 16, 36, 6, COLOR_ANDR_GRN);
    display.setTextDatum(MC_DATUM);
    display.setTextFont(2);
    display.setTextColor(COLOR_WHITE, 0x0352);
    display.drawString("CHECK FOR UPDATES NOW", cardX + cardW / 2, c4Y + 42);

    display.resetViewport();

    // Scrollbar Indicator on Right Edge
    if (maxScroll > 0) {
        int sbH = (bodyH * bodyH) / ABOUT_CONTENT_H;
        if (sbH < 20) sbH = 20;
        int sbY = bodyY + (m_aboutScrollY * (bodyH - sbH)) / maxScroll;
        display.fillRoundRect(vx + vw - 4, sbY, 3, sbH, 1, COLOR_ANDR_GRN);
    }
    display.setTextDatum(TL_DATUM);
}

void SettingsApp::onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    if (!m_needsFullRedraw) return;
    m_needsFullRedraw = false;

    drawHeader(display, vx, vy, vw);
    switch (m_currentPage) {
        case SettingsPage::MAIN_MENU:     drawMainMenu(display, vx, vy, vw, vh); break;
        case SettingsPage::WIFI:          drawWifiPage(display, vx, vy, vw, vh); break;
        case SettingsPage::DISPLAY_SOUND: drawDisplaySoundPage(display, vx, vy, vw, vh); break;
        case SettingsPage::SECURITY:      drawSecurityPage(display, vx, vy, vw, vh); break;
        case SettingsPage::REGION_TIME:   drawRegionTimePage(display, vx, vy, vw, vh); break;
        case SettingsPage::SYSTEM_LOGS:   drawSystemLogsPage(display, vx, vy, vw, vh); break;
        case SettingsPage::UPDATE_RESET:  drawUpdateResetPage(display, vx, vy, vw, vh); break;
        case SettingsPage::ABOUT:         drawAboutPage(display, vx, vy, vw, vh); break;
    }
}

// ============================================================================
// TOUCH ROUTING PER CATEGORY PAGE
// ============================================================================

void SettingsApp::handleMainMenuTap(uint16_t x, uint16_t y) {
    (void)x;
    int relY = (int)y - HEADER_H - 3;
    if (relY < 0) return;
    int idx = relY / 35; // rowH (33) + gapY (2)
    if (idx < 0 || idx >= 7) return;

    hal::beep(25);
    switch (idx) {
        case 0: openPage(SettingsPage::WIFI);          break;
        case 1: openPage(SettingsPage::DISPLAY_SOUND); break;
        case 2: openPage(SettingsPage::SECURITY);      break;
        case 3: openPage(SettingsPage::REGION_TIME);   break;
        case 4: openPage(SettingsPage::SYSTEM_LOGS);   break;
        case 5: openPage(SettingsPage::UPDATE_RESET);  break;
        case 6: openPage(SettingsPage::ABOUT);         break;
    }
}

void SettingsApp::handleWifiTap(uint16_t x, uint16_t y) {
    int relY = (int)y - HEADER_H;

    // 1. Radio toggle (relY = 4..48)
    if (relY >= 4 && relY <= 48) {
        m_wifiEnabled = !m_wifiEnabled;
        Config::getInstance().setWifiEnabled(m_wifiEnabled, true);
        if (!m_wifiEnabled) {
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            ui::UIManager::showToast("Wi-Fi Radio Disabled", 1100);
        } else {
            WiFi.mode(WIFI_STA);
            if (m_wifiSsid[0] != '\0') connectToWifi();
        }
        hal::beep(25);
        refreshCachedStatus();
        m_needsFullRedraw = true;
        return;
    }

    // 2. SSID Box (relY = 55..82)
    if (relY >= 55 && relY <= 82) {
        hal::beep(25);
        char out[33] = {0};
        if (ui::KeyboardView::prompt("Wi-Fi SSID", m_wifiSsid, out, sizeof(out))) {
            strncpy(m_wifiSsid, out, sizeof(m_wifiSsid) - 1);
            m_wifiSsid[sizeof(m_wifiSsid) - 1] = '\0';
            Config::getInstance().setWifiSsid(m_wifiSsid, true);
        }
        refreshCachedStatus();
        m_needsFullRedraw = true;
        ui::UIManager::render(true);
        return;
    }

    // 3. Password Box (relY = 83..114)
    if (relY >= 83 && relY <= 114) {
        hal::beep(25);
        char out[64] = {0};
        if (ui::KeyboardView::prompt("Wi-Fi Password", m_wifiPass, out, sizeof(out))) {
            strncpy(m_wifiPass, out, sizeof(m_wifiPass) - 1);
            m_wifiPass[sizeof(m_wifiPass) - 1] = '\0';
            Config::getInstance().setWifiPass(m_wifiPass, true);
        }
        refreshCachedStatus();
        m_needsFullRedraw = true;
        ui::UIManager::render(true);
        return;
    }

    // 4. Connect / Scan buttons (relY = 118..146)
    if (relY >= 118 && relY <= 146) {
        hal::beep(30);
        if (x < 120) {
            connectToWifi();
        } else {
            scanWifiNetworks();
        }
        return;
    }

    // 5. Scanned networks rows (relY = 168..240)
    if (relY >= 168 && m_scanCount > 0) {
        int netIdx = (relY - 168) / 24;
        if (netIdx >= 0 && netIdx < (int)m_scanCount && netIdx < 3) {
            hal::beep(30);
            strncpy(m_wifiSsid, m_scanSsids[netIdx], sizeof(m_wifiSsid) - 1);
            m_wifiSsid[sizeof(m_wifiSsid) - 1] = '\0';
            Config::getInstance().setWifiSsid(m_wifiSsid, true);

            if (!m_scanOpen[netIdx]) {
                char out[64] = {0};
                if (ui::KeyboardView::prompt("Password for Network", m_wifiPass, out, sizeof(out))) {
                    strncpy(m_wifiPass, out, sizeof(m_wifiPass) - 1);
                    m_wifiPass[sizeof(m_wifiPass) - 1] = '\0';
                    Config::getInstance().setWifiPass(m_wifiPass, true);
                }
                ui::UIManager::render(true);
            } else {
                m_wifiPass[0] = '\0';
                Config::getInstance().setWifiPass("", true);
            }
            connectToWifi();
        }
    }
}

void SettingsApp::handleDisplaySoundTap(uint16_t x, uint16_t y) {
    int relY = (int)y - HEADER_H;

    // 1. Brightness Card (relY = 4..56)
    if (relY >= 4 && relY <= 56) {
        if (x < 50) {
            int nb = (int)m_brightness - 10;
            if (nb < 10) nb = 10;
            m_brightness = (uint8_t)nb;
        } else if (x > 190) {
            int nb = (int)m_brightness + 10;
            if (nb > 100) nb = 100;
            m_brightness = (uint8_t)nb;
        } else {
            int pct = ((int)x - 54) * 100 / 132;
            if (pct < 10) pct = 10;
            if (pct > 100) pct = 100;
            m_brightness = (uint8_t)pct;
        }
        hal::setBacklight(m_brightness);
        Config::getInstance().setBrightness(m_brightness, false);
        hal::beep(15);
        m_needsFullRedraw = true;
        return;
    }

    // 2. Auto-Sleep Timeout presets (relY = 60..110)
    if (relY >= 60 && relY <= 110) {
        const uint16_t presets[5] = { 15, 30, 60, 120, 0 };
        int idx = ((int)x - 14) / 43;
        if (idx >= 0 && idx < 5) {
            m_sleepTimeoutSec = presets[idx];
            Config::getInstance().setSleepTimeoutSec(m_sleepTimeoutSec, true);
            hal::beep(25);
            m_needsFullRedraw = true;
        }
        return;
    }

    // 3. System Touch Audio (relY = 114..156)
    if (relY >= 114 && relY <= 156) {
        m_soundEnabled = !m_soundEnabled;
        Config::getInstance().setSoundEnabled(m_soundEnabled, true);
        if (m_soundEnabled) hal::beep(35);
        m_needsFullRedraw = true;
        return;
    }

    // 4. Onboard RGB LED (relY = 160..208)
    if (relY >= 160 && relY <= 208) {
        int idx = ((int)x - 14) / 43;
        if (idx >= 0 && idx < 5) {
            m_ledState = (uint8_t)idx;
            switch (m_ledState) {
                case 0: hal::setRGBLED(0, 0, 0); break;
                case 1: hal::setRGBLED(1, 0, 0); break;
                case 2: hal::setRGBLED(0, 1, 0); break;
                case 3: hal::setRGBLED(0, 0, 1); break;
                case 4: hal::setRGBLED(0, 1, 1); break;
            }
            hal::beep(20);
            m_needsFullRedraw = true;
        }
        return;
    }

    // 5. Touchscreen Calibration (relY = 212..244)
    if (relY >= 212 && relY <= 246) {
        hal::beep(35);
        bool ok = hal::runInteractiveCalibration(hal::getDisplay());
        m_needsFullRedraw = true;
        ui::UIManager::render(true);
        if (ok) {
            ui::UIManager::showToast("Touchscreen Calibrated!", 1600);
        } else {
            ui::UIManager::showToast("Calibration cancelled", 1200);
        }
        return;
    }
}

void SettingsApp::handleSecurityTap(uint16_t x, uint16_t y) {
    (void)x;
    int relY = (int)y - HEADER_H;

    // 1. Toggle PIN Lock (relY = 8..66)
    if (relY >= 8 && relY <= 66) {
        m_pinEnabled = !m_pinEnabled;
        Config::getInstance().setPinEnabled(m_pinEnabled, true);
        hal::beep(30);
        m_needsFullRedraw = true;
        return;
    }

    // 2. Change 4-Digit PIN (relY = 114..148)
    if (relY >= 114 && relY <= 148) {
        hal::beep(25);
        char out[8] = {0};
        if (ui::KeyboardView::prompt("Enter 4-Digit PIN", m_pinCode, out, sizeof(out))) {
            if (strlen(out) == 4 &&
                out[0] >= '0' && out[0] <= '9' &&
                out[1] >= '0' && out[1] <= '9' &&
                out[2] >= '0' && out[2] <= '9' &&
                out[3] >= '0' && out[3] <= '9') {
                strncpy(m_pinCode, out, 4);
                m_pinCode[4] = '\0';
                m_pinEnabled = true;
                Config::getInstance().setPinCode(m_pinCode, false);
                Config::getInstance().setPinEnabled(true, true);
                ui::UIManager::showToast("New 4-Digit PIN Saved!", 1500);
            } else {
                ui::UIManager::showToast("PIN must be 4 digits (0-9)", 1600);
            }
        }
        m_needsFullRedraw = true;
        ui::UIManager::render(true);
        return;
    }

    // 3. Lock Screen Now (relY = 178..206)
    if (relY >= 178 && relY <= 206) {
        hal::beep(30);
        ui::UIManager::lockScreen(false);
        return;
    }

    // 4. Turn Screen Off / Sleep (relY = 207..238)
    if (relY >= 207 && relY <= 238) {
        hal::beep(30);
        ui::UIManager::lockScreen(true);
        return;
    }
}

void SettingsApp::handleRegionTimeTap(uint16_t x, uint16_t y) {
    (void)x;
    int relY = (int)y - HEADER_H;

    // 1. Change Region button (relY = 126..160)
    if (relY >= 126 && relY <= 160) {
        hal::beep(25);
        char out[24] = {0};
        if (ui::KeyboardView::prompt("Region (Czechia, USA, UK...)", Config::getInstance().getRegion(), out, sizeof(out))) {
            if (out[0] != '\0') {
                Config::getInstance().setRegion(out, true);
                core::TimeService::applySavedRegion();
                if (WiFi.status() == WL_CONNECTED) {
                    core::TimeService::syncNtpNow(1800);
                }
            }
        }
        refreshCachedStatus();
        m_needsFullRedraw = true;
        ui::UIManager::render(true);
        return;
    }

    // 2. Sync NTP Now button (relY = 192..232)
    if (relY >= 192 && relY <= 232) {
        hal::beep(30);
        if (WiFi.status() != WL_CONNECTED) {
            ui::UIManager::showToast("Connect Wi-Fi first for NTP!", 1500);
        } else {
            ui::UIManager::showToast("Syncing NTP Time...", 1000);
            bool ok = core::TimeService::syncNtpNow(2500);
            ui::UIManager::showToast(ok ? "NTP Clock Synchronized!" : "NTP Timeout - Try Again", 1600);
        }
        refreshCachedStatus();
        m_needsFullRedraw = true;
    }
}

void SettingsApp::handleSystemLogsTap(uint16_t x, uint16_t y) {
    int relY = (int)y - HEADER_H;

    // 1. Diagnostics Mode toggle (relY = 76..120)
    if (relY >= 76 && relY <= 120) {
        m_devModeEnabled = !m_devModeEnabled;
        Config::getInstance().setDevModeEnabled(m_devModeEnabled, true);
        hal::beep(25);
        m_needsFullRedraw = true;
        return;
    }

    // 2. INFO / WARN filter pills (relY = 146..174)
    if (relY >= 146 && relY <= 174) {
        if (x >= 14 && x <= 80) {
            m_logInfoEnabled = !m_logInfoEnabled;
            Config::getInstance().setLogInfoEnabled(m_logInfoEnabled, true);
            hal::beep(20);
            m_needsFullRedraw = true;
        } else if (x >= 86 && x <= 152) {
            m_logWarnEnabled = !m_logWarnEnabled;
            Config::getInstance().setLogWarnEnabled(m_logWarnEnabled, true);
            hal::beep(20);
            m_needsFullRedraw = true;
        } else {
            ui::UIManager::showToast("ERR/FATAL logs are always locked ON", 1400);
        }
        return;
    }

    // 3. Export to SD / Delete Logs buttons (relY = 178..210)
    if (relY >= 178 && relY <= 210) {
        hal::beep(30);
        if (x < 120) {
            bool ok = core::Logger::syncToSD();
            ui::UIManager::showToast(ok ? "Exported to /sd/logs/kernel.log" : "SD Card Not Mounted", 1600);
        } else {
            core::Logger::clearLog();
            m_logsJustDeleted = true;
            ui::UIManager::showToast("Kernel logs cleared!", 1400);
            m_needsFullRedraw = true;
        }
    }
}

void SettingsApp::handleUpdateResetTap(uint16_t x, uint16_t y) {
    (void)x;
    int relY = (int)y - HEADER_H;

    // 1. OTA Firmware Update (GitHub) button (relY = 120..156)
    if (relY >= 120 && relY <= 156) {
        hal::beep(45);
        core::OtaRecovery::triggerRebootToOtaRecovery();
        return;
    }

    // 2. Factory Reset All button (relY = 204..240)
    if (relY >= 204 && relY <= 240) {
        if (!m_confirmReset) {
            m_confirmReset = true;
            hal::beep(50);
            ui::UIManager::showToast("Tap again to confirm Factory Reset!", 1800);
            m_needsFullRedraw = true;
        } else {
            performFactoryResetAll();
        }
    } else if (m_confirmReset) {
        m_confirmReset = false;
        m_needsFullRedraw = true;
    }
}

void SettingsApp::handleAboutTap(uint16_t x, uint16_t y) {
    int vY = (int)y - HEADER_H;
    int contentY = vY + m_aboutScrollY;

    // Card 4 Update Button is located at content Y = 672..708
    if (contentY >= 672 && contentY <= 708 && x >= 14 && x <= 226) {
        hal::beep(35);
        if (WiFi.status() != WL_CONNECTED) {
            ui::UIManager::showToast("Connect Wi-Fi first to check updates!", 1600);
            return;
        }

        ui::UIManager::showToast("Checking GitHub Releases...", 1500);
        char newTag[24] = {0};
        if (core::OtaRecovery::checkForUpdate(newTag, sizeof(newTag))) {
            m_needsFullRedraw = true;
            ui::UIManager::showUpdatePopup(newTag);
        } else {
            m_needsFullRedraw = true;
            ui::UIManager::showToast(CYDOS_FULL_NAME " is up to date!", 1800);
        }
    }
}

void SettingsApp::handleTapAt(uint16_t x, uint16_t y) {
    // Top Header Bar
    if (y < HEADER_H) {
        if (m_currentPage != SettingsPage::MAIN_MENU && x <= 72) {
            hal::beep(20);
            onBackPressed();
        }
        return;
    }

    switch (m_currentPage) {
        case SettingsPage::MAIN_MENU:     handleMainMenuTap(x, y);     break;
        case SettingsPage::WIFI:          handleWifiTap(x, y);         break;
        case SettingsPage::DISPLAY_SOUND: handleDisplaySoundTap(x, y); break;
        case SettingsPage::SECURITY:      handleSecurityTap(x, y);     break;
        case SettingsPage::REGION_TIME:   handleRegionTimeTap(x, y);   break;
        case SettingsPage::SYSTEM_LOGS:   handleSystemLogsTap(x, y);   break;
        case SettingsPage::UPDATE_RESET:  handleUpdateResetTap(x, y);  break;
        case SettingsPage::ABOUT:         handleAboutTap(x, y);        break;
    }
}

void SettingsApp::onTouch(uint16_t x, uint16_t y, bool pressed) {
    int16_t ry = (int16_t)y;

    if (pressed) {
        m_lastTouchX = x;
        m_lastTouchY = y;

        // 1. Touch Drag Scrolling on ABOUT page
        if (m_currentPage == SettingsPage::ABOUT) {
            int bodyH = 274 - HEADER_H;
            int maxScroll = ABOUT_CONTENT_H - bodyH;
            if (maxScroll < 0) maxScroll = 0;

            if (!m_wasPressed) {
                m_wasPressed = true;
                m_isDraggingScroll = false;
                m_touchStartY = ry;
                m_scrollStartY = m_aboutScrollY;
            } else {
                int16_t dy = ry - m_touchStartY;
                if (!m_isDraggingScroll && ry > HEADER_H && abs(dy) > 6) {
                    m_isDraggingScroll = true;
                }
                if (m_isDraggingScroll) {
                    int16_t newScroll = m_scrollStartY - dy;
                    if (newScroll < 0) newScroll = 0;
                    if (newScroll > maxScroll) newScroll = maxScroll;
                    if (abs(newScroll - m_aboutScrollY) >= 2) {
                        m_aboutScrollY = newScroll;
                        m_needsFullRedraw = true;
                    }
                }
            }
            return;
        }

        // 2. Brightness Slider drag on DISPLAY_SOUND page
        if (m_currentPage == SettingsPage::DISPLAY_SOUND) {
            int relY = (int)y - HEADER_H;
            if (relY >= 20 && relY <= 58 && x >= 50 && x <= 190) {
                int pct = ((int)x - 54) * 100 / 132;
                if (pct < 10) pct = 10;
                if (pct > 100) pct = 100;
                if (pct != (int)m_brightness) {
                    m_brightness = (uint8_t)pct;
                    hal::setBacklight(m_brightness);
                    m_needsFullRedraw = true;
                }
                m_isDraggingSlider = true;
            }
        }
        m_wasPressed = true;
    } else if (m_wasPressed) {
        m_wasPressed = false;
        if (m_isDraggingSlider) {
            m_isDraggingSlider = false;
            Config::getInstance().setBrightness(m_brightness, true);
            return;
        }
        if (m_isDraggingScroll) {
            m_isDraggingScroll = false;
            return;
        }
        handleTapAt(m_lastTouchX, m_lastTouchY);
    }
}

} // namespace apps
