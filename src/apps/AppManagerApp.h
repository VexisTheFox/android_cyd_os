/**
 * @file AppManagerApp.h
 * @brief Native Android-Style Application Manager & Package Installer for CYD OS
 *
 * Features:
 *   - View all registered system, bundled, local (/apps), and SD (/sd/apps) apps
 *   - Launch any app directly from App Manager
 *   - Uninstall local (/apps/) or SD apps and update Home Screen pages
 *   - Reset bundled core Lua apps to factory defaults
 *   - Browse & one-tap install .lua apps from MicroSD card to local Flash (/apps)
 *   - Zero-flicker 16-bit tiled sprite scrolling
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <vector>
#include "../core/App.h"

namespace apps {

struct SDPackageEntry {
    char name[24];
    char sdPath[64];
    char localPath[64];
    uint32_t sizeBytes;
    bool installedInFlash;
};

class AppManagerApp : public core::App {
public:
    AppManagerApp();
    virtual ~AppManagerApp() = default;

    void onCreate() override;
    void onResume() override;
    void onUpdate() override;
    void onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) override;
    void onTouch(uint16_t x, uint16_t y, bool pressed) override;
    void onPause() override;
    void onDestroy() override;

    const char* getName() const override { return "App Mgr"; }
    const char* getIcon() const override { return "ICON_APPMGR"; }
    const char* getCategory() const override { return "System"; }

private:
    static constexpr int HEADER_H  = 46; // Title bar (22px) + Tab bar (24px)
    static constexpr int FOOTER_H  = 36; // Action buttons bar at bottom
    static constexpr int ROW_H     = 38; // Height of each app list item

    uint8_t  m_activeTab;      // 0 = INSTALLED APPS, 1 = SD INSTALLER
    int16_t  m_scrollY;        // Vertical scroll offset in pixels
    int16_t  m_selectedIndex;  // Currently selected row index
    bool     m_wasPressed;
    bool     m_isDragging;
    int16_t  m_touchStartY;
    int16_t  m_scrollStartY;
    int16_t  m_lastTouchY;

    bool     m_needsFullRedraw;
    bool     m_needsListRedraw;
    bool     m_needsFooterRedraw;

    std::vector<SDPackageEntry> m_sdPackages;

    void refreshSDPackages();
    int  getItemCount() const;
    int  getMaxScroll(int listH) const;
    bool isCoreBundledLua(const char* path) const;

    void drawHeader(TFT_eSPI &display, int vx, int vy, int vw);
    void drawListArea(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void drawFooter(TFT_eSPI &display, int vx, int vy, int vw, int vh);

    void handleTap(int16_t rx, int16_t ry, int vw, int vh);
    void performPrimaryAction();
    void performSecondaryAction();
};

} // namespace apps
