/**
 * @file Config.cpp
 * @brief Implementation of NVS Configuration Store using ESP32 Preferences
 */

#include "Config.h"
#include <Preferences.h>

Config::Config()
    : m_config()
    , m_dirty(false)
    , m_initialized(false) {
}

Config& Config::getInstance() {
    static Config s_instance;
    return s_instance;
}

void Config::clampConfig(SystemConfig& cfg) {
    if (cfg.brightness < CONFIG_MIN_BRIGHTNESS) cfg.brightness = CONFIG_MIN_BRIGHTNESS;
    if (cfg.brightness > CONFIG_MAX_BRIGHTNESS) cfg.brightness = CONFIG_MAX_BRIGHTNESS;

    if (cfg.sleepTimeoutSec > CONFIG_MAX_SLEEP_TIMEOUT) cfg.sleepTimeoutSec = CONFIG_MAX_SLEEP_TIMEOUT;

    cfg.theme = (cfg.theme == 1) ? 1 : 0;

    // Validate 4-digit numeric PIN
    if (strlen(cfg.pinCode) != 4) {
        strncpy(cfg.pinCode, CONFIG_DEFAULT_PIN_CODE, sizeof(cfg.pinCode) - 1);
        cfg.pinCode[sizeof(cfg.pinCode) - 1] = '\0';
    } else {
        for (int i = 0; i < 4; i++) {
            if (cfg.pinCode[i] < '0' || cfg.pinCode[i] > '9') {
                cfg.pinCode[i] = '0';
            }
        }
        cfg.pinCode[4] = '\0';
    }

    if (cfg.wallpaperId > 5 && cfg.wallpaperId != CONFIG_WALLPAPER_CUSTOM_SD) {
        cfg.wallpaperId = 0;
    }
}

bool Config::begin() {
    if (m_initialized) return true;
    bool ok = load();
    m_initialized = true;
    return ok;
}

bool Config::load() {
    Preferences prefs;
    if (!prefs.begin(CONFIG_NVS_NAMESPACE, true)) {
        resetDefaults();
        return true;
    }

    m_config.brightness       = prefs.getUChar("brightness", CONFIG_DEFAULT_BRIGHTNESS);
    m_config.sleepTimeoutSec   = prefs.getUShort("sleepTimeoutSec", CONFIG_DEFAULT_SLEEP_TIMEOUT);
    m_config.theme             = prefs.getUChar("theme", CONFIG_DEFAULT_THEME);
    m_config.soundEnabled      = prefs.getBool("soundEnabled", CONFIG_DEFAULT_SOUND_ENABLED);
    m_config.ledEnabled        = prefs.getBool("ledEnabled", CONFIG_DEFAULT_LED_ENABLED);
    m_config.logInfoEnabled    = prefs.getBool("logInfo", CONFIG_DEFAULT_LOG_INFO);
    m_config.logWarnEnabled    = prefs.getBool("logWarn", CONFIG_DEFAULT_LOG_WARN);
    m_config.devModeEnabled    = prefs.getBool("devMode", CONFIG_DEFAULT_DEV_MODE);
    m_config.pinEnabled        = prefs.getBool("pinEn", CONFIG_DEFAULT_PIN_ENABLED);
    m_config.wifiEnabled       = prefs.getBool("wifiEn", CONFIG_DEFAULT_WIFI_ENABLED);
    m_config.wallpaperId       = prefs.getUChar("wpId", 0);

    String savedPin = prefs.getString("pinCode", CONFIG_DEFAULT_PIN_CODE);
    strncpy(m_config.pinCode, savedPin.c_str(), sizeof(m_config.pinCode) - 1);
    m_config.pinCode[sizeof(m_config.pinCode) - 1] = '\0';

    String savedSsid = prefs.getString("wifiSsid", "");
    strncpy(m_config.wifiSsid, savedSsid.c_str(), sizeof(m_config.wifiSsid) - 1);
    m_config.wifiSsid[sizeof(m_config.wifiSsid) - 1] = '\0';

    String savedPass = prefs.getString("wifiPass", "");
    strncpy(m_config.wifiPass, savedPass.c_str(), sizeof(m_config.wifiPass) - 1);
    m_config.wifiPass[sizeof(m_config.wifiPass) - 1] = '\0';

    String savedRegion = prefs.getString("region", "");
    strncpy(m_config.region, savedRegion.c_str(), sizeof(m_config.region) - 1);
    m_config.region[sizeof(m_config.region) - 1] = '\0';

    String savedWpPath = prefs.getString("wpPath", "");
    strncpy(m_config.wallpaperPath, savedWpPath.c_str(), sizeof(m_config.wallpaperPath) - 1);
    m_config.wallpaperPath[sizeof(m_config.wallpaperPath) - 1] = '\0';

    prefs.end();

    clampConfig(m_config);
    m_dirty = false;
    return true;
}

bool Config::save() {
    Preferences prefs;
    if (!prefs.begin(CONFIG_NVS_NAMESPACE, false)) {
        return false;
    }

    clampConfig(m_config);

    prefs.putUChar("brightness", m_config.brightness);
    prefs.putUShort("sleepTimeoutSec", m_config.sleepTimeoutSec);
    prefs.putUChar("theme", m_config.theme);
    prefs.putBool("soundEnabled", m_config.soundEnabled);
    prefs.putBool("ledEnabled", m_config.ledEnabled);
    prefs.putBool("logInfo", m_config.logInfoEnabled);
    prefs.putBool("logWarn", m_config.logWarnEnabled);
    prefs.putBool("devMode", m_config.devModeEnabled);
    prefs.putBool("pinEn", m_config.pinEnabled);
    prefs.putString("pinCode", m_config.pinCode);
    prefs.putBool("wifiEn", m_config.wifiEnabled);
    prefs.putString("wifiSsid", m_config.wifiSsid);
    prefs.putString("wifiPass", m_config.wifiPass);
    prefs.putString("region", m_config.region);
    prefs.putUChar("wpId", m_config.wallpaperId);
    prefs.putString("wpPath", m_config.wallpaperPath);
    prefs.end();

    m_dirty = false;
    return true;
}

void Config::resetDefaults() {
    m_config = SystemConfig();
    Preferences prefs;
    if (prefs.begin(CONFIG_NVS_NAMESPACE, false)) {
        prefs.clear();
        prefs.end();
    }
    save();
}

void Config::setBrightness(uint8_t val, bool autoSave) {
    if (val < CONFIG_MIN_BRIGHTNESS) val = CONFIG_MIN_BRIGHTNESS;
    if (val > CONFIG_MAX_BRIGHTNESS) val = CONFIG_MAX_BRIGHTNESS;
    if (m_config.brightness != val) {
        m_config.brightness = val;
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setSleepTimeoutSec(uint16_t val, bool autoSave) {
    if (val > CONFIG_MAX_SLEEP_TIMEOUT) val = CONFIG_MAX_SLEEP_TIMEOUT;
    if (m_config.sleepTimeoutSec != val) {
        m_config.sleepTimeoutSec = val;
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setTheme(uint8_t val, bool autoSave) {
    val = (val == 1) ? 1 : 0;
    if (m_config.theme != val) {
        m_config.theme = val;
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setSoundEnabled(bool val, bool autoSave) {
    if (m_config.soundEnabled != val) {
        m_config.soundEnabled = val;
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setLEDEnabled(bool val, bool autoSave) {
    if (m_config.ledEnabled != val) {
        m_config.ledEnabled = val;
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setLogInfoEnabled(bool val, bool autoSave) {
    if (m_config.logInfoEnabled != val) {
        m_config.logInfoEnabled = val;
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setLogWarnEnabled(bool val, bool autoSave) {
    if (m_config.logWarnEnabled != val) {
        m_config.logWarnEnabled = val;
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setDevModeEnabled(bool val, bool autoSave) {
    if (m_config.devModeEnabled != val) {
        m_config.devModeEnabled = val;
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setPinEnabled(bool val, bool autoSave) {
    if (m_config.pinEnabled != val) {
        m_config.pinEnabled = val;
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setPinCode(const char* pin, bool autoSave) {
    if (!pin) return;
    if (strncmp(m_config.pinCode, pin, 4) != 0) {
        strncpy(m_config.pinCode, pin, 4);
        m_config.pinCode[4] = '\0';
        clampConfig(m_config);
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setWifiEnabled(bool val, bool autoSave) {
    if (m_config.wifiEnabled != val) {
        m_config.wifiEnabled = val;
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setWifiSsid(const char* ssid, bool autoSave) {
    if (!ssid) return;
    if (strncmp(m_config.wifiSsid, ssid, sizeof(m_config.wifiSsid) - 1) != 0) {
        strncpy(m_config.wifiSsid, ssid, sizeof(m_config.wifiSsid) - 1);
        m_config.wifiSsid[sizeof(m_config.wifiSsid) - 1] = '\0';
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setWifiPass(const char* pass, bool autoSave) {
    if (!pass) return;
    if (strncmp(m_config.wifiPass, pass, sizeof(m_config.wifiPass) - 1) != 0) {
        strncpy(m_config.wifiPass, pass, sizeof(m_config.wifiPass) - 1);
        m_config.wifiPass[sizeof(m_config.wifiPass) - 1] = '\0';
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setRegion(const char* region, bool autoSave) {
    if (!region) return;
    if (strncmp(m_config.region, region, sizeof(m_config.region) - 1) != 0) {
        strncpy(m_config.region, region, sizeof(m_config.region) - 1);
        m_config.region[sizeof(m_config.region) - 1] = '\0';
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setWallpaperId(uint8_t id, bool autoSave) {
    if (m_config.wallpaperId != id) {
        m_config.wallpaperId = id;
        clampConfig(m_config);
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::setWallpaperPath(const char* path, bool autoSave) {
    if (!path) return;
    if (strncmp(m_config.wallpaperPath, path, sizeof(m_config.wallpaperPath) - 1) != 0) {
        strncpy(m_config.wallpaperPath, path, sizeof(m_config.wallpaperPath) - 1);
        m_config.wallpaperPath[sizeof(m_config.wallpaperPath) - 1] = '\0';
        m_dirty = true;
        if (autoSave) save();
    }
}

void Config::set(const SystemConfig& cfg, bool autoSave) {
    m_config = cfg;
    clampConfig(m_config);
    m_dirty = true;
    if (autoSave) save();
}

// =========================================================================
// PROJECT.md Contract § 4 Implementation
// =========================================================================
namespace config {
    void loadConfig(SystemConfig &cfg) {
        Config::getInstance().load();
        cfg = Config::getInstance().get();
    }

    void saveConfig(const SystemConfig &cfg) {
        Config::getInstance().set(cfg, true);
    }

    void resetDefaults() {
        Config::getInstance().resetDefaults();
    }
}
