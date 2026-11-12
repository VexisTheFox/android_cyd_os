/**
 * @file TouchHAL.h
 * @brief Touch Hardware Abstraction Layer for ESP32 Cheap Yellow Display (CYD)
 *
 * Interfaces with the onboard XPT2046 resistive touchscreen digitizer over a
 * dedicated SPI bus (VSPI) separate from the ST7789 TFT display (HSPI).
 * Provides coordinate calibration, edge clamping, pressure thresholding,
 * temporal debouncing, and interaction state tracking (DOWN, MOVE, UP, NONE).
 *
 * Target Hardware: ESP32-2432S028 (CYD 2 USB-C Variant)
 * SPI Bus: VSPI (CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36)
 */

#ifndef TOUCH_HAL_H
#define TOUCH_HAL_H

#include <Arduino.h>

class TFT_eSPI;

namespace hal {

/**
 * @brief Touch interaction state tracking enumeration.
 */
enum class TouchState : uint8_t {
    NONE = 0,   ///< No physical touch active (idle)
    DOWN,       ///< First contact detected (single-shot event)
    MOVE,       ///< Sustained contact / finger motion
    UP          ///< Contact released (single-shot event; coordinates reflect lift position)
};

/**
 * @brief Structured touch event representation.
 */
struct TouchEvent {
    uint16_t x;             ///< Calibrated display X coordinate (0..319)
    uint16_t y;             ///< Calibrated display Y coordinate (0..239)
    int16_t rawX;           ///< Uncalibrated digitizer raw X value
    int16_t rawY;           ///< Uncalibrated digitizer raw Y value
    uint16_t pressure;      ///< Raw pressure Z value from digitizer
    TouchState state;       ///< Event state (NONE, DOWN, MOVE, UP)
    uint32_t timestampMs;   ///< Timestamp in milliseconds (from millis())
};

/**
 * @brief Touch calibration parameters for resistive digitizer mapping.
 */
struct TouchCalibration {
    uint16_t minRawX;   ///< Raw X reading at left edge of active screen (default ~200)
    uint16_t maxRawX;   ///< Raw X reading at right edge of active screen (default ~3700)
    uint16_t minRawY;   ///< Raw Y reading at top edge of active screen (default ~240)
    uint16_t maxRawY;   ///< Raw Y reading at bottom edge of active screen (default ~3800)
    bool invertX;       ///< Invert X axis (default false for CYD landscape rotation 1)
    bool invertY;       ///< Invert Y axis (default false for CYD landscape rotation 1)
};

// =========================================================================
// Core PROJECT.md Contract APIs (§ 1: HAL ↔ UI Shell & Applications)
// =========================================================================

/**
 * @brief Initialize the XPT2046 touch controller on dedicated VSPI bus.
 * Configures GPIO pins, initializes SPIClass(VSPI), configures rotation,
 * and sets up debouncing filters.
 */
void initTouch();

/**
 * @brief Poll the current touch state and retrieve calibrated screen coordinates.
 * Conforms to PROJECT.md Contract § 1 (HAL ↔ UI Shell & Applications).
 *
 * @param[out] x Calibrated screen X coordinate (0..319)
 * @param[out] y Calibrated screen Y coordinate (0..239)
 * @return true if the screen is currently being pressed down, false otherwise
 */
bool getTouch(uint16_t &x, uint16_t &y);

// =========================================================================
// Advanced Event & State Abstraction APIs
// =========================================================================

/**
 * @brief Update touch state machine and poll for the next touch event.
 * Handles contact debouncing, jitter filtering, and DOWN -> MOVE -> UP transitions.
 *
 * @param[out] event Populated touch event structure
 * @return true if an active touch or release event is present (DOWN, MOVE, UP),
 *         false if idle (NONE)
 */
bool pollTouch(TouchEvent &event);

/**
 * @brief Get the current high-level interaction state.
 *
 * @return TouchState (NONE, DOWN, MOVE, UP)
 */
TouchState getTouchState();

/**
 * @brief Read raw digitized coordinates directly from the XPT2046 controller.
 *
 * @param[out] rawX Raw ADC X reading (~200..3800)
 * @param[out] rawY Raw ADC Y reading (~200..3800)
 * @param[out] pressure Raw ADC pressure Z reading (0..4095)
 * @return true if digitizer detected physical pressure >= MIN_PRESSURE
 */
bool getRawTouch(int16_t &rawX, int16_t &rawY, uint16_t &pressure);

// =========================================================================
// Calibration & Configuration APIs
// =========================================================================

/**
 * @brief Set custom calibration parameters for resistive digitizer mapping.
 *
 * @param cal Calibration configuration structure
 */
void setTouchCalibration(const TouchCalibration &cal);

/**
 * @brief Get current touch calibration parameters.
 *
 * @return TouchCalibration current configuration
 */
TouchCalibration getTouchCalibration();

/**
 * @brief Set touch digitizer rotation (0..3). Default is 1 for CYD landscape.
 *
 * @param rotation Rotation index (0..3) matching display rotation
 */
void setTouchRotation(uint8_t rotation);

/**
 * @brief Convert raw digitizer coordinates to calibrated display pixels.
 * Clamps coordinates to screen dimensions [0..319] and [0..239].
 *
 * @param rawX Raw digitizer X value
 * @param rawY Raw digitizer Y value
 * @param[out] screenX Output screen X pixel
 * @param[out] screenY Output screen Y pixel
 */
void calibrateTouchPoint(int16_t rawX, int16_t rawY, uint16_t &screenX, uint16_t &screenY);

/**
 * @brief Run interactive 4-point resistive touchscreen calibration UI,
 *        apply the computed mapping, and persist it to NVS Config.
 *
 * @param tft Display instance
 * @return true if calibration succeeded and was saved, false if cancelled/invalid
 */
bool runInteractiveCalibration(TFT_eSPI &tft);

} // namespace hal

#endif // TOUCH_HAL_H
