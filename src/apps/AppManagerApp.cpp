/**
 * @file AppManagerApp.cpp
 * @brief Implementation of Native Android-Style App Manager & Package Installer
 */

#include "AppManagerApp.h"
#include "../core/AppRegistry.h"
#include "../core/Logger.h"
#include "../hal/StorageHAL.h"
#include "../hal/BoardHAL.h"
#include "../ui/UIManager.h"
#include <LittleFS.h>
#include <SD.h>

namespace apps {

static constexpr uint16_t COL_BG         = 0x0821;
static constexpr uint16_t COL_HEADER     = 0x10A2;
static constexpr uint16_t COL_CARD       = 0x18E3;
static constexpr uint16_t COL_CARD_SEL   = 0x1A6B;
static constexpr uint16_t COL_BORDER     = 0x3186;
static constexpr uint16_t COL_CYAN       = 0x07FF;
static constexpr uint16_t COL_GREEN      = 0x07E0;
static constexpr uint16_t COL_AMBER      = 0xFFE0;
static constexpr uint16_t COL_ORANGE     = 0xFD20;
static constexpr uint16_t COL_RED        = 0xF800;
static constexpr uint16_t COL_MUTED      = 0x9CD3;

AppManagerApp::AppManagerApp()
    : m_activeTab(0)
    , m_scrollY(0)
    , m_selectedIndex(0)
    , m_wasPressed(false)
    , m_isDragging(false)
    , m_touchStartY(0)
    , m_scrollStartY(0)
    , m_lastTouchY(0)
    , m_needsFullRedraw(true)
    , m_needsListRedraw(true)
    , m_needsFooterRedraw(true) {
}

void AppManagerApp::onCreate() {
    m_state = core::AppState::CREATED;
    refreshSDPackages();
}

void AppManagerApp::onResume() {
    m_state = core::AppState::RUNNING;
    m_wasPressed = false;
    m_isDragging = false;
    m_needsFullRedraw = true;
    m_needsListRedraw = true;
    m_needsFooterRedraw = true;
    if (m_activeTab == 1) {
        refreshSDPackages();
    }
    LOG_INFO("APPMGR", "Opened App Manager (apps=%u, sdPkgs=%u)",
             (unsigned)core::AppRegistry::getAppCount(), (unsigned)m_sdPackages.size());
}

void AppManagerApp::onUpdate() {
}

void AppManagerApp::onPause() {
    m_state = core::AppState::PAUSED;
}

void AppManagerApp::onDestroy() {
    m_sdPackages.clear();
    m_sdPackages.shrink_to_fit();
    m_state = core::AppState::DESTROYED;
}

bool AppManagerApp::isCoreBundledLua(const char* path) const {
    if (!path || path[0] == '\0') return false;
    return (strcasecmp(path, "/apps/gallery.lua") == 0 ||
            strcasecmp(path, "/apps/files.lua") == 0 ||
            strcasecmp(path, "/apps/sysinfo.lua") == 0 ||
            strcasecmp(path, "/apps/calc.lua") == 0 ||
            strcasecmp(path, "/apps/paint.lua") == 0 ||
            strcasecmp(path, "/apps/terminal.lua") == 0);
}

void AppManagerApp::refreshSDPackages() {
    m_sdPackages.clear();
    m_sdPackages.shrink_to_fit();
    if (!StorageHAL::isSDMounted()) {
        StorageHAL::probeSDCard();
    }
    if (!StorageHAL::isSDMounted()) {
        return;
    }

    const char* dirs[] = { "/sd", "/sd/apps" };
    for (size_t d = 0; d < 2; d++) {
        std::vector<String> files;
        StorageHAL::listDirectory(dirs[d], files, nullptr);
        for (size_t i = 0; i < files.size(); i++) {
            String p = files[i];
            String lower = p;
            lower.toLowerCase();
            if (!lower.endsWith(".lua")) continue;

            // Avoid duplicate paths
            bool dup = false;
            for (size_t k = 0; k < m_sdPackages.size(); k++) {
                if (p.equalsIgnoreCase(m_sdPackages[k].sdPath)) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            SDPackageEntry entry;
            memset(&entry, 0, sizeof(entry));
            strncpy(entry.sdPath, p.c_str(), sizeof(entry.sdPath) - 1);

            int slash = p.lastIndexOf('/');
            String fname = (slash >= 0) ? p.substring(slash + 1) : p;
            int dot = fname.lastIndexOf('.');
            String base = (dot >= 0) ? fname.substring(0, dot) : fname;

            strncpy(entry.name, base.c_str(), sizeof(entry.name) - 1);
            if (entry.name[0] >= 'a' && entry.name[0] <= 'z') {
                entry.name[0] -= ('a' - 'A');
            }

            snprintf(entry.localPath, sizeof(entry.localPath), "/apps/%s", fname.c_str());
            entry.installedInFlash = StorageHAL::fileExists(entry.localPath);

            // Query file size on SD
            entry.sizeBytes = 0;
            const char* sdRel = p.c_str() + 3; // strip "/sd"
            File f = SD.open(sdRel, FILE_READ);
            if (f) {
                entry.sizeBytes = (uint32_t)f.size();
                f.close();
            }

            m_sdPackages.push_back(entry);
            if (m_sdPackages.size() >= 24) break;
        }
    }
}

int AppManagerApp::getItemCount() const {
    if (m_activeTab == 0) {
        return (int)core::AppRegistry::getAppCount();
    } else {
        return (int)m_sdPackages.size();
    }
}

int AppManagerApp::getMaxScroll(int listH) const {
    int totalH = getItemCount() * ROW_H + 8;
    int maxS = totalH - listH;
    return (maxS > 0) ? maxS : 0;
}

void AppManagerApp::drawHeader(TFT_eSPI &display, int vx, int vy, int vw) {
    // Top bar (22px)
    display.fillRect(vx, vy, vw, 22, COL_HEADER);
    display.setTextFont(2);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(TFT_WHITE, COL_HEADER);
    display.drawString("App Manager", vx + 6, vy + 3);

    display.setTextFont(1);
    display.setTextColor(COL_AMBER, COL_HEADER);
    display.drawString("[DEV BUILD]", vx + 94, vy + 7);

    // UP ^ and DN v scroll buttons in header right
    display.fillRoundRect(vx + vw - 76, vy + 2, 34, 18, 3, 0x2166);
    display.drawRoundRect(vx + vw - 76, vy + 2, 34, 18, 3, COL_CYAN);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COL_CYAN, 0x2166);
    display.drawString("UP^", vx + vw - 59, vy + 11);

    display.fillRoundRect(vx + vw - 38, vy + 2, 34, 18, 3, 0x2166);
    display.drawRoundRect(vx + vw - 38, vy + 2, 34, 18, 3, COL_CYAN);
    display.drawString("DNv", vx + vw - 21, vy + 11);

    // Tab bar (24px at vy + 22)
    int tabY = vy + 22;
    display.fillRect(vx, tabY, vw, 24, 0x0C63);

    char tab0Buf[28];
    snprintf(tab0Buf, sizeof(tab0Buf), "INSTALLED (%u)", (unsigned)core::AppRegistry::getAppCount());
    uint16_t t0Bg = (m_activeTab == 0) ? 0x04D3 : 0x18E3;
    uint16_t t0Border = (m_activeTab == 0) ? COL_CYAN : COL_BORDER;
    display.fillRoundRect(vx + 4, tabY + 2, 112, 20, 4, t0Bg);
    display.drawRoundRect(vx + 4, tabY + 2, 112, 20, 4, t0Border);
    display.setTextColor(TFT_WHITE, t0Bg);
    display.drawString(tab0Buf, vx + 60, tabY + 12);

    char tab1Buf[28];
    snprintf(tab1Buf, sizeof(tab1Buf), "SD INSTALL (%u)", (unsigned)m_sdPackages.size());
    uint16_t t1Bg = (m_activeTab == 1) ? 0x9175 : 0x18E3;
    uint16_t t1Border = (m_activeTab == 1) ? COL_AMBER : COL_BORDER;
    display.fillRoundRect(vx + 122, tabY + 2, 114, 20, 4, t1Bg);
    display.drawRoundRect(vx + 122, tabY + 2, 114, 20, 4, t1Border);
    display.setTextColor(TFT_WHITE, t1Bg);
    display.drawString(tab1Buf, vx + 179, tabY + 12);

    display.drawFastHLine(vx, vy + HEADER_H - 1, vw, COL_BORDER);
    display.setTextDatum(TL_DATUM);
}

void AppManagerApp::drawListArea(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int listY = vy + HEADER_H;
    int listH = vh - HEADER_H - FOOTER_H;
    if (listH <= 0) return;

    int count = getItemCount();

    // Use a 16-bit tiled strip sprite (240 x 48 = 23 KB) for 100% flicker-free scrolling!
    static constexpr int BAND_H = 48;
    TFT_eSprite band(&display);
    band.setColorDepth(16);
    bool spriteOk = (band.createSprite(vw, BAND_H) != nullptr);

    for (int offsetY = 0; offsetY < listH; offsetY += BAND_H) {
        int curBandH = (offsetY + BAND_H <= listH) ? BAND_H : (listH - offsetY);
        TFT_eSPI &canvas = spriteOk ? (TFT_eSPI&)band : display;
        int canvasBaseX = spriteOk ? 0 : vx;
        int canvasBaseY = spriteOk ? 0 : (listY + offsetY);

        if (spriteOk) {
            band.fillSprite(COL_BG);
        } else {
            display.setViewport(vx, listY + offsetY, vw, curBandH, false);
            display.fillRect(vx, listY + offsetY, vw, curBandH, COL_BG);
        }

        if (count == 0) {
            int msgY = canvasBaseY + (listH / 2) - offsetY - 10;
            canvas.setTextFont(2);
            canvas.setTextDatum(MC_DATUM);
            canvas.setTextColor(COL_MUTED, COL_BG);
            if (m_activeTab == 1) {
                canvas.drawString(StorageHAL::isSDMounted()
                    ? "No .lua apps found on SD"
                    : "MicroSD Card Not Inserted", canvasBaseX + vw / 2, msgY);
            } else {
                canvas.drawString("No registered applications", canvasBaseX + vw / 2, msgY);
            }
        } else {
            for (int i = 0; i < count; i++) {
                int itemRelY = 4 + i * ROW_H - m_scrollY;
                if (itemRelY + ROW_H < offsetY || itemRelY >= offsetY + curBandH) {
                    continue; // Outside this horizontal strip
                }

                int drawY = canvasBaseY + (itemRelY - offsetY);
                bool selected = (i == m_selectedIndex);
                uint16_t cardBg = selected ? COL_CARD_SEL : COL_CARD;
                uint16_t borderCol = selected ? COL_CYAN : COL_BORDER;

                int cardW = vw - 12;
                int cardX = canvasBaseX + 4;
                canvas.fillRoundRect(cardX, drawY, cardW, ROW_H - 3, 5, cardBg);
                canvas.drawRoundRect(cardX, drawY, cardW, ROW_H - 3, 5, borderCol);

                if (m_activeTab == 0) {
                    const core::AppDescriptor* desc = core::AppRegistry::getAppInfo((size_t)i);
                    if (!desc) continue;

                    // App icon color badge
                    canvas.fillRoundRect(cardX + 5, drawY + 5, 25, 25, 4, desc->iconBgColor);
                    canvas.drawRoundRect(cardX + 5, drawY + 5, 25, 25, 4, TFT_WHITE);
                    canvas.setTextFont(2);
                    canvas.setTextDatum(MC_DATUM);
                    char initial[2] = { desc->name[0], '\0' };
                    canvas.setTextColor(desc->iconFgColor, desc->iconBgColor);
                    canvas.drawString(initial, cardX + 17, drawY + 17);

                    // App Title
                    canvas.setTextDatum(TL_DATUM);
                    canvas.setTextFont(2);
                    canvas.setTextColor(TFT_WHITE, cardBg);
                    canvas.drawString(desc->name, cardX + 36, drawY + 3);

                    // Storage / Type Tag
                    canvas.setTextFont(1);
                    const char* typeTag = "[SYSTEM C++]";
                    uint16_t tagCol = COL_CYAN;
                    if (desc->path[0] != '\0') {
                        if (strncmp(desc->path, "/sd/", 4) == 0) {
                            typeTag = "[SD CARD]";
                            tagCol = COL_ORANGE;
                        } else if (isCoreBundledLua(desc->path)) {
                            typeTag = "[BUNDLED LUA]";
                            tagCol = COL_AMBER;
                        } else {
                            typeTag = "[INSTALLED /apps]";
                            tagCol = COL_GREEN;
                        }
                    }
                    canvas.setTextDatum(TR_DATUM);
                    canvas.setTextColor(tagCol, cardBg);
                    canvas.drawString(typeTag, cardX + cardW - 6, drawY + 6);

                    // Path subtitle
                    canvas.setTextDatum(TL_DATUM);
                    canvas.setTextColor(COL_MUTED, cardBg);
                    const char* sub = (desc->path[0] != '\0') ? desc->path : "Native System Firmware";
                    canvas.drawString(sub, cardX + 36, drawY + 21);
                } else {
                    const SDPackageEntry &pkg = m_sdPackages[(size_t)i];

                    // SD badge
                    uint16_t badgeCol = pkg.installedInFlash ? 0x04D3 : 0xFD00;
                    canvas.fillRoundRect(cardX + 5, drawY + 5, 25, 25, 4, badgeCol);
                    canvas.drawRoundRect(cardX + 5, drawY + 5, 25, 25, 4, TFT_WHITE);
                    canvas.setTextFont(1);
                    canvas.setTextDatum(MC_DATUM);
                    canvas.setTextColor(TFT_WHITE, badgeCol);
                    canvas.drawString("LUA", cardX + 17, drawY + 17);

                    // Package Name
                    canvas.setTextDatum(TL_DATUM);
                    canvas.setTextFont(2);
                    canvas.setTextColor(TFT_WHITE, cardBg);
                    canvas.drawString(pkg.name, cardX + 36, drawY + 3);

                    // Installed status tag
                    canvas.setTextFont(1);
                    canvas.setTextDatum(TR_DATUM);
                    if (pkg.installedInFlash) {
                        canvas.setTextColor(COL_GREEN, cardBg);
                        canvas.drawString("[INSTALLED]", cardX + cardW - 6, drawY + 6);
                    } else {
                        canvas.setTextColor(COL_AMBER, cardBg);
                        canvas.drawString("[READY TO INSTALL]", cardX + cardW - 6, drawY + 6);
                    }

                    // SD Path + Size
                    char subBuf[48];
                    snprintf(subBuf, sizeof(subBuf), "%s (%lu B)", pkg.sdPath, (unsigned long)pkg.sizeBytes);
                    canvas.setTextDatum(TL_DATUM);
                    canvas.setTextColor(COL_MUTED, cardBg);
                    canvas.drawString(subBuf, cardX + 36, drawY + 21);
                }
            }
        }

        // Right-edge scrollbar inside the band
        int maxS = getMaxScroll(listH);
        int sbX = canvasBaseX + vw - 4;
        canvas.fillRect(sbX, canvasBaseY, 3, curBandH, 0x10A2);
        if (maxS > 0) {
            int thumbH = (listH * listH) / (listH + maxS);
            if (thumbH < 18) thumbH = 18;
            int thumbY = ((listH - thumbH) * m_scrollY) / maxS;
            int relThumbY = thumbY - offsetY;
            canvas.fillRect(sbX, canvasBaseY + relThumbY, 3, thumbH, COL_CYAN);
        }

        if (spriteOk) {
            band.pushSprite(vx, listY + offsetY, 0, 0, vw, curBandH);
        } else {
            display.resetViewport();
        }
    }

    if (spriteOk) {
        band.deleteSprite();
    }
    display.setTextDatum(TL_DATUM);
}

void AppManagerApp::drawFooter(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int footY = vy + vh - FOOTER_H;
    display.fillRect(vx, footY, vw, FOOTER_H, COL_HEADER);
    display.drawFastHLine(vx, footY, vw, COL_BORDER);

    display.setTextFont(1);
    display.setTextDatum(MC_DATUM);

    if (m_activeTab == 0) {
        // Button 1: OPEN / LAUNCH (X=6..116)
        display.fillRoundRect(vx + 6, footY + 5, 110, 26, 5, 0x04D3);
        display.drawRoundRect(vx + 6, footY + 5, 110, 26, 5, COL_GREEN);
        display.setTextColor(TFT_WHITE, 0x04D3);
        display.drawString("LAUNCH APP", vx + 61, footY + 18);

        // Button 2: UNINSTALL / RESET (X=124..234)
        const core::AppDescriptor* desc = core::AppRegistry::getAppInfo((size_t)m_selectedIndex);
        bool canModify = (desc && desc->path[0] != '\0');
        uint16_t uBg = canModify ? 0x7800 : 0x2104;
        uint16_t uBorder = canModify ? COL_RED : COL_BORDER;
        const char* uLabel = "CORE SYSTEM";
        if (canModify) {
            uLabel = isCoreBundledLua(desc->path) ? "RESET / REMOVE" : "UNINSTALL APP";
        }
        display.fillRoundRect(vx + 124, footY + 5, 110, 26, 5, uBg);
        display.drawRoundRect(vx + 124, footY + 5, 110, 26, 5, uBorder);
        display.setTextColor(canModify ? TFT_WHITE : COL_MUTED, uBg);
        display.drawString(uLabel, vx + 179, footY + 18);
    } else {
        // Button 1: + INSTALL TO FLASH (X=6..148)
        bool hasPkg = (m_selectedIndex >= 0 && m_selectedIndex < (int)m_sdPackages.size());
        uint16_t iBg = hasPkg ? 0x04D3 : 0x2104;
        display.fillRoundRect(vx + 6, footY + 5, 142, 26, 5, iBg);
        display.drawRoundRect(vx + 6, footY + 5, 142, 26, 5, hasPkg ? COL_GREEN : COL_BORDER);
        display.setTextColor(hasPkg ? TFT_WHITE : COL_MUTED, iBg);
        display.drawString("+ INSTALL TO HOME", vx + 77, footY + 18);

        // Button 2: RESCAN SD (X=154..234)
        display.fillRoundRect(vx + 154, footY + 5, 80, 26, 5, 0x18E3);
        display.drawRoundRect(vx + 154, footY + 5, 80, 26, 5, COL_CYAN);
        display.setTextColor(COL_CYAN, 0x18E3);
        display.drawString("RESCAN SD", vx + 194, footY + 18);
    }

    display.setTextDatum(TL_DATUM);
}

void AppManagerApp::onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    if (m_needsFullRedraw) {
        drawHeader(display, vx, vy, vw);
        drawListArea(display, vx, vy, vw, vh);
        drawFooter(display, vx, vy, vw, vh);
        m_needsFullRedraw = false;
        m_needsListRedraw = false;
        m_needsFooterRedraw = false;
        return;
    }
    if (m_needsListRedraw) {
        drawListArea(display, vx, vy, vw, vh);
        m_needsListRedraw = false;
    }
    if (m_needsFooterRedraw) {
        drawFooter(display, vx, vy, vw, vh);
        m_needsFooterRedraw = false;
    }
}

void AppManagerApp::performPrimaryAction() {
    if (m_activeTab == 0) {
        // Launch selected registered app
        size_t count = core::AppRegistry::getAppCount();
        if (m_selectedIndex >= 0 && (size_t)m_selectedIndex < count) {
            const core::AppDescriptor* desc = core::AppRegistry::getAppInfo((size_t)m_selectedIndex);
            if (desc && strcasecmp(desc->id, "appmgr") == 0) {
                ui::UIManager::showToast("App Manager is already open", 1200);
                return;
            }
            hal::beep(30);
            ui::UIManager::launchApp((size_t)m_selectedIndex);
        }
    } else {
        // Install selected SD package to /apps/ on LittleFS and Home Screen
        if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_sdPackages.size()) {
            ui::UIManager::showToast("Select an SD .lua package first", 1400);
            return;
        }
        const SDPackageEntry &pkg = m_sdPackages[(size_t)m_selectedIndex];
        String content = "";
        if (!StorageHAL::readFile(pkg.sdPath, content) || content.length() == 0) {
            ui::UIManager::showToast("Failed to read SD script", 1500);
            return;
        }
        if (!StorageHAL::writeFile(pkg.localPath, content.c_str())) {
            ui::UIManager::showToast("Flash write failed (/apps)", 1500);
            return;
        }
        core::AppRegistry::rescan();
        refreshSDPackages();
        hal::beep(45);
        LOG_INFO("APPMGR", "Installed SD package '%s' -> '%s'", pkg.sdPath, pkg.localPath);
        char msg[48];
        snprintf(msg, sizeof(msg), "Installed %s to Home!", pkg.name);
        ui::UIManager::showToast(msg, 1800);
        m_needsFullRedraw = true;
    }
}

void AppManagerApp::performSecondaryAction() {
    if (m_activeTab == 0) {
        size_t count = core::AppRegistry::getAppCount();
        if (m_selectedIndex < 0 || (size_t)m_selectedIndex >= count) return;
        const core::AppDescriptor* desc = core::AppRegistry::getAppInfo((size_t)m_selectedIndex);
        if (!desc || !desc->isDynamic || desc->path[0] == '\0') {
            hal::beep(15);
            ui::UIManager::showToast("Protected System C++ App", 1400);
            return;
        }

        char removedName[24];
        strncpy(removedName, desc->name, sizeof(removedName) - 1);
        removedName[sizeof(removedName) - 1] = '\0';

        char targetPath[64];
        strncpy(targetPath, desc->path, sizeof(targetPath) - 1);
        targetPath[sizeof(targetPath) - 1] = '\0';

        if (strncmp(targetPath, "/sd/", 4) == 0) {
            // Delete from SD card
            SD.remove(targetPath + 3);
        } else {
            // Delete from LittleFS (/apps/...)
            LittleFS.remove(targetPath);
        }

        // Rescan AppRegistry cleanly so uninstalled app is pruned and Home Screen updates immediately
        core::AppRegistry::rescan();

        int newCount = (int)core::AppRegistry::getAppCount();
        if (m_selectedIndex >= newCount) {
            m_selectedIndex = (newCount > 0) ? (newCount - 1) : 0;
        }
        m_scrollY = 0;
        hal::beep(40);
        LOG_INFO("APPMGR", "Uninstalled app '%s' (%s)", removedName, targetPath);

        char toast[48];
        snprintf(toast, sizeof(toast), "Uninstalled %s", removedName);
        ui::UIManager::showToast(toast, 1600);
        m_needsFullRedraw = true;
    } else {
        // Rescan SD card
        StorageHAL::probeSDCard();
        refreshSDPackages();
        m_selectedIndex = 0;
        m_scrollY = 0;
        hal::beep(25);
        ui::UIManager::showToast(StorageHAL::isSDMounted() ? "SD Card Rescanned" : "No SD Card Detected", 1400);
        m_needsFullRedraw = true;
    }
}

void AppManagerApp::handleTap(int16_t rx, int16_t ry, int vw, int vh) {
    int listH = vh - HEADER_H - FOOTER_H;
    int maxS = getMaxScroll(listH);

    // 1. Top title bar UP^ / DNv buttons (ry < 22)
    if (ry < 22) {
        if (rx >= vw - 78 && rx < vw - 40) {
            m_scrollY -= 76;
            if (m_scrollY < 0) m_scrollY = 0;
            hal::beep(15);
            m_needsListRedraw = true;
        } else if (rx >= vw - 40) {
            m_scrollY += 76;
            if (m_scrollY > maxS) m_scrollY = maxS;
            hal::beep(15);
            m_needsListRedraw = true;
        }
        return;
    }

    // 2. Tab bar (ry = 22..45)
    if (ry >= 22 && ry < HEADER_H) {
        if (rx < 120 && m_activeTab != 0) {
            m_activeTab = 0;
            m_selectedIndex = 0;
            m_scrollY = 0;
            hal::beep(20);
            m_needsFullRedraw = true;
        } else if (rx >= 120 && m_activeTab != 1) {
            m_activeTab = 1;
            refreshSDPackages();
            m_selectedIndex = 0;
            m_scrollY = 0;
            hal::beep(20);
            m_needsFullRedraw = true;
        }
        return;
    }

    // 3. Bottom Footer Action Bar (ry >= vh - FOOTER_H)
    if (ry >= vh - FOOTER_H) {
        if (m_activeTab == 0) {
            if (rx < 120) {
                performPrimaryAction();
            } else {
                performSecondaryAction();
            }
        } else {
            if (rx < 150) {
                performPrimaryAction();
            } else {
                performSecondaryAction();
            }
        }
        return;
    }

    // 4. App List Row Selection
    int relY = (ry - HEADER_H) + m_scrollY - 4;
    if (relY >= 0) {
        int idx = relY / ROW_H;
        int count = getItemCount();
        if (idx >= 0 && idx < count) {
            m_selectedIndex = (int16_t)idx;
            hal::beep(15);
            m_needsListRedraw = true;
            m_needsFooterRedraw = true;
        }
    }
}

void AppManagerApp::onTouch(uint16_t x, uint16_t y, bool pressed) {
    int16_t rx = (int16_t)x;
    int16_t ry = (int16_t)y;
    int listH = 274 - HEADER_H - FOOTER_H;
    int maxS = getMaxScroll(listH);

    if (pressed && !m_wasPressed) {
        m_wasPressed = true;
        m_isDragging = false;
        m_touchStartY = ry;
        m_lastTouchY = ry;
        m_scrollStartY = m_scrollY;
    } else if (pressed && m_wasPressed) {
        int16_t dy = ry - m_touchStartY;
        if (!m_isDragging && ry > HEADER_H && ry < (274 - FOOTER_H) && abs(dy) > 8) {
            m_isDragging = true;
        }
        if (m_isDragging) {
            int16_t newScroll = m_scrollStartY - dy;
            if (newScroll < 0) newScroll = 0;
            if (newScroll > maxS) newScroll = maxS;
            if (abs(newScroll - m_scrollY) >= 2) {
                m_scrollY = newScroll;
                m_needsListRedraw = true;
            }
        }
        m_lastTouchY = ry;
    } else if (!pressed && m_wasPressed) {
        m_wasPressed = false;
        if (!m_isDragging) {
            handleTap(rx, m_lastTouchY, 240, 274);
        }
        m_isDragging = false;
    }
}

} // namespace apps
