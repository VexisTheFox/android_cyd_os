/**
 * @file OtaRecovery.cpp
 * @brief Dedicated Boot-Time Recovery & GitHub Releases Firmware OTA Updater
 */

#include "OtaRecovery.h"
#include "Version.h"
#include "Config.h"
#include "../hal/DisplayHAL.h"
#include "../hal/BoardHAL.h"
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
    tft.drawString("Entering Minimal-RAM OTA Updater...", 120, 168);

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
            // Clear immediately so any power loss or abort boots normally next time
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
        tft.drawString("Hold [BOOT] button for OTA Recovery", 120, 306);

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

    hal::initBoard();
    hal::setBacklight(95);

    TFT_eSPI &tft = hal::getDisplay();
    tft.fillScreen(0x0821);

    // Header Banner
    tft.fillRect(0, 0, 240, 36, 0x0352);
    tft.drawFastHLine(0, 36, 240, 0x07FF);
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, 0x0352);
    tft.drawString(hwButtonTriggered ? "HW BUTTON RECOVERY" : "CYD OTA RECOVERY", 120, 18);

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
        drawRecoveryScreen("ERROR: No Saved Wi-Fi", "Configure Wi-Fi in Settings first!", 0, 0xF800);
        hal::beep(200);
        delay(3500);
        ESP.restart();
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
        drawRecoveryScreen("ERROR: Wi-Fi Timeout", "Could not connect to network", 0, 0xF800);
        hal::beep(200);
        delay(3500);
        ESP.restart();
        return;
    }

    // 2. Connect to GitHub Releases and follow 302 redirect
    drawRecoveryScreen("2/4 Querying GitHub Releases...", "cyd2usb-firmware.bin", 12);

    bool otaSuccess = false;
    {
        // Declare WiFiClientSecure BEFORE HTTPClient so HTTPClient destructor runs first!
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
                        drawRecoveryScreen("4/4 OTA UPDATE COMPLETE!", "Booting new CYD OS firmware...", 100, 0x07E0);
                    } else {
                        Update.abort();
                        drawRecoveryScreen("ERROR: Flash Verify Failed", "Kept existing OS untouched", 0, 0xF800);
                    }
                } else {
                    drawRecoveryScreen("ERROR: Partition Too Small", "Check partitions_cyd_ota.csv", 0, 0xF800);
                }
            } else {
                char errBuf[42];
                snprintf(errBuf, sizeof(errBuf), "HTTP %d (Check GitHub Releases)", code);
                drawRecoveryScreen("ERROR: Release Not Found", errBuf, 0, 0xF800);
            }
            http.end();
        } else {
            drawRecoveryScreen("ERROR: HTTPS Init Failed", "Could not open GitHub TLS", 0, 0xF800);
        }
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    if (otaSuccess) {
        hal::beep(80);
        delay(120);
        hal::beep(120);
        delay(1800);
    } else {
        hal::beep(250);
        delay(3500);
    }
    ESP.restart();
}

static void parseSemVer(const char* tag, int &maj, int &min, int &pat) {
    maj = 0; min = 0; pat = 0;
    if (!tag) return;
    const char* p = tag;
    if (*p == 'v' || *p == 'V') p++;
    maj = atoi(p);
    const char* d1 = strchr(p, '.');
    if (d1) {
        min = atoi(d1 + 1);
        const char* d2 = strchr(d1 + 1, '.');
        if (d2) {
            pat = atoi(d2 + 1);
        }
    }
}

bool OtaRecovery::checkForUpdate(char* outLatestTag, size_t maxLen) {
    if (outLatestTag && maxLen > 0) {
        outLatestTag[0] = '\0';
    }
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    char foundTag[32] = {0};
    {
        // Declare WiFiClientSecure BEFORE HTTPClient so HTTPClient destructor runs first!
        WiFiClientSecure client;
        client.setInsecure();

        HTTPClient http;
        http.setConnectTimeout(4500);
        http.setTimeout(5000);
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        const char* hdrKeys[] = { "Location" };
        http.collectHeaders(hdrKeys, 1);

        if (http.begin(client, "https://github.com/" CYDOS_GITHUB_REPO "/releases/latest")) {
            int code = http.GET();
            if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
                String loc = http.getLocation();
                if (loc.length() == 0) {
                    loc = http.header("Location");
                }
                const char* marker = "/releases/tag/";
                const char* pos = strstr(loc.c_str(), marker);
                if (pos) {
                    pos += strlen(marker);
                    size_t i = 0;
                    while (pos[i] != '\0' && pos[i] != '?' && pos[i] != '#' && pos[i] != '/' && i + 1 < sizeof(foundTag)) {
                        foundTag[i] = pos[i];
                        i++;
                    }
                    foundTag[i] = '\0';
                }
            }
            http.end();
        }
    }

    if (foundTag[0] == '\0') {
        return false;
    }

    // Ignore legacy pre-stable engineering tags (e.g. v2.0.11-d34f8c4)
    if (strncasecmp(foundTag, "v2.0.", 5) == 0 && strchr(foundTag, '-') != nullptr) {
        return false;
    }

    if (strcasecmp(foundTag, CYDOS_VERSION_TAG) == 0) {
        return false;
    }

    int curMaj = 0, curMin = 0, curPat = 0;
    int remMaj = 0, remMin = 0, remPat = 0;
    parseSemVer(CYDOS_VERSION_TAG, curMaj, curMin, curPat);
    parseSemVer(foundTag, remMaj, remMin, remPat);

    bool newer = false;
    if (remMaj != curMaj)      newer = (remMaj > curMaj);
    else if (remMin != curMin) newer = (remMin > curMin);
    else if (remPat != curPat) newer = (remPat > curPat);
    else                       newer = true; // Different suffix on same base version

    if (newer && outLatestTag && maxLen > 0) {
        strncpy(outLatestTag, foundTag, maxLen - 1);
        outLatestTag[maxLen - 1] = '\0';
    }
    return newer;
}

} // namespace core
