/**
 * @file TimeService.cpp
 * @brief Implementation of Wi-Fi NTP Time Synchronization & Region Timezone Resolver
 */

#include "TimeService.h"
#include "core/Config.h"
#include "core/Logger.h"
#include <WiFi.h>
#include <cstring>

namespace core {

bool     TimeService::s_ntpConfigured     = false;
bool     TimeService::s_wasWifiConnected  = false;
uint32_t TimeService::s_lastSyncAttemptMs = 0;

struct RegionTzEntry {
    const char* keyword;
    const char* posixTz;
    const char* label;
};

// Comprehensive mapping of short country/region names (case-insensitive)
static const RegionTzEntry REGION_TABLE[] = {
    // Central European Time (UTC+1 / CEST UTC+2): Czechia, Albania, Germany, Poland, Slovakia, etc.
    { "czechia",     "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "czech",       "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "cz",          "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "albania",     "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "al",          "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "slovakia",    "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "sk",          "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "germany",     "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "de",          "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "poland",      "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "pl",          "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "austria",     "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "france",      "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "italy",       "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "spain",       "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "netherlands", "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "sweden",      "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "norway",      "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "denmark",     "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "switzerland", "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "hungary",     "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "croatia",     "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "serbia",      "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "kosovo",      "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },
    { "cet",         "CET-1CEST,M3.5.0,M10.5.0/3", "CET/UTC+1" },

    // USA & North America Timezones
    { "usa pst",     "PST8PDT,M3.2.0,M11.1.0",     "PST/UTC-8" },
    { "california",  "PST8PDT,M3.2.0,M11.1.0",     "PST/UTC-8" },
    { "pst",         "PST8PDT,M3.2.0,M11.1.0",     "PST/UTC-8" },
    { "usa mst",     "MST7MDT,M3.2.0,M11.1.0",     "MST/UTC-7" },
    { "mst",         "MST7MDT,M3.2.0,M11.1.0",     "MST/UTC-7" },
    { "usa cst",     "CST6CDT,M3.2.0,M11.1.0",     "CST/UTC-6" },
    { "chicago",     "CST6CDT,M3.2.0,M11.1.0",     "CST/UTC-6" },
    { "mexico",      "CST6CDT,M3.2.0,M11.1.0",     "CST/UTC-6" },
    { "usa",         "EST5EDT,M3.2.0,M11.1.0",     "EST/UTC-5" },
    { "us",          "EST5EDT,M3.2.0,M11.1.0",     "EST/UTC-5" },
    { "america",     "EST5EDT,M3.2.0,M11.1.0",     "EST/UTC-5" },
    { "canada",      "EST5EDT,M3.2.0,M11.1.0",     "EST/UTC-5" },
    { "est",         "EST5EDT,M3.2.0,M11.1.0",     "EST/UTC-5" },

    // UK / Ireland / Portugal / UTC (UTC+0)
    { "uk",          "GMT0BST,M3.5.0/1,M10.5.0",   "GMT/UTC+0" },
    { "gb",          "GMT0BST,M3.5.0/1,M10.5.0",   "GMT/UTC+0" },
    { "england",     "GMT0BST,M3.5.0/1,M10.5.0",   "GMT/UTC+0" },
    { "britain",     "GMT0BST,M3.5.0/1,M10.5.0",   "GMT/UTC+0" },
    { "ireland",     "GMT0BST,M3.5.0/1,M10.5.0",   "GMT/UTC+0" },
    { "portugal",    "GMT0BST,M3.5.0/1,M10.5.0",   "GMT/UTC+0" },
    { "utc",         "GMT0",                       "UTC+0" },

    // Eastern Europe (UTC+2)
    { "greece",      "EET-2EEST,M3.5.0/3,M10.5.0/4", "EET/UTC+2" },
    { "romania",     "EET-2EEST,M3.5.0/3,M10.5.0/4", "EET/UTC+2" },
    { "bulgaria",    "EET-2EEST,M3.5.0/3,M10.5.0/4", "EET/UTC+2" },
    { "finland",     "EET-2EEST,M3.5.0/3,M10.5.0/4", "EET/UTC+2" },
    { "ukraine",     "EET-2EEST,M3.5.0/3,M10.5.0/4", "EET/UTC+2" },

    // Turkey / Moscow / Middle East (UTC+3 / +4)
    { "turkey",      "TRT-3",                      "UTC+3" },
    { "russia",      "MSK-3",                      "UTC+3" },
    { "uae",         "GST-4",                      "UTC+4" },
    { "dubai",       "GST-4",                      "UTC+4" },

    // South America
    { "brazil",      "BRT3",                       "UTC-3" },
    { "argentina",   "ART3",                       "UTC-3" },

    // Asia & Oceania
    { "india",       "IST-5:30",                   "UTC+5:30" },
    { "china",       "CST-8",                      "UTC+8" },
    { "singapore",   "SGT-8",                      "UTC+8" },
    { "taiwan",      "CST-8",                      "UTC+8" },
    { "japan",       "JST-9",                      "UTC+9" },
    { "korea",       "KST-9",                      "UTC+9" },
    { "australia",   "AEST-10AEDT,M10.1.0,M4.1.0/3", "UTC+10" }
};

void TimeService::resolveRegionTz(const char* region, const char*& outPosixTz, const char*& outShortLabel) {
    if (!region || region[0] == '\0') {
        outPosixTz    = "CET-1CEST,M3.5.0,M10.5.0/3";
        outShortLabel = "CET/UTC+1";
        return;
    }

    // 1. Exact case-insensitive match first
    for (size_t i = 0; i < sizeof(REGION_TABLE) / sizeof(REGION_TABLE[0]); i++) {
        if (strcasecmp(region, REGION_TABLE[i].keyword) == 0) {
            outPosixTz    = REGION_TABLE[i].posixTz;
            outShortLabel = REGION_TABLE[i].label;
            return;
        }
    }

    // 2. Substring case-insensitive match
    for (size_t i = 0; i < sizeof(REGION_TABLE) / sizeof(REGION_TABLE[0]); i++) {
        if (strlen(REGION_TABLE[i].keyword) >= 3 && strcasestr(region, REGION_TABLE[i].keyword) != nullptr) {
            outPosixTz    = REGION_TABLE[i].posixTz;
            outShortLabel = REGION_TABLE[i].label;
            return;
        }
    }

    // Default fallback to Central European Time (Czechia / Albania)
    outPosixTz    = "CET-1CEST,M3.5.0,M10.5.0/3";
    outShortLabel = "CET/UTC+1";
}

void TimeService::applySavedRegion() {
    const char* region = Config::getInstance().getRegion();
    const char* posixTz = nullptr;
    const char* label   = nullptr;
    resolveRegionTz(region, posixTz, label);

    setenv("TZ", posixTz, 1);
    tzset();
    LOG_INFO("TIME", "Applied Region '%s' -> TZ='%s' (%s)",
             (region && region[0]) ? region : "Czechia", posixTz, label);

    if (WiFi.status() == WL_CONNECTED) {
        configTzTime(posixTz, "pool.ntp.org", "time.nist.gov", "time.google.com");
        s_ntpConfigured = true;
    }
}

bool TimeService::isSynced() {
    time_t now = time(nullptr);
    return (now > 1700000000); // > Nov 2023 epoch indicates valid NTP sync
}

bool TimeService::syncNtpNow(uint32_t waitUpToMs) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    const char* region = Config::getInstance().getRegion();
    const char* posixTz = nullptr;
    const char* label   = nullptr;
    resolveRegionTz(region, posixTz, label);

    configTzTime(posixTz, "pool.ntp.org", "time.nist.gov", "time.google.com");
    setenv("TZ", posixTz, 1);
    tzset();
    s_ntpConfigured = true;
    s_lastSyncAttemptMs = millis();

    if (waitUpToMs > 0) {
        uint32_t start = millis();
        while (!isSynced() && (millis() - start < waitUpToMs)) {
            delay(80);
        }
    }

    if (isSynced()) {
        char tBuf[16];
        formatHHMMSS(tBuf, sizeof(tBuf));
        LOG_INFO("NTP", "NTP time synced for %s (%s): %s",
                 (region && region[0]) ? region : "Czechia", label, tBuf);
        return true;
    }
    return false;
}

void TimeService::poll() {
    bool wifiConn = (WiFi.status() == WL_CONNECTED);
    uint32_t nowMs = millis();

    if (wifiConn && !s_wasWifiConnected) {
        s_wasWifiConnected = true;
        syncNtpNow(0);
    } else if (!wifiConn) {
        s_wasWifiConnected = false;
    } else if (wifiConn && !isSynced() && (nowMs - s_lastSyncAttemptMs > 15000)) {
        syncNtpNow(0);
    }
}

void TimeService::formatHHMM(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    if (isSynced()) {
        time_t now = time(nullptr);
        struct tm ti;
        localtime_r(&now, &ti);
        snprintf(outBuf, maxLen, "%02d:%02d", ti.tm_hour, ti.tm_min);
    } else {
        uint32_t totalSec = millis() / 1000;
        uint32_t min = (totalSec / 60) % 60;
        uint32_t hr  = (totalSec / 3600) % 24;
        snprintf(outBuf, maxLen, "%02lu:%02lu", (unsigned long)hr, (unsigned long)min);
    }
}

void TimeService::formatHHMMSS(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    if (isSynced()) {
        time_t now = time(nullptr);
        struct tm ti;
        localtime_r(&now, &ti);
        snprintf(outBuf, maxLen, "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        uint32_t totalSec = millis() / 1000;
        uint32_t sec = totalSec % 60;
        uint32_t min = (totalSec / 60) % 60;
        uint32_t hr  = (totalSec / 3600) % 24;
        snprintf(outBuf, maxLen, "%02lu:%02lu:%02lu",
                 (unsigned long)hr, (unsigned long)min, (unsigned long)sec);
    }
}

void TimeService::formatRegionSummary(char* outBuf, size_t maxLen) {
    if (!outBuf || maxLen == 0) return;
    const char* region = Config::getInstance().getRegion();
    const char* posixTz = nullptr;
    const char* label   = nullptr;
    resolveRegionTz(region, posixTz, label);
    snprintf(outBuf, maxLen, "%s (%s)",
             (region && region[0]) ? region : "Czechia", label);
}

} // namespace core
