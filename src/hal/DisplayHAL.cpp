/**
 * @file DisplayHAL.cpp
 * @brief Implementation of Display HAL for ST7789 & Backlight PWM (CYD)
 */

#include "DisplayHAL.h"
#include "core/Config.h"
#include "core/Logger.h"
#include <esp_arduino_version.h>

namespace hal {

// Static global display instance managed by HAL
static TFT_eSPI s_tft = TFT_eSPI();

// Backlight state variables
static uint8_t s_backlightPercent = DEFAULT_BACKLIGHT;
static uint8_t s_lastNonZeroBacklight = DEFAULT_BACKLIGHT;
static bool s_backlightInitialized = false;

void initDisplay() {
    // 1. Initialize ST7789 Display via HSPI
    s_tft.init();
    s_tft.setRotation(DEFAULT_ROTATION);
    s_tft.fillScreen(TFT_BLACK);

    // 2. Configure Hardware Backlight PWM on GPIO 21
    // Supports both ESP32 Arduino Core 3.x and Core 2.x
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    ledcAttachChannel(TFT_BL, PWM_FREQUENCY, PWM_RESOLUTION_BITS, PWM_CHANNEL);
#else
    ledcSetup(PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION_BITS);
    ledcAttachPin(TFT_BL, PWM_CHANNEL);
#endif
    s_backlightInitialized = true;

    // 3. Set default backlight level
    setBacklight(s_backlightPercent);
    LOG_INFO("DISPLAY", "ST7789 initialized (240x320 rot=%u, BL=%u%%)", DEFAULT_ROTATION, s_backlightPercent);
}

TFT_eSPI& getDisplay() {
    return s_tft;
}

void setDisplayRotation(uint8_t rotation) {
    s_tft.setRotation(rotation);
    LOG_INFO("DISPLAY", "Display rotation set to %u (%dx%d)", rotation, s_tft.width(), s_tft.height());
}

void setBacklight(uint8_t percent) {
    if (percent > 100) percent = 100;
    s_backlightPercent = percent;
    if (percent > 0) {
        s_lastNonZeroBacklight = percent;
    }

    if (!s_backlightInitialized) {
        // Fallback for pre-init calls
        pinMode(TFT_BL, OUTPUT);
        digitalWrite(TFT_BL, (percent > 0) ? HIGH : LOW);
        return;
    }

    // Map 0..100% to 0..255 duty cycle (with rounding)
    uint32_t duty = ((uint32_t)percent * 255 + 50) / 100;

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    ledcWrite(TFT_BL, duty);
#else
    ledcWrite(PWM_CHANNEL, duty);
#endif
}

uint8_t getBacklight() {
    return s_backlightPercent;
}

void turnBacklightOn() {
    uint8_t cfgBright = Config::getInstance().getBrightness();
    uint8_t target = (s_lastNonZeroBacklight >= 10) ? s_lastNonZeroBacklight : ((cfgBright >= 10) ? cfgBright : DEFAULT_BACKLIGHT);
    setBacklight(target);
}

void turnBacklightOff() {
    setBacklight(0);
}

void sleepDisplayPanel() {
    turnBacklightOff();
    s_tft.writecommand(0x28); // ST7789 DISPOFF (blanks LCD output while preserving GRAM)
}

void wakeDisplayPanel() {
    s_tft.resetViewport();
    s_tft.writecommand(0x11); // ST7789 SLPOUT
    s_tft.writecommand(0x29); // ST7789 DISPON (re-enables LCD output from GRAM)
    turnBacklightOn();
}

bool isBacklightOn() {
    return (s_backlightPercent > 0);
}

void setViewport(int32_t x, int32_t y, int32_t w, int32_t h) {
    s_tft.setViewport(x, y, w, h);
}

void resetViewport() {
    s_tft.resetViewport();
}

void clearScreen(uint16_t color) {
    s_tft.resetViewport();
    s_tft.fillScreen(color);
}

void clearViewport(uint16_t color) {
    s_tft.fillRect(s_tft.getViewportX(), s_tft.getViewportY(),
                   s_tft.getViewportWidth(), s_tft.getViewportHeight(), color);
}

} // namespace hal
