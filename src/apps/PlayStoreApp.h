/**
 * @file PlayStoreApp.h
 * @brief Dynamic Cloud Play Store & OTA Lua App Downloader for CYD OS
 *
 * Connected to live GitHub repository:
 *   https://github.com/VexisTheFox/cydos_appstore
 *   Live manifest: https://raw.githubusercontent.com/VexisTheFox/cydos_appstore/main/catalog.json
 *
 * Features:
 *   - Dynamically fetches and parses catalog.json from VexisTheFox/cydos_appstore over Wi-Fi
 *   - Caches catalog to LittleFS (/apps/.catalog.json) so zero heap is wasted during TLS
 *   - Category filter bar (ALL, NET, GAMES, MEDIA, TOOLS) + 1-tap [SYNC] button in header
 *   - Renders custom 16x16 Base64 icons (-- @icon_b64) & custom names (-- @name)
 *   - One-tap Wi-Fi OTA download of any store app to /apps/<id>.lua
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <vector>
#include "../core/App.h"

namespace apps {

enum class StoreCategory : uint8_t {
    ALL       = 0,
    INTERNET  = 1,
    GAMES     = 2,
    MEDIA     = 3,
    UTILITIES = 4
};

struct StoreCatalogItem {
    char          id[20];
    char          title[22];
    char          categoryName[10];
    StoreCategory category;
    char          desc[36];
    char          localPath[32];
    char          cloudUrl[128];
    char          iconB64[96];
    uint16_t      iconBg;
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
    static constexpr int HEADER_H  = 68; // Title (22px) + Tab bar (22px) + Category bar (24px)
    static constexpr int FOOTER_H  = 36; // Action buttons bar at bottom
    static constexpr int ROW_H     = 42; // Height of each store app card

    std::vector<StoreCatalogItem> m_catalog;
    bool     m_syncedThisSession;

    uint8_t  m_activeTab;       // 0 = APP STORE CATALOG, 1 = CUSTOM URL
    uint8_t  m_activeCategory;  // 0 = ALL, 1 = NET, 2 = GAMES, 3 = MEDIA, 4 = TOOLS
    int16_t  m_scrollY;
    int16_t  m_selectedIndex;   // Index into filtered items
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

    bool syncCatalogFromGitHub(bool showToastMsg);
    bool loadCatalogFromCache();

    int  getFilteredCount() const;
    int  getCatalogIndexFromFiltered(int filteredIdx) const;
    bool isInstalled(int catalogIdx) const;
    int  getMaxScroll(int listH) const;

    void drawHeader(TFT_eSPI &display, int vx, int vy, int vw);
    void drawListArea(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void drawUrlTab(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void drawFooter(TFT_eSPI &display, int vx, int vy, int vw, int vh);

    void handleTap(int16_t rx, int16_t ry, int vw, int vh);
    void installFeaturedApp(int catalogIdx);
    void uninstallFeaturedApp(int catalogIdx);
    void openFeaturedApp(int catalogIdx);
    bool downloadUrlToPath(const char* url, const char* destPath);
};

} // namespace apps
