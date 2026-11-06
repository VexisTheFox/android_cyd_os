/**
 * @file PlayStoreApp.cpp
 * @brief Dynamic Cloud Play Store & OTA Lua App Downloader for CYD OS
 *
 * Dynamically fetches https://raw.githubusercontent.com/VexisTheFox/cydos_appstore/main/catalog.json
 * and caches it to LittleFS (/apps/.catalog.json) for low-RAM streaming parsing.
 */

#include "PlayStoreApp.h"
#include "../core/AppRegistry.h"
#include "../core/Config.h"
#include "../core/Logger.h"
#include "../core/LuaIconCodec.h"
#include "../hal/BoardHAL.h"
#include "../hal/StorageHAL.h"
#include "../ui/UIManager.h"
#include "../ui/KeyboardView.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <cstring>
#include <cstdlib>

namespace apps {

static const char* CATALOG_CLOUD_URL = "https://raw.githubusercontent.com/VexisTheFox/cydos_appstore/main/catalog.json";
static const char* CATALOG_CACHE_PATH = "/apps/.catalog.json";

// Helper to extract a JSON string field value from a single JSON object block
static bool extractJsonField(const char* obj, const char* key, char* outBuf, size_t outMax) {
    if (!obj || !key || !outBuf || outMax == 0) return false;
    outBuf[0] = '\0';

    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(obj, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':') p++;
    if (*p != '"') return false;
    p++; // skip opening quote

    size_t i = 0;
    while (*p && *p != '"' && i + 1 < outMax) {
        if (*p == '\\' && *(p + 1) != '\0') {
            p++;
        }
        outBuf[i++] = *p++;
    }
    outBuf[i] = '\0';
    return (i > 0);
}

static StoreCategory mapCategory(const char* catStr, char* outShort, size_t shortMax) {
    if (!catStr) {
        strncpy(outShort, "APP", shortMax - 1);
        outShort[shortMax - 1] = '\0';
        return StoreCategory::ALL;
    }
    if (strncasecmp(catStr, "Internet", 8) == 0 || strncasecmp(catStr, "NET", 3) == 0) {
        strncpy(outShort, "NET", shortMax - 1);
        outShort[shortMax - 1] = '\0';
        return StoreCategory::INTERNET;
    }
    if (strncasecmp(catStr, "Game", 4) == 0) {
        strncpy(outShort, "GAMES", shortMax - 1);
        outShort[shortMax - 1] = '\0';
        return StoreCategory::GAMES;
    }
    if (strncasecmp(catStr, "Media", 5) == 0 || strncasecmp(catStr, "Audio", 5) == 0) {
        strncpy(outShort, "MEDIA", shortMax - 1);
        outShort[shortMax - 1] = '\0';
        return StoreCategory::MEDIA;
    }
    strncpy(outShort, "TOOLS", shortMax - 1);
    outShort[shortMax - 1] = '\0';
    return StoreCategory::UTILITIES;
}

PlayStoreApp::PlayStoreApp()
    : m_syncedThisSession(false),
      m_activeTab(0),
      m_activeCategory(0),
      m_scrollY(0),
      m_selectedIndex(0),
      m_wasPressed(false),
      m_isDragging(false),
      m_touchStartY(0),
      m_scrollStartY(0),
      m_lastTouchY(0),
      m_lastScrollDrawMs(0),
      m_needsFullRedraw(true),
      m_needsListRedraw(true),
      m_needsFooterRedraw(true) {
    strncpy(m_customUrl, "https://raw.githubusercontent.com/VexisTheFox/cydos_appstore/main/apps/internet/weather.lua", sizeof(m_customUrl) - 1);
    m_customUrl[sizeof(m_customUrl) - 1] = '\0';
    strncpy(m_customFile, "weather.lua", sizeof(m_customFile) - 1);
    m_customFile[sizeof(m_customFile) - 1] = '\0';
    strncpy(m_statusLine, "Repo: VexisTheFox/cydos_appstore", sizeof(m_statusLine) - 1);
    m_statusLine[sizeof(m_statusLine) - 1] = '\0';
}

void PlayStoreApp::onCreate() {
    m_needsFullRedraw = true;
    loadCatalogFromCache();
}

void PlayStoreApp::onResume() {
    m_needsFullRedraw = true;
    m_needsListRedraw = true;
    m_needsFooterRedraw = true;

    if (m_catalog.empty()) {
        loadCatalogFromCache();
    }
    if (!m_syncedThisSession && WiFi.status() == WL_CONNECTED) {
        m_syncedThisSession = true;
        syncCatalogFromGitHub(false);
    }
}

void PlayStoreApp::onUpdate() {}

void PlayStoreApp::onPause() {
    m_wasPressed = false;
    m_isDragging = false;
}

void PlayStoreApp::onDestroy() {
    m_catalog.clear();
    m_catalog.shrink_to_fit();
}

bool PlayStoreApp::loadCatalogFromCache() {
    if (!StorageHAL::fileExists(CATALOG_CACHE_PATH)) {
        return false;
    }

    File f = LittleFS.open(CATALOG_CACHE_PATH, "r");
    if (!f) return false;

    // Seek to "apps" array in catalog.json
    bool foundAppsArray = false;
    char window[8] = {0};
    while (f.available()) {
        memmove(window, window + 1, 6);
        window[6] = (char)f.read();
        window[7] = '\0';
        if (strncmp(window, "\"apps\"", 6) == 0) {
            foundAppsArray = true;
            break;
        }
    }
    if (!foundAppsArray) {
        f.close();
        return false;
    }

    // Advance until opening '[' of "apps": [
    while (f.available()) {
        char c = (char)f.read();
        if (c == '[') break;
    }

    std::vector<StoreCatalogItem> parsed;
    char objBuf[640];

    while (f.available() && parsed.size() < 48) {
        // Find next '{' or end of array ']'
        char c = 0;
        while (f.available()) {
            c = (char)f.read();
            if (c == '{' || c == ']') break;
        }
        if (c != '{') break;

        size_t pos = 0;
        objBuf[pos++] = '{';
        int depth = 1;
        bool inStr = false;
        char prev = 0;

        while (f.available() && depth > 0 && pos + 1 < sizeof(objBuf)) {
            char ch = (char)f.read();
            objBuf[pos++] = ch;
            if (ch == '"' && prev != '\\') {
                inStr = !inStr;
            } else if (!inStr) {
                if (ch == '{') depth++;
                else if (ch == '}') depth--;
            }
            prev = ch;
        }
        objBuf[pos] = '\0';

        StoreCatalogItem item;
        memset(&item, 0, sizeof(item));
        if (!extractJsonField(objBuf, "id", item.id, sizeof(item.id))) continue;
        if (!extractJsonField(objBuf, "title", item.title, sizeof(item.title))) {
            strncpy(item.title, item.id, sizeof(item.title) - 1);
        }

        char catRaw[20] = {0};
        extractJsonField(objBuf, "category", catRaw, sizeof(catRaw));
        item.category = mapCategory(catRaw, item.categoryName, sizeof(item.categoryName));

        char badgeRaw[10] = {0};
        if (extractJsonField(objBuf, "badge", badgeRaw, sizeof(badgeRaw))) {
            strncpy(item.categoryName, badgeRaw, sizeof(item.categoryName) - 1);
        }

        extractJsonField(objBuf, "description", item.desc, sizeof(item.desc));
        snprintf(item.localPath, sizeof(item.localPath), "/apps/%s.lua", item.id);

        if (!extractJsonField(objBuf, "url", item.cloudUrl, sizeof(item.cloudUrl))) {
            char relPath[48] = {0};
            if (extractJsonField(objBuf, "path", relPath, sizeof(relPath))) {
                snprintf(item.cloudUrl, sizeof(item.cloudUrl),
                         "https://raw.githubusercontent.com/VexisTheFox/cydos_appstore/main/%s", relPath);
            }
        }

        extractJsonField(objBuf, "icon_b64", item.iconB64, sizeof(item.iconB64));

        char bgStr[12] = {0};
        item.iconBg = 0x04D3;
        if (extractJsonField(objBuf, "icon_bg", bgStr, sizeof(bgStr))) {
            item.iconBg = (uint16_t)strtoul(bgStr, nullptr, 0);
        }

        parsed.push_back(item);
    }

    f.close();

    if (!parsed.empty()) {
        m_catalog.swap(parsed);
        LOG_INFO("STORE", "Loaded %u apps from %s", (unsigned)m_catalog.size(), CATALOG_CACHE_PATH);
        return true;
    }
    return false;
}

bool PlayStoreApp::syncCatalogFromGitHub(bool showToastMsg) {
    if (WiFi.status() != WL_CONNECTED) {
        if (showToastMsg) {
            ui::UIManager::showToast("Connect Wi-Fi in Settings first!", 1600);
        }
        return false;
    }

    if (showToastMsg) {
        ui::UIManager::showToast("Syncing GitHub App Store...", 1000);
    }

    if (downloadUrlToPath(CATALOG_CLOUD_URL, CATALOG_CACHE_PATH)) {
        if (loadCatalogFromCache()) {
            m_selectedIndex = 0;
            m_scrollY = 0;
            m_needsFullRedraw = true;
            if (showToastMsg) {
                char msg[44];
                snprintf(msg, sizeof(msg), "Synced %u apps from GitHub!", (unsigned)m_catalog.size());
                ui::UIManager::showToast(msg, 1600);
            }
            return true;
        }
    }

    if (showToastMsg) {
        ui::UIManager::showToast("GitHub Sync Failed", 1500);
    }
    return false;
}

int PlayStoreApp::getFilteredCount() const {
    if (m_activeCategory == 0) return (int)m_catalog.size();
    int count = 0;
    for (size_t i = 0; i < m_catalog.size(); i++) {
        if ((uint8_t)m_catalog[i].category == m_activeCategory) {
            count++;
        }
    }
    return count;
}

int PlayStoreApp::getCatalogIndexFromFiltered(int filteredIdx) const {
    if (filteredIdx < 0) return -1;
    if (m_activeCategory == 0) {
        return (filteredIdx < (int)m_catalog.size()) ? filteredIdx : -1;
    }
    int cur = 0;
    for (size_t i = 0; i < m_catalog.size(); i++) {
        if ((uint8_t)m_catalog[i].category == m_activeCategory) {
            if (cur == filteredIdx) return (int)i;
            cur++;
        }
    }
    return -1;
}

bool PlayStoreApp::isInstalled(int catalogIdx) const {
    if (catalogIdx < 0 || catalogIdx >= (int)m_catalog.size()) return false;
    return StorageHAL::fileExists(m_catalog[(size_t)catalogIdx].localPath);
}

int PlayStoreApp::getMaxScroll(int listH) const {
    int totalH = getFilteredCount() * ROW_H + 8;
    int maxS = totalH - listH;
    return (maxS > 0) ? maxS : 0;
}

void PlayStoreApp::onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    if (m_needsFullRedraw) {
        drawHeader(display, vx, vy, vw);
        if (m_activeTab == 0) {
            drawListArea(display, vx, vy, vw, vh);
            drawFooter(display, vx, vy, vw, vh);
        } else {
            drawUrlTab(display, vx, vy, vw, vh);
        }
        m_needsFullRedraw = false;
        m_needsListRedraw = false;
        m_needsFooterRedraw = false;
        return;
    }

    if (m_activeTab == 0) {
        if (m_needsListRedraw) {
            drawListArea(display, vx, vy, vw, vh);
            m_needsListRedraw = false;
        }
        if (m_needsFooterRedraw) {
            drawFooter(display, vx, vy, vw, vh);
            m_needsFooterRedraw = false;
        }
    }
}

void PlayStoreApp::drawHeader(TFT_eSPI &display, int vx, int vy, int vw) {
    // 1. Top Title Bar (22px) + Live [SYNC] button
    display.fillRect(vx, vy, vw, 22, 0x0352);
    display.setTextFont(1);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(0x07FF, 0x0352);
    display.drawString("CYD PLAY STORE [CLOUD]", vx + 6, vy + 7);

    // SYNC button in top right (rx = 176..236)
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    uint16_t syncBg = wifiOk ? 0x04D3 : 0x2104;
    display.fillRoundRect(vx + vw - 62, vy + 2, 58, 18, 3, syncBg);
    display.drawRoundRect(vx + vw - 62, vy + 2, 58, 18, 3, wifiOk ? 0x07E0 : 0xFFE0);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(TFT_WHITE, syncBg);
    display.drawString("SYNC GIT", vx + vw - 33, vy + 11);

    // 2. Main Tab Bar (22px at vy + 22)
    int tabY = vy + 22;
    int halfW = vw / 2;
    uint16_t t0Bg = (m_activeTab == 0) ? 0x18E3 : 0x0821;
    uint16_t t1Bg = (m_activeTab == 1) ? 0x18E3 : 0x0821;

    display.fillRect(vx, tabY, halfW, 22, t0Bg);
    display.fillRect(vx + halfW, tabY, vw - halfW, 22, t1Bg);
    display.drawFastHLine(vx, tabY + 21, vw, 0x3186);

    if (m_activeTab == 0) {
        display.fillRect(vx + 6, tabY + 19, halfW - 12, 2, 0x07E0);
    } else {
        display.fillRect(vx + halfW + 6, tabY + 19, halfW - 12, 2, 0x07FF);
    }

    char catTabTitle[24];
    snprintf(catTabTitle, sizeof(catTabTitle), "CATALOG (%u)", (unsigned)getFilteredCount());
    display.setTextDatum(MC_DATUM);
    display.setTextColor((m_activeTab == 0) ? TFT_WHITE : 0x8410, t0Bg);
    display.drawString(catTabTitle, vx + halfW / 2, tabY + 11);

    display.setTextColor((m_activeTab == 1) ? TFT_WHITE : 0x8410, t1Bg);
    display.drawString("CUSTOM URL / GIT", vx + halfW + halfW / 2, tabY + 11);

    // 3. Category Filter Bar (24px at vy + 44)
    int catY = vy + 44;
    display.fillRect(vx, catY, vw, 24, 0x0C63);
    display.drawFastHLine(vx, catY + 23, vw, 0x2965);

    if (m_activeTab == 0) {
        static const char* CAT_LABELS[5] = { "ALL", "NET", "GAMES", "MEDIA", "TOOLS" };
        int pillW = 44;
        for (int c = 0; c < 5; c++) {
            int px = vx + 4 + c * 47;
            bool active = (m_activeCategory == c);
            uint16_t pBg = active ? 0x04D3 : 0x18E3;
            uint16_t pBo = active ? 0x07FF : 0x3186;
            display.fillRoundRect(px, catY + 3, pillW, 18, 4, pBg);
            display.drawRoundRect(px, catY + 3, pillW, 18, 4, pBo);
            display.setTextColor(active ? TFT_WHITE : 0x9CD3, pBg);
            display.drawString(CAT_LABELS[c], px + pillW / 2, catY + 12);
        }
    } else {
        display.setTextColor(0x07FF, 0x0C63);
        display.drawString("github.com/VexisTheFox/cydos_appstore", vx + vw / 2, catY + 12);
    }

    display.setTextDatum(TL_DATUM);
}

void PlayStoreApp::drawListArea(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int listTop = vy + HEADER_H;
    int listH   = vh - HEADER_H - FOOTER_H;
    int filteredCount = getFilteredCount();

    if (filteredCount == 0) {
        display.fillRect(vx, listTop, vw, listH, 0x0821);
        display.setTextFont(1);
        display.setTextDatum(MC_DATUM);
        display.setTextColor(0xFFE0, 0x0821);
        display.drawString("Catalog not synced yet.", vx + vw / 2, listTop + listH / 2 - 12);
        display.setTextColor(0x07FF, 0x0821);
        display.drawString("Connect Wi-Fi & tap [SYNC GIT] at top-right!", vx + vw / 2, listTop + listH / 2 + 6);
        display.setTextDatum(TL_DATUM);
        return;
    }

    TFT_eSprite band(&display);
    band.setColorDepth(16);
    const int BAND_H = 64;
    bool hasSprite = (band.createSprite(vw, BAND_H) != nullptr);

    for (int yOff = 0; yOff < listH; yOff += BAND_H) {
        int curH = (yOff + BAND_H > listH) ? (listH - yOff) : BAND_H;
        TFT_eSPI &canvas = hasSprite ? (TFT_eSPI&)band : display;
        int baseX = hasSprite ? 0 : vx;
        int baseY = hasSprite ? 0 : (listTop + yOff);

        canvas.fillRect(baseX, baseY, vw, curH, 0x0821);

        for (int fIdx = 0; fIdx < filteredCount; fIdx++) {
            int rowDocY = 4 + fIdx * ROW_H;
            int rowScreenY = rowDocY - m_scrollY - yOff;
            if (rowScreenY + ROW_H <= 0 || rowScreenY >= curH) continue;

            int catIdx = getCatalogIndexFromFiltered(fIdx);
            if (catIdx < 0) continue;

            int ry = baseY + rowScreenY;
            bool sel = (fIdx == m_selectedIndex);
            bool inst = isInstalled(catIdx);
            const StoreCatalogItem &item = m_catalog[(size_t)catIdx];

            uint16_t cardBg = sel ? 0x1949 : 0x10A2;
            uint16_t border = sel ? 0x07FF : 0x2965;

            canvas.fillRoundRect(baseX + 6, ry, vw - 12, ROW_H - 4, 5, cardBg);
            canvas.drawRoundRect(baseX + 6, ry, vw - 12, ROW_H - 4, 5, border);

            // Render custom Base64 logo inside 28x28 rounded badge
            canvas.fillRoundRect(baseX + 11, ry + 5, 28, 28, 5, item.iconBg);
            canvas.drawRoundRect(baseX + 11, ry + 5, 28, 28, 5, 0x4A69);
            core::LuaIconCodec::drawBase64Icon(canvas, item.iconB64, baseX + 25, ry + 19, TFT_WHITE, 1);

            // App Title + Category Badge
            canvas.setTextFont(1);
            canvas.setTextDatum(TL_DATUM);
            canvas.setTextColor(TFT_WHITE, cardBg);
            canvas.drawString(item.title, baseX + 44, ry + 6);

            char catBadge[16];
            snprintf(catBadge, sizeof(catBadge), "[%s]", item.categoryName);
            canvas.setTextColor(0xFFE0, cardBg);
            canvas.drawString(catBadge, baseX + 116, ry + 6);

            // Description
            canvas.setTextColor(0x9CD3, cardBg);
            canvas.drawString(item.desc, baseX + 44, ry + 20);

            // Status pill
            uint16_t pillBg = inst ? 0x03E0 : 0x0276;
            uint16_t pillFg = inst ? TFT_WHITE : 0x07FF;
            canvas.fillRoundRect(baseX + vw - 64, ry + 5, 52, 13, 3, pillBg);
            canvas.setTextDatum(MC_DATUM);
            canvas.setTextColor(pillFg, pillBg);
            canvas.drawString(inst ? "INSTALLED" : "GET OTA", baseX + vw - 38, ry + 11);
            canvas.setTextDatum(TL_DATUM);
        }

        if (hasSprite) {
            band.pushSprite(vx, listTop + yOff, 0, 0, vw, curH);
        }
    }

    if (hasSprite) {
        band.deleteSprite();
    }
}

void PlayStoreApp::drawFooter(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int footY = vy + vh - FOOTER_H;
    display.fillRect(vx, footY, vw, FOOTER_H, 0x10A2);
    display.drawFastHLine(vx, footY, vw, 0x3186);

    int catIdx = getCatalogIndexFromFiltered(m_selectedIndex);
    bool inst = isInstalled(catIdx);

    // Button 1: INSTALL or UPDATE from VexisTheFox/cydos_appstore
    display.fillRoundRect(vx + 6, footY + 5, 86, 26, 5, 0x04D3);
    display.drawRoundRect(vx + 6, footY + 5, 86, 26, 5, 0x07E0);
    display.setTextFont(1);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(TFT_WHITE, 0x04D3);
    display.drawString(inst ? "UPDATE OTA" : "INSTALL OTA", vx + 49, footY + 18);

    // Button 2: OPEN
    uint16_t openBg = inst ? 0x2C9F : 0x18E3;
    display.fillRoundRect(vx + 98, footY + 5, 66, 26, 5, openBg);
    display.drawRoundRect(vx + 98, footY + 5, 66, 26, 5, inst ? 0x07FF : 0x3186);
    display.setTextColor(inst ? TFT_WHITE : 0x8410, openBg);
    display.drawString("OPEN", vx + 131, footY + 18);

    // Button 3: UNINSTALL
    uint16_t delBg = inst ? 0x7800 : 0x18E3;
    display.fillRoundRect(vx + 170, footY + 5, 64, 26, 5, delBg);
    display.drawRoundRect(vx + 170, footY + 5, 64, 26, 5, inst ? 0xF800 : 0x3186);
    display.setTextColor(inst ? TFT_WHITE : 0x8410, delBg);
    display.drawString("REMOVE", vx + 202, footY + 18);
    display.setTextDatum(TL_DATUM);
}

void PlayStoreApp::drawUrlTab(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int bodyY = vy + HEADER_H;
    int bodyH = vh - HEADER_H;
    display.fillRect(vx, bodyY, vw, bodyH, 0x0821);

    display.setTextFont(1);
    display.setTextDatum(TL_DATUM);

    // Card 1: Custom URL
    display.fillRoundRect(8, bodyY + 6, vw - 16, 58, 5, 0x10A2);
    display.drawRoundRect(8, bodyY + 6, vw - 16, 58, 5, 0x3186);
    display.setTextColor(0x07FF, 0x10A2);
    display.drawString("1. Lua Script URL (Tap to Edit):", 14, bodyY + 12);
    display.setTextColor(TFT_WHITE, 0x10A2);
    char u1[36], u2[36];
    strncpy(u1, m_customUrl, 34); u1[34] = '\0';
    size_t uLen = strlen(m_customUrl);
    if (uLen > 34) {
        strncpy(u2, m_customUrl + 34, 34); u2[34] = '\0';
    } else {
        u2[0] = '\0';
    }
    display.drawString(u1, 14, bodyY + 26);
    display.setTextColor(0x9CD3, 0x10A2);
    display.drawString(u2, 14, bodyY + 40);

    // Card 2: Target Filename
    display.fillRoundRect(8, bodyY + 70, vw - 16, 36, 5, 0x10A2);
    display.drawRoundRect(8, bodyY + 70, vw - 16, 36, 5, 0x3186);
    display.setTextColor(0xFFE0, 0x10A2);
    display.drawString("2. Save As (/apps/):", 14, bodyY + 76);
    display.setTextColor(TFT_WHITE, 0x10A2);
    display.drawString(m_customFile, 14, bodyY + 90);

    // Status Card
    display.fillRoundRect(8, bodyY + 112, vw - 16, 36, 5, 0x10A2);
    display.drawRoundRect(8, bodyY + 112, vw - 16, 36, 5, 0x2965);
    display.setTextColor(0x07E0, 0x10A2);
    display.drawString("Status:", 14, bodyY + 118);
    display.setTextColor(TFT_WHITE, 0x10A2);
    display.drawString(m_statusLine, 14, bodyY + 132);

    // Download & Install Button
    display.fillRoundRect(16, bodyY + 156, vw - 32, 40, 6, 0x04D3);
    display.drawRoundRect(16, bodyY + 156, vw - 32, 40, 6, 0x07E0);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(TFT_WHITE, 0x04D3);
    display.drawString("DOWNLOAD & INSTALL TO HOME", vx + vw / 2, bodyY + 176);
    display.setTextDatum(TL_DATUM);
}

static bool streamHttpToFile(HTTPClient& http, const char* destPath) {
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    StorageHAL::ensureDirectory("/apps");
    File f = LittleFS.open(destPath, "w");
    if (!f) {
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buff[256];
    int total = 0;
    int len = http.getSize();
    uint32_t lastAct = millis();

    while ((http.connected() || (stream && stream->available() > 0)) && (len > 0 || len == -1)) {
        size_t avail = stream ? stream->available() : 0;
        if (avail) {
            int c = stream->readBytes(buff, (avail > sizeof(buff)) ? sizeof(buff) : avail);
            if (c > 0) {
                f.write(buff, c);
                total += c;
                if (len > 0) len -= c;
                lastAct = millis();
            }
        } else if (millis() - lastAct > 4500) {
            break;
        }
        delay(1);
    }
    f.flush();
    f.close();
    http.end();
    return (total > 16);
}

bool PlayStoreApp::downloadUrlToPath(const char* url, const char* destPath) {
    if (WiFi.status() != WL_CONNECTED) return false;
    if (xPortGetFreeHeapSize() < 48000) return false;

    bool isHttps = (strncasecmp(url, "https://", 8) == 0);
    if (isHttps) {
        // CRITICAL: Declare WiFiClientSecure BEFORE HTTPClient so HTTPClient is destroyed FIRST!
        WiFiClientSecure secureClient;
        secureClient.setInsecure();
        HTTPClient http;
        http.setConnectTimeout(4500);
        http.setTimeout(4500);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        if (!http.begin(secureClient, url)) return false;
        return streamHttpToFile(http, destPath);
    } else {
        WiFiClient client;
        HTTPClient http;
        http.setConnectTimeout(4500);
        http.setTimeout(4500);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        if (!http.begin(client, url)) return false;
        return streamHttpToFile(http, destPath);
    }
}

void PlayStoreApp::installFeaturedApp(int catalogIdx) {
    if (catalogIdx < 0 || catalogIdx >= (int)m_catalog.size()) return;
    const StoreCatalogItem &item = m_catalog[(size_t)catalogIdx];

    if (WiFi.status() != WL_CONNECTED) {
        ui::UIManager::showToast("Connect Wi-Fi in Settings first!", 1800);
        return;
    }

    char dlMsg[44];
    snprintf(dlMsg, sizeof(dlMsg), "Downloading %s...", item.title);
    ui::UIManager::showToast(dlMsg, 1200);

    if (downloadUrlToPath(item.cloudUrl, item.localPath)) {
        core::AppRegistry::scanFilesystemApps();
        LOG_INFO("STORE", "OTA Installed %s from cydos_appstore -> %s", item.title, item.localPath);
        char msg[44];
        snprintf(msg, sizeof(msg), "Installed %s to Home!", item.title);
        ui::UIManager::showToast(msg, 1600);
    } else {
        LOG_ERROR("STORE", "OTA download failed for %s (%s)", item.title, item.cloudUrl);
        ui::UIManager::showToast("OTA Download Failed", 1600);
    }
    m_needsFullRedraw = true;
}

void PlayStoreApp::uninstallFeaturedApp(int catalogIdx) {
    if (catalogIdx < 0 || catalogIdx >= (int)m_catalog.size()) return;
    const StoreCatalogItem &item = m_catalog[(size_t)catalogIdx];
    if (!isInstalled(catalogIdx)) return;

    LittleFS.remove(item.localPath);
    core::AppRegistry::rescan();
    LOG_INFO("STORE", "Uninstalled %s (%s)", item.title, item.localPath);

    char msg[44];
    snprintf(msg, sizeof(msg), "Removed %s", item.title);
    ui::UIManager::showToast(msg, 1400);
    m_needsFullRedraw = true;
}

void PlayStoreApp::openFeaturedApp(int catalogIdx) {
    if (catalogIdx < 0 || catalogIdx >= (int)m_catalog.size()) return;
    if (!isInstalled(catalogIdx)) {
        installFeaturedApp(catalogIdx);
        if (!isInstalled(catalogIdx)) return;
    }
    const StoreCatalogItem &item = m_catalog[(size_t)catalogIdx];
    int appIdx = core::AppRegistry::findAppIndex(item.id);
    if (appIdx < 0) appIdx = core::AppRegistry::findAppIndex(item.title);
    if (appIdx >= 0) {
        ui::UIManager::launchApp((size_t)appIdx);
    }
}

void PlayStoreApp::onTouch(uint16_t x, uint16_t y, bool pressed) {
    int vw = 240;
    int vh = 274;
    int listH = vh - HEADER_H - FOOTER_H;

    if (pressed && !m_wasPressed) {
        m_wasPressed   = true;
        m_isDragging   = false;
        m_touchStartY  = (int16_t)y;
        m_lastTouchY   = (int16_t)y;
        m_scrollStartY = m_scrollY;
        return;
    }

    if (pressed && m_wasPressed) {
        if (m_activeTab == 0 && y >= HEADER_H && y < (vh - FOOTER_H)) {
            int dy = (int)m_touchStartY - (int)y;
            if (!m_isDragging && abs(dy) > 7) {
                m_isDragging = true;
            }
            if (m_isDragging) {
                int maxS = getMaxScroll(listH);
                int newS = m_scrollStartY + dy;
                if (newS < 0) newS = 0;
                if (newS > maxS) newS = maxS;
                uint32_t now = millis();
                if (newS != m_scrollY && (now - m_lastScrollDrawMs >= 20)) {
                    m_scrollY = (int16_t)newS;
                    m_lastScrollDrawMs = now;
                    m_needsListRedraw = true;
                }
            }
        }
        m_lastTouchY = (int16_t)y;
        return;
    }

    if (!pressed && m_wasPressed) {
        m_wasPressed = false;
        if (!m_isDragging) {
            handleTap((int16_t)x, (int16_t)y, vw, vh);
        }
        m_isDragging = false;
    }
}

void PlayStoreApp::handleTap(int16_t rx, int16_t ry, int vw, int vh) {
    // 0. Top Title Bar [SYNC GIT] Button (ry < 22, rx >= vw - 66)
    if (ry < 22) {
        if (rx >= vw - 66) {
            hal::beep(25);
            syncCatalogFromGitHub(true);
        }
        return;
    }

    // 1. Main Tab Bar (ry = 22..43)
    if (ry >= 22 && ry < 44) {
        uint8_t newTab = (rx < vw / 2) ? 0 : 1;
        if (newTab != m_activeTab) {
            hal::beep(20);
            m_activeTab = newTab;
            m_needsFullRedraw = true;
        }
        return;
    }

    // 2. Category Filter Bar (ry = 44..67)
    if (ry >= 44 && ry < HEADER_H && m_activeTab == 0) {
        int cat = (rx - 4) / 47;
        if (cat >= 0 && cat <= 4 && (uint8_t)cat != m_activeCategory) {
            hal::beep(18);
            m_activeCategory = (uint8_t)cat;
            m_selectedIndex = 0;
            m_scrollY = 0;
            m_needsFullRedraw = true;
        }
        return;
    }

    if (m_activeTab == 0) {
        if (m_catalog.empty()) {
            hal::beep(25);
            syncCatalogFromGitHub(true);
            return;
        }

        // List area tap
        if (ry >= HEADER_H && ry < (vh - FOOTER_H)) {
            int docY = (ry - HEADER_H) + m_scrollY - 4;
            int fIdx = docY / ROW_H;
            int filteredCount = getFilteredCount();
            if (fIdx >= 0 && fIdx < filteredCount) {
                hal::beep(20);
                m_selectedIndex = (int16_t)fIdx;
                int catIdx = getCatalogIndexFromFiltered(fIdx);
                if (rx >= vw - 70 && catIdx >= 0) {
                    installFeaturedApp(catIdx);
                } else {
                    m_needsListRedraw = true;
                    m_needsFooterRedraw = true;
                }
            }
            return;
        }

        // Footer buttons
        if (ry >= (vh - FOOTER_H)) {
            int catIdx = getCatalogIndexFromFiltered(m_selectedIndex);
            if (catIdx < 0) return;
            hal::beep(25);
            if (rx < 94) {
                installFeaturedApp(catIdx);
            } else if (rx < 168) {
                openFeaturedApp(catIdx);
            } else {
                uninstallFeaturedApp(catIdx);
            }
            return;
        }
    } else {
        // Tab 1: Custom URL Installer
        int bodyY = HEADER_H;
        if (ry >= bodyY + 6 && ry <= bodyY + 64) {
            hal::beep(20);
            char outUrl[128] = {0};
            if (ui::KeyboardView::prompt("Enter .lua Script URL", m_customUrl, outUrl, sizeof(outUrl))) {
                if (outUrl[0] != '\0') {
                    strncpy(m_customUrl, outUrl, sizeof(m_customUrl) - 1);
                    m_customUrl[sizeof(m_customUrl) - 1] = '\0';
                }
            }
            m_needsFullRedraw = true;
        } else if (ry >= bodyY + 70 && ry <= bodyY + 106) {
            hal::beep(20);
            char outFile[32] = {0};
            if (ui::KeyboardView::prompt("Filename in /apps/", m_customFile, outFile, sizeof(outFile))) {
                if (outFile[0] != '\0') {
                    strncpy(m_customFile, outFile, sizeof(m_customFile) - 1);
                    m_customFile[sizeof(m_customFile) - 1] = '\0';
                }
            }
            m_needsFullRedraw = true;
        } else if (ry >= bodyY + 156 && ry <= bodyY + 200) {
            hal::beep(30);
            snprintf(m_statusLine, sizeof(m_statusLine), "Downloading via HTTP/TLS...");
            m_needsFullRedraw = true;
            onRender(hal::getDisplay(), 0, 20, 240, 274);

            char dest[48];
            snprintf(dest, sizeof(dest), "/apps/%s", m_customFile);
            if (downloadUrlToPath(m_customUrl, dest)) {
                core::AppRegistry::scanFilesystemApps();
                snprintf(m_statusLine, sizeof(m_statusLine), "Installed to /apps/!");
                ui::UIManager::showToast("URL App Installed!", 1600);
            } else {
                snprintf(m_statusLine, sizeof(m_statusLine), "Download failed (Check Wi-Fi/URL)");
                ui::UIManager::showToast("Download failed", 1600);
            }
            m_needsFullRedraw = true;
        }
    }
}

} // namespace apps
