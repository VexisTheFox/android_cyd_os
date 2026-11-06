/**
 * @file NavBar.h
 * @brief Bottom Navigation Bar for CYD Android OS Shell (ESP32 Cheap Yellow Display)
 */

#ifndef NAV_BAR_H
#define NAV_BAR_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "hal/DisplayHAL.h"
#include "hal/TouchHAL.h"

namespace ui {

// Navigation Action Enumeration
#ifndef UI_NAV_ACTION_DEFINED
#define UI_NAV_ACTION_DEFINED
enum class NavAction : uint8_t {
    NONE = 0,
    BACK,
    HOME,
    TASKS
};
#endif

// Convert NavAction to human-readable string matching test suite expectations
inline const char* navActionToString(NavAction action) {
    switch (action) {
        case NavAction::BACK:  return "BACK";
        case NavAction::HOME:  return "HOME";
        case NavAction::TASKS: return "TASKS";
        default:               return "";
    }
}

// Optional callback listener for navigation events
typedef void (*NavActionCallback)(NavAction action);

// Nav Bar Geometry Constants
constexpr int32_t NAV_BAR_X             = 0;
constexpr int32_t NAV_BAR_Y             = 294;
constexpr int32_t NAV_BAR_WIDTH         = 240;
constexpr int32_t NAV_BAR_HEIGHT        = 26;

// Hitbox Partitioning Boundaries (240px Portrait Width: 3 x 80px)
constexpr int32_t HITBOX_BACK_MAX_X     = 79;
constexpr int32_t HITBOX_HOME_MAX_X     = 159;
constexpr int32_t HITBOX_TASKS_MAX_X    = 240;

// Button Center Anchors
constexpr int16_t BTN_BACK_CENTER_X     = 40;
constexpr int16_t BTN_HOME_CENTER_X     = 120;
constexpr int16_t BTN_TASKS_CENTER_X    = 200;
constexpr int16_t BTN_CENTER_Y          = 307; // 294 + 13

// Button Text Left Coordinates
constexpr int16_t BTN_BACK_TEXT_X       = 35;
constexpr int16_t BTN_HOME_TEXT_X       = 115;
constexpr int16_t BTN_TASKS_TEXT_X      = 193;
constexpr int16_t BTN_TEXT_Y            = 298; // 294 + 4

// Color Palette
constexpr uint16_t NAV_BG_COLOR         = 0x10A2; // Dark tech slate
constexpr uint16_t NAV_LINE_COLOR       = 0x7BEF; // Medium tech gray
constexpr uint16_t NAV_ICON_DEFAULT     = 0xFFFF; // Bright white
constexpr uint16_t NAV_ICON_PRESSED     = 0x07E0; // Android Green accent
constexpr uint16_t NAV_HIGHLIGHT_PILL   = 0x2965; // Pressed background pill highlight

class NavBar {
public:
    NavBar();
    ~NavBar();

    void init(TFT_eSPI* display = nullptr, bool useSprite = false);
    void render(bool forceRedraw = false);
    void update();

    NavAction handleTouch(uint16_t x, uint16_t y, bool pressed = true);
    NavAction processTouchEvent(const hal::TouchEvent& event);

    static inline bool isInside(uint16_t x, uint16_t y) {
        int16_t w = hal::getDisplay().width();
        int16_t h = hal::getDisplay().height();
        int16_t navY = h - NAV_BAR_HEIGHT;
        return ((int16_t)y >= (navY - 6) && (int16_t)y < h && (int16_t)x < w);
    }

    void setNavCallback(NavActionCallback callback) { m_callback = callback; }

    NavAction getLastAction() const { return m_lastAction; }
    NavAction getPressedButton() const { return m_pressedButton; }
    void clearPressedState();

    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

private:
    TFT_eSPI*           m_display;
    TFT_eSprite*        m_sprite;
    bool                m_useSprite;
    bool                m_spriteAllocated;
    bool                m_visible;
    NavAction           m_pressedButton;
    NavAction           m_lastAction;
    uint32_t            m_pressTimestamp;
    NavActionCallback   m_callback;

    void renderToTarget(TFT_eSPI& target, int16_t originX, int16_t originY);
    void drawButtonGlyph(TFT_eSPI& target, int16_t textX, int16_t centerX, int16_t originY, const char* label, bool pressed);
};

} // namespace ui

#endif // NAV_BAR_H
