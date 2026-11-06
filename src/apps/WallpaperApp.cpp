/**
 * @file WallpaperApp.cpp
 * @brief Native Wallpaper Manager (6 Prefilled Built-in Themes + Custom SD Card BMP/JPG Wallpapers)
 */

#include "WallpaperApp.h"
#include "core/Logger.h"
#include "core/MemoryGuard.h"
#include "hal/BoardHAL.h"
#include "hal/DisplayHAL.h"
#include "hal/StorageHAL.h"
#include "ui/UIManager.h"
#include <esp32/rom/tjpgd.h>

namespace apps {

static constexpr uint16_t COLOR_BG      = 0x0821;
static constexpr uint16_t COLOR_HEADER  = 0x10A2;
static constexpr uint16_t COLOR_CARD    = 0x18E3;
static constexpr uint16_t COLOR_DIVIDER = 0x3186;
static constexpr uint16_t COLOR_ACCENT  = 0x07E0;
static constexpr uint16_t COLOR_CYAN    = 0x07FF;
static constexpr uint16_t COLOR_AMBER   = 0xFFE0;
static constexpr uint16_t COLOR_WHITE   = 0xFFFF;
static constexpr uint16_t COLOR_MUTED   = 0x9CD3;

struct PresetWallpaperInfo {
    const char* name;
    const char* subtitle;
    uint16_t    badgeColor;
};

static const PresetWallpaperInfo PRESETS[6] = {
    { "Cyber Slate",      "Deep Tech Navy & Neon Cyan Grid",     0x07FF },
    { "Synthwave Sunset", "Outrun Retro Sun & Magenta Horizon",  0xF81F },
    { "Emerald Matrix",   "Dark Cyber Forest & Digital Green",   0x07E0 },
    { "SU7 Ultra Gold",   "Xiaomi Racing Carbon & Electric Gold",0xFFE0 },
    { "Crimson Nebula",   "Deep Space Crimson & Plasma Rings",   0xF800 },
    { "Arctic Aurora",    "Nordic Midnight Blue & Borealis",     0x05FE }
};

// ---------------------------------------------------------------------------
// Shared BMP / JPEG Wallpaper Renderer (used by LauncherView & WallpaperApp)
// ---------------------------------------------------------------------------
struct WpJpegCtx {
    File    file;
    int16_t drawX;
    int16_t drawY;
    int16_t maxW;
    int16_t maxH;
};

static UINT wpJpegIn(JDEC* jd, BYTE* buff, UINT nbyte) {
    WpJpegCtx* ctx = (WpJpegCtx*)jd->device;
    if (!ctx || !ctx->file) return 0;
    if (buff) {
        return (UINT)ctx->file.read(buff, nbyte);
    } else {
        bool ok = ctx->file.seek(ctx->file.position() + nbyte);
        return ok ? nbyte : 0;
    }
}

static UINT wpJpegOut(JDEC* jd, void* bitmap, JRECT* rect) {
    WpJpegCtx* ctx = (WpJpegCtx*)jd->device;
    if (!ctx || !bitmap || !rect) return 0;
    TFT_eSPI& d = hal::getDisplay();
    uint8_t* src = (uint8_t*)bitmap;
    int w = (int)(rect->right - rect->left + 1);
    int h = (int)(rect->bottom - rect->top + 1);

    for (int ry = 0; ry < h; ry++) {
        int py = ctx->drawY + (int)rect->top + ry;
        if (py < ctx->drawY || py >= ctx->drawY + ctx->maxH) {
            src += w * 3;
            continue;
        }
        for (int rx = 0; rx < w; rx++) {
            int px = ctx->drawX + (int)rect->left + rx;
            uint8_t r = src[0], g = src[1], b = src[2];
            src += 3;
            if (px >= ctx->drawX && px < ctx->drawX + ctx->maxW) {
                d.drawPixel(px, py, d.color565(r, g, b));
            }
        }
    }
    return 1;
}

static bool renderWallpaperBmp(File& f, int boxX, int boxY, int maxW, int maxH) {
    uint8_t hdr[54];
    if (f.read(hdr, 54) != 54 || hdr[0] != 'B' || hdr[1] != 'M') return false;

    uint32_t dataOffset = hdr[10] | (hdr[11] << 8) | (hdr[12] << 16) | (hdr[13] << 24);
    int32_t  bmpW       = (int32_t)(hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | (hdr[21] << 24));
    int32_t  bmpH       = (int32_t)(hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | (hdr[25] << 24));
    uint16_t bpp        = hdr[28] | (hdr[29] << 8);

    bool topDown = (bmpH < 0);
    if (bmpH < 0) bmpH = -bmpH;
    if (bmpW <= 0 || bmpH <= 0 || (bpp != 24 && bpp != 32 && bpp != 16)) return false;

    uint32_t rowStride = ((bmpW * bpp + 31) / 32) * 4;
    TFT_eSPI& d = hal::getDisplay();

    // Scale to fill maxW x maxH using fast row buffer if bmpW <= 240
    uint8_t rowBuf[360]; // supports up to 120px * 3 bytes directly in one read
    uint16_t line565[240];

    for (int dy = 0; dy < maxH; dy++) {
        int srcY = (dy * bmpH) / maxH;
        int fileRow = topDown ? srcY : (bmpH - 1 - srcY);
        uint32_t rowFilePos = dataOffset + (uint32_t)fileRow * rowStride;

        if (bmpW <= 120 && bpp == 24 && maxW <= 240) {
            f.seek(rowFilePos);
            f.read(rowBuf, bmpW * 3);
            for (int dx = 0; dx < maxW; dx++) {
                int srcX = (dx * bmpW) / maxW;
                uint8_t* p = &rowBuf[srcX * 3];
                line565[dx] = d.color565(p[2], p[1], p[0]);
            }
            d.pushImage(boxX, boxY + dy, maxW, 1, line565);
        } else {
            int limitW = (maxW <= 240) ? maxW : 240;
            for (int dx = 0; dx < limitW; dx++) {
                int srcX = (dx * bmpW) / limitW;
                f.seek(rowFilePos + (uint32_t)srcX * (bpp / 8));
                uint16_t col = 0;
                if (bpp == 24 || bpp == 32) {
                    uint8_t px[4] = {0};
                    f.read(px, bpp / 8);
                    col = d.color565(px[2], px[1], px[0]);
                } else {
                    uint8_t px[2] = {0};
                    f.read(px, 2);
                    col = (uint16_t)px[0] | ((uint16_t)px[1] << 8);
                }
                line565[dx] = col;
            }
            d.pushImage(boxX, boxY + dy, limitW, 1, line565);
        }
    }
    return true;
}

static bool renderWallpaperJpg(File& f, int boxX, int boxY, int maxW, int maxH) {
    const size_t WORK_SZ = 3500;
    void* work = core::MemoryGuard::safeMalloc(WORK_SZ);
    if (!work) return false;

    WpJpegCtx ctx;
    ctx.file  = f;
    ctx.drawX = boxX;
    ctx.drawY = boxY;
    ctx.maxW  = maxW;
    ctx.maxH  = maxH;

    JDEC jdec;
    JRESULT res = jd_prepare(&jdec, wpJpegIn, work, WORK_SZ, &ctx);
    if (res != JDR_OK) {
        vPortFree(work);
        return false;
    }

    BYTE scale = 0;
    while (scale < 3 && (((int)jdec.width >> scale) > maxW || ((int)jdec.height >> scale) > maxH)) {
        scale++;
    }
    int scaledW = (int)jdec.width >> scale;
    int scaledH = (int)jdec.height >> scale;
    ctx.drawX = boxX + (maxW - scaledW) / 2;
    ctx.drawY = boxY + (maxH - scaledH) / 2;

    hal::getDisplay().fillRect(boxX, boxY, maxW, maxH, 0x0000);
    res = jd_decomp(&jdec, wpJpegOut, scale);
    vPortFree(work);
    return (res == JDR_OK);
}

static bool hasExtIgnoreCase(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    size_t sLen = strlen(str);
    size_t xLen = strlen(suffix);
    if (xLen > sLen) return false;
    return strcasecmp(str + sLen - xLen, suffix) == 0;
}

bool WallpaperApp::renderWallpaperImage(const char* path, int boxX, int boxY, int maxW, int maxH) {
    if (!path || path[0] == '\0') return false;
    bool isSD = (strncmp(path, "/sd/", 4) == 0 || strcmp(path, "/sd") == 0);
    if (isSD && !StorageHAL::isSDMounted()) return false;

    fs::FS& fs = isSD ? (fs::FS&)SD : (fs::FS&)LittleFS;
    const char* fsPath = (strncmp(path, "/sd/", 4) == 0) ? (path + 3) : path;
    File f = fs.open(fsPath, "r");
    if (!f || f.isDirectory()) return false;

    bool ok = false;
    if (hasExtIgnoreCase(path, ".bmp")) {
        ok = renderWallpaperBmp(f, boxX, boxY, maxW, maxH);
    } else if (hasExtIgnoreCase(path, ".jpg") || hasExtIgnoreCase(path, ".jpeg")) {
        ok = renderWallpaperJpg(f, boxX, boxY, maxW, maxH);
    }
    f.close();
    return ok;
}

static void writeSampleWallpaperBmp(const char* path, int style) {
    const int W = 120;
    const int H = 136;
    const uint32_t rowStride = W * 3;
    const uint32_t dataSize  = rowStride * H;
    const uint32_t fileSize  = 54 + dataSize;

    bool isSD = (strncmp(path, "/sd/", 4) == 0);
    if (isSD && !StorageHAL::isSDMounted()) return;
    fs::FS& fs = isSD ? (fs::FS&)SD : (fs::FS&)LittleFS;
    const char* fsPath = isSD ? (path + 3) : path;

    File f = fs.open(fsPath, "w");
    if (!f) return;

    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = (uint8_t)(fileSize & 0xFF);
    hdr[3] = (uint8_t)((fileSize >> 8) & 0xFF);
    hdr[4] = (uint8_t)((fileSize >> 16) & 0xFF);
    hdr[5] = (uint8_t)((fileSize >> 24) & 0xFF);
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[18] = W;
    hdr[22] = H;
    hdr[26] = 1;
    hdr[28] = 24;
    f.write(hdr, 54);

    uint8_t row[W * 3];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t r = 0, g = 0, b = 0;
            if (style == 0) {
                // Cyber Horizon Grid
                if (y < 52) {
                    b = (uint8_t)(50 + y * 3);
                    r = (x % 15 == 0 || y % 10 == 0) ? 220 : 18;
                    g = (x % 15 == 0 || y % 10 == 0) ? 20 : 10;
                } else {
                    int dx = x - 60, dy = y - 68;
                    if (dx * dx + dy * dy < 625) {
                        r = 255; g = (uint8_t)(90 + (y - 52) * 2); b = 50;
                    } else {
                        r = 28; g = 8; b = (uint8_t)(45 + y / 2);
                    }
                }
            } else if (style == 1) {
                // Emerald Aurora Wave
                r = 6;
                g = (uint8_t)(30 + ((x * 2 + y * 3) % 140));
                b = (uint8_t)(45 + (y % 90));
            } else {
                // Gold SU7 Carbon Geometry
                bool line = ((x + y) % 18 == 0) || ((x - y + 200) % 18 == 0);
                r = line ? 245 : 16;
                g = line ? 190 : 20;
                b = line ? 20  : 28;
            }
            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
        f.write(row, sizeof(row));
    }
    f.flush();
    f.close();
}

void WallpaperApp::createSampleWallpapers() {
    if (StorageHAL::isSDMounted()) {
        StorageHAL::ensureDirectory("/sd/wallpapers");
        writeSampleWallpaperBmp("/sd/wallpapers/cyber_horizon.bmp", 0);
        writeSampleWallpaperBmp("/sd/wallpapers/emerald_wave.bmp", 1);
        writeSampleWallpaperBmp("/sd/wallpapers/su7_gold_grid.bmp", 2);
        LOG_INFO("WALLPAPER", "Created sample BMP wallpapers in /sd/wallpapers");
    }
    StorageHAL::ensureDirectory("/pictures");
    writeSampleWallpaperBmp("/pictures/cyber_horizon.bmp", 0);
    writeSampleWallpaperBmp("/pictures/emerald_wave.bmp", 1);
    writeSampleWallpaperBmp("/pictures/su7_gold_grid.bmp", 2);
}

// ---------------------------------------------------------------------------
// WallpaperApp Instance Implementation
// ---------------------------------------------------------------------------
WallpaperApp::WallpaperApp()
    : m_activeTab(0)
    , m_needsFullRedraw(true)
    , m_needsListRedraw(true)
    , m_scrollY(0)
    , m_maxScrollY(0)
    , m_isScrolling(false)
    , m_touchStartY(0)
    , m_scrollStartY(0)
    , m_lastTouchX(0)
    , m_lastTouchY(0)
    , m_wasPressed(false) {
}

void WallpaperApp::onCreate() {
    m_state = core::AppState::CREATED;
}

void WallpaperApp::onResume() {
    m_state = core::AppState::RUNNING;
    m_scrollY = 0;
    m_isScrolling = false;
    m_wasPressed = false;
    scanSdWallpapers();
    m_needsFullRedraw = true;
    m_needsListRedraw = true;
}

void WallpaperApp::onPause() {
    m_state = core::AppState::PAUSED;
}

static void collectWallpapersFromDir(const char* dir, std::vector<String>& outPaths) {
    std::vector<String> files;
    if (!StorageHAL::listDirectory(dir, files, nullptr)) return;
    for (size_t i = 0; i < files.size(); i++) {
        const char* p = files[i].c_str();
        if (hasExtIgnoreCase(p, ".bmp") || hasExtIgnoreCase(p, ".jpg") || hasExtIgnoreCase(p, ".jpeg")) {
            bool dup = false;
            for (size_t k = 0; k < outPaths.size(); k++) {
                if (outPaths[k] == files[i]) { dup = true; break; }
            }
            if (!dup) outPaths.push_back(files[i]);
        }
    }
}

void WallpaperApp::scanSdWallpapers() {
    m_sdWallpapers.clear();
    std::vector<String> paths;

    if (StorageHAL::isSDMounted()) {
        collectWallpapersFromDir("/sd/wallpapers", paths);
        collectWallpapersFromDir("/sd/pictures", paths);
        collectWallpapersFromDir("/sd/images", paths);
        collectWallpapersFromDir("/sd", paths);
    }
    collectWallpapersFromDir("/pictures", paths);

    for (size_t i = 0; i < paths.size(); i++) {
        SdWallpaperItem item;
        strncpy(item.path, paths[i].c_str(), sizeof(item.path) - 1);
        item.path[sizeof(item.path) - 1] = '\0';

        const char* slash = strrchr(item.path, '/');
        const char* fn = slash ? (slash + 1) : item.path;
        strncpy(item.name, fn, sizeof(item.name) - 1);
        item.name[sizeof(item.name) - 1] = '\0';

        item.sizeBytes = StorageHAL::getFileSize(item.path);
        item.isSD = StorageHAL::isSDPath(item.path);
        m_sdWallpapers.push_back(item);
    }
}

void WallpaperApp::drawHeaderAndTabs(TFT_eSPI &display, int vx, int vy, int vw) {
    // 1. Header Bar
    display.fillRect(vx, vy, vw, HEADER_H, COLOR_HEADER);
    display.drawFastHLine(vx, vy + HEADER_H - 1, vw, COLOR_DIVIDER);
    display.setTextDatum(TL_DATUM);
    display.setTextFont(2);
    display.setTextColor(COLOR_WHITE, COLOR_HEADER);
    display.drawString("Wallpapers", vx + 6, vy + 5);

    display.setTextFont(1);
    display.setTextColor(COLOR_AMBER, COLOR_HEADER);
    display.drawString("[DEV BUILD]", vx + 86, vy + 9);

    // UP ^ / DN v Buttons
    display.fillRoundRect(vx + 158, vy + 3, 36, 20, 4, 0x2166);
    display.drawRoundRect(vx + 158, vy + 3, 36, 20, 4, COLOR_CYAN);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_CYAN, 0x2166);
    display.drawString("UP^", vx + 176, vy + 13);

    display.fillRoundRect(vx + 198, vy + 3, 36, 20, 4, 0x2166);
    display.drawRoundRect(vx + 198, vy + 3, 36, 20, 4, COLOR_CYAN);
    display.drawString("DNv", vx + 216, vy + 13);

    // 2. Tab Bar (PREFILLED vs SD CARD)
    int ty = vy + HEADER_H;
    display.fillRect(vx, ty, vw, TABS_H, 0x0C63);
    display.drawFastHLine(vx, ty + TABS_H - 1, vw, COLOR_DIVIDER);

    uint16_t t0Bg = (m_activeTab == 0) ? 0x04D3 : 0x18E3;
    display.fillRoundRect(vx + 4, ty + 3, 112, 20, 4, t0Bg);
    display.drawRoundRect(vx + 4, ty + 3, 112, 20, 4, (m_activeTab == 0) ? COLOR_ACCENT : COLOR_DIVIDER);
    display.setTextColor(COLOR_WHITE, t0Bg);
    display.drawString("PREFILLED (6)", vx + 60, ty + 13);

    char sdTabBuf[28];
    snprintf(sdTabBuf, sizeof(sdTabBuf), "SD / PICS (%u)", (unsigned)m_sdWallpapers.size());
    uint16_t t1Bg = (m_activeTab == 1) ? 0x04D3 : 0x18E3;
    display.fillRoundRect(vx + 124, ty + 3, 112, 20, 4, t1Bg);
    display.drawRoundRect(vx + 124, ty + 3, 112, 20, 4, (m_activeTab == 1) ? COLOR_ACCENT : COLOR_DIVIDER);
    display.setTextColor(COLOR_WHITE, t1Bg);
    display.drawString(sdTabBuf, vx + 180, ty + 13);

    display.setTextDatum(TL_DATUM);
}

void WallpaperApp::renderPresetPreview(TFT_eSPI &canvas, uint8_t presetId, int x, int y, int w, int h) {
    switch (presetId) {
        case 0: // Cyber Slate
            canvas.fillRect(x, y, w, h / 2, 0x08A5);
            canvas.fillRect(x, y + h / 2, w, h - h / 2, 0x0002);
            canvas.drawFastHLine(x, y + h / 2, w, 0x07FF);
            break;
        case 1: // Synthwave Sunset
            canvas.fillRect(x, y, w, h / 2, 0x2009);
            canvas.fillRect(x, y + h / 2, w, h - h / 2, 0x480E);
            canvas.fillCircle(x + w / 2, y + h / 2, 8, 0xFD20);
            canvas.drawFastHLine(x, y + h / 2 + 3, w, 0xF81F);
            break;
        case 2: // Emerald Matrix
            canvas.fillRect(x, y, w, h / 2, 0x0204);
            canvas.fillRect(x, y + h / 2, w, h - h / 2, 0x0101);
            canvas.drawFastVLine(x + 10, y + 3, h - 6, 0x07E0);
            canvas.drawFastVLine(x + 22, y + 6, h - 9, 0x07E0);
            canvas.drawFastVLine(x + 34, y + 2, h - 5, 0x07E0);
            break;
        case 3: // SU7 Ultra Gold
            canvas.fillRect(x, y, w, h, 0x1082);
            canvas.drawLine(x + 2, y + h - 3, x + w - 3, y + 3, 0xFFE0);
            canvas.drawLine(x + 2, y + h - 4, x + w - 3, y + 2, 0xFFE0);
            break;
        case 4: // Crimson Nebula
            canvas.fillRect(x, y, w, h / 2, 0x4004);
            canvas.fillRect(x, y + h / 2, w, h - h / 2, 0x1800);
            canvas.drawCircle(x + w / 2, y + h / 2, 9, 0xF800);
            canvas.fillCircle(x + w / 2, y + h / 2, 3, 0xFFE0);
            break;
        default: // Arctic Aurora
            canvas.fillRect(x, y, w, h / 2, 0x0190);
            canvas.fillRect(x, y + h / 2, w, h - h / 2, 0x02F3);
            canvas.drawFastHLine(x + 4, y + h / 2 - 2, w - 8, 0x07FF);
            canvas.drawFastHLine(x + 6, y + h / 2 + 2, w - 12, 0x07E0);
            break;
    }
    canvas.drawRect(x, y, w, h, COLOR_WHITE);
}

void WallpaperApp::drawListArea(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int listY = vy + LIST_TOP;
    int listH = vh - LIST_TOP;
    int cardW = vw - 14;

    static constexpr int CARD_H = 48;
    static constexpr int CARD_GAP = 6;
    static constexpr int STEP_Y = CARD_H + CARD_GAP; // 54

    int totalHeight = 0;
    if (m_activeTab == 0) {
        totalHeight = 8 + 6 * STEP_Y + 6;
    } else {
        int count = (int)m_sdWallpapers.size();
        totalHeight = 40 + (count > 0 ? count * STEP_Y : 70) + 10;
    }
    m_maxScrollY = (totalHeight > listH) ? (totalHeight - listH) : 0;
    if (m_scrollY > m_maxScrollY) m_scrollY = m_maxScrollY;
    if (m_scrollY < 0) m_scrollY = 0;

    static constexpr int BAND_H = 54;
    TFT_eSprite band(&display);
    band.setColorDepth(16);
    bool spriteOk = (band.createSprite(vw, BAND_H) != nullptr);

    uint8_t curWpId = Config::getInstance().getWallpaperId();
    const char* curWpPath = Config::getInstance().getWallpaperPath();

    for (int offsetY = 0; offsetY < listH; offsetY += BAND_H) {
        int curBandH = (offsetY + BAND_H <= listH) ? BAND_H : (listH - offsetY);
        if (!spriteOk) break;

        band.fillSprite(COLOR_BG);
        int baseY = -m_scrollY - offsetY;

        if (m_activeTab == 0) {
            // Render 6 Prefilled Wallpaper Cards
            for (uint8_t i = 0; i < 6; i++) {
                int cy = baseY + 6 + i * STEP_Y;
                if (cy + CARD_H < 0 || cy >= curBandH) continue;

                bool isSelected = (curWpId == i);
                band.fillRoundRect(6, cy, cardW, CARD_H, 6, COLOR_CARD);
                band.drawRoundRect(6, cy, cardW, CARD_H, 6, isSelected ? COLOR_ACCENT : COLOR_DIVIDER);

                // Mini graphic preview (44 x 34)
                renderPresetPreview(band, i, 12, cy + 7, 44, 34);

                band.setTextDatum(TL_DATUM);
                band.setTextFont(2);
                band.setTextColor(PRESETS[i].badgeColor, COLOR_CARD);
                band.drawString(PRESETS[i].name, 62, cy + 6);

                band.setTextFont(1);
                band.setTextColor(COLOR_MUTED, COLOR_CARD);
                band.drawString(PRESETS[i].subtitle, 62, cy + 26);

                // Right button [ ACTIVE ] or [ APPLY ]
                uint16_t btnBg = isSelected ? 0x04D3 : 0x2166;
                band.fillRoundRect(cardW - 48, cy + 8, 50, 18, 4, btnBg);
                band.drawRoundRect(cardW - 48, cy + 8, 50, 18, 4, isSelected ? COLOR_ACCENT : COLOR_CYAN);
                band.setTextDatum(MC_DATUM);
                band.setTextColor(COLOR_WHITE, btnBg);
                band.drawString(isSelected ? "ACTIVE" : "APPLY", cardW - 23, cy + 17);
            }
        } else {
            // Top Action Row on SD Tab: [ RESCAN SD ] and [ + SAMPLE BMPs ]
            int topY = baseY + 6;
            if (topY + 28 >= 0 && topY < curBandH) {
                band.fillRoundRect(6, topY, 106, 26, 4, 0x18C3);
                band.drawRoundRect(6, topY, 106, 26, 4, COLOR_CYAN);
                band.setTextDatum(MC_DATUM);
                band.setTextFont(1);
                band.setTextColor(COLOR_CYAN, 0x18C3);
                band.drawString("RESCAN SD CARD", 59, topY + 13);

                band.fillRoundRect(118, topY, 114, 26, 4, 0x04D3);
                band.drawRoundRect(118, topY, 114, 26, 4, COLOR_ACCENT);
                band.setTextColor(COLOR_WHITE, 0x04D3);
                band.drawString("+ MAKE SD SAMPLES", 175, topY + 13);
            }

            if (m_sdWallpapers.empty()) {
                int ey = baseY + 42;
                if (ey + 56 >= 0 && ey < curBandH) {
                    band.fillRoundRect(6, ey, cardW, 56, 6, COLOR_CARD);
                    band.drawRoundRect(6, ey, cardW, 56, 6, COLOR_DIVIDER);
                    band.setTextDatum(MC_DATUM);
                    band.setTextFont(1);
                    band.setTextColor(COLOR_AMBER, COLOR_CARD);
                    band.drawString("No .bmp / .jpg found on SD or Flash", 6 + cardW / 2, ey + 18);
                    band.setTextColor(COLOR_WHITE, COLOR_CARD);
                    band.drawString("Tap '+ MAKE SD SAMPLES' above to create!", 6 + cardW / 2, ey + 36);
                }
            } else {
                for (size_t i = 0; i < m_sdWallpapers.size(); i++) {
                    int cy = baseY + 38 + (int)i * STEP_Y;
                    if (cy + CARD_H < 0 || cy >= curBandH) continue;

                    const SdWallpaperItem& item = m_sdWallpapers[i];
                    bool isSelected = (curWpId == CONFIG_WALLPAPER_CUSTOM_SD &&
                                       strcmp(curWpPath, item.path) == 0);

                    band.fillRoundRect(6, cy, cardW, CARD_H, 6, COLOR_CARD);
                    band.drawRoundRect(6, cy, cardW, CARD_H, 6, isSelected ? COLOR_ACCENT : COLOR_DIVIDER);

                    // Storage badge [SD] or [FLS]
                    uint16_t badgeBg = item.isSD ? 0xFD00 : 0x2C9F;
                    band.fillRoundRect(12, cy + 10, 32, 28, 4, badgeBg);
                    band.setTextDatum(MC_DATUM);
                    band.setTextFont(1);
                    band.setTextColor(COLOR_WHITE, badgeBg);
                    band.drawString(item.isSD ? "SD" : "FLS", 28, cy + 24);

                    band.setTextDatum(TL_DATUM);
                    band.setTextFont(2);
                    band.setTextColor(COLOR_WHITE, COLOR_CARD);
                    band.drawString(item.name, 50, cy + 6);

                    char info[48];
                    snprintf(info, sizeof(info), "%.24s (%lu KB)", item.path, (unsigned long)(item.sizeBytes / 1024));
                    band.setTextFont(1);
                    band.setTextColor(COLOR_MUTED, COLOR_CARD);
                    band.drawString(info, 50, cy + 28);

                    uint16_t btnBg = isSelected ? 0x04D3 : 0x2166;
                    band.fillRoundRect(cardW - 48, cy + 12, 50, 22, 4, btnBg);
                    band.drawRoundRect(cardW - 48, cy + 12, 50, 22, 4, isSelected ? COLOR_ACCENT : COLOR_CYAN);
                    band.setTextDatum(MC_DATUM);
                    band.setTextColor(COLOR_WHITE, btnBg);
                    band.drawString(isSelected ? "ACTIVE" : "SET WP", cardW - 23, cy + 23);
                }
            }
        }

        // Right-edge scrollbar
        if (m_maxScrollY > 0) {
            int thumbH = (listH * listH) / totalHeight;
            if (thumbH < 18) thumbH = 18;
            int thumbY = (m_scrollY * (listH - thumbH)) / m_maxScrollY;
            band.fillRect(vw - 5, 0, 3, curBandH, 0x10A2);
            band.fillRoundRect(vw - 5, thumbY - offsetY, 3, thumbH, 1, COLOR_CYAN);
        }

        band.pushSprite(vx, listY + offsetY, 0, 0, vw, curBandH);
    }

    if (spriteOk) {
        band.deleteSprite();
    }
    display.setTextDatum(TL_DATUM);
}

void WallpaperApp::onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    if (m_needsFullRedraw) {
        drawHeaderAndTabs(display, vx, vy, vw);
        drawListArea(display, vx, vy, vw, vh);
        m_needsFullRedraw = false;
        m_needsListRedraw = false;
        return;
    }
    if (m_needsListRedraw) {
        drawListArea(display, vx, vy, vw, vh);
        m_needsListRedraw = false;
    }
}

void WallpaperApp::handleTapAt(uint16_t x, uint16_t y) {
    // 1. Header UP^ / DNv
    if (y < HEADER_H) {
        if (x >= 156 && x <= 195) {
            m_scrollY -= 80;
            if (m_scrollY < 0) m_scrollY = 0;
            hal::beep(15);
            m_needsListRedraw = true;
        } else if (x >= 196 && x <= 238) {
            m_scrollY += 80;
            if (m_scrollY > m_maxScrollY) m_scrollY = m_maxScrollY;
            hal::beep(15);
            m_needsListRedraw = true;
        }
        return;
    }

    // 2. Tab Bar (PREFILLED vs SD / PICS)
    if (y >= HEADER_H && y < LIST_TOP) {
        uint8_t nextTab = (x < 120) ? 0 : 1;
        if (nextTab != m_activeTab) {
            m_activeTab = nextTab;
            m_scrollY = 0;
            if (m_activeTab == 1) {
                scanSdWallpapers();
            }
            hal::beep(20);
            m_needsFullRedraw = true;
        }
        return;
    }

    int contentY = (int)y - LIST_TOP + m_scrollY;
    static constexpr int STEP_Y = 54;

    if (m_activeTab == 0) {
        int idx = (contentY - 6) / STEP_Y;
        if (idx >= 0 && idx < 6) {
            Config::getInstance().setWallpaperId((uint8_t)idx, true);
            hal::beep(30);
            char msg[44];
            snprintf(msg, sizeof(msg), "Wallpaper: %s", PRESETS[idx].name);
            LOG_INFO("WALLPAPER", "Applied built-in wallpaper [%d]: %s", idx, PRESETS[idx].name);
            ui::UIManager::showToast(msg, 1400);
            m_needsListRedraw = true;
        }
    } else {
        // Check Top Action Row (contentY: 4..34)
        if (contentY >= 4 && contentY <= 34) {
            if (x < 115) {
                hal::beep(25);
                StorageHAL::probeSDCard();
                scanSdWallpapers();
                ui::UIManager::showToast("Rescanned SD & Flash pictures", 1300);
                m_needsFullRedraw = true;
            } else {
                hal::beep(30);
                createSampleWallpapers();
                scanSdWallpapers();
                ui::UIManager::showToast("Created 3 sample BMP wallpapers!", 1500);
                m_needsFullRedraw = true;
            }
            return;
        }

        int idx = (contentY - 38) / STEP_Y;
        if (idx >= 0 && (size_t)idx < m_sdWallpapers.size()) {
            const SdWallpaperItem& item = m_sdWallpapers[idx];
            Config::getInstance().setWallpaperPath(item.path, false);
            Config::getInstance().setWallpaperId(CONFIG_WALLPAPER_CUSTOM_SD, true);
            hal::beep(35);
            char msg[48];
            snprintf(msg, sizeof(msg), "SD Wallpaper: %.24s", item.name);
            LOG_INFO("WALLPAPER", "Applied SD/Flash wallpaper: %s", item.path);
            ui::UIManager::showToast(msg, 1500);
            m_needsListRedraw = true;
        }
    }
}

void WallpaperApp::onTouch(uint16_t x, uint16_t y, bool pressed) {
    if (pressed && !m_wasPressed) {
        m_wasPressed   = true;
        m_isScrolling  = false;
        m_touchStartY  = (int)y;
        m_scrollStartY = m_scrollY;
        m_lastTouchX   = x;
        m_lastTouchY   = y;
    } else if (pressed && m_wasPressed) {
        m_lastTouchX = x;
        m_lastTouchY = y;
        int dy = (int)y - m_touchStartY;
        if (!m_isScrolling && y >= LIST_TOP && abs(dy) > 8) {
            m_isScrolling = true;
        }
        if (m_isScrolling) {
            int newScroll = m_scrollStartY - dy;
            if (newScroll < 0) newScroll = 0;
            if (newScroll > m_maxScrollY) newScroll = m_maxScrollY;
            if (abs(newScroll - m_scrollY) >= 2) {
                m_scrollY = newScroll;
                m_needsListRedraw = true;
            }
        }
    } else if (!pressed && m_wasPressed) {
        m_wasPressed = false;
        if (!m_isScrolling) {
            handleTapAt(m_lastTouchX, m_lastTouchY);
        }
        m_isScrolling = false;
    }
}

} // namespace apps
