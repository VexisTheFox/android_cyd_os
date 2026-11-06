/**
 * @file BoardHAL.h
 * @brief Hardware Abstraction Layer for Onboard Peripherals (CYD / ESP32-2432S028)
 * 
 * Hardware Peripherals:
 *   - Onboard RGB LED: GPIO 4 (Red), GPIO 16 (Green), GPIO 17 (Blue)
 *     * Active-Low Circuit: LOW = ON, HIGH = OFF
 *   - Onboard LDR Photoresistor: GPIO 34 (ADC1 Channel 6, 12-bit 0..4095)
 *   - Onboard Piezo Speaker / Buzzer: GPIO 26 (LEDC Channel 1 / Arduino tone)
 *   - Onboard BOOT / Power Button: GPIO 0 (Active-Low with internal pull-up)
 */

#pragma once

#include <Arduino.h>

namespace hal {

// Hardware GPIO Pin Definitions
#ifndef CYD_LED_RED
#define CYD_LED_RED   4
#endif

#ifndef CYD_LED_GREEN
#define CYD_LED_GREEN 16
#endif

#ifndef CYD_LED_BLUE
#define CYD_LED_BLUE  17
#endif

#ifndef CYD_LDR
#define CYD_LDR       34
#endif

#ifndef CYD_SPEAKER
#define CYD_SPEAKER   26
#endif

#ifndef CYD_BOOT_BTN
#define CYD_BOOT_BTN  0
#endif

// Speaker Tone Constants
constexpr uint8_t  SPEAKER_LEDC_CHANNEL   = 1;    // Channel 1 (Channel 0 reserved for Backlight)
constexpr uint16_t BEEP_DEFAULT_FREQ      = 2400; // 2.4 kHz audible notification tone
constexpr uint16_t BEEP_DEFAULT_DURATION  = 80;   // 80 ms default click/beep duration

/**
 * @brief Initialize onboard peripherals (RGB LED, LDR ADC, Buzzer pin, BOOT Power button).
 */
void initBoard();

/**
 * @brief Set onboard RGB LED channel states.
 */
void setRGBLED(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set RGB LED using standard 24-bit RGB hex (e.g., 0xFF0000 = Red).
 */
void setRGBHex(uint32_t hex);

/**
 * @brief Turn off all 3 channels of the onboard RGB LED.
 */
void turnOffLED();

/**
 * @brief Set individual Red LED state.
 */
void setLEDRed(bool on);

/**
 * @brief Set individual Green LED state.
 */
void setLEDGreen(bool on);

/**
 * @brief Set individual Blue LED state.
 */
void setLEDBlue(bool on);

/**
 * @brief Read ambient light from the onboard LDR photoresistor.
 */
uint16_t readLDR();

/**
 * @brief Read ambient light as normalized percentage (0..100%).
 */
uint8_t readLDRPercent();

/**
 * @brief Play an audible tone on the onboard piezo speaker.
 */
void playTone(uint16_t freq, uint16_t durationMs = 0);

/**
 * @brief Play a short notification beep (e.g. for touch feedback or alerts).
 */
void beep(uint16_t durationMs = BEEP_DEFAULT_DURATION);

/**
 * @brief Silence any active audio output on the speaker.
 */
void stopTone();

/**
 * @brief Poll the physical BOOT button (GPIO 0) as the System Power / Sleep / Lock button.
 * @return true once per debounced button press event.
 */
bool pollPowerButton();

/**
 * @brief Check if the physical BOOT / Power button (GPIO 0) is currently held down.
 */
bool isPowerButtonDown();

} // namespace hal
