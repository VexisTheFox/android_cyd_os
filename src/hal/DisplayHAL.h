/**
 * @file DisplayHAL.h
 * @brief Hardware Abstraction Layer for ST7789 Display & Backlight PWM (CYD / ESP32-2432S028)
 * 
 * Target Hardware:
 *   Display Controller: ST7789 (HSPI @ 55 MHz)
 *   Bus Pins: MOSI: 13, MISO: 12, SCLK: 14, CS: 15, DC: 2, RST: -1
 *   Backlight Pin: GPIO 21 (Hardware PWM via ESP32 LEDC driver)
 *   Screen Resolution: 320x240 landscape (Rotation 1 or 3)
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

namespace hal {

// Display Geometry Constants
constexpr int32_t DISPLAY_WIDTH         = 240;
constexpr int32_t DISPLAY_HEIGHT        = 320;
constexpr uint8_t DEFAULT_ROTATION      = 0;   // Portrait 240x320

// CYD Android OS Shell Viewport Dimensions
constexpr int32_t STATUS_BAR_HEIGHT     = 20;  // Top status bar (y: 0..19)
constexpr int32_t VIEWPORT_Y            = 20;  // Main viewport top
constexpr int32_t VIEWPORT_HEIGHT       = 274; // Main viewport height (y: 20..293)
constexpr int32_t NAV_BAR_Y             = 294; // Bottom navigation bar top
constexpr int32_t NAV_BAR_HEIGHT        = 26;  // Bottom nav bar height (y: 294..319)

// Backlight Constants
#ifndef TFT_BL
#define TFT_BL 21
#endif
constexpr uint8_t  DEFAULT_BACKLIGHT     = 80;   // 80% default brightness
constexpr uint32_t PWM_FREQUENCY        = 5000; // 5 kHz PWM frequency (inaudible, flicker-free)
constexpr uint8_t  PWM_RESOLUTION_BITS   = 8;    // 8-bit resolution (0..255)
constexpr uint8_t  PWM_CHANNEL           = 7;    // LEDC Channel 7 (Timer 3) isolated from Speaker Tone

/**
 * @brief Initialize ST7789 display controller and hardware PWM backlight.
 * 
 * Performs:
 * 1. tft.init() ST7789 initialization on HSPI @ 55MHz.
 * 2. Sets rotation to 1 (landscape 320x240).
 * 3. Fills screen with TFT_BLACK to prevent boot noise.
 * 4. Configures ESP32 LEDC PWM timer on GPIO 21 (5 kHz, 8-bit).
 * 5. Applies initial backlight brightness (80%).
 */
void initDisplay();

/**
 * @brief Get reference to the global TFT_eSPI instance.
 * @return TFT_eSPI& reference
 */
TFT_eSPI& getDisplay();

/**
 * @brief Set display rotation (0: Portrait, 1: Landscape, 2: Inverted Portrait, 3: Inverted Landscape).
 * @param rotation Rotation index (0..3)
 */
void setDisplayRotation(uint8_t rotation);

/**
 * @brief Set hardware backlight brightness percentage via PWM.
 * @param percent Value between 0 (fully dark) and 100 (maximum brightness).
 */
void setBacklight(uint8_t percent);

/**
 * @brief Get current backlight brightness percentage.
 * @return uint8_t Current brightness (0..100%).
 */
uint8_t getBacklight();

/**
 * @brief Turn backlight on to previous non-zero level (or 100% if none).
 */
void turnBacklightOn();

/**
 * @brief Turn backlight off (0% duty) without losing saved brightness setting.
 */
void turnBacklightOff();

/**
 * @brief Put ST7789 panel into hardware display-off sleep without wiping GRAM.
 */
void sleepDisplayPanel();

/**
 * @brief Wake ST7789 panel from sleep and restore backlight PWM.
 */
void wakeDisplayPanel();

/**
 * @brief Check if backlight is currently turned on.
 * @return true if brightness > 0
 */
bool isBacklightOn();

/**
 * @brief Set a hardware/software clipping viewport for higher-level drawing.
 * @param x Left coordinate
 * @param y Top coordinate
 * @param w Viewport width
 * @param h Viewport height
 */
void setViewport(int32_t x, int32_t y, int32_t w, int32_t h);

/**
 * @brief Reset viewport clipping to full display screen (0, 0, 320, 240).
 */
void resetViewport();

/**
 * @brief Clear the entire display screen with a solid color.
 * @param color 16-bit RGB565 color (defaults to TFT_BLACK).
 */
void clearScreen(uint16_t color = TFT_BLACK);

/**
 * @brief Clear the currently active viewport area with a solid color.
 * @param color 16-bit RGB565 color (defaults to TFT_BLACK).
 */
void clearViewport(uint16_t color = TFT_BLACK);

} // namespace hal
