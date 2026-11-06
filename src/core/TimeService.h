/**
 * @file TimeService.h
 * @brief Wi-Fi NTP Time Synchronization & Region Timezone Resolver (Czechia, USA, Albania, etc.)
 */

#pragma once

#include <Arduino.h>
#include <time.h>

namespace core {

class TimeService {
public:
    /**
     * @brief Resolve a short country/region name (e.g. "Czechia", "USA", "Albania")
     * into a POSIX TZ rule and short label (e.g. "CET/UTC+1", "EST/UTC-5").
     */
    static void resolveRegionTz(const char* region, const char*& outPosixTz, const char*& outShortLabel);

    /**
     * @brief Apply the saved region timezone to the C library environment (setenv TZ + tzset).
     */
    static void applySavedRegion();

    /**
     * @brief Trigger NTP synchronization via pool.ntp.org / time.nist.gov if Wi-Fi is connected.
     * @param waitUpToMs Optional maximum wait in ms for initial NTP response (0 = non-blocking)
     * @return true if valid wall-clock time is available
     */
    static bool syncNtpNow(uint32_t waitUpToMs = 0);

    /**
     * @brief Periodic background check called by StatusBar/UIManager; automatically initiates
     * NTP sync when Wi-Fi transitions to WL_CONNECTED.
     */
    static void poll();

    /**
     * @brief Returns true once system epoch time has been synchronized via NTP.
     */
    static bool isSynced();

    /**
     * @brief Format current local time as "HH:MM" (or uptime if NTP not yet synced).
     */
    static void formatHHMM(char* outBuf, size_t maxLen);

    /**
     * @brief Format current local time as "HH:MM:SS" (or uptime if NTP not yet synced).
     */
    static void formatHHMMSS(char* outBuf, size_t maxLen);

    /**
     * @brief Format region + timezone summary, e.g. "Czechia (CET/UTC+1)".
     */
    static void formatRegionSummary(char* outBuf, size_t maxLen);

private:
    static bool     s_ntpConfigured;
    static bool     s_wasWifiConnected;
    static uint32_t s_lastSyncAttemptMs;
};

} // namespace core
