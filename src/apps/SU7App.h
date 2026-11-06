/**
 * @file SU7App.h
 * @brief Xiaomi SU7 Ultra 3D Wireframe Telemetry Native Application for CYD Android OS
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "core/App.h"
#include "../su7_data.h"

namespace apps {

enum class SU7ColorMode : uint8_t {
    REALISTIC = 0, ///< Signature Ultra Yellow, Taillight Red, Wheel Gold from su7_data.h
    CYBER_CYAN,    ///< Monochromatic Neon Cyan HUD wireframe
    ULTRA_YELLOW,  ///< High-voltage Lightning Yellow
    MATRIX_GREEN   ///< Tactical Matrix Green
};

class SU7App : public core::App {
public:
    SU7App();
    virtual ~SU7App() = default;

    // App Identity & Metadata
    const char* getName() const override { return "SU7 Ultra"; }
    const char* getIcon() const override { return "ICON_SU7"; }
    const char* getCategory() const override { return "Demo"; }

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
    float m_yawAngle;
    float m_pitchAngle;
    bool  m_autoRotate;
    uint32_t m_lastInteractionTime;

    // Touch interaction tracking
    bool     m_isDragging;
    uint16_t m_touchStartX;
    uint16_t m_touchStartY;
    uint16_t m_lastTouchX;
    uint16_t m_lastTouchY;
    uint32_t m_touchStartTime;
    uint16_t m_dragDistance;

    // Rendering & Telemetry
    SU7ColorMode m_colorMode;
    uint32_t m_lastFrameTime;
    uint32_t m_frameCount;
    float    m_fps;

    static constexpr float CAM_DIST = 6.2f;
    static constexpr float FOCAL    = 255.0f;
    static constexpr float DEFAULT_PITCH = 0.30f;

    uint16_t resolveEdgeColor(const Edge3D &edge) const;
    void drawCarCanvas(TFT_eSPI &canvas, int cw, int ch);
    void drawFrame(TFT_eSPI &display, int vx, int vy, int vw, int vh);
    TFT_eSprite* m_sprite = nullptr;
    bool m_staticDrawn = false;
};

} // namespace apps
