/**
 * @file OtaRecovery.h
 * @brief Dedicated Boot-Time Recovery & GitHub Releases Firmware OTA Updater
 *
 * Architecture:
 *   - Triggered from Settings -> "OTA FIRMWARE UPDATE (GITHUB)"
 *   - Sets an NVS boot flag ("ota_pend") and reboots the ESP32 into a clean,
 *     minimal-RAM Recovery state (>240 KB free SRAM, before UI Shell or Lua load)
 *   - Connects to saved Wi-Fi, follows GitHub Releases 302 redirect for:
 *     https://github.com/VexisTheFox/android_cyd_os/releases/latest/download/cyd2usb-firmware.bin
 *   - Streams the firmware image into the inactive OTA slot via <Update.h> with a
 *     live hardware progress bar, verifies flash integrity, and reboots into the new OS.
 */

#pragma once

#include <Arduino.h>

namespace core {

class OtaRecovery {
public:
    /**
     * @brief Arm the NVS recovery flag and reboot immediately into OTA Recovery Mode.
     */
    static void triggerRebootToOtaRecovery();

    /**
     * @brief Called early in setup() right after DisplayHAL & Config init.
     *        If the NVS recovery flag is set, runs the full-screen OTA Recovery Updater
     *        and reboots upon completion.
     */
    static void checkAndRunOnBoot();

    /**
     * @brief Query GitHub Releases (/releases/latest) over Wi-Fi to check if a newer
     *        stable cydOS firmware version is available than CYDOS_VERSION_TAG.
     * @param outLatestTag Output buffer populated with the remote tag (e.g. "v1.1")
     * @param maxLen Size of outLatestTag buffer
     * @return true if a newer stable release is available on GitHub
     */
    static bool checkForUpdate(char* outLatestTag, size_t maxLen);
};

} // namespace core
