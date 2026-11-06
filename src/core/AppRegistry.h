/**
 * @file AppRegistry.h
 * @brief Dynamic Application Discovery, Lifecycle Registry & Sandboxing
 * 
 * Conforms to Features F08, F09, F10.
 */

#pragma once

#include <Arduino.h>
#include "App.h"
#include "hal/StorageHAL.h"

namespace core {

/**
 * @brief Dynamic Script Application container for discovered .lua / .app bundles.
 */
class DynamicScriptApp : public App {
private:
    char m_name[24];
    char m_icon[16];
    char m_path[64];
    bool m_started = false;   ///< script VM has been launched
    bool m_finished = false;  ///< script returned / errored / aborted
    bool m_aborted = false;   ///< user pressed Back/Home while script ran
    bool m_finalDrawn = false;
    char m_error[120] = {0};

    void runScript();

public:
    DynamicScriptApp(const char* path, const char* name, const char* icon = "ICON_LUA") {
        strncpy(m_path, path ? path : "", sizeof(m_path) - 1);
        m_path[sizeof(m_path) - 1] = '\0';
        strncpy(m_name, name ? name : "Script", sizeof(m_name) - 1);
        m_name[sizeof(m_name) - 1] = '\0';
        strncpy(m_icon, icon ? icon : "ICON_LUA", sizeof(m_icon) - 1);
        m_icon[sizeof(m_icon) - 1] = '\0';
    }

    const char* getName() const override { return m_name; }
    const char* getIcon() const override { return m_icon; }
    const char* getCategory() const override { return "Script"; }
    const char* getPath() const { return m_path; }

    void onResume() override;
    void onUpdate() override;
    void onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) override;
    void onTouch(uint16_t x, uint16_t y, bool pressed) override;
    void onPause() override;

    /// True while a Lua VM is executing (UI must not tear the app down).
    static bool isScriptRunning();
    /// Ask the running script to stop at the next hook/delay.
    static void requestAbort();
};
/**
 * @brief Registration entry descriptor.
 */
struct AppDescriptor {
    App* app;
    char id[24];
    char name[24];
    char path[64];
    char icon[16];
    uint16_t iconBgColor;
    uint16_t iconFgColor;
    bool isDynamic;
};

/**
 * @brief Application catalog, filesystem discovery scanner, and lifecycle manager.
 */
class AppRegistry {
public:
    static constexpr size_t MAX_REGISTERED_APPS = 24;

    /**
     * @brief Initialize registry table and populate native system applications.
     */
    static void init();

    /**
     * @brief Register a pre-instantiated App pointer (PROJECT.md Contract).
     */
    static void registerApp(App* app);

    /**
     * @brief Register an application with full descriptor metadata.
     */
    static void registerApp(App* app, const char* id, const char* name, const char* icon,
                            uint16_t iconBgColor, uint16_t iconFgColor, const char* path = "",
                            bool isDynamic = false);

    /**
     * @brief Scan filesystem (/apps/ on LittleFS and /sd/apps/ on SD) for apps.
     * Conforms to PROJECT.md Contract and test suite scanFilesystemApps(storage).
     */
    static void scanFilesystemApps(StorageHAL* storage = nullptr);

    /**
     * @brief Rescan all storage devices and refresh registered app catalog.
     */
    static void rescan();

    /**
     * @brief Reset registry and free dynamic instances.
     */
    static void clear();
    static void gc();

    // -------------------------------------------------------------
    // Query APIs
    // -------------------------------------------------------------
    static size_t getAppCount();
    static App* getApp(size_t index);
    static const AppDescriptor* getAppInfo(size_t index);
    static App* getActiveApp();
    static int getActiveAppIndex();
    static int findAppIndex(const char* nameOrId);

    // -------------------------------------------------------------
    // Lifecycle Switching APIs
    // -------------------------------------------------------------
    /**
     * @brief Launch an application by index.
     * Enforces >40 KB contiguous free heap, pauses previous app, invokes onCreate/onResume.
     * @param index Slot index in registry
     * @return true if launch succeeded, false on invalid index or OOM
     */
    static bool launchApp(size_t index);

    /**
     * @brief Exit currently running application and return safely to Launcher.
     * Invokes onPause().
     * @return true if an app was exited, false if no app was active
     */
    static bool exitCurrentApp();

private:
    static AppDescriptor s_apps[MAX_REGISTERED_APPS];
    static size_t s_appCount;
    static int s_activeAppIndex;
    static bool s_initialized;

    static void capitalize(char* str);
};

} // namespace core

namespace ui {
    using core::AppRegistry;
    using core::AppDescriptor;
    using core::DynamicScriptApp;
}

