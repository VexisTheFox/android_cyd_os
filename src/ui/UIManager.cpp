/**
 * @file UIManager.cpp
 * @brief Implementation of Master UI Shell Coordinator, Lock Screen, PIN Pad & Power Button
 */

#include "UIManager.h"
#include "hal/BoardHAL.h"
#include "hal/StorageHAL.h"
#include "core/App.h"
#include "core/AppRegistry.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/TimeService.h"
#include "ui/KeyboardView.h"
#include "apps/SettingsApp.h"
#include "apps/AppManagerApp.h"
#include "apps/WallpaperApp.h"
#include "apps/AboutApp.h"
#include "apps/SU7App.h"

namespace ui {

// Dynamic subsystem instances
static BootView*  s_bootViewInstance  = nullptr;
static StatusBar* s_statusBarInstance = nullptr;
static NavBar*    s_navBarInstance    = nullptr;

// ==========================================================================
// UIManager State Machine Implementation
// ==========================================================================

UIState   UIManager::s_state               = UIState::BOOT_SPLASH;
App*      UIManager::s_activeApp           = nullptr;
bool      UIManager::s_isBuiltinApp        = false;
static bool s_isCustomApp                  = false;
static bool s_isLandscape                  = false;
static App* s_retiredApp                   = nullptr;
uint32_t  UIManager::s_bootStartTime       = 0;
uint32_t  UIManager::s_lastStatusUpdate    = 0;
uint32_t  UIManager::s_toastHideTime       = 0;
char      UIManager::s_toastMsg[48]        = {0};

bool      UIManager::s_isLocked            = false;
bool      UIManager::s_screenAsleep        = false;
bool      UIManager::s_lockedFromLandscape = false;
uint32_t  UIManager::s_lastActivityMs      = 0;
uint32_t  UIManager::s_sleepEnterMs        = 0;
uint32_t  UIManager::s_lastLockClockSec    = 0;
char      UIManager::s_pinEntry[5]         = {0};
uint8_t   UIManager::s_pinLen              = 0;
bool      UIManager::s_pinError            = false;
int16_t   UIManager::s_lockTouchStartY     = 0;

void UIManager::init() {
    s_state = UIState::BOOT_SPLASH;
    s_activeApp = nullptr;
    s_isBuiltinApp = false;
    s_bootStartTime = millis();
    s_lastStatusUpdate = 0;
    s_toastHideTime = 0;
    s_toastMsg[0] = '\0';
    s_isLocked = false;
    s_screenAsleep = false;
    s_lockedFromLandscape = false;
    s_lastActivityMs = millis();
    s_pinLen = 0;
    s_pinEntry[0] = '\0';
    s_pinError = false;

    LauncherView::init();
    LauncherView::setAppLaunchCallback([](AppID id) {
        UIManager::launchApp(id);
    });

    if (!s_bootViewInstance) {
        s_bootViewInstance = new BootView(hal::getDisplay());
        s_bootViewInstance->render();
    }

    if (!s_statusBarInstance) {
        s_statusBarInstance = new StatusBar();
        s_statusBarInstance->init(&hal::getDisplay(), true);
    }

    if (!s_navBarInstance) {
        s_navBarInstance = new NavBar();
        s_navBarInstance->init(&hal::getDisplay(), false);
    }
    LOG_INFO("UI", "UI Shell initialized (BOOT_SPLASH)");
}

UIState UIManager::getUIState() {
    return s_state;
}

App* UIManager::getActiveApp() {
    return s_activeApp;
}

bool UIManager::isLocked() {
    return s_isLocked;
}

bool UIManager::isScreenAsleep() {
    return s_screenAsleep;
}

void UIManager::setUIState(UIState newState) {
    if (s_state == newState) return;
    s_state = newState;
    if (s_navBarInstance) {
        s_navBarInstance->clearPressedState();
    }
    LauncherView::clearPressedState();
    render(true);
}

void UIManager::setBootProgress(uint8_t percent, const char* stage) {
    if (s_bootViewInstance) {
        s_bootViewInstance->setProgress((int)percent, stage);
    }
}

void UIManager::finishBoot() {
    if (s_state == UIState::BOOT_SPLASH) {
        if (s_bootViewInstance) {
            delete s_bootViewInstance;
            s_bootViewInstance = nullptr;
        }
        s_lastActivityMs = millis();
        s_state = UIState::HOME_LAUNCHER;

        // First-Boot Region Selection via Horizontal QWERTY Keyboard (e.g. Czechia, USA, Albania)
        if (!Config::getInstance().hasRegion()) {
            LOG_INFO("UI", "First boot detected -> prompting for NTP Region on Horizontal Keyboard");
            char regBuf[24] = {0};
            bool ok = KeyboardView::prompt("1st Boot Region (Czechia, USA, Albania)", "Czechia", regBuf, sizeof(regBuf));
            if (!ok || regBuf[0] == '\0') {
                strncpy(regBuf, "Czechia", sizeof(regBuf) - 1);
            }
            Config::getInstance().setRegion(regBuf, true);
            core::TimeService::applySavedRegion();
            core::TimeService::syncNtpNow(1800);
        } else {
            core::TimeService::applySavedRegion();
        }

        // Start on the Lock Screen on boot if PIN lock is enabled
        if (Config::getInstance().isPinEnabled()) {
            LOG_INFO("UI", "Boot complete -> LOCK_SCREEN (PIN enabled)");
            lockScreen(false);
        } else {
            LOG_INFO("UI", "Boot splash complete -> HOME_LAUNCHER");
            render(true);
        }
    }
}

void UIManager::lockScreen(bool sleepDisplay) {
    if (s_state == UIState::BOOT_SPLASH) return;

    if (s_navBarInstance) {
        s_navBarInstance->clearPressedState();
    }
    LauncherView::clearPressedState();

    if (!s_isLocked) {
        s_lockedFromLandscape = s_isLandscape;
        if (s_isLandscape) {
            hal::setDisplayRotation(0);
            hal::setTouchRotation(0);
        }
    }

    s_isLocked = true;
    s_pinLen = 0;
    s_pinEntry[0] = '\0';
    s_pinError = false;

    if (sleepDisplay) {
        s_screenAsleep = true;
        s_sleepEnterMs = millis();
        hal::turnBacklightOff();
        hal::getDisplay().fillScreen(TFT_BLACK);
        LOG_INFO("POWER", "Screen OFF & Device Locked");
    } else {
        s_screenAsleep = false;
        hal::turnBacklightOn();
        drawLockScreen(true);
        LOG_INFO("POWER", "Lock Screen displayed (PIN=%s)",
                 Config::getInstance().isPinEnabled() ? "ON" : "OFF");
    }
}

void UIManager::unlockScreen() {
    if (!s_isLocked && !s_screenAsleep) return;

    s_isLocked = false;
    s_screenAsleep = false;
    s_pinLen = 0;
    s_pinEntry[0] = '\0';
    s_pinError = false;
    s_lastActivityMs = millis();

    hal::turnBacklightOn();
    if (s_lockedFromLandscape && s_state == UIState::ACTIVE_APP) {
        hal::setDisplayRotation(1);
        hal::setTouchRotation(1);
    }
    s_lockedFromLandscape = false;

    hal::beep(35);
    LOG_INFO("POWER", "Device Unlocked -> %s",
             (s_state == UIState::ACTIVE_APP) ? "ACTIVE_APP" : "HOME_LAUNCHER");

    // Force full redraw of underlying app or Home Launcher
    hal::getDisplay().fillScreen(TFT_BLACK);
    if (s_state == UIState::ACTIVE_APP && s_activeApp) {
        s_activeApp->onResume();
    }
    render(true);
}

void UIManager::drawLockClock() {
    TFT_eSPI &tft = hal::getDisplay();
    char timeBuf[16];
    core::TimeService::formatHHMMSS(timeBuf, sizeof(timeBuf));

    if (Config::getInstance().isPinEnabled()) {
        tft.setTextFont(1);
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(0x07FF, 0x0821);
        tft.drawString(timeBuf, 228, 30);
        tft.setTextDatum(TL_DATUM);
    } else {
        tft.setTextFont(4);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, 0x0821);
        tft.fillRect(48, 92, 144, 28, 0x0821);
        tft.drawString(timeBuf, 120, 106);
        tft.setTextDatum(TL_DATUM);
    }
}

void UIManager::drawLockPinDots() {
    TFT_eSPI &tft = hal::getDisplay();
    uint16_t boxBorder = s_pinError ? 0xF800 : 0x07FF;
    tft.fillRoundRect(20, 52, 200, 38, 6, 0x10A2);
    tft.drawRoundRect(20, 52, 200, 38, 6, boxBorder);

    if (s_pinError) {
        tft.setTextFont(1);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(0xF800, 0x10A2);
        tft.drawString("WRONG PIN", 120, 71);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    for (uint8_t i = 0; i < 4; i++) {
        int16_t cx = 66 + i * 36;
        int16_t cy = 71;
        if (i < s_pinLen) {
            tft.fillCircle(cx, cy, 8, 0x07E0);
            tft.drawCircle(cx, cy, 8, TFT_WHITE);
        } else {
            tft.drawCircle(cx, cy, 8, 0x52AA);
        }
    }
}

void UIManager::drawLockScreen(bool fullRedraw) {
    if (s_screenAsleep) return;

    TFT_eSPI &tft = hal::getDisplay();
    if (s_statusBarInstance) {
        s_statusBarInstance->render(fullRedraw);
    }

    if (!fullRedraw) {
        drawLockClock();
        return;
    }

    // Fill lockscreen viewport (Y = 20..319)
    tft.fillRect(0, 20, 240, 300, 0x0821);

    if (!Config::getInstance().isPinEnabled()) {
        // =====================================================================
        // SWIPE / TAP LOCK SCREEN (PIN DISABLED) - COMPACT & FAST
        // =====================================================================
        tft.setTextFont(1);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(0xFFE0, 0x0821);
        tft.drawString("[DEV BUILD - LOCKED]", 120, 30);

        // Padlock Vector Icon at (120, 62)
        tft.drawRoundRect(111, 44, 18, 16, 6, 0x07FF);
        tft.fillRoundRect(106, 56, 28, 22, 4, 0x18E3);
        tft.drawRoundRect(106, 56, 28, 22, 4, 0x07FF);
        tft.fillCircle(120, 67, 3, 0x07E0);

        drawLockClock();

        tft.setTextFont(1);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(0x07FF, 0x0821);
        tft.drawString("CYD OS v2.0", 120, 132);

        // Compact Status Pill (Y = 156..196)
        tft.fillRoundRect(24, 156, 192, 40, 6, 0x10A2);
        tft.drawRoundRect(24, 156, 192, 40, 6, 0x3186);

        char regSum[24];
        core::TimeService::formatRegionSummary(regSum, sizeof(regSum));
        char infoBuf[40];
        snprintf(infoBuf, sizeof(infoBuf), "RAM %luK | %s",
                 (unsigned long)(ESP.getFreeHeap() / 1024), regSum);
        tft.setTextColor(TFT_WHITE, 0x10A2);
        tft.drawString(infoBuf, 120, 169);

        tft.setTextColor(0x9CD3, 0x10A2);
        tft.drawString(core::TimeService::isSynced() ? "NTP Synced | BOOT=Sleep" : "Clock Local | BOOT=Sleep", 120, 184);

        // Unlock Capsule Button (Y = 236..284)
        tft.fillRoundRect(24, 236, 192, 48, 12, 0x04D3);
        tft.drawRoundRect(24, 236, 192, 48, 12, 0x07E0);
        tft.setTextFont(2);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, 0x04D3);
        tft.drawString("TAP TO UNLOCK", 120, 260);
        tft.setTextDatum(TL_DATUM);
    } else {
        // =====================================================================
        // 4-DIGIT PIN LOCK SCREEN - COMPACT & FAST
        // =====================================================================
        tft.setTextFont(1);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(TFT_WHITE, 0x0821);
        tft.drawString("ENTER PIN [DEV]", 14, 30);

        drawLockClock();
        drawLockPinDots();

        // 3x4 Numeric Keypad (Y = 98..306)
        const char* keys[12] = {
            "1", "2", "3",
            "4", "5", "6",
            "7", "8", "9",
            "CLR", "0", "DEL"
        };
        int startX = 18;
        int startY = 98;
        int keyW   = 64;
        int keyH   = 46;
        int gapX   = 6;
        int gapY   = 6;

        for (int i = 0; i < 12; i++) {
            int col = i % 3;
            int row = i / 3;
            int kx = startX + col * (keyW + gapX);
            int ky = startY + row * (keyH + gapY);

            uint16_t kBg = 0x18E3;
            uint16_t kBorder = 0x3186;
            uint16_t kFg = TFT_WHITE;
            if (i == 9) { // CLR
                kBg = 0x3800;
                kBorder = 0xF800;
                kFg = 0xFFE0;
            } else if (i == 11) { // DEL
                kBg = 0x2166;
                kBorder = 0x07FF;
                kFg = 0x07FF;
            }

            tft.fillRoundRect(kx, ky, keyW, keyH, 6, kBg);
            tft.drawRoundRect(kx, ky, keyW, keyH, 6, kBorder);
            tft.setTextFont(2);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(kFg, kBg);
            tft.drawString(keys[i], kx + keyW / 2, ky + keyH / 2);
        }
        tft.setTextDatum(TL_DATUM);
    }
}

void UIManager::handleLockScreenTouch(const hal::TouchEvent& event) {
    s_lastActivityMs = millis();

    if (!Config::getInstance().isPinEnabled()) {
        if (event.state == hal::TouchState::DOWN) {
            s_lockTouchStartY = (int16_t)event.y;
            // Direct tap on unlock capsule button (Y: 235..296)
            if (event.y >= 235 && event.y <= 298 && event.x >= 16 && event.x <= 224) {
                unlockScreen();
            }
        } else if (event.state == hal::TouchState::UP) {
            // Upward swipe anywhere unlocks
            int16_t dy = (int16_t)event.y - s_lockTouchStartY;
            if (dy < -35) {
                unlockScreen();
            }
        }
        return;
    }

    // PIN Keypad touch handling with strict anti-double-click edge arming + 185ms debounce
    static uint32_t s_lastPinTapMs = 0;
    static bool     s_pinKeyArmed  = true;

    if (event.state == hal::TouchState::UP || event.state == hal::TouchState::NONE) {
        s_pinKeyArmed = true;
        return;
    }
    if (event.state != hal::TouchState::DOWN || !s_pinKeyArmed) {
        return;
    }
    uint32_t nowMs = millis();
    if (nowMs - s_lastPinTapMs < 185) {
        return;
    }

    int startX = 18;
    int startY = 98;
    int keyW   = 64;
    int keyH   = 46;
    int gapX   = 6;
    int gapY   = 6;

    if ((int)event.y < startY || (int)event.y > startY + 4 * (keyH + gapY)) {
        return;
    }

    for (int i = 0; i < 12; i++) {
        int col = i % 3;
        int row = i / 3;
        int kx = startX + col * (keyW + gapX);
        int ky = startY + row * (keyH + gapY);

        if ((int)event.x >= kx && (int)event.x <= (kx + keyW) &&
            (int)event.y >= ky && (int)event.y <= (ky + keyH)) {
            s_pinKeyArmed = false;
            s_lastPinTapMs = nowMs;
            hal::beep(20);
            s_pinError = false;

            if (i == 9) {
                // CLR
                s_pinLen = 0;
                s_pinEntry[0] = '\0';
                drawLockPinDots();
                return;
            }
            if (i == 11) {
                // DEL
                if (s_pinLen > 0) {
                    s_pinLen--;
                    s_pinEntry[s_pinLen] = '\0';
                }
                drawLockPinDots();
                return;
            }

            char digit = (i == 10) ? '0' : (char)('1' + i);
            if (s_pinLen < 4) {
                s_pinEntry[s_pinLen++] = digit;
                s_pinEntry[s_pinLen] = '\0';
                drawLockPinDots();
            }

            if (s_pinLen == 4) {
                if (strncmp(s_pinEntry, Config::getInstance().getPinCode(), 4) == 0) {
                    unlockScreen();
                } else {
                    LOG_WARN("SECURITY", "Incorrect PIN entered on Lock Screen");
                    s_pinError = true;
                    s_pinLen = 0;
                    s_pinEntry[0] = '\0';
                    hal::playTone(400, 120);
                    drawLockPinDots();
                }
            }
            return;
        }
    }
}

void UIManager::checkPowerAndSleep() {
    uint32_t now = millis();

    // 1. Hardware BOOT Button (GPIO 0) acts as Power / Sleep / Wake Button
    if (hal::pollPowerButton()) {
        s_lastActivityMs = now;
        hal::beep(25);
        if (!s_screenAsleep) {
            // Screen is currently awake -> Lock and put display to sleep!
            lockScreen(true);
        } else {
            // Screen is currently asleep -> Wake up to Lock Screen!
            lockScreen(false);
        }
        return;
    }

    // 2. Auto-Sleep Inactivity Timeout (when awake)
    if (!s_screenAsleep && s_state != UIState::BOOT_SPLASH) {
        uint16_t timeoutSec = Config::getInstance().getSleepTimeoutSec();
        if (timeoutSec > 0 && (now - s_lastActivityMs >= (uint32_t)timeoutSec * 1000UL)) {
            s_lastActivityMs = now;
            LOG_INFO("POWER", "Auto-sleep inactivity timeout (%us) -> Screen OFF & Locked", timeoutSec);
            lockScreen(true);
        }
    }
}

void UIManager::showToast(const char* message, uint16_t durationMs) {
    if (!message) return;
    strncpy(s_toastMsg, message, sizeof(s_toastMsg) - 1);
    s_toastMsg[sizeof(s_toastMsg) - 1] = '\0';
    s_toastHideTime = millis() + durationMs;
    if (!s_screenAsleep && !s_isLocked) {
        drawToast();
    }
}

void UIManager::setOrientation(bool landscape) {
    if (s_isLandscape == landscape) return;
    s_isLandscape = landscape;
    LOG_INFO("UI", "Orientation compat switch -> %s", landscape ? "Landscape (320x240)" : "Portrait (240x320)");

    hal::setDisplayRotation(landscape ? 1 : 0);
    hal::setTouchRotation(landscape ? 1 : 0);
    hal::getDisplay().fillScreen(TFT_BLACK);

    if (s_statusBarInstance) {
        s_statusBarInstance->render(true);
    }
    if (s_navBarInstance) {
        s_navBarInstance->clearPressedState();
        s_navBarInstance->render(true);
    }
}

bool UIManager::isLandscape() {
    return s_isLandscape;
}

void UIManager::drawToast() {
    if (s_toastHideTime == 0 || s_toastMsg[0] == '\0' || s_screenAsleep) return;

    TFT_eSPI &tft = hal::getDisplay();
    int16_t screenW = tft.width();
    int16_t navBarY = tft.height() - NAV_BAR_HEIGHT;

    tft.setTextFont(2);
    int16_t tw = tft.textWidth(s_toastMsg) + 20;
    if (tw > screenW - 8) tw = screenW - 8;
    int16_t th = 26;
    int16_t tx = (screenW - tw) / 2;
    int16_t ty = navBarY - 32;

    tft.fillRoundRect(tx, ty, tw, th, 12, 0x18C3);
    tft.drawRoundRect(tx, ty, tw, th, 12, 0x52AA);
    tft.setTextColor(TFT_WHITE, 0x18C3);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(s_toastMsg, screenW / 2, ty + (th / 2));
}

void UIManager::pump() {
    checkPowerAndSleep();

    // If locked or asleep while a blocking Lua script is running, stay in a modal lock loop
    while (s_screenAsleep || s_isLocked) {
        checkPowerAndSleep();
        routeTouchEvents();
        uint32_t lNow = millis();
        if (!s_screenAsleep && s_isLocked && (lNow / 1000 != s_lastLockClockSec)) {
            s_lastLockClockSec = lNow / 1000;
            if (s_statusBarInstance) {
                s_statusBarInstance->pollTelemetry();
                if (s_statusBarInstance->isDirty()) {
                    s_statusBarInstance->render(false);
                }
            }
            drawLockScreen(false);
        }
        delay(10);
    }

    routeTouchEvents();
    if (s_navBarInstance) {
        s_navBarInstance->update();
    }

    uint32_t now = millis();
    if (s_statusBarInstance && (now - s_lastStatusUpdate >= 1000)) {
        s_lastStatusUpdate = now;
        s_statusBarInstance->pollTelemetry();
        if (s_statusBarInstance->isDirty()) {
            s_statusBarInstance->render(false);
        }
    }
}

void UIManager::handleNavAction(NavAction action) {
    switch (action) {
        case NavAction::BACK:
        case NavAction::HOME:
            if (core::DynamicScriptApp::isScriptRunning()) {
                core::DynamicScriptApp::requestAbort();
                hal::beep(30);
                return;
            }
            if (s_state == UIState::ACTIVE_APP) {
                exitToHome();
                hal::beep(30);
            } else {
                if (s_navBarInstance) {
                    s_navBarInstance->clearPressedState();
                }
                LauncherView::clearPressedState();
                LauncherView::render(true);
                hal::beep(15);
            }
            break;

        case NavAction::TASKS:
            // Launch App Manager quickly from Recents/Tasks button if on Home, or show toast
            if (s_state == UIState::HOME_LAUNCHER) {
                int idx = core::AppRegistry::findAppIndex("appmgr");
                if (idx >= 0) {
                    hal::beep(25);
                    launchApp((size_t)idx);
                    return;
                }
            }
            showToast("App Manager available on Home", 1200);
            hal::beep(25);
            break;

        default:
            break;
    }
}

void UIManager::routeTouchEvents() {
    hal::TouchEvent event;
    bool hasEvent = hal::pollTouch(event);

    if (!hasEvent) {
        return;
    }

    s_lastActivityMs = millis();

    // If screen is asleep, touching the screen (after 450ms guard) wakes to Lock Screen
    if (s_screenAsleep) {
        if (event.state == hal::TouchState::DOWN && (millis() - s_sleepEnterMs > 450)) {
            hal::beep(20);
            lockScreen(false);
        }
        return;
    }

    // If Lock Screen is active, route all touches exclusively to Lock Screen / PIN pad
    if (s_isLocked) {
        handleLockScreenTouch(event);
        return;
    }

    // Always clear any stuck button states on global touch UP!
    if (event.state == hal::TouchState::UP) {
        if (s_navBarInstance && s_navBarInstance->getPressedButton() != NavAction::NONE) {
            s_navBarInstance->clearPressedState();
        }
    }

    // -------------------------------------------------------------
    // PRIORITY 1: Bottom Navigation Bar
    // -------------------------------------------------------------
    int16_t dynamicNavY = hal::getDisplay().height() - NAV_BAR_HEIGHT;
    if ((int16_t)event.y >= (dynamicNavY - 6)) {
        // Clear any pressed Home icon if finger moved into NavBar
        LauncherView::clearPressedState();
        if (s_navBarInstance) {
            NavAction action = s_navBarInstance->processTouchEvent(event);
            if (action != NavAction::NONE) {
                handleNavAction(action);
            }
        }
        return;
    } else if (s_navBarInstance && s_navBarInstance->getPressedButton() != NavAction::NONE) {
        // Finger moved above NavBar: clear NavBar highlight immediately
        s_navBarInstance->clearPressedState();
    }

    // -------------------------------------------------------------
    // PRIORITY 2: Top Status Bar (Y < 20)
    // -------------------------------------------------------------
    if (event.y < STATUS_BAR_HEIGHT) {
        LauncherView::clearPressedState();
        if (event.state == hal::TouchState::DOWN) {
            char buf[48];
            snprintf(buf, sizeof(buf), "RAM: %luKB | BOOT=Power/Lock", (unsigned long)(ESP.getFreeHeap() / 1024));
            showToast(buf, 1100);
        }
        return;
    }

    // -------------------------------------------------------------
    // PRIORITY 3: Viewport Area (Y = 20..293)
    // -------------------------------------------------------------
    if (s_state == UIState::HOME_LAUNCHER) {
        LauncherView::handleTouch(event);
    } else if (s_state == UIState::ACTIVE_APP && s_activeApp) {
        bool isDown = (event.state != hal::TouchState::UP);
        s_activeApp->onTouch(event.x, event.y - VIEWPORT_Y, isDown);
    }
}

void UIManager::createBuiltinApp(AppID id) {
    destroyBuiltinApp();

    switch (id) {
        case AppID::SETTINGS:
            s_activeApp = new apps::SettingsApp();
            break;
        case AppID::APP_MANAGER:
            s_activeApp = new apps::AppManagerApp();
            break;
        case AppID::WALLPAPERS:
            s_activeApp = new apps::WallpaperApp();
            break;
        case AppID::ABOUT:
            s_activeApp = new apps::AboutApp();
            break;
        case AppID::SU7_3D:
            s_activeApp = new apps::SU7App();
            break;
        case AppID::GALLERY:
            s_activeApp = new core::DynamicScriptApp("/apps/gallery.lua", "Gallery");
            break;
        case AppID::CALCULATOR:
            s_activeApp = new core::DynamicScriptApp("/apps/calc.lua", "Calc");
            break;
        case AppID::PAINT:
            s_activeApp = new core::DynamicScriptApp("/apps/paint.lua", "Paint");
            break;
        case AppID::SYSINFO:
            s_activeApp = new core::DynamicScriptApp("/apps/sysinfo.lua", "Sysinfo");
            break;
        case AppID::FILES:
            s_activeApp = new core::DynamicScriptApp("/apps/files.lua", "Files");
            break;
        case AppID::TERMINAL:
            s_activeApp = new core::DynamicScriptApp("/apps/terminal.lua", "Lua IDE");
            break;
        default:
            s_activeApp = new apps::AboutApp();
            break;
    }
    s_isBuiltinApp = true;
}

void UIManager::destroyBuiltinApp() {
    if (s_activeApp && s_isBuiltinApp) {
        delete s_activeApp;
        s_activeApp = nullptr;
        s_isBuiltinApp = false;
    }
}

void UIManager::launchApp(size_t registryIndex) {
    LauncherView::clearPressedState();
    if (s_navBarInstance) {
        s_navBarInstance->clearPressedState();
    }
    destroyBuiltinApp();
    if (core::AppRegistry::launchApp(registryIndex)) {
        s_activeApp = core::AppRegistry::getActiveApp();
        s_isBuiltinApp = false;
        s_state = UIState::ACTIVE_APP;
        LOG_INFO("UI", "Launched registry app [%u]: %s", (unsigned)registryIndex,
                 s_activeApp ? s_activeApp->getName() : "unknown");
        render(true);
    } else {
        LOG_ERROR("UI", "Failed to launch registry app index %u", (unsigned)registryIndex);
    }
}

void UIManager::launchApp(AppID appId) {
    LauncherView::clearPressedState();
    if ((size_t)appId < core::AppRegistry::getAppCount()) {
        launchApp((size_t)appId);
        return;
    }
    createBuiltinApp(appId);
    if (s_activeApp) {
        s_state = UIState::ACTIVE_APP;
        LOG_INFO("UI", "Launched builtin app: %s", s_activeApp->getName());
        if (s_activeApp->getState() == core::AppState::UNINITIALIZED) {
            s_activeApp->onCreate();
        }
        s_activeApp->onResume();
        render(true);
    }
}

void UIManager::launchApp(App* app) {
    if (!app) return;
    LauncherView::clearPressedState();
    destroyBuiltinApp();
    if (s_isCustomApp && s_activeApp) {
        delete s_activeApp;
        s_activeApp = nullptr;
    }
    s_activeApp = app;
    s_isBuiltinApp = false;
    s_isCustomApp = true;
    s_state = UIState::ACTIVE_APP;
    LOG_INFO("UI", "Launched custom app: %s", s_activeApp->getName());
    if (s_activeApp->getState() == core::AppState::UNINITIALIZED) {
        s_activeApp->onCreate();
    }
    s_activeApp->onResume();
    render(true);
}

void UIManager::exitToHome() {
    // Clear any pressed button state on NavBar and LauncherView immediately!
    if (s_navBarInstance) {
        s_navBarInstance->clearPressedState();
    }
    LauncherView::clearPressedState();

    if (s_state == UIState::ACTIVE_APP) {
        if (s_activeApp) {
            LOG_INFO("UI", "Exiting app: %s -> HOME_LAUNCHER", s_activeApp->getName());
            s_activeApp->onPause();
            s_activeApp->onDestroy();
            if (s_isBuiltinApp) {
                if (s_retiredApp && s_retiredApp != s_activeApp) {
                    delete s_retiredApp;
                }
                s_retiredApp = s_activeApp;
                s_isBuiltinApp = false;
            } else if (s_isCustomApp) {
                if (s_retiredApp && s_retiredApp != s_activeApp) {
                    delete s_retiredApp;
                }
                s_retiredApp = s_activeApp;
                s_isCustomApp = false;
            } else {
                core::AppRegistry::exitCurrentApp();
            }
            s_activeApp = nullptr;
        }
        hal::setRGBLED(0, 0, 0);
        setOrientation(false);
        setUIState(UIState::HOME_LAUNCHER);
    }
}

void UIManager::render(bool forceFullRedraw) {
    if (s_screenAsleep) return;
    if (s_isLocked) {
        drawLockScreen(forceFullRedraw);
        return;
    }

    TFT_eSPI &tft = hal::getDisplay();

    switch (s_state) {
        case UIState::BOOT_SPLASH: {
            if (s_bootViewInstance) {
                s_bootViewInstance->render();
            } else {
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(0x3EE0, TFT_BLACK);
                tft.setTextFont(4);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("CYD ANDROID OS", 120, 140);
                tft.setTextFont(2);
                tft.drawString("Booting...", 120, 180);
            }
            break;
        }

        case UIState::HOME_LAUNCHER: {
            if (s_statusBarInstance) {
                s_statusBarInstance->render(forceFullRedraw);
            }
            LauncherView::render(forceFullRedraw);
            if (s_navBarInstance) {
                s_navBarInstance->render(forceFullRedraw);
            }
            break;
        }

        case UIState::ACTIVE_APP: {
            if (s_statusBarInstance) {
                s_statusBarInstance->render(forceFullRedraw);
            }
            if (s_activeApp) {
                int16_t vpW = tft.width();
                int16_t vpH = tft.height() - STATUS_BAR_HEIGHT - NAV_BAR_HEIGHT;
                s_activeApp->onRender(tft, 0, VIEWPORT_Y, vpW, vpH);
            }
            if (s_navBarInstance) {
                s_navBarInstance->render(forceFullRedraw);
            }
            break;
        }
    }

    if (s_toastHideTime > 0) {
        if (millis() < s_toastHideTime) {
            drawToast();
        } else {
            s_toastHideTime = 0;
            s_toastMsg[0] = '\0';
        }
    }
}

void UIManager::update() {
    uint32_t now = millis();

    // 0. Poll Hardware BOOT Power Button & Auto-Sleep Timer
    checkPowerAndSleep();

    // 1. Process Touch Events & Navigation Overrides
    routeTouchEvents();

    // If display is asleep, skip UI rendering
    if (s_screenAsleep) {
        return;
    }

    // If Lock Screen is active, update status bar and live lock clock once per second
    if (s_isLocked) {
        if (now / 1000 != s_lastLockClockSec) {
            s_lastLockClockSec = now / 1000;
            if (s_statusBarInstance) {
                s_statusBarInstance->pollTelemetry();
                if (s_statusBarInstance->isDirty()) {
                    s_statusBarInstance->render(false);
                }
            }
            drawLockScreen(false);
        }
        return;
    }

    // Auto-unpress NavBar highlight pill after pulse
    if (s_navBarInstance) {
        s_navBarInstance->update();
    }

    // 2. State-Specific View Machine Tick
    switch (s_state) {
        case UIState::BOOT_SPLASH: {
            if (s_bootViewInstance) {
                s_bootViewInstance->update();
                if (s_bootViewInstance->isComplete()) {
                    finishBoot();
                    break;
                }
            }
            if (now - s_bootStartTime > 2200) {
                finishBoot();
            }
            break;
        }

        case UIState::HOME_LAUNCHER: {
            if (s_statusBarInstance && (now - s_lastStatusUpdate >= 1000)) {
                s_lastStatusUpdate = now;
                s_statusBarInstance->pollTelemetry();
                if (s_statusBarInstance->isDirty()) {
                    s_statusBarInstance->render(false);
                }
            }
            core::AppRegistry::gc();
            if (s_retiredApp) { delete s_retiredApp; s_retiredApp = nullptr; }
            core::AppRegistry::gc();
            LauncherView::update();
            break;
        }

        case UIState::ACTIVE_APP: {
            if (s_statusBarInstance && (now - s_lastStatusUpdate >= 1000)) {
                s_lastStatusUpdate = now;
                s_statusBarInstance->pollTelemetry();
                if (s_statusBarInstance->isDirty()) {
                    s_statusBarInstance->render(false);
                }
            }
            if (s_activeApp) {
                s_activeApp->onUpdate();
                if (s_state == UIState::ACTIVE_APP && s_activeApp && !s_isLocked && !s_screenAsleep) {
                    TFT_eSPI &tft = hal::getDisplay();
                    int16_t vpW = tft.width();
                    int16_t vpH = tft.height() - STATUS_BAR_HEIGHT - NAV_BAR_HEIGHT;
                    s_activeApp->onRender(tft, 0, VIEWPORT_Y, vpW, vpH);
                }
            }
            core::AppRegistry::gc();
            if (s_retiredApp) {
                delete s_retiredApp;
                s_retiredApp = nullptr;
            }
            break;
        }
    }

    // 3. Dismiss expired toast overlays
    if (s_toastHideTime > 0 && now >= s_toastHideTime) {
        s_toastHideTime = 0;
        s_toastMsg[0] = '\0';
        render(true);
    }
}

} // namespace ui
