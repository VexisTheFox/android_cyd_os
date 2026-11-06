#pragma once
#include <Arduino.h>
#include "calc_lua.h"
#include "paint_lua.h"
#include "sysinfo_lua.h"
#include "files_lua.h"
#include "terminal_lua.h"
#include "gallery_lua.h"

/**
 * @file default_apps.h
 * @brief Registry of bundled applications embedded in firmware PROGMEM.
 */

struct BundledAppScript {
    const char* filePath;      // Target path on LittleFS, e.g. "/apps/calc.lua"
    const char* progmemData;   // Pointer to Flash PROGMEM string literal
    const char* displayName;   // Human-readable app title
    const char* iconKey;       // Icon resource identifier
};

static const BundledAppScript BUNDLED_APP_SCRIPTS[] = {
    {
        "/apps/gallery.lua",
        BUNDLED_GALLERY_LUA,
        "Gallery",
        "gallery"
    },
    {
        "/apps/files.lua",
        BUNDLED_FILES_LUA,
        "Files",
        "files"
    },
    {
        "/apps/sysinfo.lua",
        BUNDLED_SYSINFO_LUA,
        "SysInfo",
        "sysinfo"
    },
    {
        "/apps/calc.lua",
        BUNDLED_CALC_LUA,
        "Calculator",
        "calc"
    },
    {
        "/apps/paint.lua",
        BUNDLED_PAINT_LUA,
        "Paint",
        "paint"
    },
    {
        "/apps/terminal.lua",
        BUNDLED_TERMINAL_LUA,
        "Lua IDE",
        "terminal"
    }
};

static const size_t NUM_BUNDLED_APP_SCRIPTS = sizeof(BUNDLED_APP_SCRIPTS) / sizeof(BUNDLED_APP_SCRIPTS[0]);
