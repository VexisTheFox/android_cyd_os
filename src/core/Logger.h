/**
 * @file Logger.h
 * @brief System-Wide Kernel Logger for CYD Android OS
 *
 * Writes structured component telemetry & diagnostics to `/kernel.log` on
 * internal LittleFS flash and automatically mirrors/copies to `/sd/kernel.log`
 * whenever a MicroSD card is present.
 *
 * Log Filtering Rules:
 *   - INFO  : Can be enabled/disabled in Settings (persisted in NVS Config)
 *   - WARN  : Can be enabled/disabled in Settings (persisted in NVS Config)
 *   - ERROR : ALWAYS enabled (cannot be disabled)
 *   - FATAL : ALWAYS enabled (cannot be disabled)
 */

#pragma once

#include <Arduino.h>

namespace core {

enum class LogLevel : uint8_t {
    INFO  = 0,
    WARN  = 1,
    ERROR = 2,
    FATAL = 3
};

class Logger {
public:
    static constexpr const char* FLASH_LOG_PATH = "/kernel.log";
    static constexpr const char* SD_LOG_PATH    = "/sd/kernel.log";
    static constexpr size_t      MAX_LOG_BYTES  = 14336; // 14 KB max before rotation
    static constexpr size_t      KEEP_LOG_BYTES = 8192;  // Keep latest 8 KB on rotation

    /**
     * @brief Log a formatted message from a system component.
     */
    static void log(LogLevel level, const char* component, const char* fmt, ...);

    /**
     * @brief Flush any early pre-LittleFS boot logs to `/kernel.log` and sync to SD if mounted.
     */
    static void flushEarlyLogs();

    /**
     * @brief Copy the complete `/kernel.log` from LittleFS flash to `/sd/kernel.log` if SD is mounted.
     * @return true if SD is mounted and `/sd/kernel.log` was updated
     */
    static bool syncToSD();

    /**
     * @brief Read the contents of `/kernel.log` from LittleFS flash.
     */
    static String readLog();

    /**
     * @brief Clear `/kernel.log` and record a fresh header entry.
     */
    static void clearLog();
};

} // namespace core

#define LOG_INFO(comp, fmt, ...)  ::core::Logger::log(::core::LogLevel::INFO,  comp, fmt, ##__VA_ARGS__)
#define LOG_WARN(comp, fmt, ...)  ::core::Logger::log(::core::LogLevel::WARN,  comp, fmt, ##__VA_ARGS__)
#define LOG_ERROR(comp, fmt, ...) ::core::Logger::log(::core::LogLevel::ERROR, comp, fmt, ##__VA_ARGS__)
#define LOG_FATAL(comp, fmt, ...) ::core::Logger::log(::core::LogLevel::FATAL, comp, fmt, ##__VA_ARGS__)
