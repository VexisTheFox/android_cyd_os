/**
 * @file Logger.cpp
 * @brief Implementation of System-Wide Kernel Logger (`kernel.log`) - OOM Hardened
 */

#include "Logger.h"
#include "Config.h"
#include "MemoryGuard.h"
#include "hal/StorageHAL.h"
#include <LittleFS.h>
#include <SD.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace core {

static char   s_earlyBuffer[384]  = {0};
static size_t s_earlyLen          = 0;
static bool   s_inLogger          = false; // Re-entrancy guard

static const char* levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default:              return "INFO";
    }
}

static void rotateFlashLogIfNeeded() {
    if (!StorageHAL::isLittleFSMounted()) return;
    // Do not attempt log rotation while in low-memory / OOM recovery state
    if (xPortGetFreeHeapSize() < 16384) return;
    if (!LittleFS.exists(Logger::FLASH_LOG_PATH)) return;

    File f = LittleFS.open(Logger::FLASH_LOG_PATH, "r");
    if (!f) return;
    size_t sz = f.size();
    if (sz <= Logger::MAX_LOG_BYTES) {
        f.close();
        return;
    }

    size_t keepLen = Logger::KEEP_LOG_BYTES;
    char* tailBuf = (char*)MemoryGuard::safeMalloc(keepLen + 1);
    if (!tailBuf) {
        f.close();
        return;
    }

    f.seek(sz - keepLen);
    size_t nRead = f.readBytes(tailBuf, keepLen);
    tailBuf[nRead] = '\0';
    f.close();

    // Align to first newline so we don't leave a partial line at top
    char* startPtr = strchr(tailBuf, '\n');
    if (startPtr && *(startPtr + 1) != '\0') {
        startPtr++;
    } else {
        startPtr = tailBuf;
    }

    File out = LittleFS.open(Logger::FLASH_LOG_PATH, "w");
    if (out) {
        out.println("--- [KERNEL.LOG ROTATED] ---");
        out.write((const uint8_t*)startPtr, strlen(startPtr));
        out.flush();
        out.close();
    }
    vPortFree(tailBuf);
}

void Logger::log(LogLevel level, const char* component, const char* fmt, ...) {
    if (s_inLogger) return;

    // Filtering: INFO and WARN can be disabled in Settings; ERROR and FATAL cannot!
    if (level == LogLevel::INFO && !Config::getInstance().isLogInfoEnabled()) {
        return;
    }
    if (level == LogLevel::WARN && !Config::getInstance().isLogWarnEnabled()) {
        return;
    }

    s_inLogger = true;

    // If logging an ERROR or FATAL while heap is critically low, release Emergency Reserve Pool!
    if ((level == LogLevel::ERROR || level == LogLevel::FATAL) &&
        xPortGetFreeHeapSize() < MemoryGuard::CRITICAL_HEAP_FLOOR) {
        MemoryGuard::releaseEmergencyPool("Logger ERROR/FATAL in low heap");
    }

    // Fixed-size stack buffers ONLY (zero dynamic String allocations)
    char msgBuf[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt ? fmt : "", args);
    va_end(args);
    msgBuf[sizeof(msgBuf) - 1] = '\0';

    uint32_t ms = millis();
    uint32_t sec = ms / 1000;
    uint32_t min = (sec / 60) % 60;
    uint32_t remSec = sec % 60;
    uint32_t remMs = ms % 1000;

    char lineBuf[224];
    int lineLen = snprintf(lineBuf, sizeof(lineBuf), "[%02lu:%02lu.%03lu] [%s] [%s] %s\n",
                           (unsigned long)min, (unsigned long)remSec, (unsigned long)remMs,
                           levelToString(level), component ? component : "SYS", msgBuf);
    if (lineLen < 0) lineLen = 0;
    if ((size_t)lineLen >= sizeof(lineBuf)) lineLen = sizeof(lineBuf) - 1;
    lineBuf[lineLen] = '\0';

    // Always echo to Serial console
    Serial.print(lineBuf);

    if (!StorageHAL::isLittleFSMounted()) {
        // Buffer early boot messages in static BSS buffer until LittleFS mounts
        if (s_earlyLen + (size_t)lineLen + 1 < sizeof(s_earlyBuffer)) {
            memcpy(s_earlyBuffer + s_earlyLen, lineBuf, (size_t)lineLen + 1);
            s_earlyLen += (size_t)lineLen;
        }
        s_inLogger = false;
        return;
    }

    // Flush any early boot buffer first
    if (s_earlyLen > 0) {
        File ef = LittleFS.open(FLASH_LOG_PATH, "a");
        if (ef) {
            ef.write((const uint8_t*)s_earlyBuffer, s_earlyLen);
            ef.flush();
            ef.close();
        }
        s_earlyLen = 0;
        s_earlyBuffer[0] = '\0';
    }

    rotateFlashLogIfNeeded();

    // Append to /kernel.log in LittleFS flash
    File f = LittleFS.open(FLASH_LOG_PATH, "a");
    if (f) {
        f.write((const uint8_t*)lineBuf, (size_t)lineLen);
        f.flush();
        f.close();
    }

    // If MicroSD card is mounted, also append directly to /sd/kernel.log
    if (StorageHAL::isSDMounted() && xPortGetFreeHeapSize() >= MemoryGuard::CRITICAL_HEAP_FLOOR) {
        File sdf = SD.open("/kernel.log", FILE_APPEND);
        if (!sdf) {
            sdf = SD.open("/kernel.log", FILE_WRITE);
        }
        if (sdf) {
            sdf.write((const uint8_t*)lineBuf, (size_t)lineLen);
            sdf.flush();
            sdf.close();
        }
    }

    s_inLogger = false;
}

void Logger::flushEarlyLogs() {
    if (!StorageHAL::isLittleFSMounted()) return;
    if (s_earlyLen > 0) {
        s_inLogger = true;
        File ef = LittleFS.open(FLASH_LOG_PATH, "a");
        if (ef) {
            ef.write((const uint8_t*)s_earlyBuffer, s_earlyLen);
            ef.flush();
            ef.close();
        }
        s_earlyLen = 0;
        s_earlyBuffer[0] = '\0';
        s_inLogger = false;
    }
    if (StorageHAL::isSDMounted()) {
        syncToSD();
    }
}

bool Logger::syncToSD() {
    if (!StorageHAL::isLittleFSMounted() || !StorageHAL::isSDMounted()) {
        return false;
    }
    if (xPortGetFreeHeapSize() < MemoryGuard::LUA_HARD_HEAP_FLOOR) {
        return false;
    }

    bool prevGuard = s_inLogger;
    s_inLogger = true;

    File src = LittleFS.open(FLASH_LOG_PATH, "r");
    if (!src) {
        s_inLogger = prevGuard;
        return false;
    }

    File dst = SD.open("/kernel.log", FILE_WRITE);
    if (!dst) {
        src.close();
        s_inLogger = prevGuard;
        return false;
    }

    uint8_t buf[256];
    while (src.available()) {
        int n = src.read(buf, sizeof(buf));
        if (n > 0) {
            dst.write(buf, (size_t)n);
        } else {
            break;
        }
    }
    dst.flush();
    dst.close();
    src.close();

    s_inLogger = prevGuard;
    return true;
}

String Logger::readLog() {
    if (!StorageHAL::isLittleFSMounted()) {
        return String(s_earlyBuffer);
    }
    if (!LittleFS.exists(FLASH_LOG_PATH)) {
        return String("(kernel.log is empty)\n");
    }
    if (!MemoryGuard::ensureHeapFloor(4096)) {
        return String("[LOW MEMORY: Cannot load full kernel.log]\n");
    }
    File f = LittleFS.open(FLASH_LOG_PATH, "r");
    if (!f) {
        return String("(Failed to open /kernel.log)\n");
    }
    size_t sz = f.size();
    // Cap returned string to last 6 KB if heap is tight
    if (sz > 6144) {
        f.seek(sz - 6144);
    }
    String content = f.readString();
    f.close();
    return content;
}

void Logger::clearLog() {
    if (!StorageHAL::isLittleFSMounted()) {
        s_earlyLen = 0;
        s_earlyBuffer[0] = '\0';
        return;
    }
    s_inLogger = true;
    File f = LittleFS.open(FLASH_LOG_PATH, "w");
    if (f) {
        f.println("[00:00.000] [INFO] [KERNEL] kernel.log cleared by user");
        f.flush();
        f.close();
    }
    s_inLogger = false;
    if (StorageHAL::isSDMounted()) {
        syncToSD();
    }
}

} // namespace core
