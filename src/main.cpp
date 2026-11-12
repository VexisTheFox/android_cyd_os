/**
 * @file main.cpp
 * @brief Master Entry Point for CYD Android OS Prototype
 * 
 * Target Hardware: ESP32 Cheap Yellow Display (CYD 2 USB-C / ESP32-2432S028)
 * Subsystems:
 *   - ST7789 320x240 LCD (HSPI @ 55 MHz)
 *   - Hardware PWM Backlight (GPIO 21 LEDC @ 5 kHz)
 *   - LittleFS Flash Storage & Turnkey App Provisioner
 *   - MicroSD Storage (VSPI Bus)
 *   - XPT2046 Resistive Touch Digitizer (VSPI Bus @ 2.5 MHz)
 *   - Onboard Peripherals (RGB LED, LDR Light Sensor, Speaker)
 *   - Android UI Shell (BootView, StatusBar, NavBar, LauncherView, UIManager)
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "hal/DisplayHAL.h"
#include "hal/StorageHAL.h"
#include "hal/TouchHAL.h"
#include "hal/BoardHAL.h"
#include "ui/UIManager.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "core/MemoryGuard.h"
#include "core/TimeService.h"
#include "core/AppRegistry.h"
#include "core/OtaRecovery.h"
#include <WiFi.h>

// Lua VM + TFT drawing need more than the default 8 KB loop stack
SET_LOOP_TASK_STACK_SIZE(20 * 1024);

// ============================================================================
// Arduino Setup & System Supervisor
// ============================================================================
void setup() {
    // 1. Initialize Serial Console, Emergency RAM Reserve Pool & NVS Config Store first
    Serial.begin(115200);
    delay(100);
    core::MemoryGuard::init();
    Config::getInstance().begin();
    Serial.println(F("\n======================================================="));
    Serial.println(F(" CYD Android OS Prototype (ESP32-2432S028)             "));
    Serial.println(F(" Milestone 2: Android UI Shell & Navigation System     "));
    Serial.println(F("======================================================="));
    LOG_INFO("KERNEL", "Booting CYD Android OS (INFO=%s WARN=%s ERR/FATAL=LOCKED)",
             Config::getInstance().isLogInfoEnabled() ? "ON" : "OFF",
             Config::getInstance().isLogWarnEnabled() ? "ON" : "OFF");

    // 2. Initialize Display & Backlight HAL First
    // Turns on ST7789, fills screen with black to suppress power-on artifacts,
    // and configures hardware LEDC PWM backlight timer on GPIO 21 at 80% duty.
    Serial.println(F("[BOOT] Initializing Display HAL (ST7789 @ 55 MHz)..."));
    hal::initDisplay();

    // Check if rebooted into Minimal-RAM OTA Firmware Recovery Mode
    core::OtaRecovery::checkAndRunOnBoot();

    // 3. Initialize UI Shell Manager in BOOT_SPLASH State
    // Shows the Android Bugdroid vector logo immediately so user sees instant boot response.
    Serial.println(F("[BOOT] Initializing UI Shell Manager..."));
    ui::UIManager::init();
    ui::UIManager::setBootProgress(10, "Display initialized");

    // 4. Initialize Storage Subsystem (LittleFS & MicroSD)
    // CRITICAL: StorageHAL::init() must execute BEFORE TouchHAL::initTouch()
    // to cleanly isolate SD SPI CS pin 5 from digitizer VSPI bus!
    Serial.println(F("[BOOT] Initializing Storage HAL (LittleFS & SD Card)..."));
    bool storageOk = StorageHAL::init();
    Serial.printf("[BOOT] LittleFS mounted: %s (%lu KB used / %lu KB total)\n",
                  storageOk ? "YES" : "NO",
                  (unsigned long)(StorageHAL::getStorageUsedBytes() / 1024),
                  (unsigned long)(StorageHAL::getStorageTotalBytes() / 1024));
    ui::UIManager::setBootProgress(35, "Storage ready");

    // 5. Initialize Touch HAL on Dedicated VSPI Bus
    // Calibrates XPT2046 digitizer coordinates to 240x320 screen pixels.
    Serial.println(F("[BOOT] Initializing Touch HAL (XPT2046 @ 2.5 MHz)..."));
    hal::initTouch();
    if (Config::getInstance().isTouchCalibrated()) {
        hal::TouchCalibration cal = hal::getTouchCalibration();
        Config::getInstance().getTouchCal(cal.minRawX, cal.maxRawX, cal.minRawY, cal.maxRawY);
        hal::setTouchCalibration(cal);
        LOG_INFO("TOUCH", "Loaded NVS calibration: X[%u..%u] Y[%u..%u]",
                 cal.minRawX, cal.maxRawX, cal.minRawY, cal.maxRawY);
    }
    ui::UIManager::setBootProgress(65, "Touch calibrated");

    // 6. Initialize Onboard Peripherals (RGB LED, LDR Photoresistor, Buzzer)
    Serial.println(F("[BOOT] Initializing Board Peripherals..."));
    hal::initBoard();
    LOG_INFO("BOARD", "Peripherals initialized (RGB LED, LDR, Buzzer)");
    // Brief green LED blink to confirm hardware readiness
    hal::setLEDGreen(true);
    delay(50);
    hal::setLEDGreen(false);
    ui::UIManager::setBootProgress(75, "Hardware OK");

    // 7. Restore saved NVS configuration settings
    Serial.println(F("[BOOT] Restoring NVS Configuration Store..."));
    uint8_t savedBrightness = Config::getInstance().getBrightness();
    hal::setBacklight(savedBrightness);
    Serial.printf("[BOOT] Backlight restored to %u%%\n", savedBrightness);
    LOG_INFO("CONFIG", "NVS loaded: brightness=%u%% sound=%s sleep=%us",
             savedBrightness,
             Config::getInstance().isSoundEnabled() ? "ON" : "OFF",
             Config::getInstance().getSleepTimeoutSec());
    if (Config::getInstance().hasRegion()) {
        core::TimeService::applySavedRegion();
    }
    if (Config::getInstance().isWifiEnabled()) {
        WiFi.mode(WIFI_STA);
        const char* ssid = Config::getInstance().getWifiSsid();
        const char* pass = Config::getInstance().getWifiPass();
        if (ssid && ssid[0] != '\0') {
            LOG_INFO("WIFI", "Auto-connecting saved Wi-Fi SSID: %s", ssid);
            if (pass && pass[0] != '\0') WiFi.begin(ssid, pass);
            else WiFi.begin(ssid);
        }
    }
    ui::UIManager::setBootProgress(85, "Config loaded");

    // 8. Initialize Dynamic Application Registry
    Serial.println(F("[BOOT] Initializing Dynamic App Registry..."));
    core::AppRegistry::init();
    Serial.printf("[BOOT] Registered %u applications.\n", (unsigned)core::AppRegistry::getAppCount());
    LOG_INFO("REGISTRY", "Registered %u applications", (unsigned)core::AppRegistry::getAppCount());
    ui::UIManager::setBootProgress(95, "Apps registered");

    // 9. Complete Boot Sequence
    Serial.printf("[BOOT] Boot sequence finished! Free Heap: %lu bytes\n", (unsigned long)ESP.getFreeHeap());
    LOG_INFO("KERNEL", "Boot complete! Free Heap: %lu KB", (unsigned long)(ESP.getFreeHeap() / 1024));
    ui::UIManager::setBootProgress(100, "Starting OS...");
    delay(150);

    // Audio confirmation chirp
    hal::beep(50);

    // Transition from BOOT_SPLASH to HOME_LAUNCHER
    ui::UIManager::finishBoot();
    Serial.println(F("[BOOT] UI Shell entered HOME_LAUNCHER. System ready."));
}

void loop() {
    // Drive UI Shell coordinator (touch routing, status bar updates, viewport redraws)
    ui::UIManager::update();

    // Yield 1ms timeslice to FreeRTOS supervisor while keeping 3D rendering >= 35-50 FPS
    delay(1);
}

