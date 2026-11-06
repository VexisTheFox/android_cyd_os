/**
 * @file BoardHAL.cpp
 * @brief Implementation of Board Peripherals HAL (RGB LED, LDR, Speaker, BOOT Power Button)
 */

#include "BoardHAL.h"
#include "core/Config.h"
#include <esp_arduino_version.h>

namespace hal {

// Cached LED states (0 = OFF, 1 = ON)
static uint8_t s_ledR = 0;
static uint8_t s_ledG = 0;
static uint8_t s_ledB = 0;

// Power button (GPIO 0) debounce state
static bool     s_powerBtnDown       = false;
static bool     s_powerBtnFired      = false;
static uint32_t s_powerBtnChangeMs   = 0;
static uint32_t s_powerBtnCooldownMs = 0;

void initBoard() {
    // 1. Configure RGB LED GPIOs
    // Active-LOW logic: HIGH = OFF, LOW = ON.
    pinMode(CYD_LED_RED, OUTPUT);
    digitalWrite(CYD_LED_RED, HIGH);

    pinMode(CYD_LED_GREEN, OUTPUT);
    digitalWrite(CYD_LED_GREEN, HIGH);

    pinMode(CYD_LED_BLUE, OUTPUT);
    digitalWrite(CYD_LED_BLUE, HIGH);

    s_ledR = 0;
    s_ledG = 0;
    s_ledB = 0;

    // 2. Configure LDR Photoresistor ADC
    pinMode(CYD_LDR, INPUT);
#if defined(ADC_11db)
    analogSetPinAttenuation(CYD_LDR, ADC_11db);
#endif

    // 3. Configure Speaker Buzzer
    pinMode(CYD_SPEAKER, OUTPUT);
    digitalWrite(CYD_SPEAKER, LOW);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    // Dedicate LEDC channel 1 for tones to avoid conflict with Backlight (channel 0)
    setToneChannel(SPEAKER_LEDC_CHANNEL);
#endif

    // 4. Configure BOOT Button (GPIO 0) as Hardware Power Button (Active LOW)
    pinMode(CYD_BOOT_BTN, INPUT_PULLUP);
    s_powerBtnDown = false;
    s_powerBtnFired = false;
    s_powerBtnChangeMs = millis();
    s_powerBtnCooldownMs = millis();
}

void setRGBLED(uint8_t r, uint8_t g, uint8_t b) {
    s_ledR = (r != 0) ? 1 : 0;
    s_ledG = (g != 0) ? 1 : 0;
    s_ledB = (b != 0) ? 1 : 0;

    // Active-LOW: LOW turns LED on, HIGH turns LED off
    digitalWrite(CYD_LED_RED,   s_ledR ? LOW : HIGH);
    digitalWrite(CYD_LED_GREEN, s_ledG ? LOW : HIGH);
    digitalWrite(CYD_LED_BLUE,  s_ledB ? LOW : HIGH);
}

void setRGBHex(uint32_t hex) {
    uint8_t r = ((hex >> 16) & 0xFF) > 127 ? 1 : 0;
    uint8_t g = ((hex >> 8)  & 0xFF) > 127 ? 1 : 0;
    uint8_t b = (hex & 0xFF)         > 127 ? 1 : 0;
    setRGBLED(r, g, b);
}

void turnOffLED() {
    setRGBLED(0, 0, 0);
}

void setLEDRed(bool on) {
    s_ledR = on ? 1 : 0;
    digitalWrite(CYD_LED_RED, s_ledR ? LOW : HIGH);
}

void setLEDGreen(bool on) {
    s_ledG = on ? 1 : 0;
    digitalWrite(CYD_LED_GREEN, s_ledG ? LOW : HIGH);
}

void setLEDBlue(bool on) {
    s_ledB = on ? 1 : 0;
    digitalWrite(CYD_LED_BLUE, s_ledB ? LOW : HIGH);
}

uint16_t readLDR() {
    // 4-sample oversampling to suppress ESP32 SAR ADC noise
    uint32_t sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += analogRead(CYD_LDR);
    }
    return static_cast<uint16_t>(sum / 4);
}

uint8_t readLDRPercent() {
    uint16_t raw = readLDR();
    return static_cast<uint8_t>((uint32_t)raw * 100 / 4095);
}

void playTone(uint16_t freq, uint16_t durationMs) {
    if (freq == 0) {
        stopTone();
        return;
    }
    tone(CYD_SPEAKER, static_cast<unsigned int>(freq), static_cast<unsigned long>(durationMs));
}

void beep(uint16_t durationMs) {
    if (!Config::getInstance().isSoundEnabled()) {
        return;
    }
    playTone(BEEP_DEFAULT_FREQ, durationMs);
}

void stopTone() {
    noTone(CYD_SPEAKER);
}

bool isPowerButtonDown() {
    return (digitalRead(CYD_BOOT_BTN) == LOW);
}

bool pollPowerButton() {
    return (pollPowerButtonEvent() == PowerButtonEvent::SHORT_PRESS);
}

PowerButtonEvent pollPowerButtonEvent() {
    uint32_t now = millis();
    bool rawDown = (digitalRead(CYD_BOOT_BTN) == LOW);

    if (rawDown != s_powerBtnDown) {
        s_powerBtnDown = rawDown;
        if (rawDown) {
            s_powerBtnChangeMs = now;
            s_powerBtnFired = false;
        } else {
            uint32_t heldMs = now - s_powerBtnChangeMs;
            if (!s_powerBtnFired && heldMs >= 35 && (now - s_powerBtnCooldownMs >= 250)) {
                s_powerBtnCooldownMs = now;
                return PowerButtonEvent::SHORT_PRESS;
            }
            s_powerBtnFired = false;
        }
    } else if (rawDown && !s_powerBtnFired) {
        if (now - s_powerBtnChangeMs >= 600) {
            s_powerBtnFired = true;
            s_powerBtnCooldownMs = now;
            return PowerButtonEvent::LONG_PRESS;
        }
    }
    return PowerButtonEvent::NONE;
}
#if 0
    uint32_t now = millis();
    bool rawDown = (digitalRead(CYD_BOOT_BTN) == LOW);

    if (rawDown != s_powerBtnDown) {
        s_powerBtnDown = rawDown;
        s_powerBtnChangeMs = now;
        if (!rawDown) {
            s_powerBtnFired = false;
        }
    } else if (rawDown && !s_powerBtnFired) {
        // Button held low for >= 35ms debounce and outside 350ms cooldown
        if ((now - s_powerBtnChangeMs >= 35) && (now - s_powerBtnCooldownMs >= 350)) {
            s_powerBtnFired = true;
            s_powerBtnCooldownMs = now;
            return true;
        }
    }
    return false;
}

#endif
} // namespace hal
