/**
 * @file OtaRecovery.cpp
 * @brief Dedicated Boot-Time Recovery & GitHub Releases Firmware OTA Updater
 */

#include "OtaRecovery.h"
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
static const char* OTA_REL_URL  = "https://github.com/VexisTheFox/android_cyd_os/releases/latest/download/cyd2usb-firmware.bin";

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
    if (prefs.begin(OTA_NVS_NS, false)) {
        pending = prefs.getBool(OTA_NVS_KEY, false);
        if (pending) {
            // Clear immediately so any power loss or abort boots normally next time
            prefs.putBool(OTA_NVS_KEY, false);
        }
        prefs.end();
    }

    if (!pending) {
        return;
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
    tft.drawString("CYD OTA RECOVERY", 120, 18);

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

} // namespace core
