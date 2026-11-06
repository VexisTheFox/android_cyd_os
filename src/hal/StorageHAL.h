#pragma once

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <vector>
#include <cstring>
#include <strings.h>

/**
 * @file StorageHAL.h
 * @brief Hardware Abstraction Layer for Storage & Turnkey App Provisioning
 *
 * Supports LittleFS on internal SPI flash (primary storage) and MicroSD on VSPI (secondary storage).
 * Includes auto-provisioner that extracts bundled Lua apps from PROGMEM to LittleFS on first boot.
 */

// CYD MicroSD Pin Definitions on VSPI Bus
#define CYD_SD_CS   5
#define CYD_SD_CLK  18
#define CYD_SD_MISO 19
#define CYD_SD_MOSI 23

class StorageHAL {
public:
    /**
     * @brief Initialize storage subsystem: mounts LittleFS, verifies /apps directory,
     * provisions default bundled apps from PROGMEM if missing, and probes for MicroSD.
     * @return true if primary storage (LittleFS) mounted successfully.
     */
    static bool init();

    /**
     * @brief Cleanly unmount filesystems and release SPI buses.
     */
    static void end();

    // -------------------------------------------------------------
    // Status Queries
    // -------------------------------------------------------------
    static bool isLittleFSMounted();
    static bool isSDMounted();

    static size_t getStorageTotalBytes(); // Returns LittleFS total bytes (primary OS storage)
    static size_t getStorageUsedBytes();  // Returns LittleFS used bytes
    static size_t getLittleFSTotalBytes();
    static size_t getLittleFSUsedBytes();

    static size_t getSDTotalBytes();
    static size_t getSDUsedBytes();
    static uint8_t getSDCardType();

    /**
     * @brief Attempt to detect and mount MicroSD card dynamically at runtime.
     * @return true if card was found and successfully mounted.
     */
    static bool probeSDCard();

    // -------------------------------------------------------------
    // Turnkey Provisioning & Directory Utilities
    // -------------------------------------------------------------
    /**
     * @brief Ensures that a directory exists, creating parent directories if necessary.
     * @param dirPath Path to directory, e.g. "/apps" or "/sd/apps"
     * @return true if directory exists or was successfully created.
     */
    static bool ensureDirectory(const char* dirPath);

    /**
     * @brief Auto-provisions default bundled scripts from PROGMEM to LittleFS.
     * Only writes files that do not currently exist on LittleFS, preserving user modifications.
     */
    static void provisionDefaultApps();

    /**
     * @brief Factory reset: forces re-writing all bundled apps from PROGMEM, overwriting existing files.
     */
    static void forceProvisionDefaultApps();

    // -------------------------------------------------------------
    // High-Level File I/O Utilities (VFS-aware)
    // -------------------------------------------------------------
    /**
     * @brief Checks if a file exists on LittleFS or SD card.
     * @param path Virtual path (e.g. "/apps/calc.lua" or "/sd/apps/demo.lua")
     */
    static bool fileExists(const char* path);

    /**
     * @brief Returns size of file in bytes, or 0 if file does not exist.
     */
    static size_t getFileSize(const char* path);

    /**
     * @brief Reads entire file contents into an Arduino String.
     * @param path Virtual path
     * @param outContent Output string destination
     * @return true if read succeeded
     */
    static bool readFile(const char* path, String &outContent);

    /**
     * @brief Writes text content to file, overwriting existing content.
     */
    static bool writeFile(const char* path, const char* content);

    /**
     * @brief Writes binary buffer to file, overwriting existing content.
     */
    static bool writeFile(const char* path, const uint8_t* buffer, size_t length);

    /**
     * @brief Appends text content to file.
     */
    static bool appendFile(const char* path, const char* content);

    /**
     * @brief Deletes file from LittleFS or SD.
     */
    static bool deleteFile(const char* path);

    /**
     * @brief Lists all files in a directory, optionally filtered by extension.
     * @param dirPath Directory path (e.g. "/apps" or "/sd/apps")
     * @param outFiles Vector to receive discovered file paths
     * @param extensionFilter Optional extension filter (e.g. ".lua")
     * @return true if directory was successfully read
     */
    static bool listDirectory(const char* dirPath, std::vector<String> &outFiles, const char* extensionFilter = nullptr);

    // -------------------------------------------------------------
    // VFS Path Resolution Helpers
    // -------------------------------------------------------------
    static bool isSDPath(const char* path);
    static const char* stripSDPrefix(const char* path);
    static fs::FS& getFSForPath(const char* path);

private:
    static bool s_littlefsMounted;
    static bool s_sdMounted;
    static SPIClass s_spiSD;

    static void writeProgmemFile(const char* path, const char* progmemData);
    static bool hasExtension(const char* filename, const char* ext);
};

// ===================================================================
// Master Specification HAL Namespace Contract (PROJECT.md lines 90-93)
// ===================================================================
namespace hal {
    inline bool initStorage() {
        return StorageHAL::init();
    }

    inline bool isSDCardMounted() {
        return StorageHAL::isSDMounted();
    }

    inline size_t getStorageTotalBytes() {
        return StorageHAL::getStorageTotalBytes();
    }

    inline size_t getStorageUsedBytes() {
        return StorageHAL::getStorageUsedBytes();
    }
}
