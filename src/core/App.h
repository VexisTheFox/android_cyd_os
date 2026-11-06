/**
 * @file App.h
 * @brief Base Application Lifecycle Class for CYD Android OS
 * 
 * Conforms to PROJECT.md § 2 Interface Contract and Feature F07.
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "hal/TouchHAL.h"

namespace core {

/**
 * @brief Application lifecycle states.
 */
enum class AppState : uint8_t {
    UNINITIALIZED = 0,  ///< Constructed but onCreate not yet invoked
    CREATED,            ///< Initialized and resources allocated
    RUNNING,            ///< Active in foreground, receiving updates and renders
    PAUSED,             ///< Suspended in background
    DESTROYED           ///< Teardown complete, awaiting deallocation
};

/**
 * @brief Abstract Base Class for all CYD Android OS applications.
 */
class App {
public:
    // Standard CYD 240x320 Portrait Viewport Geometry Constants
    static constexpr int32_t VIEWPORT_X      = 0;
    static constexpr int32_t VIEWPORT_Y      = 20;   ///< Below 20px Status Bar
    static constexpr int32_t VIEWPORT_WIDTH  = 240;
    static constexpr int32_t VIEWPORT_HEIGHT = 274;  ///< Above 26px Nav Bar (320 - 20 - 26 = 274)

    virtual ~App() {
        if (m_state != AppState::DESTROYED && m_state != AppState::UNINITIALIZED) {
            onDestroy();
        }
    }

    // -------------------------------------------------------------
    // Application Metadata
    // -------------------------------------------------------------
    virtual const char* getName() const = 0;
    virtual const char* getIcon() const = 0;
    virtual const char* getCategory() const { return "System"; }
    virtual const char* getAuthor() const { return "CYD Team"; }
    virtual const char* getVersion() const { return "1.0.0"; }
    virtual const char* getDescription() const { return ""; }

    // -------------------------------------------------------------
    // Lifecycle Hooks
    // -------------------------------------------------------------
    virtual void onCreate() {
        m_state = AppState::CREATED;
    }

    virtual void onResume() {
        m_state = AppState::RUNNING;
    }

    virtual void onUpdate(uint32_t deltaMs) {
        (void)deltaMs;
        onUpdate();
    }

    virtual void onUpdate() {
        // Optional parameterless update hook
    }

    /**
     * @brief Render callback into designated viewport.
     */
    virtual void onRender(TFT_eSPI &display, int viewportX, int viewportY, int viewportW, int viewportH) = 0;

    /**
     * @brief Convenience render overload defaulting to standard 320x194 viewport.
     */
    virtual void onRender(TFT_eSPI &display) {
        onRender(display, VIEWPORT_X, VIEWPORT_Y, VIEWPORT_WIDTH, VIEWPORT_HEIGHT);
    }

    /**
     * @brief Structured touch event hook.
     */
    virtual void onTouch(const hal::TouchEvent& event) {
        int16_t relY = (int16_t)event.y - VIEWPORT_Y;
        uint16_t clampedY = (relY < 0) ? 0 : ((relY >= VIEWPORT_HEIGHT) ? (VIEWPORT_HEIGHT - 1) : (uint16_t)relY);
        onTouch(event.x, clampedY, event.state != hal::TouchState::UP);
    }

    /**
     * @brief Basic coordinate touch hook (PROJECT.md Contract).
     * @param x Screen X coordinate (0..319)
     * @param y Viewport-relative Y coordinate (0..193)
     * @param pressed true if pressed down
     */
    virtual void onTouch(uint16_t x, uint16_t y, bool pressed) {
        (void)x; (void)y; (void)pressed;
    }

    virtual void onPause() {
        m_state = AppState::PAUSED;
    }

    virtual void onDestroy() {
        m_state = AppState::DESTROYED;
    }

    // -------------------------------------------------------------
    // State Query
    // -------------------------------------------------------------
    AppState getState() const { return m_state; }
    bool isRunning() const { return m_state == AppState::RUNNING; }

protected:
    AppState m_state = AppState::UNINITIALIZED;
};

} // namespace core

#ifndef UI_APP_BASE_DEFINED
#define UI_APP_BASE_DEFINED
namespace ui {
    using core::App;
    using core::AppState;
}
#endif
