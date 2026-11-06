/**
 * @file KeyboardView.h
 * @brief Full Horizontal (Landscape 320x240) On-Screen QWERTY Keyboard for CYD Android OS
 *
 * Automatically rotates the display & touch digitizer into Horizontal/Landscape (320x240)
 * when opened, providing a full 5-row QWERTY + Numbers + Symbols keyboard with zero-flicker
 * input box updates and anti-double-click debouncing. Restores Portrait (or previous
 * orientation) upon completion.
 */

#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

namespace ui {

class KeyboardView {
public:
    /**
     * @brief Open the modal Horizontal (320x240) Full QWERTY Keyboard and block until
     * the user taps DONE/OK or CANCEL.
     *
     * @param title Prompt title shown in top header (e.g. "Wi-Fi Password")
     * @param initialText Default pre-filled text (can be "" or nullptr)
     * @param outBuf Destination buffer to receive the entered C-string
     * @param outMaxLen Capacity of outBuf in bytes (including null terminator)
     * @return true if user tapped DONE/OK, false if user tapped CANCEL
     */
    static bool prompt(const char* title, const char* initialText, char* outBuf, size_t outMaxLen);

private:
    static void drawFullKeyboard(TFT_eSPI &tft, const char* title, const char* buf,
                                 bool shift, bool symbols);
    static void drawInputBox(TFT_eSPI &tft, const char* buf);
    static void drawKeyGrid(TFT_eSPI &tft, bool shift, bool symbols);
};

} // namespace ui
