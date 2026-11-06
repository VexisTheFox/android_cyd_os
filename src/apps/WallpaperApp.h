/**
 * @file WallpaperApp.h
 * @brief Native Wallpaper Selector App (Prefilled Built-In Wallpapers + SD Card Image Wallpapers)
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <vector>
#include "core/App.h"
#include "core/Config.h"

namespace apps {

class WallpaperApp : public core::App {
public:
    WallpaperApp();
    virtual ~WallpaperApp() = default;

    const char* getName() const override { return "Wallpapers"; }
    const char* getIcon() const override { return "ICON_WALLPAPER"; }
    const char* getCategory() const override { return "System"; }

    void onCreate() override;
    void onResume() override;
    void onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) override;
    void onTouch(uint16_t x, uint16_t y, bool pressed) override;
    void onPause() override;

    /**
     * @brief Render a BMP or JPEG image from SD Card or LittleFS scaled into (boxX, boxY, maxW, maxH).
     * Used by both WallpaperApp and LauncherView::drawWallpaper().
     */
    static bool renderWallpaperImage(const char* path, int boxX, int boxY, int maxW, int maxH);

    /**
     * @brief Generate sample 24-bit BMP wallpaper files on SD card (/sd/wallpapers) and Flash (/pictures).
     */
    static void createSampleWallpapers();

private:
    static constexpr int HEADER_H = 26;
    static constexpr int TABS_H   = 26;
    static constexpr int LIST_TOP = HEADER_H + TABS_H; // 52

    struct SdWallpaperItem {
        char   path[64];
        char   name[32];
        size_t sizeBytes;
        bool   isSD;
    };

    uint8_t m_activeTab; // 0 = PREFILLED (6), 1 = SD CARD / FLASH
    bool    m_needsFullRedraw;
    bool    m_needsListRedraw;
    int     m_scrollY;
    int     m_maxScrollY;
    bool    m_isScrolling;
    int     m_touchStartY;
    int     m_scrollStartY;
    uint16_t m_lastTouchX;
    uint16_t m_lastTouchY;
    bool    m_wasPressed;

    std::vector<SdWallpaperItem> m_sdWallpapers;

    void scanSdWallpapers();
    void drawHeaderAndTabs(TFT_eSPI &display, int vx, int vy, int vw);
    void drawListArea(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    void renderPresetPreview(TFT_eSPI &canvas, uint8_t presetId, int x, int y, int w, int h);
    void handleTapAt(uint16_t x, uint16_t y);
};

} // namespace apps
