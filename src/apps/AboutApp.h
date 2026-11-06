/**
 * @file AboutApp.h
 * @brief Android-Style Comprehensive About System & Telemetry Dashboard for CYD Android OS
 * 
 * Conforms to PROJECT.md Feature F21 and E2E Test Suite Specifications.
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "core/App.h"
#include "hal/StorageHAL.h"
#include "hal/DisplayHAL.h"

namespace apps {

class AboutApp : public core::App {
public:
    AboutApp();
    virtual ~AboutApp() = default;

    // App Identity & Metadata
    const char* getName() const override { return "About"; }
    const char* getIcon() const override { return "ICON_ABOUT"; }
    const char* getCategory() const override { return "System"; }

    // Lifecycle Hooks
    void onCreate() override;
    void onResume() override;
    void onUpdate(uint32_t deltaMs) override;
    void onUpdate() override;
    void onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) override;
    void onTouch(uint16_t x, uint16_t y, bool pressed) override;
    void onPause() override;
    void onDestroy() override;

private:
    bool     m_needsFullRedraw;
    uint32_t m_lastTelemetryPoll;
    uint32_t m_cachedUptimeSec;
    uint32_t m_cachedFreeHeapKb;
    uint32_t m_cachedMinHeapKb;
    bool     m_cachedSdMounted;
    bool     m_isTouched;

    void drawBugdroidMascot(TFT_eSPI &display, int16_t cx, int16_t cy);
    void renderDynamicTelemetry(TFT_eSPI &display, int vx, int vy, int vw, int vh);
};

} // namespace apps
