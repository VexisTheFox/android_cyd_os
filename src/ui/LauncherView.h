/**
 * @file LauncherView.h
 * @brief Multi-Page Android-Style Home App Launcher (Max 6 Apps Per Page, Development Build)
 * 
 * Viewport Dimensions: 240x274 pixels (Y = 20..293, between Status Bar and Nav Bar)
 * Grid: 2 columns x 3 rows (max 6 app shortcuts per page, dynamic multi-page support)
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "hal/DisplayHAL.h"
#include "hal/TouchHAL.h"

namespace ui {

// Application Identifiers for Launcher Shortcuts & Icon Glyphs
enum class AppID : uint8_t {
    SETTINGS = 0,   ///< System Settings & About (Scrollable)
    APP_MANAGER,    ///< Native Application Manager & SD Installer
    WALLPAPERS,     ///< Native Wallpaper Selector (Prefilled + SD Card)
    SU7_3D,         ///< Xiaomi SU7 Ultra 3D Real-Time Demo
    GALLERY,        ///< SD Card & Flash Picture Gallery
    FILES,          ///< File Manager & SD App Installer
    SYSINFO,        ///< Hardware Performance Telemetry & Kernel Log
    CALCULATOR,     ///< Interactive Calculator
    PAINT,          ///< Touch Paint & Sketchpad
    TERMINAL,       ///< Lua Scripting Engine & Terminal
    ABOUT,          ///< Legacy About ID (maps to Settings About section)
    COUNT
};

// Application Launcher Item Descriptor
struct AppItem {
    AppID id;               ///< Glyph / Application ID
    const char* title;      ///< Display title label
    uint16_t iconBgColor;   ///< RGB565 base color for icon tile
    uint16_t iconFgColor;   ///< RGB565 accent/glyph color
    const char* subtitle;   ///< Short description or category
};

// Callback type invoked when an application is tapped (receives global registry index cast as AppID)
typedef void (*AppLaunchCallback)(AppID appId);

class LauncherView {
public:
    // Viewport Geometry Constants (Portrait 240x274)
    static constexpr int32_t VIEWPORT_X       = 0;
    static constexpr int32_t VIEWPORT_Y       = 20;
    static constexpr int32_t VIEWPORT_WIDTH   = 240;
    static constexpr int32_t VIEWPORT_HEIGHT  = 274;

    // Grid Layout Constants (Portrait: 2 columns x 3 rows = max 6 apps per page)
    static constexpr uint8_t GRID_COLS        = 2;   ///< 2 columns
    static constexpr uint8_t GRID_ROWS        = 3;   ///< 3 rows
    static constexpr uint8_t APPS_PER_PAGE    = 6;   ///< Max 6 apps per page
    static constexpr uint8_t TOTAL_APPS       = 6;   ///< 6 slots per page
    static constexpr int32_t GRID_TOP_Y       = VIEWPORT_Y + 18; ///< Below Dev Build banner (Y=38)
    static constexpr int32_t COL_WIDTH        = 120; ///< 240 / 2 = 120 px slot
    static constexpr int32_t ROW_HEIGHT       = 72;  ///< 3 rows x 72 = 216 px (Y=38..254)
    static constexpr int32_t PAGER_BAR_Y      = VIEWPORT_Y + 236; ///< Bottom pager bar (Y=256..292)
    static constexpr int32_t ICON_SIZE        = 44;  ///< 44x44 px icon
    static constexpr int32_t ICON_RADIUS      = 8;   ///< 8 px rounded corners

    // Resistive Touch Hitbox Dimensions
    static constexpr int32_t HITBOX_WIDTH     = 100; ///< 100 px wide
    static constexpr int32_t HITBOX_HEIGHT    = 66;  ///< 66 px tall

    // Android Theme Color Palette
    static constexpr uint16_t COLOR_WALLPAPER_TOP = 0x08A5; ///< Deep Tech Slate Navy
    static constexpr uint16_t COLOR_WALLPAPER_BOT = 0x0000; ///< Midnight Pitch Black
    static constexpr uint16_t COLOR_WALLPAPER_DOT = 0x18C3; ///< Subtle background grid dot
    static constexpr uint16_t COLOR_LABEL_TEXT    = 0xFFFF; ///< Crisp White for labels
    static constexpr uint16_t COLOR_PRESS_RING    = 0x07FF; ///< Neon Cyan press ring
    static constexpr uint16_t COLOR_PRESS_BORDER  = 0xFFFF; ///< White inner press border

    static void init();
    static void render(bool forceFullRedraw = false);
    static void update();
    static void clearPressedState();
    static int8_t handleTouch(const hal::TouchEvent& event);
    static int8_t handleTouchPoint(uint16_t x, uint16_t y, bool pressed);
    static int8_t getHitAppIndex(uint16_t x, uint16_t y);
    static void setAppLaunchCallback(AppLaunchCallback callback);
    static void drawAppIcon(uint8_t slotIndex, bool pressed);
    static const AppItem& getAppInfo(uint8_t globalIndex);
    static void drawWallpaper();

    static uint8_t getCurrentPage() { return s_currentPage; }
    static uint8_t getTotalPages();
    static void nextPage();
    static void prevPage();

private:
    static AppItem s_fallbackApps[9];
    static AppItem s_dynamicItem;
    static AppLaunchCallback s_callback;
    static int8_t   s_pressedSlot;
    static uint32_t s_pressTimestamp;
    static bool     s_isTouched;
    static bool     s_needsRedraw;
    static uint8_t  s_currentPage;
    static int16_t  s_touchDownX;
    static int16_t  s_touchDownY;

    static void drawTopDevBanner();
    static void drawBottomPagerBar();

    // Vector Glyph Renderers for 44x44 Android App Icons
    static void drawIconGlyph(AppID id, int16_t cx, int16_t cy, uint16_t fgColor);
    static void drawSettingsGlyph(int16_t cx, int16_t cy, uint16_t fgColor);
    static void drawAppManagerGlyph(int16_t cx, int16_t cy, uint16_t fgColor);
    static void drawWallpapersGlyph(int16_t cx, int16_t cy, uint16_t fgColor);
    static void drawAboutGlyph(int16_t cx, int16_t cy, uint16_t fgColor);
    static void drawSU7Glyph(int16_t cx, int16_t cy, uint16_t fgColor);
    static void drawGalleryGlyph(int16_t cx, int16_t cy, uint16_t fgColor);
    static void drawCalcGlyph(int16_t cx, int16_t cy, uint16_t fgColor);
    static void drawPaintGlyph(int16_t cx, int16_t cy, uint16_t fgColor);
    static void drawSysInfoGlyph(int16_t cx, int16_t cy, uint16_t fgColor);
    static void drawFilesGlyph(int16_t cx, int16_t cy, uint16_t fgColor);
    static void drawTerminalGlyph(int16_t cx, int16_t cy, uint16_t fgColor);

    // Coordinate calculation helpers for slot (0..5) on current page
    static void getIconCenter(uint8_t slotIndex, int16_t &cx, int16_t &cy);
    static void getIconTopLeft(uint8_t slotIndex, int16_t &x, int16_t &y);
    static void getHitbox(uint8_t slotIndex, int16_t &x, int16_t &y, int16_t &w, int16_t &h);
};

} // namespace ui
