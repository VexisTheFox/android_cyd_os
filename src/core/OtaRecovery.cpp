/**
 * @file OtaRecovery.cpp
 * @brief Dedicated Boot-Time Recovery Menu & GitHub Releases Firmware OTA Updater
 */

#include "OtaRecovery.h"
#include "Version.h"
#include "Config.h"
#include "Logger.h"
#include "../hal/DisplayHAL.h"
#include "../hal/BoardHAL.h"
#include "../hal/TouchHAL.h"
#include "../hal/StorageHAL.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <esp_ota_ops.h>

namespace core {

static const char* OTA_NVS_NS   = "cyd_ota";
static const char* OTA_NVS_KEY  = "ota_pend";
static const char* OTA_REL_URL  = "https://github.com/VexisTheFox/android_cyd_os/releases/latest/download/cyd2usb-firmware-0x10000.bin";

static void drawRecoveryScreen(const char* stage, const char* detail, int pct, uint16_t barCol = 0x07E0) {
    TFT_eSPI &tft = hal::getDisplay();
    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);

    // Status Card (Y = 86..170)
    tft.fillRoundRect(14, 86, 212, 84, 6, 0x10A2);
    tft.drawRoundRect(14, 86, 212, 84, 6, 0x07FF);

    tft.setTextColor(0xFFE0, 0x10A2);
    tft.drawString("RECOVERY STAGE:", 22, 96);
    tft.setTextColor(TFT_WHITE, 0x10A2);
    tft.drawString(stage, 22, 112);

    tft.setTextColor(0x9CD3, 0x10A2);
    tft.drawString(detail ? detail : "", 22, 134);

    char ramBuf[36];
    snprintf(ramBuf, sizeof(ramBuf), "Free Recovery RAM: %lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
    tft.setTextColor(0x07FF, 0x10A2);
    tft.drawString(ramBuf, 22, 152);

    // Progress Bar (Y = 188..236)
    tft.fillRoundRect(14, 188, 212, 48, 6, 0x10A2);
    tft.drawRoundRect(14, 188, 212, 48, 6, 0x3186);

    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int fillW = (pct * 192) / 100;
    tft.fillRect(24, 200, 192, 16, 0x0821);
    if (fillW > 0) {
        tft.fillRect(24, 200, fillW, 16, barCol);
    }
    tft.drawRect(24, 200, 192, 16, TFT_WHITE);

    char pctBuf[20];
    snprintf(pctBuf, sizeof(pctBuf), "%d%% Complete", pct);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, 0x10A2);
    tft.drawString(pctBuf, 120, 226);
    tft.setTextDatum(TL_DATUM);
}

static void waitForTapOrTimeout(uint32_t timeoutMs) {
    uint32_t start = millis();
    uint16_t x = 0, y = 0;
    while (millis() - start < timeoutMs) {
        if (hal::getTouch(x, y)) {
            while (hal::getTouch(x, y)) delay(20);
            break;
        }
        delay(20);
    }
}

static void rebootSystem(TFT_eSPI &tft) {
    hal::beep(30);
    tft.fillScreen(0x0821);
    tft.fillRoundRect(20, 110, 200, 70, 6, 0x10A2);
    tft.drawRoundRect(20, 110, 200, 70, 6, 0x07FF);
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, 0x10A2);
    tft.drawString("REBOOTING CYDOS...", 120, 136);
    tft.setTextFont(1);
    tft.setTextColor(0x07FF, 0x10A2);
    tft.drawString("Starting operating system...", 120, 158);
    tft.setTextDatum(TL_DATUM);
    delay(400);
    ESP.restart();
}

static void executeFactoryReset(TFT_eSPI &tft) {
    hal::beep(80);
    tft.fillScreen(0x0821);
    tft.fillRoundRect(14, 96, 212, 98, 8, 0x3800);
    tft.drawRoundRect(14, 96, 212, 98, 8, TFT_WHITE);
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, 0x3800);
    tft.drawString("FACTORY RESETTING...", 120, 120);
    tft.setTextFont(1);
    tft.setTextColor(0xFFE0, 0x3800);
    tft.drawString("Wiping PIN, NVS, Wi-Fi & restoring apps", 120, 146);
    tft.setTextColor(0x07E0, 0x3800);
    tft.drawString("Forgotten password has been cleared!", 120, 166);
    tft.setTextDatum(TL_DATUM);

    // 1. Clear Wi-Fi radio & connection
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);

    // 2. Clear NVS configurations (wiping PIN lock)
    Config::getInstance().resetDefaults();
    Preferences prefs;
    if (prefs.begin("cyd_cfg", false)) { prefs.clear(); prefs.end(); }
    if (prefs.begin("cyd_ota", false)) { prefs.clear(); prefs.end(); }

    // 3. Clear kernel logs
    core::Logger::clearLog();

    // 4. Restore default bundled apps on LittleFS
    std::vector<String> appFiles;
    if (StorageHAL::listDirectory("/apps", appFiles, nullptr)) {
        for (size_t i = 0; i < appFiles.size(); i++) {
            StorageHAL::deleteFile(appFiles[i].c_str());
        }
    }
    StorageHAL::forceProvisionDefaultApps();

    delay(700);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, 0x3800);
    tft.drawString("Reset complete! Rebooting unlocked...", 120, 180);
    hal::beep(60);
    delay(500);
    ESP.restart();
}

static void runOtaReflash(TFT_eSPI &tft) {
    tft.fillScreen(0x0821);

    // Header Banner
    tft.fillRect(0, 0, 240, 36, 0x0352);
    tft.drawFastHLine(0, 36, 240, 0x07FF);
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, 0x0352);
    tft.drawString("SYSTEM REFLASH (GITHUB)", 120, 18);

    // Partition Info Pill
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* nextPart = esp_ota_get_next_update_partition(nullptr);
    tft.fillRoundRect(14, 44, 212, 34, 5, 0x18E3);
    tft.drawRoundRect(14, 44, 212, 34, 5, 0x3186);
    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
    char partInfo[44];
    snprintf(partInfo, sizeof(partInfo), "Active: %s -> Target: %s",
             running ? running->label : "?",
             nextPart ? nextPart->label : "NONE");
    tft.setTextColor(0x07E0, 0x18E3);
    tft.drawString(partInfo, 22, 50);
    tft.setTextColor(0x9CD3, 0x18E3);
    tft.drawString("Repo: VexisTheFox/android_cyd_os", 22, 64);

    // Footer safety note
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x9CD3, 0x0821);
    tft.drawString("Do not unplug power during Flash write", 120, 290);
    tft.drawString("Auto-rollback protects OS if interrupted", 120, 304);
    tft.setTextDatum(TL_DATUM);

    // 1. Connect to saved Wi-Fi
    const char* ssid = Config::getInstance().getWifiSsid();
    const char* pass = Config::getInstance().getWifiPass();
    if (!ssid || ssid[0] == '\0') {
        drawRecoveryScreen("ERROR: No Saved Wi-Fi", "Configure Wi-Fi in Settings first!\nTap screen to return.", 0, 0xF800);
        hal::beep(200);
        waitForTapOrTimeout(4000);
        return;
    }

    char wifiMsg[42];
    snprintf(wifiMsg, sizeof(wifiMsg), "SSID: %.28s", ssid);
    drawRecoveryScreen("1/4 Connecting to Wi-Fi...", wifiMsg, 5);

    WiFi.mode(WIFI_STA);
    if (pass && pass[0] != '\0') WiFi.begin(ssid, pass);
    else WiFi.begin(ssid);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0 < 12000)) {
        delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
        drawRecoveryScreen("ERROR: Wi-Fi Timeout", "Could not connect to network.\nTap screen to return.", 0, 0xF800);
        hal::beep(200);
        waitForTapOrTimeout(4000);
        return;
    }

    // 2. Connect to GitHub Releases and follow 302 redirect
    drawRecoveryScreen("2/4 Querying GitHub Releases...", "cyd2usb-firmware.bin", 12);

    bool otaSuccess = false;
    {
        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        http.setConnectTimeout(10000);
        http.setTimeout(15000);
        http.setRedirectLimit(5);
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

        if (http.begin(client, OTA_REL_URL)) {
            int code = http.GET();
            int totalLen = http.getSize();

            if (code == HTTP_CODE_OK && totalLen > 100000) {
                char sizeMsg[42];
                snprintf(sizeMsg, sizeof(sizeMsg), "Image Size: %d KB", totalLen / 1024);
                drawRecoveryScreen("3/4 Flashing OTA Partition...", sizeMsg, 15);

                if (Update.begin((size_t)totalLen, U_FLASH)) {
                    WiFiClient* stream = http.getStreamPtr();
                    uint8_t buff[1024];
                    int written = 0;
                    int lastDrawnPct = -1;
                    uint32_t lastDataMs = millis();

                    while ((http.connected() || (stream && stream->available() > 0)) && written < totalLen) {
                        size_t avail = stream ? stream->available() : 0;
                        if (avail > 0) {
                            size_t toRead = (avail > sizeof(buff)) ? sizeof(buff) : avail;
                            int r = stream->readBytes(buff, toRead);
                            if (r > 0) {
                                if (Update.write(buff, (size_t)r) != (size_t)r) {
                                    break;
                                }
                                written += r;
                                lastDataMs = millis();

                                int pct = (int)(((int64_t)written * 100) / totalLen);
                                if (pct != lastDrawnPct && (pct % 2 == 0 || pct == 100)) {
                                    lastDrawnPct = pct;
                                    char progStr[42];
                                    snprintf(progStr, sizeof(progStr), "%d / %d KB written",
                                             written / 1024, totalLen / 1024);
                                    drawRecoveryScreen("3/4 Flashing OTA Partition...", progStr, pct);
                                }
                            }
                        } else if (millis() - lastDataMs > 12000) {
                            break;
                        }
                        delay(1);
                    }

                    if (written == totalLen && Update.end(true)) {
                        otaSuccess = true;
                        drawRecoveryScreen("4/4 REFLASH COMPLETE!", "Booting fresh cydOS firmware...", 100, 0x07E0);
                    } else {
                        Update.abort();
                        drawRecoveryScreen("ERROR: Flash Verify Failed", "Kept existing OS untouched.\nTap screen to return.", 0, 0xF800);
                        hal::beep(200);
                        waitForTapOrTimeout(4000);
                        return;
                    }
                } else {
                    drawRecoveryScreen("ERROR: Partition Too Small", "Check partitions_cyd_ota.csv.\nTap screen to return.", 0, 0xF800);
                    hal::beep(200);
                    waitForTapOrTimeout(4000);
                    return;
                }
            } else {
                char errBuf[42];
                snprintf(errBuf, sizeof(errBuf), "HTTP %d (Check GitHub Releases)", code);
                drawRecoveryScreen("ERROR: Release Not Found", errBuf, 0, 0xF800);
                hal::beep(200);
                waitForTapOrTimeout(4000);
                return;
            }
            http.end();
        } else {
            drawRecoveryScreen("ERROR: HTTPS Init Failed", "Could not open GitHub TLS.\nTap screen to return.", 0, 0xF800);
            hal::beep(200);
            waitForTapOrTimeout(4000);
            return;
        }
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    if (otaSuccess) {
        hal::beep(80);
        delay(120);
        hal::beep(120);
        delay(1800);
        ESP.restart();
    }
}

static void drawRecoveryMenu(TFT_eSPI &tft, bool confirmReset) {
    tft.fillScreen(0x0821);

    // 1. Header Banner
    tft.fillRect(0, 0, 240, 36, 0x0352);
    tft.drawFastHLine(0, 36, 240, 0x07FF);
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, 0x0352);
    tft.drawString("cydOS RECOVERY MENU", 120, 18);

    // 2. Status Pill (Y = 42..82, H = 40)
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* nextPart = esp_ota_get_next_update_partition(nullptr);
    tft.fillRoundRect(10, 42, 220, 40, 5, 0x18E3);
    tft.drawRoundRect(10, 42, 220, 40, 5, 0x3186);
    tft.setTextFont(1);
    tft.setTextDatum(TL_DATUM);
    char partInfo[44];
    snprintf(partInfo, sizeof(partInfo), "Active: %s (0x%lX) -> %s",
             running ? running->label : "app0",
             running ? (unsigned long)running->address : 0x10000UL,
             nextPart ? nextPart->label : "app1");
    tft.setTextColor(0x07E0, 0x18E3);
    tft.drawString(partInfo, 18, 48);

    const char* ssid = Config::getInstance().getWifiSsid();
    char wifiInfo[44];
    if (ssid && ssid[0] != '\0') {
        snprintf(wifiInfo, sizeof(wifiInfo), "Wi-Fi: %.18s (Configured)", ssid);
        tft.setTextColor(0x07FF, 0x18E3);
    } else {
        strcpy(wifiInfo, "Wi-Fi: Not Configured");
        tft.setTextColor(0xFFE0, 0x18E3);
    }
    tft.drawString(wifiInfo, 18, 64);

    // 3. Button 1: Fix System (Reflash GitHub) (Y = 88..144, H = 56)
    tft.fillRoundRect(10, 88, 220, 56, 6, 0x18E3);
    tft.drawRoundRect(10, 88, 220, 56, 6, 0x07E0);

    tft.fillRoundRect(16, 96, 28, 40, 5, 0x04A0);
    tft.drawRoundRect(16, 96, 28, 40, 5, TFT_WHITE);
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, 0x04A0);
    tft.drawString("F", 30, 116);

    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, 0x18E3);
    tft.drawString("FIX SYSTEM (REFLASH)", 50, 94);
    tft.setTextFont(1);
    tft.setTextColor(0x07E0, 0x18E3);
    tft.drawString("Download & reflash cydOS via Wi-Fi", 50, 115);
    tft.setTextColor(0x9CD3, 0x18E3);
    tft.drawString("Recovers corrupted OS from GitHub", 50, 128);

    // 4. Button 2: Complete Factory Reset (Y = 150..206, H = 56)
    if (!confirmReset) {
        tft.fillRoundRect(10, 150, 220, 56, 6, 0x18E3);
        tft.drawRoundRect(10, 150, 220, 56, 6, 0xF800);

        tft.fillRoundRect(16, 158, 28, 40, 5, 0x9000);
        tft.drawRoundRect(16, 158, 28, 40, 5, TFT_WHITE);
        tft.setTextFont(2);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, 0x9000);
        tft.drawString("W", 30, 178);

        tft.setTextDatum(TL_DATUM);
        tft.setTextFont(2);
        tft.setTextColor(0xFFE0, 0x18E3);
        tft.drawString("COMPLETE FACTORY RESET", 50, 156);
        tft.setTextFont(1);
        tft.setTextColor(0xFFE0, 0x18E3);
        tft.drawString("Wipe PIN passcode & reset settings", 50, 177);
        tft.setTextColor(0x9CD3, 0x18E3);
        tft.drawString("Unlocks device if password forgotten", 50, 190);
    } else {
        tft.fillRoundRect(10, 150, 220, 56, 6, 0x9000);
        tft.drawRoundRect(10, 150, 220, 56, 6, TFT_WHITE);
        tft.setTextFont(2);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_WHITE, 0x9000);
        tft.drawString("TAP TO CONFIRM WIPE & UNLOCK", 120, 168);
        tft.setTextFont(1);
        tft.setTextColor(0xFFE0, 0x9000);
        tft.drawString("Clears PIN passcode! Tap elsewhere to cancel", 120, 190);
    }

    // 5. Button 3: Reboot System Normally (Y = 212..262, H = 50)
    tft.fillRoundRect(10, 212, 220, 50, 6, 0x10A2);
    tft.drawRoundRect(10, 212, 220, 50, 6, 0x07FF);

    tft.fillRoundRect(16, 218, 28, 38, 5, 0x2166);
    tft.drawRoundRect(16, 218, 28, 38, 5, TFT_WHITE);
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, 0x2166);
    tft.drawString("R", 30, 237);

    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, 0x10A2);
    tft.drawString("REBOOT SYSTEM", 50, 218);
    tft.setTextFont(1);
    tft.setTextColor(0x07FF, 0x10A2);
    tft.drawString("Exit Recovery & boot current cydOS", 50, 240);

    // 6. Footer
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(0x9CD3, 0x0821);
    tft.drawString("Tap any option above to select", 120, 276);
    tft.drawString("Hold [BOOT] on power-up for Recovery", 120, 292);
    tft.setTextColor(0x07FF, 0x0821);
    tft.drawString("cydOS Hardware Recovery Daemon", 120, 308);
    tft.setTextDatum(TL_DATUM);
}

void OtaRecovery::triggerRebootToOtaRecovery() {
    Preferences prefs;
    if (prefs.begin(OTA_NVS_NS, false)) {
        prefs.putBool(OTA_NVS_KEY, true);
        prefs.end();
    }

    TFT_eSPI &tft = hal::getDisplay();
    tft.fillScreen(0x0821);
    tft.fillRoundRect(16, 110, 208, 90, 8, 0x10A2);
    tft.drawRoundRect(16, 110, 208, 90, 8, 0x07FF);
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x07FF, 0x10A2);
    tft.drawString("REBOOTING TO RECOVERY", 120, 138);
    tft.setTextFont(1);
    tft.setTextColor(TFT_WHITE, 0x10A2);
    tft.drawString("Entering cydOS System Recovery Menu...", 120, 168);

    hal::beep(60);
    delay(600);
    ESP.restart();
}

void OtaRecovery::checkAndRunOnBoot() {
    Preferences prefs;
    bool pending = false;
    bool hwButtonTriggered = false;
    if (prefs.begin(OTA_NVS_NS, false)) {
        pending = prefs.getBool(OTA_NVS_KEY, false);
        if (pending) {
            prefs.putBool(OTA_NVS_KEY, false);
        }
        prefs.end();
    }

    // Hardware Recovery Trigger: check physical BOOT button (GPIO 0, active-LOW) during early startup
    if (!pending) {
        pinMode(0, INPUT_PULLUP);
        TFT_eSPI &tft = hal::getDisplay();
        tft.setTextFont(1);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(0x52AA, TFT_BLACK);
        tft.drawString("Hold [BOOT] button for Recovery", 120, 306);

        uint32_t windowStart = millis();
        while (millis() - windowStart < 1300) {
            if (digitalRead(0) == LOW) {
                hal::initBoard();
                hal::beep(45);
                tft.fillRoundRect(16, 268, 208, 44, 6, 0x10A2);
                tft.drawRoundRect(16, 268, 208, 44, 6, 0xFFE0);
                tft.setTextColor(0xFFE0, 0x10A2);
                tft.drawString("KEEP HOLDING [BOOT] FOR RECOVERY", 120, 280);
                tft.drawRect(28, 292, 184, 10, TFT_WHITE);

                uint32_t holdStart = millis();
                bool heldFull = true;
                while (millis() - holdStart < 1000) {
                    if (digitalRead(0) != LOW) {
                        heldFull = false;
                        break;
                    }
                    int w = (int)(((millis() - holdStart) * 180) / 1000);
                    if (w > 180) w = 180;
                    tft.fillRect(30, 294, w, 6, 0x07E0);
                    delay(20);
                }
                if (heldFull) {
                    pending = true;
                    hwButtonTriggered = true;
                    hal::beep(90);
                }
                break;
            }
            delay(20);
        }
        tft.setTextDatum(TL_DATUM);
        if (!pending) {
            tft.fillRect(0, 264, 240, 56, TFT_BLACK);
            return;
        }
    }

    // Initialize required HAL subsystems for interactive Recovery Menu
    hal::initBoard();
    StorageHAL::init();
    hal::initTouch();
    hal::setTouchRotation(0);
    hal::setBacklight(95);

    TFT_eSPI &tft = hal::getDisplay();

    bool confirmReset = false;
    drawRecoveryMenu(tft, confirmReset);

    // Interactive Recovery Menu Loop
    bool wasTouching = false;
    uint16_t lastX = 0, lastY = 0;

    while (true) {
        uint16_t tx = 0, ty = 0;
        bool touching = hal::getTouch(tx, ty);

        if (touching) {
            lastX = tx;
            lastY = ty;
            wasTouching = true;
        } else if (wasTouching) {
            wasTouching = false;

            // 1. Button 1: Fix System (Reflash GitHub) (Y = 88..144)
            if (lastY >= 88 && lastY <= 144 && lastX >= 10 && lastX <= 230) {
                hal::beep(35);
                confirmReset = false;
                runOtaReflash(tft);
                drawRecoveryMenu(tft, confirmReset);
            }
            // 2. Button 2: Complete Factory Reset (Y = 150..206)
            else if (lastY >= 150 && lastY <= 206 && lastX >= 10 && lastX <= 230) {
                if (!confirmReset) {
                    confirmReset = true;
                    hal::beep(45);
                    drawRecoveryMenu(tft, true);
                } else {
                    executeFactoryReset(tft);
                }
            }
            // 3. Button 3: Reboot System Normally (Y = 212..262)
            else if (lastY >= 212 && lastY <= 262 && lastX >= 10 && lastX <= 230) {
                rebootSystem(tft);
            }
            // 4. Tap outside when confirmReset is active: cancel reset confirmation
            else if (confirmReset) {
                confirmReset = false;
                drawRecoveryMenu(tft, false);
            }
        }

        delay(20);
    }
}

static void parseSemVer(const char* tag, int &maj, int &min, int &pat, int &rev) {
    maj = 0; min = 0; pat = 0; rev = 0;
    if (!tag) return;
    const char* p = tag;
    if (*p == 'v' || *p == 'V') p++;
    maj = atoi(p);
    const char* dot1 = strchr(p, '.');
    if (dot1) {
        min = atoi(dot1 + 1);
        const char* dot2 = strchr(dot1 + 1, '.');
        if (dot2) {
            pat = atoi(dot2 + 1);
            const char* dot3 = strchr(dot2 + 1, '.');
            if (dot3) {
                rev = atoi(dot3 + 1);
            }
        }
    }
}

bool OtaRecovery::checkForUpdate(char* outLatestTag, size_t maxLen) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(6000);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

    const char* headerKeys[] = { "Location" };
    http.collectHeaders(headerKeys, 1);

    const char* latestUrl = "https://github.com/VexisTheFox/android_cyd_os/releases/latest";
    if (!http.begin(client, latestUrl)) {
        return false;
    }

    int httpCode = http.GET();
    String locHeader;
    if (httpCode == 301 || httpCode == 302 || httpCode == 307 || httpCode == 308) {
        locHeader = http.header("Location");
    }
    http.end();

    if (locHeader.length() == 0) {
        return false;
    }

    int tagIdx = locHeader.lastIndexOf("/tag/");
    if (tagIdx < 0) {
        tagIdx = locHeader.lastIndexOf('/');
    } else {
        tagIdx += 5;
    }

    String tagStr = locHeader.substring(tagIdx);
    tagStr.trim();
    if (tagStr.length() == 0) {
        return false;
    }

    if (tagStr.startsWith("v2.0.") || tagStr.indexOf("-") >= 0) {
        return false;
    }

    int curMaj = 0, curMin = 0, curPat = 0, curRev = 0;
    int remMaj = 0, remMin = 0, remPat = 0, remRev = 0;
    parseSemVer(CYDOS_VERSION_TAG, curMaj, curMin, curPat, curRev);
    parseSemVer(tagStr.c_str(), remMaj, remMin, remPat, remRev);

    bool isNewer = false;
    if (remMaj > curMaj) isNewer = true;
    else if (remMaj == curMaj && remMin > curMin) isNewer = true;
    else if (remMaj == curMaj && remMin == curMin && remPat > curPat) isNewer = true;
    else if (remMaj == curMaj && remMin == curMin && remPat == curPat && remRev > curRev) isNewer = true;

    if (isNewer) {
        if (outLatestTag && maxLen > 0) {
            strncpy(outLatestTag, tagStr.c_str(), maxLen - 1);
            outLatestTag[maxLen - 1] = '\0';
        }
        return true;
    }

    return false;
}

} // namespace core
