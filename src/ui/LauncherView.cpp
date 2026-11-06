/**
 * @file LauncherView.cpp
 * @brief Multi-Page Android-Style Home App Launcher (Max 6 Apps Per Page, Development Build)
 */

#include "LauncherView.h"
#include "hal/BoardHAL.h"
#include "core/AppRegistry.h"
#include "core/Config.h"
#include "apps/WallpaperApp.h"

namespace ui {

AppItem LauncherView::s_fallbackApps[9] = {
    { AppID::SETTINGS,    "Settings", 0x324B, TFT_WHITE, "System & About" },
    { AppID::APP_MANAGER, "App Mgr",  0x04D3, TFT_WHITE, "Manage & Install" },
    { AppID::SU7_3D,      "SU7 3D",   0xFEE0, 0x10A2,    "3D Telemetry Demo" },
    { AppID::GALLERY,     "Gallery",  0xD8A7, TFT_WHITE, "SD Photo Viewer" },
    { AppID::FILES,       "Files",    0xFD00, TFT_WHITE, "SD App Installer" },
    { AppID::SYSINFO,     "SysInfo",  0x05FE, TFT_BLACK, "Telemetry & Logs" },
    { AppID::CALCULATOR,  "Calc",     0x2C9F, TFT_WHITE, "Math Calculator" },
    { AppID::PAINT,       "Paint",    0x9175, TFT_WHITE, "Touch Canvas" },
    { AppID::TERMINAL,    "Lua IDE",  0x1CE6, TFT_WHITE, "Script Engine" }
};

AppItem           LauncherView::s_dynamicItem    = { AppID::TERMINAL, "App", 0x04D3, TFT_WHITE, "App" };
AppLaunchCallback LauncherView::s_callback       = nullptr;
int8_t            LauncherView::s_pressedSlot    = -1;
uint32_t          LauncherView::s_pressTimestamp = 0;
bool              LauncherView::s_isTouched      = false;
bool              LauncherView::s_needsRedraw    = true;
uint8_t           LauncherView::s_currentPage    = 0;
int16_t           LauncherView::s_touchDownX     = 0;
int16_t           LauncherView::s_touchDownY     = 0;

void LauncherView::init() {
    s_pressedSlot    = -1;
    s_pressTimestamp = 0;
    s_isTouched      = false;
    s_needsRedraw    = true;
    s_currentPage    = 0;
}

void LauncherView::setAppLaunchCallback(AppLaunchCallback callback) {
    s_callback = callback;
}

void LauncherView::clearPressedState() {
    if (s_pressedSlot >= 0) {
        uint8_t prev = (uint8_t)s_pressedSlot;
        s_pressedSlot = -1;
        drawAppIcon(prev, false);
    }
}

uint8_t LauncherView::getTotalPages() {
    size_t count = core::AppRegistry::getAppCount();
    if (count == 0) count = 9;
    uint8_t pages = (uint8_t)((count + APPS_PER_PAGE - 1) / APPS_PER_PAGE);
    return (pages == 0) ? 1 : pages;
}

void LauncherView::nextPage() {
    uint8_t total = getTotalPages();
    if (total > 1) {
        s_currentPage = (s_currentPage + 1) % total;
        s_pressedSlot = -1;
        render(true);
    }
}

void LauncherView::prevPage() {
    uint8_t total = getTotalPages();
    if (total > 1) {
        s_currentPage = (s_currentPage == 0) ? (total - 1) : (s_currentPage - 1);
        s_pressedSlot = -1;
        render(true);
    }
}

static AppID mapDescriptorToGlyphId(const core::AppDescriptor* desc) {
    if (!desc) return AppID::TERMINAL;
    if (strcasecmp(desc->name, "Settings") == 0 || strcasecmp(desc->id, "settings") == 0) return AppID::SETTINGS;
    if (strcasecmp(desc->name, "App Mgr") == 0 || strcasecmp(desc->id, "appmgr") == 0) return AppID::APP_MANAGER;
    if (strcasecmp(desc->name, "Wallpapers") == 0 || strcasecmp(desc->id, "wallpapers") == 0) return AppID::WALLPAPERS;
    if (strcasecmp(desc->name, "About") == 0 || strcasecmp(desc->id, "about") == 0) return AppID::ABOUT;
    if (strcasecmp(desc->name, "SU7 3D") == 0 || strcasecmp(desc->name, "SU7 Ultra") == 0 || strcasecmp(desc->id, "su7") == 0) return AppID::SU7_3D;
    if (strcasecmp(desc->name, "Gallery") == 0 || strcasecmp(desc->id, "gallery") == 0) return AppID::GALLERY;
    if (strcasecmp(desc->name, "Calc") == 0 || strcasecmp(desc->id, "calc") == 0) return AppID::CALCULATOR;
    if (strcasecmp(desc->name, "Paint") == 0 || strcasecmp(desc->id, "paint") == 0) return AppID::PAINT;
    if (strcasecmp(desc->name, "SysInfo") == 0 || strcasecmp(desc->id, "sysinfo") == 0) return AppID::SYSINFO;
    if (strcasecmp(desc->name, "Files") == 0 || strcasecmp(desc->id, "files") == 0) return AppID::FILES;
    if (strcasecmp(desc->name, "Lua IDE") == 0 || strcasecmp(desc->id, "terminal") == 0) return AppID::TERMINAL;
    return AppID::TERMINAL;
}

const AppItem& LauncherView::getAppInfo(uint8_t globalIndex) {
    if (core::AppRegistry::getAppCount() > globalIndex) {
        const core::AppDescriptor* desc = core::AppRegistry::getAppInfo(globalIndex);
        if (desc) {
            s_dynamicItem.title       = desc->name;
            s_dynamicItem.iconBgColor = desc->iconBgColor;
            s_dynamicItem.iconFgColor = desc->iconFgColor;
            s_dynamicItem.id          = mapDescriptorToGlyphId(desc);
            s_dynamicItem.subtitle    = desc->path;
            return s_dynamicItem;
        }
    }
    return s_fallbackApps[globalIndex % 9];
}

void LauncherView::getIconCenter(uint8_t slotIndex, int16_t &cx, int16_t &cy) {
    uint8_t col = slotIndex % GRID_COLS;
    uint8_t row = slotIndex / GRID_COLS;

    cx = col * COL_WIDTH + (COL_WIDTH / 2); // 60, 180
    cy = GRID_TOP_Y + (row * ROW_HEIGHT) + 6 + (ICON_SIZE / 2);
}

void LauncherView::getIconTopLeft(uint8_t slotIndex, int16_t &x, int16_t &y) {
    int16_t cx = 0, cy = 0;
    getIconCenter(slotIndex, cx, cy);
    x = cx - (ICON_SIZE / 2);
    y = cy - (ICON_SIZE / 2);
}

void LauncherView::getHitbox(uint8_t slotIndex, int16_t &x, int16_t &y, int16_t &w, int16_t &h) {
    uint8_t col = slotIndex % GRID_COLS;
    uint8_t row = slotIndex / GRID_COLS;

    int16_t colCenter = col * COL_WIDTH + (COL_WIDTH / 2);
    x = colCenter - (HITBOX_WIDTH / 2);
    y = GRID_TOP_Y + (row * ROW_HEIGHT) + 2;
    w = HITBOX_WIDTH;
    h = HITBOX_HEIGHT;
}

int8_t LauncherView::getHitAppIndex(uint16_t x, uint16_t y) {
    if (y < GRID_TOP_Y || y >= PAGER_BAR_Y) {
        return -1;
    }

    size_t totalApps = core::AppRegistry::getAppCount();
    if (totalApps == 0) totalApps = 9;

    for (uint8_t slot = 0; slot < APPS_PER_PAGE; slot++) {
        size_t globalIdx = (size_t)s_currentPage * APPS_PER_PAGE + slot;
        if (globalIdx >= totalApps) break;

        int16_t hx = 0, hy = 0, hw = 0, hh = 0;
        getHitbox(slot, hx, hy, hw, hh);

        if (x >= (uint16_t)hx && x < (uint16_t)(hx + hw) &&
            y >= (uint16_t)hy && y < (uint16_t)(hy + hh)) {
            return (int8_t)slot;
        }
    }
    return -1;
}

void LauncherView::drawWallpaper() {
    TFT_eSPI &tft = hal::getDisplay();

    uint8_t wpId = Config::getInstance().getWallpaperId();
    if (wpId == CONFIG_WALLPAPER_CUSTOM_SD) {
        const char* wpPath = Config::getInstance().getWallpaperPath();
        if (apps::WallpaperApp::renderWallpaperImage(wpPath, VIEWPORT_X, VIEWPORT_Y + 16, VIEWPORT_WIDTH, VIEWPORT_HEIGHT - 54)) {
            return;
        }
        wpId = 0; // Fallback if SD removed
    }

    int32_t topR = 8, topG = 20, topB = 38;
    int32_t botR = 0, botG = 2,  botB = 6;

    switch (wpId) {
        case 1: // Synthwave Sunset
            topR = 28; topG = 4;  topB = 58;
            botR = 72; botG = 8;  botB = 42;
            break;
        case 2: // Emerald Matrix
            topR = 2;  topG = 32; topB = 14;
            botR = 0;  botG = 6;  botB = 2;
            break;
        case 3: // SU7 Ultra Gold
            topR = 24; topG = 22; topB = 12;
            botR = 4;  botG = 4;  botB = 4;
            break;
        case 4: // Crimson Nebula
            topR = 54; topG = 4;  topB = 18;
            botR = 8;  botG = 0;  botB = 4;
            break;
        case 5: // Arctic Aurora
            topR = 4;  topG = 38; topB = 62;
            botR = 2;  botG = 10; botB = 22;
            break;
        default: // 0: Cyber Slate
            break;
    }

    const int32_t bandH = 16;
    for (int32_t line = 0; line < VIEWPORT_HEIGHT; line += bandH) {
        int32_t h = (line + bandH > VIEWPORT_HEIGHT) ? (VIEWPORT_HEIGHT - line) : bandH;
        int32_t r = topR + (botR - topR) * line / VIEWPORT_HEIGHT;
        int32_t g = topG + (botG - topG) * line / VIEWPORT_HEIGHT;
        int32_t b = topB + (botB - topB) * line / VIEWPORT_HEIGHT;
        uint16_t color = tft.color565(r, g, b);
        tft.fillRect(VIEWPORT_X, VIEWPORT_Y + line, VIEWPORT_WIDTH, h, color);
    }

    // Theme-specific vector accents
    if (wpId == 1) {
        // Synthwave Retro Sun & Horizon Grid
        tft.fillCircle(120, VIEWPORT_Y + 135, 34, 0xFD20);
        tft.drawFastHLine(0, VIEWPORT_Y + 150, 240, 0xF81F);
        tft.drawFastHLine(0, VIEWPORT_Y + 175, 240, 0x780F);
        tft.drawFastHLine(0, VIEWPORT_Y + 205, 240, 0x780F);
    } else if (wpId == 2) {
        // Matrix Digital Rain Bars
        for (int x = 20; x < 230; x += 34) {
            tft.drawFastVLine(x, VIEWPORT_Y + 24, 190, 0x03E0);
        }
    } else if (wpId == 3) {
        // SU7 Racing Diagonal Stripes
        tft.drawLine(0, VIEWPORT_Y + 210, 239, VIEWPORT_Y + 50, 0x5260);
        tft.drawLine(0, VIEWPORT_Y + 212, 239, VIEWPORT_Y + 52, 0xFFE0);
    } else if (wpId == 4) {
        // Crimson Nebula Rings
        tft.drawCircle(120, VIEWPORT_Y + 130, 55, 0x7800);
        tft.drawCircle(120, VIEWPORT_Y + 130, 32, 0xF800);
    } else if (wpId == 5) {
        // Arctic Aurora Ribbon
        tft.drawFastHLine(16, VIEWPORT_Y + 100, 208, 0x07FF);
        tft.drawFastHLine(28, VIEWPORT_Y + 145, 184, 0x07E0);
    }
}

void LauncherView::drawTopDevBanner() {
    TFT_eSPI &tft = hal::getDisplay();
    tft.fillRect(VIEWPORT_X, VIEWPORT_Y, VIEWPORT_WIDTH, 16, 0x10A2);
    tft.drawFastHLine(VIEWPORT_X, VIEWPORT_Y + 16, VIEWPORT_WIDTH, 0x324B);

    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(0xFFE0, 0x10A2);
    tft.drawString("CYD OS - DEVELOPMENT BUILD", VIEWPORT_X + 6, VIEWPORT_Y + 4);

    char pageBuf[20];
    snprintf(pageBuf, sizeof(pageBuf), "PG %u/%u", (unsigned)(s_currentPage + 1), (unsigned)getTotalPages());
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(0x07FF, 0x10A2);
    tft.drawString(pageBuf, VIEWPORT_X + VIEWPORT_WIDTH - 6, VIEWPORT_Y + 4);
    tft.setTextDatum(TL_DATUM);
}

void LauncherView::drawBottomPagerBar() {
    TFT_eSPI &tft = hal::getDisplay();
    uint8_t totalPages = getTotalPages();

    tft.fillRect(VIEWPORT_X, PAGER_BAR_Y, VIEWPORT_WIDTH, 38, 0x0821);
    tft.drawFastHLine(VIEWPORT_X + 8, PAGER_BAR_Y, VIEWPORT_WIDTH - 16, 0x324B);

    // 1. < PREV Page Button (X=8..72, Y=PAGER_BAR_Y+5, W=64, H=26)
    tft.fillRoundRect(8, PAGER_BAR_Y + 5, 64, 26, 5, 0x18C3);
    tft.drawRoundRect(8, PAGER_BAR_Y + 5, 64, 26, 5, (totalPages > 1) ? 0x07FF : 0x324B);
    tft.setTextFont(1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor((totalPages > 1) ? 0x07FF : 0x52AA, 0x18C3);
    tft.drawString("< PREV", 40, PAGER_BAR_Y + 18);

    // 2. Center Page Indicator Dots
    int16_t dotSpan = (int16_t)totalPages * 14;
    int16_t startX  = 120 - (dotSpan / 2) + 7;
    for (uint8_t p = 0; p < totalPages; p++) {
        int16_t dx = startX + p * 14;
        int16_t dy = PAGER_BAR_Y + 18;
        if (p == s_currentPage) {
            tft.fillCircle(dx, dy, 4, 0x3EE0);
            tft.drawCircle(dx, dy, 4, TFT_WHITE);
        } else {
            tft.fillCircle(dx, dy, 3, 0x4208);
        }
    }

    // 3. NEXT > Page Button (X=168..232, Y=PAGER_BAR_Y+5, W=64, H=26)
    tft.fillRoundRect(168, PAGER_BAR_Y + 5, 64, 26, 5, 0x18C3);
    tft.drawRoundRect(168, PAGER_BAR_Y + 5, 64, 26, 5, (totalPages > 1) ? 0x07FF : 0x324B);
    tft.setTextColor((totalPages > 1) ? 0x07FF : 0x52AA, 0x18C3);
    tft.drawString("NEXT >", 200, PAGER_BAR_Y + 18);

    tft.setTextDatum(TL_DATUM);
}

void LauncherView::render(bool forceFullRedraw) {
    if (forceFullRedraw) {
        // Always clear any lingering pressed slot when performing a full Home Screen redraw!
        s_pressedSlot = -1;
        s_isTouched = false;
    }

    if (forceFullRedraw || s_needsRedraw) {
        uint8_t totalPages = getTotalPages();
        if (s_currentPage >= totalPages) {
            s_currentPage = totalPages - 1;
        }

        drawWallpaper();
        drawTopDevBanner();

        size_t totalApps = core::AppRegistry::getAppCount();
        if (totalApps == 0) totalApps = 9;

        for (uint8_t slot = 0; slot < APPS_PER_PAGE; slot++) {
            size_t globalIdx = (size_t)s_currentPage * APPS_PER_PAGE + slot;
            if (globalIdx < totalApps) {
                drawAppIcon(slot, (s_pressedSlot == (int8_t)slot));
            }
        }

        drawBottomPagerBar();
        s_needsRedraw = false;
    }
}

void LauncherView::update() {
    // Guarantee app icons never stay stuck in pressed state if touch was released
    if (s_pressedSlot >= 0) {
        if (hal::getTouchState() == hal::TouchState::NONE || (millis() - s_pressTimestamp > 600)) {
            clearPressedState();
        }
    }
}

void LauncherView::drawAppIcon(uint8_t slotIndex, bool pressed) {
    if (slotIndex >= APPS_PER_PAGE) return;

    size_t totalApps = core::AppRegistry::getAppCount();
    if (totalApps == 0) totalApps = 9;
    size_t globalIdx = (size_t)s_currentPage * APPS_PER_PAGE + slotIndex;
    if (globalIdx >= totalApps) return;

    TFT_eSPI &tft = hal::getDisplay();
    const AppItem &app = getAppInfo((uint8_t)globalIdx);

    int16_t iconX = 0, iconY = 0;
    int16_t cx = 0, cy = 0;
    getIconTopLeft(slotIndex, iconX, iconY);
    getIconCenter(slotIndex, cx, cy);

    if (pressed) {
        tft.drawRoundRect(iconX - 2, iconY - 2, ICON_SIZE + 4, ICON_SIZE + 4, ICON_RADIUS + 2, COLOR_PRESS_RING);
        tft.drawRoundRect(iconX - 1, iconY - 1, ICON_SIZE + 2, ICON_SIZE + 2, ICON_RADIUS + 1, COLOR_PRESS_BORDER);
        tft.fillRoundRect(iconX, iconY, ICON_SIZE, ICON_SIZE, ICON_RADIUS, app.iconBgColor);
    } else {
        tft.drawRoundRect(iconX - 2, iconY - 2, ICON_SIZE + 4, ICON_SIZE + 4, ICON_RADIUS + 2, COLOR_WALLPAPER_TOP);
        tft.drawRoundRect(iconX - 1, iconY - 1, ICON_SIZE + 2, ICON_SIZE + 2, ICON_RADIUS + 1, COLOR_WALLPAPER_TOP);
        tft.fillRoundRect(iconX, iconY, ICON_SIZE, ICON_SIZE, ICON_RADIUS, app.iconBgColor);
        tft.drawFastHLine(iconX + ICON_RADIUS, iconY + 1, ICON_SIZE - (2 * ICON_RADIUS), 0xFFFF);
        tft.drawRoundRect(iconX, iconY, ICON_SIZE, ICON_SIZE, ICON_RADIUS, 0x4A69);
    }

    drawIconGlyph(app.id, cx, cy, app.iconFgColor);

    int16_t labelY = iconY + ICON_SIZE + 4;
    tft.setTextFont(1);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COLOR_LABEL_TEXT, 0x0821);

    tft.fillRoundRect(cx - 44, labelY - 1, 88, 10, 3, 0x0821);
    tft.drawString(app.title, cx, labelY);
}

void LauncherView::drawIconGlyph(AppID id, int16_t cx, int16_t cy, uint16_t fgColor) {
    switch (id) {
        case AppID::SETTINGS:    drawSettingsGlyph(cx, cy, fgColor);   break;
        case AppID::APP_MANAGER: drawAppManagerGlyph(cx, cy, fgColor); break;
        case AppID::WALLPAPERS:  drawWallpapersGlyph(cx, cy, fgColor); break;
        case AppID::ABOUT:       drawAboutGlyph(cx, cy, fgColor);      break;
        case AppID::SU7_3D:      drawSU7Glyph(cx, cy, fgColor);        break;
        case AppID::GALLERY:     drawGalleryGlyph(cx, cy, fgColor);    break;
        case AppID::CALCULATOR:  drawCalcGlyph(cx, cy, fgColor);       break;
        case AppID::PAINT:       drawPaintGlyph(cx, cy, fgColor);      break;
        case AppID::SYSINFO:     drawSysInfoGlyph(cx, cy, fgColor);    break;
        case AppID::FILES:       drawFilesGlyph(cx, cy, fgColor);      break;
        case AppID::TERMINAL:    drawTerminalGlyph(cx, cy, fgColor);   break;
        default:                 drawTerminalGlyph(cx, cy, fgColor);   break;
    }
}

void LauncherView::drawSettingsGlyph(int16_t cx, int16_t cy, uint16_t fgColor) {
    TFT_eSPI &tft = hal::getDisplay();
    tft.drawCircle(cx, cy, 9, fgColor);
    tft.drawCircle(cx, cy, 8, fgColor);
    tft.fillCircle(cx, cy, 4, 0x324B);

    tft.fillRect(cx - 3, cy - 13, 6, 4, fgColor);
    tft.fillRect(cx - 3, cy + 9,  6, 4, fgColor);
    tft.fillRect(cx - 13, cy - 3, 4, 6, fgColor);
    tft.fillRect(cx + 9,  cy - 3, 4, 6, fgColor);

    tft.fillRect(cx - 9, cy - 9, 3, 3, fgColor);
    tft.fillRect(cx + 6, cy - 9, 3, 3, fgColor);
    tft.fillRect(cx - 9, cy + 6, 3, 3, fgColor);
    tft.fillRect(cx + 6, cy + 6, 3, 3, fgColor);
}

void LauncherView::drawAppManagerGlyph(int16_t cx, int16_t cy, uint16_t fgColor) {
    TFT_eSPI &tft = hal::getDisplay();
    // 2x2 Android App Grid tiles with a plus/install tile in bottom-right
    tft.fillRoundRect(cx - 12, cy - 12, 10, 10, 2, fgColor);
    tft.fillRoundRect(cx + 2,  cy - 12, 10, 10, 2, fgColor);
    tft.fillRoundRect(cx - 12, cy + 2,  10, 10, 2, fgColor);
    // Highlighted bottom-right package tile
    tft.drawRoundRect(cx + 2,  cy + 2,  10, 10, 2, 0xFFE0);
    tft.drawFastHLine(cx + 4,  cy + 7,  6, 0xFFE0);
    tft.drawFastVLine(cx + 7,  cy + 4,  6, 0xFFE0);
}

void LauncherView::drawWallpapersGlyph(int16_t cx, int16_t cy, uint16_t fgColor) {
    TFT_eSPI &tft = hal::getDisplay();
    // Frame + glowing sun + mountain horizon + sparkle
    tft.drawRoundRect(cx - 13, cy - 11, 26, 22, 3, fgColor);
    tft.fillCircle(cx + 5, cy - 4, 4, 0xFFE0);
    tft.drawLine(cx - 11, cy + 8, cx - 3, cy - 1, 0x07FF);
    tft.drawLine(cx - 3,  cy - 1, cx + 4, cy + 6, 0x07FF);
    tft.drawLine(cx + 1,  cy + 4, cx + 7, cy - 1, fgColor);
    tft.drawLine(cx + 7,  cy - 1, cx + 11, cy + 8, fgColor);
}

void LauncherView::drawAboutGlyph(int16_t cx, int16_t cy, uint16_t fgColor) {
    TFT_eSPI &tft = hal::getDisplay();
    tft.fillCircle(cx, cy - 1, 10, fgColor);
    tft.fillRect(cx - 11, cy - 1, 23, 11, 0x3EE0);
    tft.drawLine(cx - 6, cy - 7, cx - 10, cy - 12, fgColor);
    tft.drawLine(cx + 6, cy - 7, cx + 10, cy - 12, fgColor);
    tft.fillCircle(cx - 4, cy - 5, 1, 0x3EE0);
    tft.fillCircle(cx + 4, cy - 5, 1, 0x3EE0);
    tft.fillRoundRect(cx - 8, cy + 1, 16, 11, 2, fgColor);
    tft.fillRoundRect(cx - 12, cy + 2, 3, 9, 1, fgColor);
    tft.fillRoundRect(cx + 9,  cy + 2, 3, 9, 1, fgColor);
    tft.drawFastHLine(cx - 8, cy, 16, 0x3EE0);
}

void LauncherView::drawSU7Glyph(int16_t cx, int16_t cy, uint16_t fgColor) {
    TFT_eSPI &tft = hal::getDisplay();
    tft.drawLine(cx - 14, cy + 3, cx - 8, cy - 3, fgColor);
    tft.drawLine(cx - 8, cy - 3, cx + 3, cy - 3, fgColor);
    tft.drawLine(cx + 3, cy - 3, cx + 13, cy + 3, fgColor);
    tft.drawFastHLine(cx - 15, cy + 4, 30, fgColor);
    tft.drawFastHLine(cx - 15, cy - 7, 8, fgColor);
    tft.drawLine(cx - 12, cy - 7, cx - 11, cy - 3, fgColor);
    tft.fillCircle(cx - 8, cy + 5, 4, fgColor);
    tft.fillCircle(cx + 8, cy + 5, 4, fgColor);
    tft.fillCircle(cx - 8, cy + 5, 2, 0xFEE0);
    tft.fillCircle(cx + 8, cy + 5, 2, 0xFEE0);
    tft.drawFastHLine(cx + 10, cy + 5, 5, fgColor);
}

void LauncherView::drawGalleryGlyph(int16_t cx, int16_t cy, uint16_t fgColor) {
    TFT_eSPI &tft = hal::getDisplay();
    tft.drawRoundRect(cx - 13, cy - 10, 26, 20, 2, fgColor);
    tft.drawRoundRect(cx - 12, cy - 9, 24, 18, 2, fgColor);
    tft.fillCircle(cx - 6, cy - 4, 3, 0xFFE0);
    tft.drawLine(cx - 10, cy + 7, cx - 2, cy - 1, fgColor);
    tft.drawLine(cx - 2,  cy - 1, cx + 4, cy + 5, fgColor);
    tft.drawLine(cx + 1,  cy + 2, cx + 6, cy - 3, fgColor);
    tft.drawLine(cx + 6,  cy - 3, cx + 11, cy + 7, fgColor);
}

void LauncherView::drawCalcGlyph(int16_t cx, int16_t cy, uint16_t fgColor) {
    TFT_eSPI &tft = hal::getDisplay();
    tft.fillRoundRect(cx - 11, cy - 13, 22, 6, 1, fgColor);
    tft.drawFastHLine(cx - 9, cy - 2, 5, fgColor);
    tft.drawFastVLine(cx - 7, cy - 4, 5, fgColor);
    tft.drawFastHLine(cx + 3, cy - 2, 5, fgColor);
    tft.drawLine(cx - 9, cy + 4, cx - 5, cy + 8, fgColor);
    tft.drawLine(cx - 5, cy + 4, cx - 9, cy + 8, fgColor);
    tft.drawFastHLine(cx + 3, cy + 5, 5, fgColor);
    tft.drawFastHLine(cx + 3, cy + 8, 5, fgColor);
}

void LauncherView::drawPaintGlyph(int16_t cx, int16_t cy, uint16_t fgColor) {
    TFT_eSPI &tft = hal::getDisplay();
    tft.fillCircle(cx, cy, 11, fgColor);
    tft.fillCircle(cx + 4, cy + 4, 3, 0x9175);
    tft.fillCircle(cx - 5, cy - 4, 2, TFT_RED);
    tft.fillCircle(cx,     cy - 7, 2, TFT_YELLOW);
    tft.fillCircle(cx + 5, cy - 3, 2, TFT_CYAN);
    tft.fillCircle(cx - 5, cy + 3, 2, TFT_GREEN);
}

void LauncherView::drawSysInfoGlyph(int16_t cx, int16_t cy, uint16_t fgColor) {
    TFT_eSPI &tft = hal::getDisplay();
    tft.fillRect(cx - 8, cy - 8, 16, 16, fgColor);
    tft.drawRect(cx - 6, cy - 6, 12, 12, 0x05FE);
    tft.fillRect(cx - 2, cy - 2, 4, 4, TFT_WHITE);
    tft.drawFastVLine(cx - 4, cy - 12, 4, fgColor);
    tft.drawFastVLine(cx + 4, cy - 12, 4, fgColor);
    tft.drawFastVLine(cx - 4, cy + 8,  4, fgColor);
    tft.drawFastVLine(cx + 4, cy + 8,  4, fgColor);
    tft.drawFastHLine(cx - 12, cy - 4, 4, fgColor);
    tft.drawFastHLine(cx - 12, cy + 4, 4, fgColor);
    tft.drawFastHLine(cx + 8,  cy - 4, 4, fgColor);
    tft.drawFastHLine(cx + 8,  cy + 4, 4, fgColor);
}

void LauncherView::drawFilesGlyph(int16_t cx, int16_t cy, uint16_t fgColor) {
    TFT_eSPI &tft = hal::getDisplay();
    tft.fillRect(cx - 11, cy - 9, 10, 4, fgColor);
    tft.fillRoundRect(cx - 11, cy - 6, 22, 16, 2, fgColor);
    tft.fillRect(cx - 9, cy - 2, 18, 10, 0xFD00);
    tft.drawFastHLine(cx - 9, cy - 2, 18, fgColor);
}

void LauncherView::drawTerminalGlyph(int16_t cx, int16_t cy, uint16_t fgColor) {
    TFT_eSPI &tft = hal::getDisplay();
    tft.fillRoundRect(cx - 12, cy - 9, 24, 18, 2, 0x0821);
    tft.drawRoundRect(cx - 12, cy - 9, 24, 18, 2, fgColor);
    tft.drawLine(cx - 8, cy - 4, cx - 4, cy - 1, TFT_GREEN);
    tft.drawLine(cx - 4, cy - 1, cx - 8, cy + 2, TFT_GREEN);
    tft.drawFastHLine(cx - 1, cy + 3, 5, TFT_WHITE);
}

int8_t LauncherView::handleTouch(const hal::TouchEvent& event) {
    switch (event.state) {
        case hal::TouchState::DOWN: {
            s_touchDownX = (int16_t)event.x;
            s_touchDownY = (int16_t)event.y;

            if (event.y >= PAGER_BAR_Y && event.y <= (VIEWPORT_Y + VIEWPORT_HEIGHT)) {
                if (event.x >= 6 && event.x <= 78) {
                    hal::beep(20);
                    prevPage();
                } else if (event.x >= 162 && event.x <= 236) {
                    hal::beep(20);
                    nextPage();
                }
                return -1;
            }

            int8_t hit = getHitAppIndex(event.x, event.y);
            if (hit >= 0) {
                s_pressedSlot = hit;
                s_pressTimestamp = millis();
                drawAppIcon((uint8_t)hit, true);
                hal::beep(30);
            }
            break;
        }

        case hal::TouchState::MOVE: {
            if (s_pressedSlot >= 0) {
                int8_t currentHit = getHitAppIndex(event.x, event.y);
                if (currentHit != s_pressedSlot) {
                    clearPressedState();
                }
            }
            break;
        }

        case hal::TouchState::UP: {
            int16_t dx = (int16_t)event.x - s_touchDownX;
            int16_t dy = (int16_t)event.y - s_touchDownY;

            if (abs(dx) > 48 && abs(dx) > abs(dy) * 2) {
                clearPressedState();
                hal::beep(20);
                if (dx < 0) nextPage();
                else        prevPage();
                return -1;
            }

            if (s_pressedSlot >= 0) {
                uint8_t slot = (uint8_t)s_pressedSlot;
                s_pressedSlot = -1;
                drawAppIcon(slot, false);

                uint8_t globalIndex = s_currentPage * APPS_PER_PAGE + slot;
                if (s_callback) {
                    s_callback((AppID)globalIndex);
                }
                return (int8_t)globalIndex;
            }
            break;
        }

        default:
            break;
    }
    return -1;
}

int8_t LauncherView::handleTouchPoint(uint16_t x, uint16_t y, bool pressed) {
    hal::TouchEvent ev;
    ev.x = x;
    ev.y = y;
    if (pressed && !s_isTouched) {
        s_isTouched = true;
        ev.state = hal::TouchState::DOWN;
    } else if (pressed && s_isTouched) {
        ev.state = hal::TouchState::MOVE;
    } else if (!pressed && s_isTouched) {
        s_isTouched = false;
        ev.state = hal::TouchState::UP;
    } else {
        return -1;
    }
    return handleTouch(ev);
}

} // namespace ui
