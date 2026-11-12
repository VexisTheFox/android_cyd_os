/**
 * @file TouchHAL.cpp
 * @brief Implementation of Touch Hardware Abstraction Layer for ESP32 CYD
 *
 * Implements dedicated bit-banged Software SPI for XPT2046 resistive digitizer,
 * linear calibration with boundary clamping, pressure filtering, temporal
 * release debouncing, coordinate jitter suppression, and high-level interaction
 * state tracking (DOWN, MOVE, UP, NONE).
 *
 * Running on Software SPI completely frees the hardware VSPI bus (pins 18, 19, 23, CS 5)
 * for the MicroSD card slot with ZERO pin contention, zero timeouts, and zero crashes!
 *
 * Target Hardware: ESP32-2432S028 (CYD 2 USB-C Variant)
 */

#include "TouchHAL.h"
#include "BoardHAL.h"
#include "core/Config.h"
#include "core/Logger.h"
#include <TFT_eSPI.h>

namespace hal {

// Default CYD pin definitions from platformio.ini
#ifndef XPT2046_CLK
#define XPT2046_CLK  25
#endif
#ifndef XPT2046_MOSI
#define XPT2046_MOSI 32
#endif
#ifndef XPT2046_MISO
#define XPT2046_MISO 39
#endif
#ifndef XPT2046_CS
#define XPT2046_CS   33
#endif
#ifndef XPT2046_IRQ
#define XPT2046_IRQ  36
#endif

// Display Dimensions in Portrait Orientation
static constexpr uint16_t SCREEN_WIDTH  = 240;
static constexpr uint16_t SCREEN_HEIGHT = 320;

// Digitizer Pressure & Boundary Constraints
static constexpr uint16_t MIN_PRESSURE      = 480;  // Minimum Z pressure for deliberate touch (rejects light/static misstouches)
static constexpr uint16_t MAX_PRESSURE      = 3900; // Reject shorted/rail pressure spikes
static constexpr int16_t  RAW_MIN_BOUND     = 80;   // Reject transient readings below 80
static constexpr int16_t  RAW_MAX_BOUND     = 4020; // Reject transient readings above 4020
static constexpr int16_t  MAX_SAMPLE_SPREAD = 240;  // Reject noisy press/release frames where 3 samples disagree
static constexpr int16_t  MAX_JUMP_PX       = 75;   // Max pixel travel in single frame (rejects multi-point glitches)
static constexpr int16_t  MAX_CONFIRM_DIST  = 28;   // Max px drift between 2 press-confirmation samples

// Debouncing & Jitter Timing Constants
static constexpr uint32_t POLL_INTERVAL_MS    = 8;  // State machine tick rate (125 Hz)
static constexpr uint32_t RELEASE_DEBOUNCE_MS = 60; // Stabilization window before declaring UP (prevents contact-bounce double clicks)
static constexpr uint32_t REPRESS_COOLDOWN_MS = 90; // Minimum interval after UP before a new DOWN can fire
static constexpr int16_t  MOVE_HYSTERESIS_PX  = 3;  // Pixel threshold for movement reporting

// Active Calibration Configuration (Factory defaults for CYD portrait)
static TouchCalibration s_calibration = {
    .minRawX = 240,
    .maxRawX = 3800,
    .minRawY = 200,
    .maxRawY = 3700,
    .invertX = false,
    .invertY = false
};

static uint8_t s_rotation = 0; // Default portrait rotation matching ST7789 display

// Internal Interaction State Variables
static TouchState s_currentState           = TouchState::NONE;
static uint16_t   s_lastX                  = 0;
static uint16_t   s_lastY                  = 0;
static uint16_t   s_downX                  = 0;
static uint16_t   s_downY                  = 0;
static int16_t    s_lastRawX               = 0;
static int16_t    s_lastRawY               = 0;
static uint16_t   s_lastPressure           = 0;
static uint32_t   s_lastValidTouchMs       = 0;
static uint32_t   s_lastPollMs             = 0;
static uint32_t   s_lastUpMs               = 0;
static bool       s_isPhysicallyDown       = false;
static uint8_t    s_pendingDownCount       = 0;
static uint16_t   s_pendingX               = 0;
static uint16_t   s_pendingY               = 0;

// --------------------------------------------------------------------------
// Dedicated Software SPI Driver for XPT2046 (500 kHz - fully within 2MHz spec)
// --------------------------------------------------------------------------
static inline uint8_t swSpiTransfer8(uint8_t out) {
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
        digitalWrite(XPT2046_MOSI, (out & (1 << i)) ? HIGH : LOW);
        delayMicroseconds(1);
        digitalWrite(XPT2046_CLK, HIGH);
        delayMicroseconds(1);
        if (digitalRead(XPT2046_MISO)) {
            in |= (1 << i);
        }
        digitalWrite(XPT2046_CLK, LOW);
        delayMicroseconds(1);
    }
    return in;
}

static inline uint16_t swSpiTransfer16(uint8_t nextCmd) {
    // Matches hardware transfer16(cmd): high byte 0x00 first, command in low byte.
    // The previous conversion result is clocked out while the next command is sent.
    uint8_t b1 = swSpiTransfer8(0x00);
    uint8_t b2 = swSpiTransfer8(nextCmd);
    return ((uint16_t)b1 << 8) | b2;
}

static int16_t besttwoavg(int16_t x, int16_t y, int16_t z) {
    int16_t da = abs(x - y);
    int16_t db = abs(x - z);
    int16_t dc = abs(y - z);
    if (da <= db && da <= dc) return (x + y) >> 1;
    if (db <= da && db <= dc) return (x + z) >> 1;
    return (y + z) >> 1;
}

static bool swReadRawPoint(int16_t &outX, int16_t &outY, int16_t &outZ) {
    digitalWrite(XPT2046_CS, LOW);
    delayMicroseconds(2);

    // Read Z1 pressure
    swSpiTransfer8(0xB1);
    int16_t z1 = (int16_t)(swSpiTransfer16(0xC1) >> 3);
    int16_t z = z1 + 4095;
    // Read Z2 pressure
    int16_t z2 = (int16_t)(swSpiTransfer16(0x91) >> 3);
    z -= z2;

    if (z < (int16_t)MIN_PRESSURE || z > (int16_t)MAX_PRESSURE) {
        // Below touch pressure threshold or rail spike: power down ADC
        swSpiTransfer8(0xD0);
        swSpiTransfer8(0x00);
        digitalWrite(XPT2046_CS, HIGH);
        return false;
    }

    // Dummy X measurement
    swSpiTransfer16(0x91);

    // Acquire 3 pairs of X and Y measurements
    int16_t data[6];
    data[0] = (int16_t)(swSpiTransfer16(0xD1) >> 3);
    data[1] = (int16_t)(swSpiTransfer16(0x91) >> 3);
    data[2] = (int16_t)(swSpiTransfer16(0xD1) >> 3);
    data[3] = (int16_t)(swSpiTransfer16(0x91) >> 3);
    data[4] = (int16_t)(swSpiTransfer16(0xD0) >> 3); // Last Y touch power down
    data[5] = (int16_t)(swSpiTransfer16(0x00) >> 3);

    digitalWrite(XPT2046_CS, HIGH);

    // Reject unstable press/release transition frames where samples wildly disagree
    int16_t minX = min(data[0], min(data[2], data[4]));
    int16_t maxX = max(data[0], max(data[2], data[4]));
    int16_t minY = min(data[1], min(data[3], data[5]));
    int16_t maxY = max(data[1], max(data[3], data[5]));
    if ((maxX - minX) > MAX_SAMPLE_SPREAD || (maxY - minY) > MAX_SAMPLE_SPREAD) {
        return false;
    }

    int16_t x = besttwoavg(data[0], data[2], data[4]);
    int16_t y = besttwoavg(data[1], data[3], data[5]);

    outZ = z;
    switch (s_rotation) {
        case 0: outX = 4095 - y; outY = x;        break;
        case 1: outX = x;        outY = y;        break;
        case 2: outX = y;        outY = 4095 - x; break;
        default: outX = 4095 - x; outY = 4095 - y; break;
    }
    return true;
}

/**
 * @brief Internal state machine update function.
 * Evaluates hardware digitizer readings and drives transition between
 * NONE -> DOWN -> MOVE -> UP.
 */
static void updateStateMachine() {
    uint32_t now = millis();
    if (now - s_lastPollMs < POLL_INTERVAL_MS) {
        return;
    }
    s_lastPollMs = now;

    // Single-shot state cleanup:
    if (s_currentState == TouchState::UP) {
        s_currentState = TouchState::NONE;
    }

    // Read hardware digitizer
    bool validTouch = false;
    int16_t rx = 0, ry = 0, rz = 0;
    if (swReadRawPoint(rx, ry, rz)) {
        if (rz >= (int16_t)MIN_PRESSURE &&
            rx >= RAW_MIN_BOUND && rx <= RAW_MAX_BOUND &&
            ry >= RAW_MIN_BOUND && ry <= RAW_MAX_BOUND) {
            validTouch = true;
        }
    }

    if (validTouch) {
        uint16_t screenX = 0, screenY = 0;
        calibrateTouchPoint(rx, ry, screenX, screenY);

        if (!s_isPhysicallyDown) {
            if (now - s_lastUpMs < REPRESS_COOLDOWN_MS) {
                s_pendingDownCount = 0;
                return; // Reject contact bounce right after finger release
            }

            // Misstouch prevention: require 2 consistent samples before firing DOWN
            if (s_pendingDownCount == 0) {
                s_pendingDownCount = 1;
                s_pendingX = screenX;
                s_pendingY = screenY;
                return;
            }
            if (abs((int16_t)screenX - (int16_t)s_pendingX) > MAX_CONFIRM_DIST ||
                abs((int16_t)screenY - (int16_t)s_pendingY) > MAX_CONFIRM_DIST) {
                s_pendingX = screenX;
                s_pendingY = screenY;
                return;
            }

            screenX = (uint16_t)(((uint32_t)s_pendingX + (uint32_t)screenX) >> 1);
            screenY = (uint16_t)(((uint32_t)s_pendingY + (uint32_t)screenY) >> 1);
            s_pendingDownCount = 0;

            s_lastValidTouchMs = now;
            s_lastRawX = rx;
            s_lastRawY = ry;
            s_lastPressure = (uint16_t)rz;

            s_isPhysicallyDown = true;
            s_currentState = TouchState::DOWN;
            s_downX = screenX;
            s_downY = screenY;
            s_lastX = screenX;
            s_lastY = screenY;
        } else {
            int16_t jumpX = abs((int16_t)screenX - (int16_t)s_lastX);
            int16_t jumpY = abs((int16_t)screenY - (int16_t)s_lastY);
            if (jumpX > MAX_JUMP_PX || jumpY > MAX_JUMP_PX) {
                return; // Suppress coordinate teleportation glitch
            }

            s_lastValidTouchMs = now;
            s_lastRawX = rx;
            s_lastRawY = ry;
            s_lastPressure = (uint16_t)rz;

            if (jumpX >= MOVE_HYSTERESIS_PX || jumpY >= MOVE_HYSTERESIS_PX) {
                s_currentState = TouchState::MOVE;
                s_lastX = (uint16_t)(((uint32_t)s_lastX * 2 + (uint32_t)screenX) / 3);
                s_lastY = (uint16_t)(((uint32_t)s_lastY * 2 + (uint32_t)screenY) / 3);
            } else {
                if (s_currentState == TouchState::DOWN) {
                    s_currentState = TouchState::MOVE;
                }
            }
        }
    } else {
        s_pendingDownCount = 0;
        if (s_isPhysicallyDown) {
            if (now - s_lastValidTouchMs >= RELEASE_DEBOUNCE_MS) {
                s_isPhysicallyDown = false;
                s_currentState = TouchState::UP;
                s_lastUpMs = now;
            }
        } else {
            s_currentState = TouchState::NONE;
        }
    }
}

void initTouch() {
    // 1. Configure Touch digitizer GPIO pins for Software SPI
    pinMode(XPT2046_IRQ, INPUT);
    pinMode(XPT2046_MISO, INPUT);
    pinMode(XPT2046_MOSI, OUTPUT);
    pinMode(XPT2046_CLK, OUTPUT);
    pinMode(XPT2046_CS, OUTPUT);

    digitalWrite(XPT2046_CS, HIGH);
    digitalWrite(XPT2046_CLK, LOW);
    digitalWrite(XPT2046_MOSI, LOW);

    // 2. Ensure SD CS pin 5 is initially deselected (HIGH)
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);

    // 3. Reset internal state machine
    s_currentState       = TouchState::NONE;
    s_isPhysicallyDown   = false;
    s_pendingDownCount   = 0;
    s_lastX              = 0;
    s_lastY              = 0;
    s_downX              = 0;
    s_downY              = 0;
    s_lastRawX           = 0;
    s_lastRawY           = 0;
    s_lastPressure       = 0;
    s_lastValidTouchMs   = 0;
    s_lastPollMs         = 0;
    LOG_INFO("TOUCH", "XPT2046 Software SPI initialized (CLK=25 MOSI=32 MISO=39 CS=33)");
}

bool getTouch(uint16_t &x, uint16_t &y) {
    updateStateMachine();
    if (s_isPhysicallyDown) {
        x = s_lastX;
        y = s_lastY;
        return true;
    }
    return false;
}

bool pollTouch(TouchEvent &event) {
    updateStateMachine();
    event.state       = s_currentState;
    event.x           = s_lastX;
    event.y           = s_lastY;
    event.rawX        = s_lastRawX;
    event.rawY        = s_lastRawY;
    event.pressure    = s_lastPressure;
    event.timestampMs = s_lastValidTouchMs;

    return (s_currentState != TouchState::NONE);
}

TouchState getTouchState() {
    updateStateMachine();
    return s_currentState;
}

bool getRawTouch(int16_t &rawX, int16_t &rawY, uint16_t &pressure) {
    int16_t rx = 0, ry = 0, rz = 0;
    if (swReadRawPoint(rx, ry, rz)) {
        rawX = rx;
        rawY = ry;
        pressure = (uint16_t)rz;
        return (pressure >= MIN_PRESSURE);
    }
    rawX = 0;
    rawY = 0;
    pressure = 0;
    return false;
}

void setTouchCalibration(const TouchCalibration &cal) {
    s_calibration = cal;
}

TouchCalibration getTouchCalibration() {
    return s_calibration;
}

void setTouchRotation(uint8_t rotation) {
    s_rotation = rotation % 4;
    if (s_rotation == 1 || s_rotation == 3) {
        // Landscape calibration (320x240)
        s_calibration.minRawX = 200;
        s_calibration.maxRawX = 3700;
        s_calibration.minRawY = 240;
        s_calibration.maxRawY = 3800;
    } else {
        // Portrait calibration (240x320)
        s_calibration.minRawX = 240;
        s_calibration.maxRawX = 3800;
        s_calibration.minRawY = 200;
        s_calibration.maxRawY = 3700;
    }
    s_isPhysicallyDown = false;
    s_pendingDownCount = 0;
    s_currentState = TouchState::NONE;
    LOG_INFO("TOUCH", "Touch rotation switched to %u (%s)", s_rotation,
             (s_rotation == 1 || s_rotation == 3) ? "Landscape" : "Portrait");
}

void calibrateTouchPoint(int16_t rawX, int16_t rawY, uint16_t &screenX, uint16_t &screenY) {
    const int32_t width  = (s_rotation == 1 || s_rotation == 3) ? 320 : SCREEN_WIDTH;
    const int32_t height = (s_rotation == 1 || s_rotation == 3) ? 240 : SCREEN_HEIGHT;

    int32_t xRange = (int32_t)s_calibration.maxRawX - (int32_t)s_calibration.minRawX;
    int32_t yRange = (int32_t)s_calibration.maxRawY - (int32_t)s_calibration.minRawY;

    if (xRange == 0) xRange = 1;
    if (yRange == 0) yRange = 1;

    // Linear mapping from raw digitizer range to display resolution
    int32_t mappedX = ((int32_t)rawX - (int32_t)s_calibration.minRawX) * (width - 1) / xRange;
    int32_t mappedY = ((int32_t)rawY - (int32_t)s_calibration.minRawY) * (height - 1) / yRange;

    // Axis inversion if specified
    if (s_calibration.invertX) {
        mappedX = (width - 1) - mappedX;
    }
    if (s_calibration.invertY) {
        mappedY = (height - 1) - mappedY;
    }

    // Boundary clamping to prevent underflow or out-of-screen coordinates
    if (mappedX < 0) mappedX = 0;
    else if (mappedX >= width) mappedX = width - 1;

    if (mappedY < 0) mappedY = 0;
    else if (mappedY >= height) mappedY = height - 1;

    screenX = (uint16_t)mappedX;
    screenY = (uint16_t)mappedY;
}

bool runInteractiveCalibration(TFT_eSPI &tft) {
    static constexpr int16_t TARGETS[4][2] = {
        { 24,  24 },
        { 215, 24 },
        { 24,  295 },
        { 215, 295 }
    };
    int32_t avgX[4] = {0};
    int32_t avgY[4] = {0};

    pinMode(0, INPUT_PULLUP);

    for (int pt = 0; pt < 4; pt++) {
        int16_t tx = TARGETS[pt][0];
        int16_t ty = TARGETS[pt][1];

        tft.fillScreen(0x0821);
        tft.setTextFont(2);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(0x07FF, 0x0821);
        tft.drawString("TOUCH CALIBRATION", 120, 128);

        tft.setTextFont(1);
        tft.setTextColor(TFT_WHITE, 0x0821);
        char stepBuf[40];
        snprintf(stepBuf, sizeof(stepBuf), "Tap & hold crosshair (%d/4)", pt + 1);
        tft.drawString(stepBuf, 120, 150);
        tft.setTextColor(0x9CD3, 0x0821);
        tft.drawString("Press [BOOT] to cancel", 120, 168);

        // Draw target crosshair
        tft.drawCircle(tx, ty, 12, 0x07FF);
        tft.drawCircle(tx, ty, 5, 0xFFE0);
        tft.drawLine(tx - 16, ty, tx + 16, ty, TFT_WHITE);
        tft.drawLine(tx, ty - 16, tx, ty + 16, TFT_WHITE);

        // Wait for 6 consistent samples at this target
        int32_t sumX = 0, sumY = 0;
        int samples = 0;
        int16_t firstX = 0, firstY = 0;

        while (samples < 6) {
            if (digitalRead(0) == LOW) {
                hal::beep(30);
                tft.setTextDatum(TL_DATUM);
                return false;
            }
            int16_t rx = 0, ry = 0;
            uint16_t rz = 0;
            if (getRawTouch(rx, ry, rz) && rz >= 500) {
                if (samples == 0) {
                    firstX = rx;
                    firstY = ry;
                    sumX = rx;
                    sumY = ry;
                    samples = 1;
                } else if (abs(rx - firstX) <= 140 && abs(ry - firstY) <= 140) {
                    sumX += rx;
                    sumY += ry;
                    samples++;
                } else {
                    samples = 0;
                }
            } else {
                samples = 0;
            }
            delay(15);
        }

        avgX[pt] = sumX / 6;
        avgY[pt] = sumY / 6;

        // Visual + audio confirmation
        tft.fillCircle(tx, ty, 10, 0x07E0);
        hal::beep(35);

        // Wait until finger is lifted cleanly before next point
        uint32_t releaseStart = millis();
        while (millis() - releaseStart < 140) {
            int16_t rx = 0, ry = 0;
            uint16_t rz = 0;
            if (getRawTouch(rx, ry, rz)) {
                releaseStart = millis();
            }
            delay(15);
        }
    }

    int32_t rawLeft   = (avgX[0] + avgX[2]) / 2;
    int32_t rawRight  = (avgX[1] + avgX[3]) / 2;
    int32_t rawTop    = (avgY[0] + avgY[1]) / 2;
    int32_t rawBottom = (avgY[2] + avgY[3]) / 2;

    int32_t dx = rawRight - rawLeft;
    int32_t dy = rawBottom - rawTop;

    if (dx < 500 || dy < 500) {
        tft.fillScreen(0x0821);
        tft.setTextFont(2);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(0xF800, 0x0821);
        tft.drawString("CALIBRATION FAILED", 120, 148);
        tft.setTextFont(1);
        tft.setTextColor(TFT_WHITE, 0x0821);
        tft.drawString("Tap each corner accurately", 120, 170);
        hal::beep(90);
        delay(1400);
        tft.setTextDatum(TL_DATUM);
        return false;
    }

    // Extrapolate from inset (24..215, 24..295) to full (0..239, 0..319)
    int32_t minX = rawLeft   - (dx * 24) / 191;
    int32_t maxX = rawRight  + (dx * 24) / 191;
    int32_t minY = rawTop    - (dy * 24) / 271;
    int32_t maxY = rawBottom + (dy * 24) / 271;

    if (minX < 0) minX = 0;
    if (maxX > 4095) maxX = 4095;
    if (minY < 0) minY = 0;
    if (maxY > 4095) maxY = 4095;

    TouchCalibration cal = getTouchCalibration();
    cal.minRawX = (uint16_t)minX;
    cal.maxRawX = (uint16_t)maxX;
    cal.minRawY = (uint16_t)minY;
    cal.maxRawY = (uint16_t)maxY;
    cal.invertX = false;
    cal.invertY = false;

    setTouchCalibration(cal);
    Config::getInstance().setTouchCal(cal.minRawX, cal.maxRawX, cal.minRawY, cal.maxRawY, true);

    s_isPhysicallyDown = false;
    s_pendingDownCount = 0;
    s_currentState = TouchState::NONE;

    tft.fillScreen(0x0821);
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x07E0, 0x0821);
    tft.drawString("TOUCH CALIBRATED!", 120, 150);
    hal::beep(60);
    delay(700);
    tft.setTextDatum(TL_DATUM);
    return true;
}

} // namespace hal
