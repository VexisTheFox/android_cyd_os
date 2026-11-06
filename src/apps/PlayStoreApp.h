/**
 * @file PlayStoreApp.h
 * @brief Native Android-Style Play Store & OTA Lua App Downloader for CYD OS
 *
 * Features:
 *   - Browse featured Lua apps with custom Base64 icons (-- @icon_b64) & custom names (-- @name)
 *   - One-tap Wi-Fi OTA download from GitHub (with instant offline PROGMEM fallback)
 *   - Custom HTTPS/HTTP URL installer with 512-byte chunked streaming to /apps/
 *   - Launch or uninstall store apps directly and sync Home Screen launcher pages
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "../core/App.h"

namespace apps {

struct StoreCatalogItem {
    const char* id;
    const char* title;
    const char* category;
    const char* desc;
    const char* localPath;
    const char* cloudUrl;
    const char* iconB64;
    uint16_t    iconBg;
    const char* progmemSrc;
};

class PlayStoreApp : public core::App {
public:
    PlayStoreApp();
    virtual ~PlayStoreApp() = default;

    void onCreate() override;
    void onResume() override;
    void onUpdate() override;
    void onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) override;
    void onTouch(uint16_t x, uint16_t y, bool pressed) override;
    void onPause() override;
    void onDestroy() override;

    const char* getName() const override { return "Play Store"; }
    const char* getIcon() const override { return "ICON_PLAYSTORE"; }
    const char* getCategory() const override { return "System"; }

private:
    static constexpr int HEADER_H  = 46; // Title bar (22px) + Tab bar (24px)
    static constexpr int FOOTER_H  = 36; // Action buttons bar at bottom
    static constexpr int ROW_H     = 42; // Height of each store app card

    uint8_t  m_activeTab;      // 0 = FEATURED APPS, 1 = URL / OTA GET
    int16_t  m_scrollY;
    int16_t  m_selectedIndex;
    bool     m_wasPressed;
    bool     m_isDragging;
    int16_t  m_touchStartY;
    int16_t  m_scrollStartY;
    int16_t  m_lastTouchY;
    uint32_t m_lastScrollDrawMs;

    bool     m_needsFullRedraw;
    bool     m_needsListRedraw;
    bool     m_needsFooterRedraw;

    char     m_customUrl[128];
    char     m_customFile[32];
    char     m_statusLine[48];

    bool isInstalled(int idx) const;
    int  getMaxScroll(int listH) const;

    void drawHeader(TFT_eSPI &display, int vx, int vy, int vw);
    void drawListArea(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void drawUrlTab(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void drawFooter(TFT_eSPI &display, int vx, int vy, int vw, int vh);

    void handleTap(int16_t rx, int16_t ry, int vw, int vh);
    void installFeaturedApp(int idx);
    void uninstallFeaturedApp(int idx);
    void openFeaturedApp(int idx);
    bool downloadUrlToPath(const char* url, const char* destPath);
};

} // namespace apps
