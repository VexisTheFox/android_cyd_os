/**
 * @file DynamicScriptApp.cpp
 * @brief Embedded Lua 5.4 runtime for dynamic script apps (gui / input / sys / file / wifi APIs)
 *
 * Hardened against memory starvation / OOM attacks:
 *  1. Mandatory nullptr checks on all C++ / Lua C-API allocations and string conversions
 *  2. Emergency RAM reserve pool released on OOM/crash for OS error UI & kernel logger
 *  3. Hard heap floor limit enforced via custom lua_Alloc + C-API entry & instruction hooks
 *  4. Zero-heap-allocation crash formatting using fixed-size stack buffers (snprintf)
 */

#include "AppRegistry.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/MemoryGuard.h"
#include "core/TimeService.h"
#include "core/LuaIconCodec.h"
#include "hal/BoardHAL.h"
#include "hal/DisplayHAL.h"
#include "ui/UIManager.h"
#include "ui/KeyboardView.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <SD.h>
#include <esp32/rom/tjpgd.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace core {

static volatile bool s_running   = false;
static volatile bool s_abort     = false;
static bool     s_touched = false;
static uint16_t s_touchX  = 0;
static uint16_t s_touchY  = 0;

static const char* ABORT_TAG = "__script_aborted__";
static const char* OOM_TAG   = "out of memory (hard heap floor)";

// Hard heap floor guard macro invoked before C++ bindings allocate or execute heavy operations
#define LUA_CHECK_HEAP(L, extraBytes) \
    do { \
        if (!core::MemoryGuard::ensureHeapFloor(extraBytes)) { \
            core::MemoryGuard::releaseEmergencyPool("C-API heap floor breach"); \
            luaL_error((L), "%s", OOM_TAG); \
        } \
    } while (0)

bool DynamicScriptApp::isScriptRunning() { return s_running; }
void DynamicScriptApp::requestAbort()    { s_abort = true; }

// ---------------------------------------------------------------- helpers
static void pumpOS() {
    ui::UIManager::pump();
}

static void checkAbort(lua_State* L) {
    if (s_abort) {
        luaL_error(L, "%s", ABORT_TAG);
    }
    if (core::MemoryGuard::wasOOMTriggered() || !core::MemoryGuard::ensureHeapFloor(0)) {
        core::MemoryGuard::releaseEmergencyPool("VM hook heap floor breach");
        luaL_error(L, "%s", OOM_TAG);
    }
}

static void hookFn(lua_State* L, lua_Debug*) {
    static uint32_t s_lastYield = 0;
    uint32_t now = millis();
    if (now - s_lastYield >= 12) {
        s_lastYield = now;
        delay(1); // Feed FreeRTOS watchdog & yield timeslice
    }
    pumpOS();
    checkAbort(L);
}

static TFT_eSPI& tft() { return hal::getDisplay(); }

// Safe non-null string helper for Lua arguments
static const char* safeCheckString(lua_State* L, int idx) {
    LUA_CHECK_HEAP(L, 64);
    const char* s = luaL_checkstring(L, idx);
    if (!s) {
        luaL_error(L, "out of memory");
        return "";
    }
    return s;
}

static const char* safeOptString(lua_State* L, int idx, const char* def) {
    if (lua_isnoneornil(L, idx)) return def;
    LUA_CHECK_HEAP(L, 64);
    const char* s = luaL_optstring(L, idx, def);
    return s ? s : def;
}

// -------------------------------------------------------------- gui.* API
static int gui_fillRect(lua_State* L) {
    LUA_CHECK_HEAP(L, 0);
    tft().fillRect((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                   (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4),
                   (uint16_t)luaL_checkinteger(L, 5));
    return 0;
}
static int gui_drawRect(lua_State* L) {
    LUA_CHECK_HEAP(L, 0);
    tft().drawRect((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                   (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4),
                   (uint16_t)luaL_checkinteger(L, 5));
    return 0;
}
static int gui_drawText(lua_State* L) {
    LUA_CHECK_HEAP(L, 128);
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    const char* s = nullptr;
    if (lua_type(L, 3) == LUA_TSTRING) {
        s = lua_tostring(L, 3);
    } else {
        s = luaL_tolstring(L, 3, nullptr);
    }
    if (!s) {
        return luaL_error(L, "out of memory");
    }
    uint16_t color = (uint16_t)luaL_optinteger(L, 4, 0xFFFF);
    int font = (int)luaL_optinteger(L, 5, 1);
    if (font != 1 && font != 2 && font != 4) font = 2;
    TFT_eSPI& t = tft();
    t.setTextFont(font);
    t.setTextDatum(TL_DATUM);
    t.setTextColor(color);  // transparent background (scripts clear area themselves)
    t.drawString(s, x, y);
    if (lua_type(L, 3) != LUA_TSTRING) {
        lua_pop(L, 1); // pop temporary string pushed by luaL_tolstring
    }
    return 0;
}
static int gui_drawLine(lua_State* L) {
    LUA_CHECK_HEAP(L, 0);
    tft().drawLine((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                   (int)luaL_checkinteger(L, 3), (int)luaL_checkinteger(L, 4),
                   (uint16_t)luaL_checkinteger(L, 5));
    return 0;
}
static int gui_drawFastHLine(lua_State* L) {
    LUA_CHECK_HEAP(L, 0);
    tft().drawFastHLine((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                        (int)luaL_checkinteger(L, 3), (uint16_t)luaL_checkinteger(L, 4));
    return 0;
}
static int gui_fillCircle(lua_State* L) {
    LUA_CHECK_HEAP(L, 0);
    tft().fillCircle((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                     (int)luaL_checkinteger(L, 3), (uint16_t)luaL_checkinteger(L, 4));
    return 0;
}
static int gui_drawPixel(lua_State* L) {
    LUA_CHECK_HEAP(L, 0);
    tft().drawPixel((int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2),
                    (uint16_t)luaL_checkinteger(L, 3));
    return 0;
}

static int gui_keyboard(lua_State* L) {
    LUA_CHECK_HEAP(L, 512);
    const char* title = safeOptString(L, 1, "Enter Text");
    const char* initial = safeOptString(L, 2, "");
    char outBuf[64] = {0};
    bool ok = ui::KeyboardView::prompt(title, initial, outBuf, sizeof(outBuf));
    // Clear viewport in case orientation flipped back
    TFT_eSPI& d = hal::getDisplay();
    d.fillRect(0, 20, d.width(), d.height() - 46, 0x0000);
    if (ok) {
        if (!lua_pushstring(L, outBuf)) {
            return luaL_error(L, "out of memory");
        }
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

// ------------------------------------------------ BMP & JPEG Image Decoder
struct JpegDecodeCtx {
    File file;
    int16_t drawX;
    int16_t drawY;
    int16_t maxW;
    int16_t maxH;
};

static UINT jpegInFunc(JDEC* jd, BYTE* buff, UINT nbyte) {
    JpegDecodeCtx* ctx = (JpegDecodeCtx*)jd->device;
    if (!ctx || !ctx->file) return 0;
    if (buff) {
        return (UINT)ctx->file.read(buff, nbyte);
    } else {
        bool ok = ctx->file.seek(ctx->file.position() + nbyte);
        return ok ? nbyte : 0;
    }
}

static UINT jpegOutFunc(JDEC* jd, void* bitmap, JRECT* rect) {
    JpegDecodeCtx* ctx = (JpegDecodeCtx*)jd->device;
    if (!ctx || !bitmap || !rect) return 0;
    TFT_eSPI& d = tft();
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
            uint8_t r = src[0];
            uint8_t g = src[1];
            uint8_t b = src[2];
            src += 3;
            if (px >= ctx->drawX && px < ctx->drawX + ctx->maxW) {
                d.drawPixel(px, py, d.color565(r, g, b));
            }
        }
    }
    return 1;
}

static bool renderBmpFile(File& f, int boxX, int boxY, int maxW, int maxH, int& outW, int& outH) {
    uint8_t hdr[54];
    if (f.read(hdr, 54) != 54 || hdr[0] != 'B' || hdr[1] != 'M') {
        return false;
    }
    uint32_t dataOffset = hdr[10] | (hdr[11] << 8) | (hdr[12] << 16) | (hdr[13] << 24);
    int32_t  bmpW       = (int32_t)(hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | (hdr[21] << 24));
    int32_t  bmpH       = (int32_t)(hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | (hdr[25] << 24));
    uint16_t bpp        = hdr[28] | (hdr[29] << 8);

    bool topDown = (bmpH < 0);
    if (bmpH < 0) bmpH = -bmpH;
    if (bmpW <= 0 || bmpH <= 0 || (bpp != 24 && bpp != 32 && bpp != 16)) {
        return false;
    }
    outW = bmpW;
    outH = bmpH;

    // Compute scale factor to fit inside maxW x maxH
    int scale = 1;
    while ((bmpW / scale) > maxW || (bmpH / scale) > maxH) {
        scale++;
    }
    int dispW = bmpW / scale;
    int dispH = bmpH / scale;
    int startX = boxX + (maxW - dispW) / 2;
    int startY = boxY + (maxH - dispH) / 2;

    uint32_t rowStride = ((bmpW * bpp + 31) / 32) * 4;
    TFT_eSPI& d = tft();

    for (int dy = 0; dy < dispH; dy++) {
        int srcY = dy * scale;
        int fileRow = topDown ? srcY : (bmpH - 1 - srcY);
        f.seek(dataOffset + (uint32_t)fileRow * rowStride);

        for (int dx = 0; dx < dispW; dx++) {
            int srcX = dx * scale;
            f.seek(dataOffset + (uint32_t)fileRow * rowStride + (uint32_t)srcX * (bpp / 8));
            uint16_t col = 0;
            if (bpp == 24 || bpp == 32) {
                uint8_t px[4] = {0};
                f.read(px, bpp / 8);
                col = d.color565(px[2], px[1], px[0]);
            } else if (bpp == 16) {
                uint8_t px[2] = {0};
                f.read(px, 2);
                col = (uint16_t)px[0] | ((uint16_t)px[1] << 8);
            }
            d.drawPixel(startX + dx, startY + dy, col);
        }
        if ((dy & 15) == 0) {
            pumpOS();
        }
    }
    return true;
}

static bool renderJpgFile(File& f, int boxX, int boxY, int maxW, int maxH, int& outW, int& outH) {
    const size_t WORK_SZ = 3500;
    void* work = core::MemoryGuard::safeMalloc(WORK_SZ);
    if (!work) return false;

    JpegDecodeCtx ctx;
    ctx.file = f;
    ctx.maxW = maxW;
    ctx.maxH = maxH;
    ctx.drawX = boxX;
    ctx.drawY = boxY;

    JDEC jdec;
    JRESULT res = jd_prepare(&jdec, jpegInFunc, work, WORK_SZ, &ctx);
    if (res != JDR_OK) {
        vPortFree(work);
        return false;
    }

    outW = (int)jdec.width;
    outH = (int)jdec.height;

    BYTE scale = 0; // 0->1/1, 1->1/2, 2->1/4, 3->1/8
    while (scale < 3 && (((int)jdec.width >> scale) > maxW || ((int)jdec.height >> scale) > maxH)) {
        scale++;
    }
    int scaledW = (int)jdec.width >> scale;
    int scaledH = (int)jdec.height >> scale;
    ctx.drawX = boxX + (maxW - scaledW) / 2;
    ctx.drawY = boxY + (maxH - scaledH) / 2;

    res = jd_decomp(&jdec, jpegOutFunc, scale);
    vPortFree(work);
    return (res == JDR_OK);
}

static bool endsWithIgnoreCase(const char* str, const char* suffix) {
    if (!str || !suffix) return false;
    size_t sLen = strlen(str);
    size_t xLen = strlen(suffix);
    if (xLen > sLen) return false;
    return strcasecmp(str + sLen - xLen, suffix) == 0;
}

static int gui_drawImage(lua_State* L) {
    LUA_CHECK_HEAP(L, 4096);
    const char* path = safeCheckString(L, 1);
    int boxX = (int)luaL_optinteger(L, 2, 6);
    int boxY = (int)luaL_optinteger(L, 3, 50);
    int maxW = (int)luaL_optinteger(L, 4, 228);
    int maxH = (int)luaL_optinteger(L, 5, 160);

    bool isSD = (strncmp(path, "/sd/", 4) == 0 || strcmp(path, "/sd") == 0);
    if (isSD && !StorageHAL::isSDMounted()) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "SD not mounted");
        return 2;
    }

    fs::FS& fs = isSD ? (fs::FS&)SD : (fs::FS&)LittleFS;
    const char* fsPath = (strncmp(path, "/sd/", 4) == 0) ? (path + 3) : path;
    File f = fs.open(fsPath, "r");
    if (!f || f.isDirectory()) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Cannot open image");
        return 2;
    }

    int imgW = 0, imgH = 0;
    bool ok = false;
    if (endsWithIgnoreCase(path, ".bmp")) {
        ok = renderBmpFile(f, boxX, boxY, maxW, maxH, imgW, imgH);
    } else if (endsWithIgnoreCase(path, ".jpg") || endsWithIgnoreCase(path, ".jpeg")) {
        ok = renderJpgFile(f, boxX, boxY, maxW, maxH, imgW, imgH);
    }
    f.close();

    if (ok) {
        lua_pushboolean(L, true);
        lua_pushinteger(L, imgW);
        lua_pushinteger(L, imgH);
        return 3;
    }
    lua_pushboolean(L, false);
    lua_pushstring(L, "Unsupported or corrupt image");
    return 2;
}

static void writeSampleBmp(const char* path, int style) {
    const int W = 120;
    const int H = 84;
    const uint32_t rowStride = W * 3;
    const uint32_t dataSize = rowStride * H;
    const uint32_t fileSize = 54 + dataSize;

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
                if (y < 36) {
                    b = 40 + y * 3;
                    r = (x % 16 == 0 || y % 8 == 0) ? 0 : 12;
                    g = (x % 16 == 0 || y % 8 == 0) ? 240 : 18;
                } else {
                    int dx = x - 60, dy = y - 48;
                    if (dx * dx + dy * dy < 480) {
                        r = 255; g = (uint8_t)(120 + (y - 36) * 2); b = 40;
                    } else {
                        r = 35; g = 10; b = (uint8_t)(60 + y);
                    }
                }
            } else if (style == 1) {
                int cx = x - 60, cy = y - 42;
                int dist2 = cx * cx + cy * cy;
                if (dist2 > 500 && dist2 < 900) {
                    r = 0; g = 255; b = 120;
                } else if ((x + y) % 14 == 0) {
                    r = 255; g = 210; b = 0;
                } else {
                    r = 8; g = 28; b = 48;
                }
            } else {
                r = (uint8_t)((x * 2) ^ (y * 2));
                g = (uint8_t)(40 + (y * 2));
                b = (uint8_t)(140 + (x % 90));
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

static int gui_createSampleImages(lua_State* L) {
    LUA_CHECK_HEAP(L, 1024);
    if (StorageHAL::isSDMounted()) {
        StorageHAL::ensureDirectory("/sd/pictures");
        writeSampleBmp("/sd/pictures/synth_sun.bmp", 0);
        writeSampleBmp("/sd/pictures/dev_badge.bmp", 1);
        writeSampleBmp("/sd/pictures/plasma_sky.bmp", 2);
        LOG_INFO("GALLERY", "Created sample BMP pictures in /sd/pictures");
    }
    StorageHAL::ensureDirectory("/pictures");
    writeSampleBmp("/pictures/synth_sun.bmp", 0);
    writeSampleBmp("/pictures/dev_badge.bmp", 1);
    writeSampleBmp("/pictures/plasma_sky.bmp", 2);
    lua_pushboolean(L, true);
    return 1;
}

static int gui_drawBase64(lua_State* L) {
    LUA_CHECK_HEAP(L, 128);
    int16_t cx      = (int16_t)luaL_checkinteger(L, 1);
    int16_t cy      = (int16_t)luaL_checkinteger(L, 2);
    const char* b64 = safeCheckString(L, 3);
    uint8_t scale   = (uint8_t)luaL_optinteger(L, 4, 2);
    uint16_t fg     = (uint16_t)luaL_optinteger(L, 5, TFT_WHITE);
    bool ok = core::LuaIconCodec::drawBase64Icon(tft(), b64, cx, cy, fg, scale);
    lua_pushboolean(L, ok);
    return 1;
}

// ------------------------------------------------------------ input.* API
static int input_getTouch(lua_State* L) {
    LUA_CHECK_HEAP(L, 0);
    lua_pushinteger(L, s_touchX);
    lua_pushinteger(L, s_touchY);
    lua_pushboolean(L, s_touched);
    return 3;
}

// -------------------------------------------------------------- sys.* API
static int sys_delay(lua_State* L) {
    LUA_CHECK_HEAP(L, 0);
    uint32_t ms = (uint32_t)luaL_checkinteger(L, 1);
    uint32_t start = millis();
    do {
        pumpOS();
        checkAbort(L);
        if (ms == 0) break;
        delay(2);
    } while (millis() - start < ms);
    return 0;
}
static int sys_beep(lua_State* L) {
    LUA_CHECK_HEAP(L, 0);
    uint16_t freq = (uint16_t)luaL_optinteger(L, 1, 1000);
    uint16_t dur  = (uint16_t)luaL_optinteger(L, 2, 20);
    hal::playTone(freq, dur);
    return 0;
}
static int sys_getHeap(lua_State* L)  {
    lua_pushinteger(L, (lua_Integer)xPortGetFreeHeapSize());
    return 1;
}
static int sys_getLdr(lua_State* L)   { lua_pushinteger(L, hal::readLDR()); return 1; }
static int sys_getUptime(lua_State* L){ lua_pushinteger(L, (lua_Integer)(millis() / 1000)); return 1; }
static int sys_getTime(lua_State* L) {
    LUA_CHECK_HEAP(L, 64);
    char tBuf[16];
    core::TimeService::formatHHMMSS(tBuf, sizeof(tBuf));
    bool synced = core::TimeService::isSynced();
    time_t now = time(nullptr);
    struct tm tmInfo;
    memset(&tmInfo, 0, sizeof(tmInfo));
    localtime_r(&now, &tmInfo);

    lua_pushstring(L, tBuf);
    lua_pushboolean(L, synced);
    lua_pushinteger(L, (lua_Integer)now);
    lua_pushinteger(L, tmInfo.tm_hour);
    lua_pushinteger(L, tmInfo.tm_min);
    lua_pushinteger(L, tmInfo.tm_sec);
    lua_pushinteger(L, tmInfo.tm_mday);
    lua_pushinteger(L, tmInfo.tm_mon + 1);
    lua_pushinteger(L, tmInfo.tm_year + 1900);
    return 9;
}
static int sys_syncNTP(lua_State* L) {
    LUA_CHECK_HEAP(L, 512);
    uint32_t waitMs = (uint32_t)luaL_optinteger(L, 1, 2000);
    if (waitMs > 4000) waitMs = 4000;
    bool ok = core::TimeService::syncNtpNow(waitMs);
    char tBuf[16];
    core::TimeService::formatHHMMSS(tBuf, sizeof(tBuf));
    lua_pushboolean(L, ok);
    lua_pushstring(L, tBuf);
    return 2;
}
static int sys_getRegion(lua_State* L) {
    LUA_CHECK_HEAP(L, 64);
    char regBuf[32];
    core::TimeService::formatRegionSummary(regBuf, sizeof(regBuf));
    lua_pushstring(L, regBuf);
    return 1;
}
static int sys_getStorageTotal(lua_State* L) { lua_pushinteger(L, (lua_Integer)StorageHAL::getStorageTotalBytes()); return 1; }
static int sys_getStorageUsed(lua_State* L)  { lua_pushinteger(L, (lua_Integer)StorageHAL::getStorageUsedBytes()); return 1; }
static int sys_isSDMounted(lua_State* L)     { lua_pushboolean(L, StorageHAL::isSDMounted()); return 1; }
static int sys_setLED(lua_State* L) {
    LUA_CHECK_HEAP(L, 0);
    hal::setRGBLED((uint8_t)luaL_optinteger(L, 1, 0), (uint8_t)luaL_optinteger(L, 2, 0),
                   (uint8_t)luaL_optinteger(L, 3, 0));
    return 0;
}
static int sys_setBrightness(lua_State* L) {
    LUA_CHECK_HEAP(L, 0);
    int val = (int)luaL_checkinteger(L, 1);
    if (val < 0) val = 0;
    if (val > 100) {
        if (val > 255) val = 255;
        val = (val * 100) / 255;
    }
    hal::setBacklight((uint8_t)val);
    return 0;
}

static int sys_setOrientation(lua_State* L) {
    LUA_CHECK_HEAP(L, 64);
    bool wantLandscape = false;
    if (lua_isboolean(L, 1)) {
        wantLandscape = lua_toboolean(L, 1);
    } else if (lua_isinteger(L, 1)) {
        int rot = (int)lua_tointeger(L, 1);
        wantLandscape = (rot == 1 || rot == 3);
    } else if (lua_isstring(L, 1)) {
        const char* mode = lua_tostring(L, 1);
        if (mode) {
            wantLandscape = (strcasestr(mode, "land") != nullptr ||
                             strcasestr(mode, "horiz") != nullptr ||
                             strcmp(mode, "1") == 0);
        }
    }
    ui::UIManager::setOrientation(wantLandscape);
    return 0;
}

static int sys_getOrientation(lua_State* L) {
    LUA_CHECK_HEAP(L, 32);
    lua_pushstring(L, ui::UIManager::isLandscape() ? "landscape" : "portrait");
    return 1;
}

static int sys_getViewport(lua_State* L) {
    TFT_eSPI& d = hal::getDisplay();
    int16_t vpW = d.width();
    int16_t vpH = d.height() - 20 - 26;
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 20);
    lua_pushinteger(L, vpW);
    lua_pushinteger(L, vpH);
    return 4;
}

static int sys_getScreenSize(lua_State* L) {
    TFT_eSPI& d = hal::getDisplay();
    lua_pushinteger(L, d.width());
    lua_pushinteger(L, d.height());
    return 2;
}

static bool detectLandscapeScript(const String& src) {
    const char* raw = src.c_str();
    if (!raw) return false;

    if (strcasestr(raw, "@orientation: landscape") ||
        strcasestr(raw, "@orientation:landscape") ||
        strcasestr(raw, "@orientation: horizontal") ||
        strcasestr(raw, "@orientation:horizontal") ||
        strcasestr(raw, "setorientation(\"landscape\")") ||
        strcasestr(raw, "setorientation('landscape')") ||
        strcasestr(raw, "setorientation(\"horizontal\")") ||
        strcasestr(raw, "setorientation('horizontal')")) {
        return true;
    }
    if (strcasestr(raw, "@orientation: portrait") ||
        strcasestr(raw, "@orientation:portrait") ||
        strcasestr(raw, "@orientation: vertical") ||
        strcasestr(raw, "@orientation:vertical") ||
        strcasestr(raw, "setorientation(\"portrait\")") ||
        strcasestr(raw, "setorientation('portrait')") ||
        strcasestr(raw, "setorientation(\"vertical\")") ||
        strcasestr(raw, "setorientation('vertical')")) {
        return false;
    }

    if (strcasestr(raw, "vp_w = 240") ||
        strcasestr(raw, "vp_w=240") ||
        strcasestr(raw, "240, 274") ||
        strcasestr(raw, "240,274")) {
        return false;
    }

    if (strcasestr(raw, "vp_w = 320") ||
        strcasestr(raw, "vp_w=320") ||
        strcasestr(raw, "vp_h = 194") ||
        strcasestr(raw, "vp_h=194") ||
        strcasestr(raw, "320, 194") ||
        strcasestr(raw, "320,194") ||
        strcasestr(raw, "width = 320") ||
        strcasestr(raw, "w = 320")) {
        return true;
    }

    return false;
}

static char s_pendingLaunchPath[64] = {0};

static int sys_launch(lua_State* L) {
    const char* path = safeCheckString(L, 1);
    strncpy(s_pendingLaunchPath, path, sizeof(s_pendingLaunchPath) - 1);
    s_pendingLaunchPath[sizeof(s_pendingLaunchPath) - 1] = '\0';
    s_abort = true;
    return 0;
}

static int sys_probeSD(lua_State* L) {
    LUA_CHECK_HEAP(L, 512);
    bool ok = StorageHAL::probeSDCard();
    if (ok) {
        core::AppRegistry::scanFilesystemApps();
    }
    lua_pushboolean(L, ok);
    return 1;
}

static int sys_getKernelLog(lua_State* L) {
    LUA_CHECK_HEAP(L, 2048);
    if (StorageHAL::isSDMounted()) {
        core::Logger::syncToSD();
    }
    String logContent = core::Logger::readLog();
    if (!lua_pushstring(L, logContent.c_str())) {
        return luaL_error(L, "out of memory");
    }
    return 1;
}

static int sys_clearKernelLog(lua_State* L) {
    LUA_CHECK_HEAP(L, 128);
    core::Logger::clearLog();
    return 0;
}

static int sys_syncKernelLog(lua_State* L) {
    LUA_CHECK_HEAP(L, 256);
    bool ok = core::Logger::syncToSD();
    lua_pushboolean(L, ok);
    return 1;
}

static int sys_log(lua_State* L) {
    LUA_CHECK_HEAP(L, 256);
    const char* lvlStr = safeOptString(L, 1, "INFO");
    const char* comp   = safeOptString(L, 2, "LUA");
    const char* msg    = safeOptString(L, 3, "");
    core::LogLevel level = core::LogLevel::INFO;
    if (strcasecmp(lvlStr, "FATAL") == 0)      level = core::LogLevel::FATAL;
    else if (strcasecmp(lvlStr, "ERROR") == 0 || strcasecmp(lvlStr, "ERR") == 0) level = core::LogLevel::ERROR;
    else if (strcasecmp(lvlStr, "WARN") == 0  || strcasecmp(lvlStr, "WARNING") == 0) level = core::LogLevel::WARN;
    core::Logger::log(level, comp, "%s", msg);
    return 0;
}

static int sys_isDevMode(lua_State* L) {
    lua_pushboolean(L, Config::getInstance().isDevModeEnabled());
    return 1;
}

static int sys_setDevMode(lua_State* L) {
    bool en = lua_toboolean(L, 1);
    Config::getInstance().setDevModeEnabled(en, true);
    return 0;
}

static int sys_installApp(lua_State* L) {
    const char* srcPath = safeCheckString(L, 1);
    size_t fsz = StorageHAL::getFileSize(srcPath);
    if (fsz == 0 || fsz > 65536) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Invalid source script size");
        return 2;
    }
    LUA_CHECK_HEAP(L, fsz + 1024);

    String content;
    if (!StorageHAL::readFile(srcPath, content) || content.length() == 0) {
        LOG_ERROR("INSTALL", "Failed to read %s for install", srcPath);
        lua_pushboolean(L, false);
        lua_pushstring(L, "Cannot read source script");
        return 2;
    }

    const char* lastSlash = strrchr(srcPath, '/');
    const char* fn = lastSlash ? (lastSlash + 1) : srcPath;
    if (!fn || fn[0] == '\0') {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Invalid filename");
        return 2;
    }

    char destPath[64];
    snprintf(destPath, sizeof(destPath), "/apps/%s", fn);
    StorageHAL::ensureDirectory("/apps");
    if (!StorageHAL::writeFile(destPath, content.c_str())) {
        LOG_ERROR("INSTALL", "Failed writing %s to Flash", destPath);
        lua_pushboolean(L, false);
        lua_pushstring(L, "Flash write failed");
        return 2;
    }

    content = String(); // free buffer before rescan
    core::AppRegistry::rescan();
    LOG_INFO("INSTALL", "Installed %s -> %s on Home Screen", srcPath, destPath);
    lua_pushboolean(L, true);
    lua_pushstring(L, destPath);
    return 2;
}

static int sys_runScript(lua_State* L) {
    const char* path = safeCheckString(L, 1);
    size_t fsz = StorageHAL::getFileSize(path);
    if (fsz == 0 || fsz > 65536) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "File not found or invalid size");
        return 2;
    }
    LUA_CHECK_HEAP(L, fsz + 2048);

    String src;
    if (!StorageHAL::readFile(path, src)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "File not found or unreadable");
        return 2;
    }
    int rc = luaL_loadbuffer(L, src.c_str(), src.length(), path);
    src = String(); // release source text immediately before executing
    if (rc != LUA_OK) {
        lua_pushboolean(L, false);
        lua_insert(L, -2);
        return 2;
    }
    rc = lua_pcall(L, 0, 0, 0);
    if (rc != LUA_OK) {
        lua_pushboolean(L, false);
        lua_insert(L, -2);
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

// ------------------------------------------------------------- file.* API
static int file_list(lua_State* L) {
    LUA_CHECK_HEAP(L, 2048);
    const char* path = safeOptString(L, 1, "/apps");
    std::vector<String> fileList;
    StorageHAL::listDirectory(path, fileList, nullptr);

    if ((strcmp(path, "/sd") == 0 || strcmp(path, "/sd/") == 0) && StorageHAL::isSDMounted()) {
        std::vector<String> sdApps;
        StorageHAL::listDirectory("/sd/apps", sdApps, nullptr);
        for (size_t k = 0; k < sdApps.size(); k++) {
            bool dup = false;
            for (size_t d = 0; d < fileList.size(); d++) {
                if (fileList[d] == sdApps[k]) { dup = true; break; }
            }
            if (!dup) fileList.push_back(sdApps[k]);
        }
    }

    lua_newtable(L);
    for (size_t i = 0; i < fileList.size(); i++) {
        LUA_CHECK_HEAP(L, 256);
        lua_newtable(L);
        const char* fullPath = fileList[i].c_str();
        lua_pushstring(L, fullPath);
        lua_setfield(L, -2, "path");

        const char* lastSlash = strrchr(fullPath, '/');
        const char* fn = lastSlash ? (lastSlash + 1) : fullPath;
        lua_pushstring(L, fn);
        lua_setfield(L, -2, "name");

        size_t sz = StorageHAL::getFileSize(fullPath);
        lua_pushinteger(L, (lua_Integer)sz);
        lua_setfield(L, -2, "size");

        bool isSD = StorageHAL::isSDPath(fullPath);
        lua_pushboolean(L, isSD);
        lua_setfield(L, -2, "isSD");

        bool isLua = endsWithIgnoreCase(fn, ".lua") || endsWithIgnoreCase(fn, ".app");
        lua_pushboolean(L, isLua);
        lua_setfield(L, -2, "isLua");

        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int file_read(lua_State* L) {
    const char* path = safeCheckString(L, 1);
    size_t sz = StorageHAL::getFileSize(path);
    if (sz > 65536) {
        return luaL_error(L, "file too large for memory");
    }
    LUA_CHECK_HEAP(L, sz + 512);

    String content;
    if (StorageHAL::readFile(path, content)) {
        if (!lua_pushstring(L, content.c_str())) {
            return luaL_error(L, "out of memory");
        }
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int file_write(lua_State* L) {
    LUA_CHECK_HEAP(L, 512);
    const char* path = safeCheckString(L, 1);
    const char* content = safeCheckString(L, 2);
    bool ok = StorageHAL::writeFile(path, content);
    lua_pushboolean(L, ok);
    return 1;
}

static int file_remove(lua_State* L) {
    LUA_CHECK_HEAP(L, 128);
    const char* path = safeCheckString(L, 1);
    bool ok = StorageHAL::deleteFile(path);
    lua_pushboolean(L, ok);
    return 1;
}

static int file_exists(lua_State* L) {
    LUA_CHECK_HEAP(L, 128);
    const char* path = safeCheckString(L, 1);
    bool ok = StorageHAL::fileExists(path);
    lua_pushboolean(L, ok);
    return 1;
}

static void collectImagesFromDir(const char* dirPath, std::vector<String>& outPaths) {
    if (!core::MemoryGuard::ensureHeapFloor(1024)) return;
    std::vector<String> list;
    if (!StorageHAL::listDirectory(dirPath, list, nullptr)) return;
    for (size_t i = 0; i < list.size(); i++) {
        const char* p = list[i].c_str();
        if (endsWithIgnoreCase(p, ".bmp") || endsWithIgnoreCase(p, ".jpg") || endsWithIgnoreCase(p, ".jpeg")) {
            bool dup = false;
            for (size_t k = 0; k < outPaths.size(); k++) {
                if (outPaths[k] == list[i]) { dup = true; break; }
            }
            if (!dup) outPaths.push_back(list[i]);
        }
    }
}

static int file_listImages(lua_State* L) {
    LUA_CHECK_HEAP(L, 2048);
    std::vector<String> imgPaths;
    if (StorageHAL::isSDMounted()) {
        collectImagesFromDir("/sd", imgPaths);
        collectImagesFromDir("/sd/pictures", imgPaths);
        collectImagesFromDir("/sd/images", imgPaths);
        collectImagesFromDir("/sd/dcim", imgPaths);
    }
    collectImagesFromDir("/pictures", imgPaths);
    collectImagesFromDir("/", imgPaths);

    lua_newtable(L);
    for (size_t i = 0; i < imgPaths.size(); i++) {
        LUA_CHECK_HEAP(L, 256);
        lua_newtable(L);
        const char* fullPath = imgPaths[i].c_str();
        lua_pushstring(L, fullPath);
        lua_setfield(L, -2, "path");

        const char* lastSlash = strrchr(fullPath, '/');
        const char* fn = lastSlash ? (lastSlash + 1) : fullPath;
        lua_pushstring(L, fn);
        lua_setfield(L, -2, "name");

        size_t sz = StorageHAL::getFileSize(fullPath);
        lua_pushinteger(L, (lua_Integer)sz);
        lua_setfield(L, -2, "size");

        bool isSD = StorageHAL::isSDPath(fullPath);
        lua_pushboolean(L, isSD);
        lua_setfield(L, -2, "isSD");

        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

static int sys_lockScreen(lua_State* L) {
    LUA_CHECK_HEAP(L, 0);
    bool sleepDisp = lua_toboolean(L, 1) != 0;
    ui::UIManager::lockScreen(sleepDisp);
    return 0;
}

// ------------------------------------------------------------- wifi.* API (Real ESP32 802.11 Station)
static int wifi_isPrepared(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

static int wifi_isEnabled(lua_State* L) {
    lua_pushboolean(L, Config::getInstance().isWifiEnabled() ? 1 : 0);
    return 1;
}

static int wifi_setEnabled(lua_State* L) {
    LUA_CHECK_HEAP(L, 256);
    bool en = lua_toboolean(L, 1) != 0;
    Config::getInstance().setWifiEnabled(en, true);
    if (en) {
        WiFi.mode(WIFI_STA);
        const char* ssid = Config::getInstance().getWifiSsid();
        const char* pass = Config::getInstance().getWifiPass();
        if (ssid && ssid[0] != '\0') {
            WiFi.begin(ssid, pass);
        }
    } else {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    lua_pushboolean(L, en);
    return 1;
}

static int wifi_getStatus(lua_State* L) {
    LUA_CHECK_HEAP(L, 64);
    if (!Config::getInstance().isWifiEnabled()) {
        lua_pushstring(L, "DISABLED");
        return 1;
    }
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        lua_pushstring(L, "CONNECTED");
    } else if (st == WL_IDLE_STATUS) {
        lua_pushstring(L, "CONNECTING");
    } else if (st == WL_NO_SSID_AVAIL) {
        lua_pushstring(L, "NO_SSID");
    } else if (st == WL_CONNECT_FAILED) {
        lua_pushstring(L, "AUTH_FAILED");
    } else {
        lua_pushstring(L, "DISCONNECTED");
    }
    return 1;
}

static int wifi_getMac(lua_State* L) {
    LUA_CHECK_HEAP(L, 64);
    uint64_t mac = ESP.getEfuseMac();
    char buf[24];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)(mac & 0xFF), (unsigned)((mac >> 8) & 0xFF),
             (unsigned)((mac >> 16) & 0xFF), (unsigned)((mac >> 24) & 0xFF),
             (unsigned)((mac >> 32) & 0xFF), (unsigned)((mac >> 40) & 0xFF));
    lua_pushstring(L, buf);
    return 1;
}

static int wifi_getSSID(lua_State* L) {
    LUA_CHECK_HEAP(L, 64);
    if (WiFi.status() == WL_CONNECTED) {
        lua_pushstring(L, WiFi.SSID().c_str());
    } else {
        lua_pushstring(L, Config::getInstance().getWifiSsid());
    }
    return 1;
}

static int wifi_getIP(lua_State* L) {
    LUA_CHECK_HEAP(L, 64);
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        char ipBuf[24];
        snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        lua_pushstring(L, ipBuf);
    } else {
        lua_pushstring(L, "0.0.0.0");
    }
    return 1;
}

static int wifi_getRSSI(lua_State* L) {
    if (WiFi.status() == WL_CONNECTED) {
        lua_pushinteger(L, WiFi.RSSI());
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

static int wifi_connect(lua_State* L) {
    LUA_CHECK_HEAP(L, 1024);
    const char* ssid = safeCheckString(L, 1);
    const char* pass = safeOptString(L, 2, "");
    Config::getInstance().setWifiEnabled(true, false);
    Config::getInstance().setWifiSsid(ssid, false);
    Config::getInstance().setWifiPass(pass, true);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    delay(20);
    if (pass && pass[0] != '\0') {
        WiFi.begin(ssid, pass);
    } else {
        WiFi.begin(ssid);
    }
    LOG_INFO("WIFI", "Lua initiated Wi-Fi connect to SSID: %s", ssid);
    core::TimeService::syncNtpNow(1800);
    lua_pushboolean(L, true);
    return 1;
}

static int wifi_disconnect(lua_State* L) {
    LUA_CHECK_HEAP(L, 128);
    WiFi.disconnect(false);
    LOG_INFO("WIFI", "Wi-Fi disconnected via Lua API");
    lua_pushboolean(L, true);
    return 1;
}

static int wifi_scan(lua_State* L) {
    LUA_CHECK_HEAP(L, 2048);
    WiFi.mode(WIFI_STA);
    int n = WiFi.scanNetworks(false, false);
    lua_newtable(L);
    if (n > 0) {
        int maxCount = (n > 12) ? 12 : n;
        for (int i = 0; i < maxCount; i++) {
            if (!core::MemoryGuard::ensureHeapFloor(256)) break;
            lua_newtable(L);
            lua_pushstring(L, WiFi.SSID(i).c_str());
            lua_setfield(L, -2, "ssid");
            lua_pushinteger(L, WiFi.RSSI(i));
            lua_setfield(L, -2, "rssi");
            lua_pushboolean(L, WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
            lua_setfield(L, -2, "open");
            lua_rawseti(L, -2, i + 1);
        }
    }
    WiFi.scanDelete();
    return 1;
}

// -------------------------------------------------------- http.* & net.* API
static constexpr int LUA_HTTP_MAX_STRING_SIZE = 16384; // 16 KB cap to protect Lua SRAM
static constexpr int LUA_HTTP_DEFAULT_TIMEOUT = 4000;  // 4s max blocking timeout

static void applyLuaHeaders(lua_State* L, int idx, HTTPClient& http) {
    if (!lua_istable(L, idx)) return;
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
            http.addHeader(lua_tostring(L, -2), lua_tostring(L, -1));
        }
        lua_pop(L, 1);
    }
}

static int l_http_request_impl(lua_State* L, const char* method, const char* url,
                               const char* body, size_t bodyLen,
                               const char* contentType, int headersIdx, int timeoutMs) {
    LUA_CHECK_HEAP(L, 4096);
    if (WiFi.status() != WL_CONNECTED) {
        lua_pushnil(L);
        lua_pushinteger(L, -1);
        lua_pushstring(L, "WiFi not connected");
        return 3;
    }

    if (timeoutMs < 500) timeoutMs = 500;
    if (timeoutMs > 8000) timeoutMs = 8000;

    pumpOS();
    checkAbort(L);

    bool isHttps = (strncasecmp(url, "https://", 8) == 0);
    int httpCode = -1;
    String payload;
    String errStr;
    bool tooLarge = false;

    if (isHttps) {
        // Check heap before MbedTLS allocation (~40 KB required)
        if (xPortGetFreeHeapSize() < 48000) {
            lua_pushnil(L);
            lua_pushinteger(L, -4);
            lua_pushstring(L, "Low SRAM for TLS");
            return 3;
        }
        // Declare WiFiClientSecure BEFORE HTTPClient so HTTPClient destructor runs first!
        WiFiClientSecure secureClient;
        secureClient.setInsecure(); // Skip CA chain allocation to save ~40 KB SRAM
        HTTPClient http;
        http.setConnectTimeout(timeoutMs);
        http.setTimeout(timeoutMs);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        if (!http.begin(secureClient, url)) {
            lua_pushnil(L);
            lua_pushinteger(L, -2);
            lua_pushstring(L, "http.begin failed");
            return 3;
        }
        if (contentType && contentType[0] != '\0') {
            http.addHeader("Content-Type", contentType);
        }
        if (headersIdx > 0 && lua_istable(L, headersIdx)) {
            applyLuaHeaders(L, headersIdx, http);
        }
        if (strcasecmp(method, "GET") == 0) {
            httpCode = http.GET();
        } else if (strcasecmp(method, "POST") == 0) {
            httpCode = http.POST((uint8_t*)body, bodyLen);
        } else {
            httpCode = http.sendRequest(method, (uint8_t*)body, bodyLen);
        }
        if (httpCode > 0) {
            int len = http.getSize();
            if (len > LUA_HTTP_MAX_STRING_SIZE) {
                tooLarge = true;
            } else {
                payload = http.getString();
                if ((int)payload.length() > LUA_HTTP_MAX_STRING_SIZE) {
                    payload = payload.substring(0, LUA_HTTP_MAX_STRING_SIZE);
                }
            }
        } else {
            errStr = http.errorToString(httpCode);
        }
        http.end();
    } else {
        // Declare WiFiClient BEFORE HTTPClient so HTTPClient destructor runs first!
        WiFiClient client;
        HTTPClient http;
        http.setConnectTimeout(timeoutMs);
        http.setTimeout(timeoutMs);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        if (!http.begin(client, url)) {
            lua_pushnil(L);
            lua_pushinteger(L, -2);
            lua_pushstring(L, "http.begin failed");
            return 3;
        }
        if (contentType && contentType[0] != '\0') {
            http.addHeader("Content-Type", contentType);
        }
        if (headersIdx > 0 && lua_istable(L, headersIdx)) {
            applyLuaHeaders(L, headersIdx, http);
        }
        if (strcasecmp(method, "GET") == 0) {
            httpCode = http.GET();
        } else if (strcasecmp(method, "POST") == 0) {
            httpCode = http.POST((uint8_t*)body, bodyLen);
        } else {
            httpCode = http.sendRequest(method, (uint8_t*)body, bodyLen);
        }
        if (httpCode > 0) {
            int len = http.getSize();
            if (len > LUA_HTTP_MAX_STRING_SIZE) {
                tooLarge = true;
            } else {
                payload = http.getString();
                if ((int)payload.length() > LUA_HTTP_MAX_STRING_SIZE) {
                    payload = payload.substring(0, LUA_HTTP_MAX_STRING_SIZE);
                }
            }
        } else {
            errStr = http.errorToString(httpCode);
        }
        http.end();
    }

    pumpOS();
    checkAbort(L);

    if (tooLarge) {
        lua_pushnil(L);
        lua_pushinteger(L, httpCode);
        lua_pushstring(L, "Payload > 16KB; use http.download()");
        return 3;
    }

    if (httpCode > 0) {
        LUA_CHECK_HEAP(L, payload.length() + 128);
        lua_pushlstring(L, payload.c_str(), payload.length());
        lua_pushinteger(L, httpCode);
        return 2;
    } else {
        lua_pushnil(L);
        lua_pushinteger(L, httpCode);
        lua_pushstring(L, errStr.c_str());
        return 3;
    }
}

// http.get(url [, headers_tbl, timeout_ms]) -> body|nil, code, err
static int http_get(lua_State* L) {
    const char* url = safeCheckString(L, 1);
    int timeout = (int)luaL_optinteger(L, 3, LUA_HTTP_DEFAULT_TIMEOUT);
    return l_http_request_impl(L, "GET", url, nullptr, 0, nullptr, 2, timeout);
}

// http.post(url, body [, content_type, headers_tbl]) -> body|nil, code, err
static int http_post(lua_State* L) {
    const char* url = safeCheckString(L, 1);
    size_t bodyLen = 0;
    const char* body = luaL_optlstring(L, 2, "", &bodyLen);
    const char* contentType = safeOptString(L, 3, "application/json");
    return l_http_request_impl(L, "POST", url, body, bodyLen, contentType, 4, LUA_HTTP_DEFAULT_TIMEOUT);
}

// http.request(method, url [, body, headers_tbl, timeout_ms]) -> body|nil, code, err
static int http_request(lua_State* L) {
    const char* method = safeCheckString(L, 1);
    const char* url = safeCheckString(L, 2);
    size_t bodyLen = 0;
    const char* body = luaL_optlstring(L, 3, "", &bodyLen);
    int timeout = (int)luaL_optinteger(L, 5, LUA_HTTP_DEFAULT_TIMEOUT);
    return l_http_request_impl(L, method, url, body, bodyLen, nullptr, 4, timeout);
}

// http.download(url, dest_path) -> ok (bool), bytes_written (int), status_code (int)
static int http_download(lua_State* L) {
    LUA_CHECK_HEAP(L, 2048);
    const char* url  = safeCheckString(L, 1);
    const char* path = safeCheckString(L, 2);

    if (WiFi.status() != WL_CONNECTED) {
        lua_pushboolean(L, 0);
        lua_pushinteger(L, 0);
        lua_pushinteger(L, -1);
        return 3;
    }

    bool isHttps = (strncasecmp(url, "https://", 8) == 0);
    if (isHttps && xPortGetFreeHeapSize() < 48000) {
        lua_pushboolean(L, 0);
        lua_pushinteger(L, 0);
        lua_pushinteger(L, -4);
        return 3;
    }

    auto streamToDestination = [&](HTTPClient &http, int &outTotalBytes, int &outHttpCode) {
        outHttpCode = http.GET();
        if (outHttpCode != HTTP_CODE_OK) {
            http.end();
            return;
        }

        if (strncmp(path, "/apps/", 6) == 0) {
            StorageHAL::ensureDirectory("/apps");
        }

        File f;
        if (strncmp(path, "/sd/", 4) == 0) {
            f = SD.open(path + 3, FILE_WRITE);
        } else {
            f = LittleFS.open(path, "w");
        }

        if (!f) {
            http.end();
            outHttpCode = -3;
            return;
        }

        WiFiClient* stream = http.getStreamPtr();
        uint8_t buff[512];
        int len = http.getSize();
        uint32_t lastActivity = millis();

        while ((http.connected() || (stream && stream->available() > 0)) && (len > 0 || len == -1)) {
            size_t avail = stream ? stream->available() : 0;
            if (avail) {
                int toRead = (avail > sizeof(buff)) ? (int)sizeof(buff) : (int)avail;
                int c = stream->readBytes(buff, toRead);
                if (c > 0) {
                    f.write(buff, c);
                    outTotalBytes += c;
                    if (len > 0) len -= c;
                    lastActivity = millis();
                }
            } else if (millis() - lastActivity > 5000) {
                break;
            }
            pumpOS();
            if (s_abort) break;
            delay(1);
        }

        f.flush();
        f.close();
        http.end();
    };

    int totalBytes = 0;
    int httpCode = -2;
    if (isHttps) {
        // Declare WiFiClientSecure BEFORE HTTPClient so HTTPClient destructor runs first!
        WiFiClientSecure secureClient;
        secureClient.setInsecure();
        HTTPClient http;
        http.setConnectTimeout(5000);
        http.setTimeout(5000);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        if (http.begin(secureClient, url)) {
            streamToDestination(http, totalBytes, httpCode);
        }
    } else {
        WiFiClient client;
        HTTPClient http;
        http.setConnectTimeout(5000);
        http.setTimeout(5000);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        if (http.begin(client, url)) {
            streamToDestination(http, totalBytes, httpCode);
        }
    }

    if (s_abort) {
        checkAbort(L);
    }

    // Automatically register newly downloaded /apps/*.lua scripts on Home Screen
    if (totalBytes > 0 && strncmp(path, "/apps/", 6) == 0) {
        core::AppRegistry::scanFilesystemApps();
    }

    LOG_INFO("HTTP", "Downloaded %s -> %s (%d B, HTTP %d)", url, path, totalBytes, httpCode);
    lua_pushboolean(L, totalBytes > 0);
    lua_pushinteger(L, totalBytes);
    lua_pushinteger(L, httpCode);
    return 3;
}

// net.ping(host [, port, timeout_ms]) -> alive (bool), rtt_ms (int)
static int net_ping(lua_State* L) {
    LUA_CHECK_HEAP(L, 512);
    const char* host = safeCheckString(L, 1);
    uint16_t port = (uint16_t)luaL_optinteger(L, 2, 80);
    int timeoutMs = (int)luaL_optinteger(L, 3, 2000);
    if (port == 0) port = 80;

    if (WiFi.status() != WL_CONNECTED) {
        lua_pushboolean(L, false);
        lua_pushinteger(L, -1);
        return 2;
    }

    pumpOS();
    checkAbort(L);

    uint32_t t0 = millis();
    WiFiClient client;
    bool ok = client.connect(host, port, timeoutMs);
    uint32_t rtt = millis() - t0;
    if (ok) {
        client.stop();
    } else if (port == 80) {
        // Try port 53 (DNS) or 443 (HTTPS) if port 80 is closed on target host (e.g. 1.1.1.1 / 8.8.8.8)
        t0 = millis();
        ok = client.connect(host, 53, timeoutMs);
        rtt = millis() - t0;
        if (ok) client.stop();
    }

    lua_pushboolean(L, ok);
    lua_pushinteger(L, ok ? (lua_Integer)rtt : -1);
    return 2;
}

// net.getTime() -> epoch (int), formatted_str (string), hour, min, sec, day, month, year
static int net_getTime(lua_State* L) {
    LUA_CHECK_HEAP(L, 64);
    char tBuf[16];
    core::TimeService::formatHHMMSS(tBuf, sizeof(tBuf));
    time_t now = time(nullptr);
    struct tm tmInfo;
    memset(&tmInfo, 0, sizeof(tmInfo));
    localtime_r(&now, &tmInfo);

    lua_pushinteger(L, (lua_Integer)now);
    lua_pushstring(L, tBuf);
    lua_pushinteger(L, tmInfo.tm_hour);
    lua_pushinteger(L, tmInfo.tm_min);
    lua_pushinteger(L, tmInfo.tm_sec);
    lua_pushinteger(L, tmInfo.tm_mday);
    lua_pushinteger(L, tmInfo.tm_mon + 1);
    lua_pushinteger(L, tmInfo.tm_year + 1900);
    return 8;
}

static const luaL_Reg kGuiLib[] = {
    {"fillRect", gui_fillRect}, {"drawRect", gui_drawRect}, {"drawText", gui_drawText},
    {"drawLine", gui_drawLine}, {"drawFastHLine", gui_drawFastHLine},
    {"fillCircle", gui_fillCircle}, {"drawPixel", gui_drawPixel},
    {"drawImage", gui_drawImage}, {"drawBase64", gui_drawBase64},
    {"createSampleImages", gui_createSampleImages},
    {"keyboard", gui_keyboard},
    {nullptr, nullptr}};

static const luaL_Reg kInputLib[] = {{"getTouch", input_getTouch}, {nullptr, nullptr}};

static const luaL_Reg kSysLib[] = {
    {"delay", sys_delay}, {"beep", sys_beep}, {"getHeap", sys_getHeap},
    {"getLdr", sys_getLdr}, {"getUptime", sys_getUptime},
    {"getTime", sys_getTime}, {"syncNTP", sys_syncNTP}, {"getRegion", sys_getRegion},
    {"getStorageTotal", sys_getStorageTotal}, {"getStorageUsed", sys_getStorageUsed},
    {"isSDMounted", sys_isSDMounted}, {"probeSD", sys_probeSD},
    {"getKernelLog", sys_getKernelLog}, {"clearKernelLog", sys_clearKernelLog},
    {"syncKernelLog", sys_syncKernelLog}, {"log", sys_log},
    {"isDevMode", sys_isDevMode}, {"setDevMode", sys_setDevMode},
    {"installApp", sys_installApp}, {"lockScreen", sys_lockScreen},
    {"setLED", sys_setLED}, {"setBrightness", sys_setBrightness},
    {"setOrientation", sys_setOrientation}, {"getOrientation", sys_getOrientation},
    {"getViewport", sys_getViewport}, {"getScreenSize", sys_getScreenSize},
    {"launch", sys_launch}, {"runScript", sys_runScript}, {nullptr, nullptr}};

static const luaL_Reg kFileLib[] = {
    {"list", file_list}, {"listImages", file_listImages},
    {"read", file_read}, {"write", file_write},
    {"remove", file_remove}, {"exists", file_exists},
    {"run", sys_runScript}, {nullptr, nullptr}};

static const luaL_Reg kWifiLib[] = {
    {"isPrepared", wifi_isPrepared}, {"isEnabled", wifi_isEnabled},
    {"setEnabled", wifi_setEnabled}, {"getStatus", wifi_getStatus},
    {"getMac", wifi_getMac}, {"getSSID", wifi_getSSID},
    {"getIP", wifi_getIP}, {"getRSSI", wifi_getRSSI},
    {"connect", wifi_connect}, {"disconnect", wifi_disconnect},
    {"scan", wifi_scan}, {nullptr, nullptr}};

static const luaL_Reg kHttpLib[] = {
    {"get", http_get}, {"post", http_post},
    {"request", http_request}, {"download", http_download},
    {nullptr, nullptr}};

static const luaL_Reg kNetLib[] = {
    {"ping", net_ping}, {"getTime", net_getTime}, {"syncNTP", sys_syncNTP},
    {"get", http_get}, {"post", http_post},
    {"request", http_request}, {"download", http_download},
    {nullptr, nullptr}};

static void registerLib(lua_State* L, const char* name, const luaL_Reg* funcs) {
    int count = 0;
    while (funcs[count].name != nullptr) count++;
    lua_createtable(L, 0, count);
    luaL_setfuncs(L, funcs, 0);
    lua_setglobal(L, name);
}

// Lazy C-module loader attached to _G.__index:
// Only allocates file, wifi, http, or net tables when a Lua script actually accesses them!
static int lazyGlobalIndex(lua_State* L) {
    const char* key = lua_tostring(L, 2);
    if (!key) {
        lua_pushnil(L);
        return 1;
    }

    const luaL_Reg* targetFuncs = nullptr;
    if (strcmp(key, "file") == 0)      targetFuncs = kFileLib;
    else if (strcmp(key, "wifi") == 0) targetFuncs = kWifiLib;
    else if (strcmp(key, "http") == 0) targetFuncs = kHttpLib;
    else if (strcmp(key, "net") == 0)  targetFuncs = kNetLib;

    if (!targetFuncs) {
        lua_pushnil(L);
        return 1;
    }

    int count = 0;
    while (targetFuncs[count].name != nullptr) count++;
    lua_createtable(L, 0, count);
    luaL_setfuncs(L, targetFuncs, 0);

    // Cache table directly in _G[key] so subsequent accesses have zero metatable overhead
    lua_pushvalue(L, 2); // key
    lua_pushvalue(L, -2); // table
    lua_rawset(L, 1);     // _G[key] = table
    return 1;             // return table on top of stack
}

static void openSandbox(lua_State* L) {
    luaL_requiref(L, "_G", luaopen_base, 1);   lua_pop(L, 1);
    luaL_requiref(L, "string", luaopen_string, 1); lua_pop(L, 1);
    luaL_requiref(L, "table", luaopen_table, 1);   lua_pop(L, 1);
    luaL_requiref(L, "math", luaopen_math, 1);     lua_pop(L, 1);

    // Remove file-loading primitives from base library
    lua_pushnil(L); lua_setglobal(L, "dofile");
    lua_pushnil(L); lua_setglobal(L, "loadfile");

    // Eagerly load only core display/input/system primitives
    registerLib(L, "gui", kGuiLib);
    registerLib(L, "input", kInputLib);
    registerLib(L, "sys", kSysLib);

    // Attach lazy __index metatable to _G so file, wifi, http, and net C modules
    // are instantiated ONLY when an app actually uses them!
    lua_pushglobaltable(L);
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, lazyGlobalIndex);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_pop(L, 1);
}

// ------------------------------------------------------------- app logic
void DynamicScriptApp::onResume() {
    App::onResume();
    m_started = false;
    m_finished = false;
    m_aborted = false;
    m_finalDrawn = false;
    m_error[0] = '\0';
    s_abort = false;
    s_touched = false;
}

void DynamicScriptApp::onPause() {
    App::onPause();
    s_abort = true;
}

void DynamicScriptApp::onTouch(uint16_t x, uint16_t y, bool pressed) {
    // UIManager hands us viewport-relative Y; scripts use absolute screen coordinates.
    s_touchX = x;
    s_touchY = y + VIEWPORT_Y;
    s_touched = pressed;
}

void DynamicScriptApp::onUpdate() {
    if (m_started) return;
    m_started = true;
    runScript();
    s_running = false;

    // Check if script requested launching a custom app (zero-heap stack path handling)
    if (s_pendingLaunchPath[0] != '\0') {
        char nextPath[64];
        strncpy(nextPath, s_pendingLaunchPath, sizeof(nextPath) - 1);
        nextPath[sizeof(nextPath) - 1] = '\0';
        s_pendingLaunchPath[0] = '\0';

        const char* slash = strrchr(nextPath, '/');
        const char* baseStart = slash ? (slash + 1) : nextPath;
        char baseBuf[32];
        strncpy(baseBuf, baseStart, sizeof(baseBuf) - 1);
        baseBuf[sizeof(baseBuf) - 1] = '\0';
        char* dot = strrchr(baseBuf, '.');
        if (dot) *dot = '\0';

        char appName[32];
        snprintf(appName, sizeof(appName), "SD-%.24s", baseBuf);
        core::DynamicScriptApp* nextApp = new (std::nothrow) core::DynamicScriptApp(nextPath, appName, "ICON_LUA");
        if (nextApp) {
            ui::UIManager::launchApp(nextApp);
            return;
        }
    }

    // Crash Guard & Auto-Exit using fixed-size stack buffers (Zero heap allocations)
    if (m_error[0] != '\0') {
        hal::beep(60);
        delay(60);
        hal::beep(100);
        char toast[48];
        snprintf(toast, sizeof(toast), "App error: %.28s", m_error);
        ui::UIManager::showToast(toast, 2500);
        ui::UIManager::exitToHome();
        return;
    }

    // Clean exit back to home screen (app cannot freeze the system when finished)
    ui::UIManager::exitToHome();
}

void DynamicScriptApp::runScript() {
    core::MemoryGuard::clearOOMFlag();
    core::MemoryGuard::reclaimEmergencyPool();

    String src;
    if (!StorageHAL::readFile(m_path, src)) {
        snprintf(m_error, sizeof(m_error), "Cannot read %s", m_path);
        LOG_ERROR("LUA", "Cannot read script %s", m_path);
        m_finished = true;
        return;
    }

    // Compatibility Layer: auto-detect whether script targets Landscape (320x240) or Portrait (240x320)
    bool wantLandscape = detectLandscapeScript(src);
    ui::UIManager::setOrientation(wantLandscape);
    LOG_INFO("LUA", "Executing %s (%s, %u B, FreeHeap=%u B)",
             m_path, wantLandscape ? "landscape" : "portrait",
             (unsigned)src.length(), (unsigned)xPortGetFreeHeapSize());

    // Clear active viewport once; afterwards the script owns the viewport
    TFT_eSPI& d = hal::getDisplay();
    int16_t vpW = d.width();
    int16_t vpH = d.height() - VIEWPORT_Y - 26;
    d.fillRect(VIEWPORT_X, VIEWPORT_Y, vpW, vpH, 0x0000);

    // Create sandboxed Lua state with Hard Heap Floor allocator
    lua_State* L = lua_newstate(core::MemoryGuard::luaSandboxedAlloc, nullptr);
    if (!L) {
        core::MemoryGuard::releaseEmergencyPool("lua_newstate OOM");
        snprintf(m_error, sizeof(m_error), "Out of memory creating Lua VM");
        LOG_FATAL("LUA", "Out of memory creating Lua VM for %s", m_path);
        m_finished = true;
        core::MemoryGuard::reclaimEmergencyPool();
        return;
    }
    openSandbox(L);
    lua_sethook(L, hookFn, LUA_MASKCOUNT, 500);

    s_abort = false;
    s_running = true;

    int rc = luaL_loadbuffer(L, src.c_str(), src.length(), m_path);
    // Immediately release script source String from heap before running script bytecode!
    src = String();

    if (rc == LUA_OK) {
        rc = lua_pcall(L, 0, 0, 0);
    }
    if (rc != LUA_OK) {
        if (rc == LUA_ERRMEM || core::MemoryGuard::wasOOMTriggered()) {
            // Release emergency reserve pool so OS crash handler & logger have guaranteed SRAM
            core::MemoryGuard::releaseEmergencyPool("Lua VM OOM");
            snprintf(m_error, sizeof(m_error), "Out of memory (OOM guard)");
            LOG_ERROR("LUA", "OOM guard terminated %s (FreeHeap=%u B)",
                      m_path, (unsigned)xPortGetFreeHeapSize());
        } else {
            const char* msg = lua_tostring(L, -1);
            if (msg && strstr(msg, ABORT_TAG)) {
                m_aborted = true;
                LOG_INFO("LUA", "Script %s exited via user navigation", m_path);
            } else {
                if (!core::MemoryGuard::ensureHeapFloor(0)) {
                    core::MemoryGuard::releaseEmergencyPool("Script error handler");
                }
                snprintf(m_error, sizeof(m_error), "%s", msg ? msg : "unknown error");
                Serial.printf("[Lua] %s: %s\n", m_path, m_error);
                LOG_ERROR("LUA", "Script crash in %s: %s", m_path, m_error);
            }
        }
    }
    s_running = false;
    lua_close(L);

    // After closing the Lua VM (which freed all script allocations), reclaim emergency reserve pool
    core::MemoryGuard::reclaimEmergencyPool();

    m_finished = true;
    if (s_abort) m_aborted = true;
    s_abort = false;
}

void DynamicScriptApp::onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    // While the script is alive it draws everything itself: never repaint (that caused flashing).
    if (!m_started || !m_finished || m_aborted || m_finalDrawn) return;
    m_finalDrawn = true;

    display.fillRect(vx, vy, vw, vh, 0x0821);
    display.setTextDatum(TL_DATUM);
    display.setTextFont(2);
    if (m_error[0]) {
        display.setTextColor(0xF800, 0x0821);
        display.drawString("Script error", vx + 10, vy + 10);
        display.setTextColor(TFT_WHITE, 0x0821);
        display.setTextFont(1);
        // Safe fixed-size stack word-wrap at ~48 chars (zero heap allocation)
        char line[52];
        int y = vy + 36;
        const char* p = m_error;
        while (*p && y < vy + vh - 12) {
            size_t n = strlen(p);
            if (n > 48) n = 48;
            memcpy(line, p, n);
            line[n] = '\0';
            display.drawString(line, vx + 10, y);
            p += n;
            y += 12;
        }
    } else {
        display.setTextColor(0x07E0, 0x0821);
        display.drawString("Script finished", vx + 10, vy + 10);
    }
    display.setTextColor(0x07FF, 0x0821);
    display.setTextFont(1);
    display.drawString("Press Back or Home to exit", vx + 10, vy + vh - 14);
}

} // namespace core
