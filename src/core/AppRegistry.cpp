/**
 * @file AppRegistry.cpp
 * @brief Implementation of Dynamic Application Discovery & Lazy On-Demand Lifecycle Registry
 *
 * Memory Optimization Architecture:
 *   - At boot, ONLY lightweight AppDescriptor metadata is registered (0 App instances in RAM).
 *   - Native apps (Settings, Play Store, App Mgr, Wallpapers, SU7 3D) and Lua apps are
 *     instantiated on-demand ONLY when opened via launchApp(), and freed immediately when exited!
 *   - Home Screen only displays installed apps in /apps/ (SD card scripts must be installed first).
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
static App* s_deferredDelete = nullptr;

static void flushDeferredDelete(const App* keepAlive = nullptr) {
    if (s_deferredDelete && s_deferredDelete != keepAlive) {
        delete s_deferredDelete;
        s_deferredDelete = nullptr;
    }
}

static void retireAppInstance(App* app) {
    if (!app) return;
    flushDeferredDelete(app);
    s_deferredDelete = app;
}

// Lazy factory helper: instantiates an app ONLY when it is opened
static App* createLazyAppInstance(const AppDescriptor& desc) {
    if (strcasecmp(desc.id, "settings") == 0)   return new (std::nothrow) apps::SettingsApp();
    if (strcasecmp(desc.id, "playstore") == 0)  return new (std::nothrow) apps::PlayStoreApp();
    if (strcasecmp(desc.id, "appmgr") == 0)     return new (std::nothrow) apps::AppManagerApp();
    if (strcasecmp(desc.id, "wallpapers") == 0) return new (std::nothrow) apps::WallpaperApp();
    if (strcasecmp(desc.id, "su7") == 0)        return new (std::nothrow) apps::SU7App();
    if (desc.path[0] != '\0') {
        return new (std::nothrow) DynamicScriptApp(desc.path, desc.name, desc.icon);
    }
    return nullptr;
}

void AppRegistry::capitalize(char* str) {
    if (str && str[0] >= 'a' && str[0] <= 'z') {
        str[0] = str[0] - ('a' - 'A');
    }
}

void AppRegistry::init() {
    if (s_initialized) return;

    clear();

    // Register native system application descriptors lazily (app = nullptr until opened!)
    registerApp(nullptr, "settings",   "Settings",   "ICON_SETTINGS",  0x324B, TFT_WHITE, "", true);
    registerApp(nullptr, "playstore",  "Play Store", "ICON_PLAYSTORE", 0x0276, TFT_WHITE, "", true);
    registerApp(nullptr, "appmgr",     "App Mgr",    "ICON_APPMGR",    0x04D3, TFT_WHITE, "", true);
    registerApp(nullptr, "wallpapers", "Wallpapers", "ICON_WALLPAPER", 0x6815, TFT_WHITE, "", true);
    registerApp(nullptr, "su7",        "SU7 3D",     "ICON_SU7",       0xFEE0, 0x10A2,    "", true);

    // Pre-register core bundled apps in clean launcher page order (app = nullptr until opened!)
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
                registerApp(nullptr, CORE_APPS[i].id, CORE_APPS[i].name, "ICON_LUA",
                            CORE_APPS[i].bg, CORE_APPS[i].fg, CORE_APPS[i].path, true);
            }
        }
    }

    // Discover any additional installed apps in internal Flash (/apps/)
    scanFilesystemApps();

    s_initialized = true;
}

void AppRegistry::registerApp(App* app) {
    if (!app || s_appCount >= MAX_REGISTERED_APPS) return;

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
    if (s_appCount >= MAX_REGISTERED_APPS) return;

    // Idempotent registration check
    for (size_t i = 0; i < s_appCount; i++) {
        if (app && s_apps[i].app == app) return;
        if (path && path[0] != '\0' && strcmp(s_apps[i].path, path) == 0) return;
        if (id && id[0] != '\0' && (!path || path[0] == '\0') && strcasecmp(s_apps[i].id, id) == 0) return;
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

    if (!StorageHAL::isLittleFSMounted()) return;

    // 1. Prune any previously registered /apps/*.lua entry whose file was uninstalled/deleted
    size_t idx = 0;
    while (idx < s_appCount) {
        if (s_apps[idx].path[0] != '\0' && !StorageHAL::fileExists(s_apps[idx].path) &&
            s_activeAppIndex != (int)idx) {
            if (s_apps[idx].isDynamic && s_apps[idx].app) {
                delete s_apps[idx].app;
                s_apps[idx].app = nullptr;
            }
            for (size_t k = idx; k + 1 < s_appCount; k++) {
                s_apps[k] = s_apps[k + 1];
            }
            s_appCount--;
            if (s_activeAppIndex > (int)idx) {
                s_activeAppIndex--;
            }
        } else {
            idx++;
        }
    }

    // 2. Scan ONLY internal LittleFS (/apps/) for installed applications.
    // SD card apps (/sd/apps/) are NOT shown on Home Screen until installed to /apps/!
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

        // Capitalize fallback title
        char capName[24];
        strncpy(capName, baseName.c_str(), sizeof(capName) - 1);
        capName[sizeof(capName) - 1] = '\0';
        capitalize(capName);

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

        // Check if already registered; if so, refresh its display name & bg color
        bool exists = false;
        for (size_t j = 0; j < s_appCount; j++) {
            if (strcmp(s_apps[j].path, cstr) == 0) {
                strncpy(s_apps[j].name, displayName, sizeof(s_apps[j].name) - 1);
                s_apps[j].name[sizeof(s_apps[j].name) - 1] = '\0';
                s_apps[j].iconBgColor = bgColor;
                exists = true;
                break;
            }
        }
        if (exists) continue;

        if (s_appCount >= MAX_REGISTERED_APPS) break;

        // Register descriptor lazily (app = nullptr until launched!)
        registerApp(nullptr, baseName.c_str(), displayName, "ICON_LUA", bgColor, TFT_WHITE, cstr, true);
    }
}

void AppRegistry::rescan() {
    scanFilesystemApps();
}

void AppRegistry::clear() {
    flushDeferredDelete();
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
        if (!s_apps[index].app && s_apps[index].isDynamic) {
            s_apps[index].app = createLazyAppInstance(s_apps[index]);
        }
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

    // If another app is active, pause & retire it safely (so 'this' is never deleted mid-callback)
    App* prevRunning = nullptr;
    if (s_activeAppIndex >= 0 && s_activeAppIndex != (int)index && (size_t)s_activeAppIndex < s_appCount) {
        AppDescriptor &prev = s_apps[s_activeAppIndex];
        prevRunning = prev.app;
        if (prev.app) {
            prev.app->onPause();
            prev.app->onDestroy();
            if (prev.isDynamic) {
                retireAppInstance(prev.app);
                prev.app = nullptr;
            }
        }
        s_activeAppIndex = -1;
    } else {
        flushDeferredDelete();
    }

    // Lazily instantiate the target app on demand!
    if (!s_apps[index].app && s_apps[index].isDynamic) {
        s_apps[index].app = createLazyAppInstance(s_apps[index]);
    }

    App* target = s_apps[index].app;
    if (!target) {
        return false;
    }

    if (target->getState() == AppState::UNINITIALIZED) {
        target->onCreate();
    }
    target->onResume();
    s_activeAppIndex = (int)index;
    (void)prevRunning;

    return true;
}

bool AppRegistry::exitCurrentApp() {
    if (s_activeAppIndex >= 0 && (size_t)s_activeAppIndex < s_appCount) {
        AppDescriptor &cur = s_apps[s_activeAppIndex];
        if (cur.app) {
            if (cur.app->getState() == AppState::RUNNING) {
                cur.app->onPause();
            }
            if (cur.app->getState() != AppState::DESTROYED) {
                cur.app->onDestroy();
            }
            if (cur.isDynamic) {
                retireAppInstance(cur.app);
                cur.app = nullptr;
            }
        }
        s_activeAppIndex = -1;
        return true;
    }
    flushDeferredDelete();
    return false;
}

} // namespace core
