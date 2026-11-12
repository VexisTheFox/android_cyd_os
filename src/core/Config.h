/**
 * @file Config.h
 * @brief NVS System Configuration & Preferences Manager for CYD Android OS
 * 
 * Hardware Target: ESP32 Cheap Yellow Display (ESP32-2432S028)
 * Storage Backend: ESP32 NVS via Preferences library (Namespace: "cyd_os")
 * Conforms to PROJECT.md Feature F20.
 */

#pragma once

#include <Arduino.h>
#include <string.h>

// Boundary Constants
constexpr uint8_t  CONFIG_DEFAULT_BRIGHTNESS     = 80;
constexpr uint8_t  CONFIG_MIN_BRIGHTNESS         = 10;   // Prevent unreadable pitch-black screen
constexpr uint8_t  CONFIG_MAX_BRIGHTNESS         = 100;

constexpr uint16_t CONFIG_DEFAULT_SLEEP_TIMEOUT  = 60;   // 60 seconds
constexpr uint16_t CONFIG_MAX_SLEEP_TIMEOUT      = 3600; // 1 hour max

constexpr uint8_t  CONFIG_DEFAULT_THEME          = 0;    // 0: Dark, 1: Light
constexpr bool     CONFIG_DEFAULT_SOUND_ENABLED  = true;
constexpr bool     CONFIG_DEFAULT_LED_ENABLED    = true;
constexpr bool     CONFIG_DEFAULT_LOG_INFO       = true;
constexpr bool     CONFIG_DEFAULT_LOG_WARN       = true;
constexpr bool     CONFIG_DEFAULT_DEV_MODE       = false;
constexpr bool     CONFIG_DEFAULT_PIN_ENABLED    = false;
constexpr const char* CONFIG_DEFAULT_PIN_CODE    = "1234";
constexpr bool     CONFIG_DEFAULT_WIFI_ENABLED   = false;
constexpr uint8_t  CONFIG_WALLPAPER_CUSTOM_SD    = 255;

constexpr const char* CONFIG_NVS_NAMESPACE       = "cyd_os";

/**
 * @brief Plain Old Data representation of system configuration
 * Conforms to PROJECT.md Contract § 4.
 */
struct SystemConfig {
    uint8_t  brightness;        ///< Backlight percentage: 10..100%
    uint16_t sleepTimeoutSec;   ///< Sleep timeout in seconds: 0 = disabled, 10..3600
    uint8_t  theme;             ///< UI theme: 0 = Dark / Teal, 1 = Light
    bool     soundEnabled;      ///< UI audio feedback enabled
    bool     ledEnabled;        ///< Onboard RGB status LED indicator enabled
    bool     logInfoEnabled;    ///< Kernel log INFO level enabled (can be disabled)
    bool     logWarnEnabled;    ///< Kernel log WARN level enabled (can be disabled)
                                ///< Note: ERROR and FATAL are permanently enabled
    bool     devModeEnabled;    ///< Unlocked by tapping Build Number 5 times in Settings
    bool     pinEnabled;        ///< Lock Screen 4-digit PIN lock enabled
    char     pinCode[8];        ///< 4-digit numeric PIN string (e.g. "1234")
    bool     wifiEnabled;       ///< Wi-Fi 802.11 station enabled state
    char     wifiSsid[33];      ///< Wi-Fi target SSID
    char     wifiPass[64];      ///< Wi-Fi WPA2 password
    char     region[24];        ///< Short country/region name (e.g. "Czechia", "USA", "Albania")
    uint8_t  wallpaperId;       ///< 0..5 = built-in preset, 255 = custom SD/Flash image
    char     wallpaperPath[64]; ///< Custom wallpaper image path on SD or Flash
    uint16_t touchMinX;         ///< Calibrated XPT2046 raw X min
    uint16_t touchMaxX;         ///< Calibrated XPT2046 raw X max
    uint16_t touchMinY;         ///< Calibrated XPT2046 raw Y min
    uint16_t touchMaxY;         ///< Calibrated XPT2046 raw Y max
    bool     touchCalibrated;   ///< True once user has completed touch calibration

    SystemConfig()
        : brightness(CONFIG_DEFAULT_BRIGHTNESS)
        , sleepTimeoutSec(CONFIG_DEFAULT_SLEEP_TIMEOUT)
        , theme(CONFIG_DEFAULT_THEME)
        , soundEnabled(CONFIG_DEFAULT_SOUND_ENABLED)
        , ledEnabled(CONFIG_DEFAULT_LED_ENABLED)
        , logInfoEnabled(CONFIG_DEFAULT_LOG_INFO)
        , logWarnEnabled(CONFIG_DEFAULT_LOG_WARN)
        , devModeEnabled(CONFIG_DEFAULT_DEV_MODE)
        , pinEnabled(CONFIG_DEFAULT_PIN_ENABLED)
        , wifiEnabled(CONFIG_DEFAULT_WIFI_ENABLED)
        , wallpaperId(0)
        , touchMinX(240)
        , touchMaxX(3800)
        , touchMinY(200)
        , touchMaxY(3700)
        , touchCalibrated(false) {
        strncpy(pinCode, CONFIG_DEFAULT_PIN_CODE, sizeof(pinCode) - 1);
        pinCode[sizeof(pinCode) - 1] = '\0';
        wifiSsid[0] = '\0';
        wifiPass[0] = '\0';
        region[0] = '\0';
        wallpaperPath[0] = '\0';
    }

    SystemConfig(uint8_t b, uint16_t s, uint8_t t, bool snd, bool led = true, bool lInfo = true, bool lWarn = true, bool dev = false)
        : brightness(b), sleepTimeoutSec(s), theme(t), soundEnabled(snd), ledEnabled(led), logInfoEnabled(lInfo), logWarnEnabled(lWarn), devModeEnabled(dev), pinEnabled(false), wifiEnabled(false), wallpaperId(0), touchMinX(240), touchMaxX(3800), touchMinY(200), touchMaxY(3700), touchCalibrated(false) {
        strncpy(pinCode, CONFIG_DEFAULT_PIN_CODE, sizeof(pinCode) - 1);
        pinCode[sizeof(pinCode) - 1] = '\0';
        wifiSsid[0] = '\0';
        wifiPass[0] = '\0';
        region[0] = '\0';
        wallpaperPath[0] = '\0';
    }
};

/**
 * @brief Singleton Manager for persistent system configuration
 */
class Config {
public:
    static Config& getInstance();

    bool begin();
    bool load();
    bool save();
    void resetDefaults();

    // In-memory Getters
    uint8_t  getBrightness() const       { return m_config.brightness; }
    uint16_t getSleepTimeoutSec() const  { return m_config.sleepTimeoutSec; }
    uint8_t  getTheme() const            { return m_config.theme; }
    bool     isSoundEnabled() const      { return m_config.soundEnabled; }
    bool     isLEDEnabled() const        { return m_config.ledEnabled; }
    bool     isLogInfoEnabled() const    { return m_config.logInfoEnabled; }
    bool     isLogWarnEnabled() const    { return m_config.logWarnEnabled; }
    bool     isDevModeEnabled() const    { return m_config.devModeEnabled; }
    bool     isPinEnabled() const        { return m_config.pinEnabled; }
    const char* getPinCode() const       { return m_config.pinCode; }
    bool     isWifiEnabled() const       { return m_config.wifiEnabled; }
    const char* getWifiSsid() const      { return m_config.wifiSsid; }
    const char* getWifiPass() const      { return m_config.wifiPass; }
    const char* getRegion() const        { return m_config.region; }
    bool     hasRegion() const           { return m_config.region[0] != '\0'; }
    uint8_t  getWallpaperId() const      { return m_config.wallpaperId; }
    const char* getWallpaperPath() const { return m_config.wallpaperPath; }
    bool     isTouchCalibrated() const   { return m_config.touchCalibrated; }
    void     getTouchCal(uint16_t &minX, uint16_t &maxX, uint16_t &minY, uint16_t &maxY) const {
        minX = m_config.touchMinX;
        maxX = m_config.touchMaxX;
        minY = m_config.touchMinY;
        maxY = m_config.touchMaxY;
    }
    const SystemConfig& get() const      { return m_config; }
    bool     isDirty() const             { return m_dirty; }

    // In-memory Setters with optional immediate auto-save
    void setBrightness(uint8_t val, bool autoSave = false);
    void setSleepTimeoutSec(uint16_t val, bool autoSave = false);
    void setTheme(uint8_t val, bool autoSave = false);
    void setSoundEnabled(bool val, bool autoSave = false);
    void setLEDEnabled(bool val, bool autoSave = false);
    void setLogInfoEnabled(bool val, bool autoSave = false);
    void setLogWarnEnabled(bool val, bool autoSave = false);
    void setDevModeEnabled(bool val, bool autoSave = false);
    void setPinEnabled(bool val, bool autoSave = false);
    void setPinCode(const char* pin, bool autoSave = false);
    void setWifiEnabled(bool val, bool autoSave = false);
    void setWifiSsid(const char* ssid, bool autoSave = false);
    void setWifiPass(const char* pass, bool autoSave = false);
    void setRegion(const char* region, bool autoSave = false);
    void setWallpaperId(uint8_t id, bool autoSave = false);
    void setWallpaperPath(const char* path, bool autoSave = false);
    void setTouchCal(uint16_t minX, uint16_t maxX, uint16_t minY, uint16_t maxY, bool autoSave = true);
    void set(const SystemConfig& cfg, bool autoSave = false);

private:
    Config();
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    void clampConfig(SystemConfig& cfg);

    SystemConfig m_config;
    bool         m_dirty;
    bool         m_initialized;
};

// =========================================================================
// PROJECT.md Contract § 4 Compatibility Wrapper
// =========================================================================
namespace config {
    void loadConfig(SystemConfig &cfg);
    void saveConfig(const SystemConfig &cfg);
    void resetDefaults();
}
