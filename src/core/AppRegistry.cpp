/**
 * @file AppRegistry.cpp
 * @brief Implementation of Dynamic Application Discovery & Lifecycle Registry
 */

#include "AppRegistry.h"
#include "LuaIconCodec.h"
#include "apps/SettingsApp.h"
#include "apps/PlayStoreApp.h"
#include "apps/AppManagerApp.h"
#include "apps/WallpaperApp.h"
#include "apps/SU7App.h"
#include "ui/UIManager.h"

namespace core {

AppDescriptor AppRegistry::s_apps[MAX_REGISTERED_APPS];
size_t AppRegistry::s_appCount = 0;
int AppRegistry::s_activeAppIndex = -1;
bool AppRegistry::s_initialized = false;

// Native static application instances (zero dynamic allocations at launch)
static apps::SettingsApp   s_nativeSettings;
static apps::PlayStoreApp  s_nativePlayStore;
static apps::AppManagerApp s_nativeAppMgr;
static apps::WallpaperApp  s_nativeWallpapers;
static apps::SU7App        s_nativeSU7;

void AppRegistry::capitalize(char* str) {
    if (str && str[0] >= 'a' && str[0] <= 'z') {
        str[0] = str[0] - ('a' - 'A');
    }
}

void AppRegistry::init() {
    if (s_initialized) return;

    clear();

    // Register native system applications (About is now integrated into Settings)
    registerApp(&s_nativeSettings,   "settings",   "Settings",   "ICON_SETTINGS",  0x324B, TFT_WHITE, "", false);
    registerApp(&s_nativePlayStore,  "playstore",  "Play Store", "ICON_PLAYSTORE", 0x0276, TFT_WHITE, "", false);
    registerApp(&s_nativeAppMgr,     "appmgr",     "App Mgr",    "ICON_APPMGR",    0x04D3, TFT_WHITE, "", false);
    registerApp(&s_nativeWallpapers, "wallpapers", "Wallpapers", "ICON_WALLPAPER", 0x6815, TFT_WHITE, "", false);
    registerApp(&s_nativeSU7,        "su7",        "SU7 3D",     "ICON_SU7",       0xFEE0, 0x10A2,    "", false);

    // Pre-register core bundled apps in clean launcher page order:
    // Page 1 (6 apps): Settings, SU7 3D, Gallery, Files, SysInfo, Calc
    // Page 2 (2+ apps): Paint, Lua IDE, + any installed SD apps
    if (StorageHAL::isLittleFSMounted()) {
        struct CoreLuaApp {
            const char* id;
            const char* name;
            const char* path;
            uint16_t bg;
            uint16_t fg;
        };
        static const CoreLuaApp CORE_APPS[] = {
            { "gallery",  "Gallery",  "/apps/gallery.lua",  0xD8A7, TFT_WHITE },
            { "files",    "Files",    "/apps/files.lua",    0xFD00, TFT_WHITE },
            { "sysinfo",  "SysInfo",  "/apps/sysinfo.lua",  0x05FE, TFT_BLACK },
            { "calc",     "Calc",     "/apps/calc.lua",     0x2C9F, TFT_WHITE },
            { "paint",    "Paint",    "/apps/paint.lua",    0x9175, TFT_WHITE },
            { "terminal", "Lua IDE",  "/apps/terminal.lua", 0x1CE6, TFT_WHITE }
        };
        for (size_t i = 0; i < sizeof(CORE_APPS) / sizeof(CORE_APPS[0]); i++) {
            if (StorageHAL::fileExists(CORE_APPS[i].path)) {
                DynamicScriptApp* dynApp = new DynamicScriptApp(CORE_APPS[i].path, CORE_APPS[i].name, "ICON_LUA");
                registerApp(dynApp, CORE_APPS[i].id, CORE_APPS[i].name, "ICON_LUA",
                            CORE_APPS[i].bg, CORE_APPS[i].fg, CORE_APPS[i].path, true);
            }
        }
    }

    // Discover any additional installed apps on LittleFS (/apps/) and MicroSD
    scanFilesystemApps();

    s_initialized = true;
}

void AppRegistry::registerApp(App* app) {
    if (!app || s_appCount >= MAX_REGISTERED_APPS) return;

    // Check if already registered
    for (size_t i = 0; i < s_appCount; i++) {
        if (s_apps[i].app == app) return;
    }

    const char* name = app->getName();
    const char* icon = app->getIcon();

    registerApp(app, name, name, icon, 0x18C3, TFT_WHITE, "", false);
}

void AppRegistry::registerApp(App* app, const char* id, const char* name, const char* icon,
                            uint16_t iconBgColor, uint16_t iconFgColor, const char* path,
                            bool isDynamic) {
    if (!app || s_appCount >= MAX_REGISTERED_APPS) return;

    // Idempotent registration check
    for (size_t i = 0; i < s_appCount; i++) {
        if (s_apps[i].app == app) return;
        if (path && path[0] != '\0' && strcmp(s_apps[i].path, path) == 0) return;
    }

    AppDescriptor &desc = s_apps[s_appCount++];
    desc.app = app;
    strncpy(desc.id, id ? id : "app", sizeof(desc.id) - 1);
    desc.id[sizeof(desc.id) - 1] = '\0';
    strncpy(desc.name, name ? name : "App", sizeof(desc.name) - 1);
    desc.name[sizeof(desc.name) - 1] = '\0';
    strncpy(desc.path, path ? path : "", sizeof(desc.path) - 1);
    desc.path[sizeof(desc.path) - 1] = '\0';
    strncpy(desc.icon, icon ? icon : "app", sizeof(desc.icon) - 1);
    desc.icon[sizeof(desc.icon) - 1] = '\0';
    desc.iconBgColor = iconBgColor;
    desc.iconFgColor = iconFgColor;
    desc.isDynamic = isDynamic;
}

void AppRegistry::scanFilesystemApps(StorageHAL* storage) {
    (void)storage;

    // 1. Scan LittleFS (/apps/)
    if (StorageHAL::isLittleFSMounted()) {
        std::vector<String> files;
        StorageHAL::listDirectory("/apps", files, nullptr);

        for (size_t i = 0; i < files.size(); i++) {
            const String &p = files[i];
            const char* cstr = p.c_str();

            // Ignore nested subdirectories (e.g. /apps/lib/sub.lua)
            if (strncmp(cstr, "/apps/", 6) == 0 && strchr(cstr + 6, '/') != nullptr) {
                continue;
            }

            // Must end with .lua or .app (case-insensitive)
            String lowerP = p;
            lowerP.toLowerCase();
            if (!lowerP.endsWith(".lua") && !lowerP.endsWith(".app")) {
                continue;
            }

            // Extract filename
            int lastSlash = p.lastIndexOf('/');
            String filename = (lastSlash >= 0) ? p.substring(lastSlash + 1) : p;

            // Remove extension
            int dotIdx = filename.lastIndexOf('.');
            String baseName = (dotIdx >= 0) ? filename.substring(0, dotIdx) : filename;

            // Capitalize
            char capName[32];
            strncpy(capName, baseName.c_str(), sizeof(capName) - 1);
            capName[sizeof(capName) - 1] = '\0';
            capitalize(capName);

            // Check if already registered (or upgrade an SD entry to local /apps/ entry)
            bool exists = false;
            for (size_t j = 0; j < s_appCount; j++) {
                if (strcmp(s_apps[j].path, cstr) == 0) {
                    exists = true;
                    break;
                }
                if (strcasecmp(s_apps[j].id, baseName.c_str()) == 0 && strncmp(s_apps[j].path, "/sd/", 4) == 0) {
                    if (s_apps[j].isDynamic && s_apps[j].app && s_activeAppIndex != (int)j) {
                        delete s_apps[j].app;
                    }
                    s_apps[j].app = new DynamicScriptApp(cstr, capName, "ICON_LUA");
                    strncpy(s_apps[j].name, capName, sizeof(s_apps[j].name) - 1);
                    s_apps[j].name[sizeof(s_apps[j].name) - 1] = '\0';
                    strncpy(s_apps[j].path, cstr, sizeof(s_apps[j].path) - 1);
                    s_apps[j].path[sizeof(s_apps[j].path) - 1] = '\0';
                    s_apps[j].iconBgColor = 0x04D3;
                    exists = true;
                    break;
                }
            }
            if (exists) continue;

            if (s_appCount >= MAX_REGISTERED_APPS) break;

            // Assign distinct color palette & check Lua script header (-- @name, -- @bg)
            uint16_t bgColor = 0x04D3; // Installed Local App Teal
            char customName[24] = {0};
            LuaIconCodec::parseScriptHeader(cstr, customName, sizeof(customName), &bgColor, nullptr, 0);

            const char* displayName = (customName[0] != '\0') ? customName : capName;
            if (strcasecmp(baseName.c_str(), "gallery") == 0) bgColor = 0xD8A7;
            else if (strcasecmp(baseName.c_str(), "calc") == 0) bgColor = 0x2C9F;
            else if (strcasecmp(baseName.c_str(), "paint") == 0) bgColor = 0x9175;
            else if (strcasecmp(baseName.c_str(), "sysinfo") == 0) bgColor = 0x05FE;
            else if (strcasecmp(baseName.c_str(), "files") == 0) bgColor = 0xFD00;
            else if (strcasecmp(baseName.c_str(), "terminal") == 0) {
                bgColor = 0x1CE6;
                displayName = "Lua IDE";
            }

            DynamicScriptApp* dynApp = new DynamicScriptApp(cstr, displayName, "ICON_LUA");
            registerApp(dynApp, baseName.c_str(), displayName, "ICON_LUA", bgColor, TFT_WHITE, cstr, true);
        }
    }

    // 2. Scan MicroSD (/sd and /sd/apps/) if mounted
    if (StorageHAL::isSDMounted()) {
        std::vector<String> sdFiles;
        StorageHAL::listDirectory("/sd", sdFiles, nullptr);
        std::vector<String> sdSubFiles;
        StorageHAL::listDirectory("/sd/apps", sdSubFiles, nullptr);
        for (size_t k = 0; k < sdSubFiles.size(); k++) {
            sdFiles.push_back(sdSubFiles[k]);
        }

        for (size_t i = 0; i < sdFiles.size(); i++) {
            const String &p = sdFiles[i];
            const char* cstr = p.c_str();

            // Must end with .lua or .app (case-insensitive)
            String lowerP = p;
            lowerP.toLowerCase();
            if (!lowerP.endsWith(".lua") && !lowerP.endsWith(".app")) {
                continue;
            }

            int lastSlash = p.lastIndexOf('/');
            String filename = (lastSlash >= 0) ? p.substring(lastSlash + 1) : p;

            int dotIdx = filename.lastIndexOf('.');
            String baseName = (dotIdx >= 0) ? filename.substring(0, dotIdx) : filename;

            char capName[20];
            strncpy(capName, baseName.c_str(), sizeof(capName) - 1);
            capName[sizeof(capName) - 1] = '\0';
            capitalize(capName);

            uint16_t sdBg = 0xFD00;
            char customSdName[24] = {0};
            LuaIconCodec::parseScriptHeader(cstr, customSdName, sizeof(customSdName), &sdBg, nullptr, 0);

            // Use custom -- @name if present, otherwise prefix with "SD-"
            char sdName[24];
            if (customSdName[0] != '\0') {
                strncpy(sdName, customSdName, sizeof(sdName) - 1);
                sdName[sizeof(sdName) - 1] = '\0';
            } else {
                snprintf(sdName, sizeof(sdName), "SD-%.19s", capName);
            }

            bool exists = false;
            for (size_t j = 0; j < s_appCount; j++) {
                if (strcmp(s_apps[j].path, cstr) == 0 || strcasecmp(s_apps[j].id, baseName.c_str()) == 0) {
                    exists = true;
                    break;
                }
            }
            if (exists) continue;

            if (s_appCount >= MAX_REGISTERED_APPS) break;

            DynamicScriptApp* dynApp = new DynamicScriptApp(cstr, sdName, "ICON_LUA");
            registerApp(dynApp, baseName.c_str(), sdName, "ICON_LUA", sdBg, TFT_WHITE, cstr, true);
        }
    } else {
        // SD is unmounted: prune all registered apps with path starting with /sd/
        size_t idx = 0;
        while (idx < s_appCount) {
            if (strncmp(s_apps[idx].path, "/sd/", 4) == 0) {
                if (s_apps[idx].isDynamic && s_apps[idx].app) {
                    delete s_apps[idx].app;
                }
                for (size_t k = idx; k + 1 < s_appCount; k++) {
                    s_apps[k] = s_apps[k + 1];
                }
                s_appCount--;
                if (s_activeAppIndex == (int)idx) s_activeAppIndex = -1;
                else if (s_activeAppIndex > (int)idx) s_activeAppIndex--;
            } else {
                idx++;
            }
        }
    }
}

void AppRegistry::rescan() {
    scanFilesystemApps();
}

void AppRegistry::clear() {
    for (size_t i = 0; i < s_appCount; i++) {
        if (s_apps[i].isDynamic && s_apps[i].app) {
            delete s_apps[i].app;
            s_apps[i].app = nullptr;
        }
    }
    s_appCount = 0;
    s_activeAppIndex = -1;
    s_initialized = false;
}

size_t AppRegistry::getAppCount() {
    return s_appCount;
}

App* AppRegistry::getApp(size_t index) {
    if (index < s_appCount) {
        return s_apps[index].app;
    }
    return nullptr;
}

const AppDescriptor* AppRegistry::getAppInfo(size_t index) {
    if (index < s_appCount) {
        return &s_apps[index];
    }
    return nullptr;
}

App* AppRegistry::getActiveApp() {
    if (s_activeAppIndex >= 0 && (size_t)s_activeAppIndex < s_appCount) {
        return s_apps[s_activeAppIndex].app;
    }
    return nullptr;
}

int AppRegistry::getActiveAppIndex() {
    return s_activeAppIndex;
}

int AppRegistry::findAppIndex(const char* nameOrId) {
    if (!nameOrId) return -1;
    for (size_t i = 0; i < s_appCount; i++) {
        if (strcasecmp(s_apps[i].id, nameOrId) == 0 || strcasecmp(s_apps[i].name, nameOrId) == 0) {
            return (int)i;
        }
    }
    return -1;
}

bool AppRegistry::launchApp(size_t index) {
    if (index >= s_appCount) {
        return false;
    }

    // Hardware Sandboxing: enforce >40 KB contiguous free heap
    uint32_t largestBlock = ESP.getMaxAllocHeap();
    if (largestBlock < 40960) {
        Serial.printf("[AppRegistry] OOM Prevention: contiguous heap %u < 40960 B. Launch aborted.\n",
                      (unsigned)largestBlock);
        ui::UIManager::showToast("Low RAM: Cannot Launch App", 2000);
        return false;
    }

    // If another app is active, pause it
    if (s_activeAppIndex >= 0 && s_activeAppIndex != (int)index && (size_t)s_activeAppIndex < s_appCount) {
        s_apps[s_activeAppIndex].app->onPause();
    }

    App* target = s_apps[index].app;
    if (target->getState() == AppState::UNINITIALIZED) {
        target->onCreate();
    }
    target->onResume();
    s_activeAppIndex = (int)index;

    return true;
}

bool AppRegistry::exitCurrentApp() {
    if (s_activeAppIndex < 0 || (size_t)s_activeAppIndex >= s_appCount) {
        return false;
    }

    s_apps[s_activeAppIndex].app->onPause();
    s_activeAppIndex = -1;
    return true;
}

} // namespace core
