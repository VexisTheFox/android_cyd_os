/**
 * @file NavBar.cpp
 * @brief Bottom Navigation Bar implementation for CYD Android OS Shell
 */

#include "NavBar.h"
#include "hal/DisplayHAL.h"
#include "hal/BoardHAL.h"

namespace ui {

NavBar::NavBar()
    : m_display(nullptr),
      m_sprite(nullptr),
      m_useSprite(false),
      m_spriteAllocated(false),
      m_visible(true),
      m_pressedButton(NavAction::NONE),
      m_lastAction(NavAction::NONE),
      m_pressTimestamp(0),
      m_callback(nullptr) {
}

NavBar::~NavBar() {
    if (m_sprite) {
        m_sprite->deleteSprite();
        delete m_sprite;
        m_sprite = nullptr;
    }
}

void NavBar::init(TFT_eSPI* display, bool useSprite) {
    m_display = (display != nullptr) ? display : &hal::getDisplay();
    m_useSprite = useSprite;

    if (m_useSprite && !m_spriteAllocated) {
        m_sprite = new TFT_eSprite(m_display);
        m_sprite->setColorDepth(16);
        void* ptr = m_sprite->createSprite(NAV_BAR_WIDTH, NAV_BAR_HEIGHT);
        if (ptr != nullptr) {
            m_spriteAllocated = true;
        } else {
            delete m_sprite;
            m_sprite = nullptr;
            m_spriteAllocated = false;
        }
    }
}

void NavBar::update() {
    if (m_pressedButton != NavAction::NONE) {
        uint32_t now = millis();
        // Auto-release highlight if touch is no longer held OR after 180ms visual pulse
        if (hal::getTouchState() == hal::TouchState::NONE || (now - m_pressTimestamp >= 180)) {
            clearPressedState();
        }
    }
}

NavAction NavBar::handleTouch(uint16_t x, uint16_t y, bool pressed) {
    if (!pressed) {
        return NavAction::NONE;
    }

    int16_t navW = (m_display != nullptr) ? m_display->width() : hal::getDisplay().width();
    int16_t screenH = (m_display != nullptr) ? m_display->height() : hal::getDisplay().height();
    int16_t navY = screenH - NAV_BAR_HEIGHT;

    if ((int16_t)y < (navY - 6) || (int16_t)y >= screenH || (int16_t)x >= navW) {
        return NavAction::NONE;
    }

    int16_t col1Max = (navW / 3) - 1;
    int16_t col2Max = ((navW * 2) / 3) - 1;

    if ((int16_t)x <= col1Max) {
        return NavAction::BACK;
    } else if ((int16_t)x <= col2Max) {
        return NavAction::HOME;
    } else {
        return NavAction::TASKS;
    }
}

NavAction NavBar::processTouchEvent(const hal::TouchEvent& event) {
    if (!m_visible) {
        return NavAction::NONE;
    }

    switch (event.state) {
        case hal::TouchState::DOWN: {
            if (isInside(event.x, event.y)) {
                NavAction btn = handleTouch(event.x, event.y, true);
                if (btn != NavAction::NONE) {
                    m_pressedButton = btn;
                    m_lastAction = btn;
                    m_pressTimestamp = millis();
                    render(true);
                    hal::beep(20);
                    if (m_callback != nullptr) {
                        m_callback(btn);
                    }
                    return btn;
                }
            }
            break;
        }

        case hal::TouchState::MOVE: {
            if (m_pressedButton != NavAction::NONE) {
                if (!isInside(event.x, event.y)) {
                    clearPressedState();
                }
            }
            break;
        }

        case hal::TouchState::UP: {
            clearPressedState();
            break;
        }

        default:
            break;
    }

    return NavAction::NONE;
}

void NavBar::clearPressedState() {
    if (m_pressedButton != NavAction::NONE) {
        m_pressedButton = NavAction::NONE;
        render(true);
    }
}

void NavBar::render(bool forceRedraw) {
    (void)forceRedraw;
    if (!m_visible) {
        return;
    }

    if (m_display == nullptr) {
        m_display = &hal::getDisplay();
    }

    int16_t navY = m_display->height() - NAV_BAR_HEIGHT;

    if (m_useSprite && m_spriteAllocated && m_sprite != nullptr) {
        renderToTarget(*m_sprite, 0, 0);
        m_sprite->pushSprite(NAV_BAR_X, navY);
    } else {
        renderToTarget(*m_display, NAV_BAR_X, navY);
    }
}

void NavBar::renderToTarget(TFT_eSPI& target, int16_t originX, int16_t originY) {
    int16_t navW = (m_display != nullptr) ? m_display->width() : NAV_BAR_WIDTH;

    target.fillRect(originX, originY, navW, NAV_BAR_HEIGHT, NAV_BG_COLOR);
    target.drawFastHLine(originX, originY, navW, NAV_LINE_COLOR);

    int16_t backCX  = originX + (navW / 6);
    int16_t homeCX  = originX + (navW / 2);
    int16_t tasksCX = originX + navW - (navW / 6);

    drawButtonGlyph(target, backCX - 5, backCX, originY,
                    "<", (m_pressedButton == NavAction::BACK));

    drawButtonGlyph(target, homeCX - 5, homeCX, originY,
                    "O", (m_pressedButton == NavAction::HOME));

    drawButtonGlyph(target, tasksCX - 7, tasksCX, originY,
                    "[]", (m_pressedButton == NavAction::TASKS));
}

void NavBar::drawButtonGlyph(TFT_eSPI& target, int16_t textX, int16_t centerX, int16_t originY,
                             const char* label, bool pressed) {
    if (pressed) {
        target.fillRoundRect(centerX - 24, originY + 2, 48, 22, 5, NAV_HIGHLIGHT_PILL);
        target.drawRoundRect(centerX - 24, originY + 2, 48, 22, 5, NAV_ICON_PRESSED);
        target.setTextColor(NAV_ICON_PRESSED, NAV_HIGHLIGHT_PILL);
    }

    (void)textX;
    uint16_t col = pressed ? NAV_ICON_PRESSED : NAV_ICON_DEFAULT;
    int16_t cy = originY + 13;

    if (label[0] == '<') {
        target.fillTriangle(centerX - 6, cy, centerX + 5, cy - 7, centerX + 5, cy + 7, col);
    } else if (label[0] == 'O') {
        target.drawCircle(centerX, cy, 7, col);
        target.drawCircle(centerX, cy, 6, col);
    } else {
        target.drawRect(centerX - 6, cy - 6, 12, 12, col);
        target.drawRect(centerX - 5, cy - 5, 10, 10, col);
    }
}

} // namespace ui
