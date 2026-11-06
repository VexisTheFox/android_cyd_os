/**
 * @file UIManager.h
 * @brief Master UI Shell Coordinator, Lock Screen, Power Manager & Viewport State Machine
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "hal/DisplayHAL.h"
#include "hal/TouchHAL.h"
#include "core/App.h"
#include "core/AppRegistry.h"
#include "LauncherView.h"
#include "BootView.h"
#include "StatusBar.h"
#include "NavBar.h"

namespace ui {

using core::App;
using core::AppRegistry;

// UI Shell Coordinator States
enum class UIState : uint8_t {
    BOOT_SPLASH = 0,    ///< Initial Android boot logo and progress bar
    HOME_LAUNCHER,      ///< Main home interface with multi-page 2x3 app grid
    ACTIVE_APP          ///< An active native or dynamic script application
};

// Navigation Bar Action Identifiers
#ifndef UI_NAV_ACTION_DEFINED
#define UI_NAV_ACTION_DEFINED
enum class NavAction : uint8_t {
    NONE = 0,
    BACK,
    HOME,
    TASKS
};
#endif

class UIManager {
public:
    // Viewport Geometry Constants (Portrait 240x320)
    static constexpr int32_t STATUS_BAR_Y      = 0;
    static constexpr int32_t STATUS_BAR_HEIGHT = 20;   // Y: 0..19
    static constexpr int32_t VIEWPORT_Y        = 20;   // Y: 20..293
    static constexpr int32_t VIEWPORT_HEIGHT   = 274;  // Height: 274
    static constexpr int32_t NAV_BAR_Y         = 294;  // Y: 294..319
    static constexpr int32_t NAV_BAR_HEIGHT    = 26;   // Height: 26

    static void init();
    static void update();
    static void render(bool forceFullRedraw = false);

    static void setUIState(UIState newState);
    static UIState getUIState();

    static void launchApp(AppID appId);
    static void launchApp(size_t registryIndex);
    static void launchApp(App* app);
    static void exitToHome();
    static App* getActiveApp();

    static void setBootProgress(uint8_t percent, const char* stage = nullptr);
    static void finishBoot();

    static void showToast(const char* message, uint16_t durationMs = 1500);
    static void showUpdatePopup(const char* newTag);
    static void handleNavAction(NavAction action);
    static void pump();

    static void setOrientation(bool landscape);
    static bool isLandscape();

    /**
     * @brief Lock the device and optionally put the display backlight to sleep.
     * @param sleepDisplay If true, turns off backlight until BOOT power button or touch wake.
     */
    static void lockScreen(bool sleepDisplay = false);
    static void unlockScreen();
    static bool isLocked();
    static bool isScreenAsleep();

private:
    static UIState  s_state;
    static App*     s_activeApp;
    static bool     s_isBuiltinApp;
    static uint32_t s_bootStartTime;
    static uint32_t s_lastStatusUpdate;
    static uint32_t s_toastHideTime;
    static char     s_toastMsg[48];

    // Power & Lock Screen state
    static bool     s_isLocked;
    static bool     s_screenAsleep;
    static bool     s_lockedFromLandscape;
    static uint32_t s_lastActivityMs;
    static uint32_t s_sleepEnterMs;
    static uint32_t s_lastLockClockSec;
    static char     s_pinEntry[5];
    static uint8_t  s_pinLen;
    static bool     s_pinError;
    static int16_t  s_lockTouchStartY;

    static void createBuiltinApp(AppID id);
    static void destroyBuiltinApp();

    static void routeTouchEvents();
    static void drawToast();

    // Lock Screen & Power helpers
    static void checkPowerAndSleep();
    static void drawLockScreen(bool fullRedraw);
    static void drawLockPinDots();
    static void drawLockClock();
    static void handleLockScreenTouch(const hal::TouchEvent& event);
};

} // namespace ui
