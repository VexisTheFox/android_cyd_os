/**
 * @file KeyboardView.cpp
 * @brief Implementation of Full Horizontal (Landscape 320x240) On-Screen QWERTY Keyboard
 */

#include "KeyboardView.h"
#include "UIManager.h"
#include "../hal/DisplayHAL.h"
#include "../hal/TouchHAL.h"
#include "../hal/BoardHAL.h"
#include "../core/Logger.h"
#include <cstring>
#include <cstdio>

namespace ui {

static constexpr uint16_t KB_BG         = 0x0821;
static constexpr uint16_t KB_HEADER_BG  = 0x10A2;
static constexpr uint16_t KB_INPUT_BG   = 0x0000;
static constexpr uint16_t KB_KEY_BG     = 0x18E3;
static constexpr uint16_t KB_KEY_SPEC   = 0x2166;
static constexpr uint16_t KB_KEY_BORDER = 0x39E7;
static constexpr uint16_t KB_CYAN       = 0x07FF;
static constexpr uint16_t KB_GREEN      = 0x07E0;
static constexpr uint16_t KB_AMBER      = 0xFFE0;
static constexpr uint16_t KB_RED        = 0xF800;

static const char ROW0_NORM[10] = { '1','2','3','4','5','6','7','8','9','0' };
static const char ROW0_SYM[10]  = { '!','@','#','$','%','^','&','*','(',')' };

static const char ROW1_LOW[10]  = { 'q','w','e','r','t','y','u','i','o','p' };
static const char ROW1_UP[10]   = { 'Q','W','E','R','T','Y','U','I','O','P' };
static const char ROW1_SYM[10]  = { '-','_','=','+','[',']','{','}','\\','|' };

static const char ROW2_LOW[10]  = { 'a','s','d','f','g','h','j','k','l','@' };
static const char ROW2_UP[10]   = { 'A','S','D','F','G','H','J','K','L',':' };
static const char ROW2_SYM[10]  = { ';',':','\'','"',',','.','<','>','/','?' };

static const char ROW3_LOW[7]   = { 'z','x','c','v','b','n','m' };
static const char ROW3_UP[7]    = { 'Z','X','C','V','B','N','M' };
static const char ROW3_SYM[7]   = { '~','`','!','#','$','%','&' };

void KeyboardView::drawInputBox(TFT_eSPI &tft, const char* buf) {
    tft.fillRoundRect(4, 24, 312, 24, 4, KB_INPUT_BG);
    tft.drawRoundRect(4, 24, 312, 24, 4, KB_CYAN);

    tft.setTextFont(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_WHITE, KB_INPUT_BG);

    // Show rightmost 34 chars if string is long
    size_t len = strlen(buf);
    const char* disp = (len > 32) ? (buf + (len - 32)) : buf;
    char cursorBuf[40];
    snprintf(cursorBuf, sizeof(cursorBuf), "%s|", disp);
    tft.drawString(cursorBuf, 10, 36);
    tft.setTextDatum(TL_DATUM);
}

void KeyboardView::drawKeyGrid(TFT_eSPI &tft, bool shift, bool symbols) {
    tft.fillRect(0, 50, 320, 190, KB_BG);

    const char* r0 = symbols ? ROW0_SYM : ROW0_NORM;
    const char* r1 = symbols ? ROW1_SYM : (shift ? ROW1_UP : ROW1_LOW);
    const char* r2 = symbols ? ROW2_SYM : (shift ? ROW2_UP : ROW2_LOW);
    const char* r3 = symbols ? ROW3_SYM : (shift ? ROW3_UP : ROW3_LOW);

    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);

    // Rows 0, 1, 2: 10 keys each (width 30px, gap 2px, startX = 2)
    const char* rows[3] = { r0, r1, r2 };
    for (int r = 0; r < 3; r++) {
        int ky = 52 + r * 36;
        for (int c = 0; c < 10; c++) {
            int kx = 2 + c * 32;
            uint16_t bg = (r == 0) ? 0x14A5 : KB_KEY_BG;
            tft.fillRoundRect(kx, ky, 30, 33, 4, bg);
            tft.drawRoundRect(kx, ky, 30, 33, 4, KB_KEY_BORDER);
            char s[2] = { rows[r][c], '\0' };
            tft.setTextColor((r == 0) ? KB_CYAN : TFT_WHITE, bg);
            tft.drawString(s, kx + 15, ky + 16);
        }
    }

    // Row 3 (Y = 160): [SHIFT (46px)] + 7 letter keys (32px stride) + [DEL (46px)]
    int r3Y = 160;
    uint16_t shBg = shift ? 0x04D3 : KB_KEY_SPEC;
    tft.fillRoundRect(2, r3Y, 44, 33, 4, shBg);
    tft.drawRoundRect(2, r3Y, 44, 33, 4, shift ? KB_GREEN : KB_CYAN);
    tft.setTextFont(1);
    tft.setTextColor(TFT_WHITE, shBg);
    tft.drawString(shift ? "CAPS" : "SHIFT", 24, r3Y + 16);

    tft.setTextFont(2);
    for (int c = 0; c < 7; c++) {
        int kx = 48 + c * 32;
        tft.fillRoundRect(kx, r3Y, 30, 33, 4, KB_KEY_BG);
        tft.drawRoundRect(kx, r3Y, 30, 33, 4, KB_KEY_BORDER);
        char s[2] = { r3[c], '\0' };
        tft.setTextColor(TFT_WHITE, KB_KEY_BG);
        tft.drawString(s, kx + 15, r3Y + 16);
    }

    // Backspace / DEL key (X = 272..318)
    tft.fillRoundRect(272, r3Y, 46, 33, 4, 0x3800);
    tft.drawRoundRect(272, r3Y, 46, 33, 4, KB_RED);
    tft.setTextFont(1);
    tft.setTextColor(KB_AMBER, 0x3800);
    tft.drawString("< BKSP", 295, r3Y + 16);

    // Row 4 (Y = 198): [?123 (54px)] + [SPACE (132px)] + [CLR (42px)] + [DONE/OK (80px)]
    int r4Y = 198;
    uint16_t symBg = symbols ? 0x9175 : KB_KEY_SPEC;
    tft.fillRoundRect(2, r4Y, 54, 36, 4, symBg);
    tft.drawRoundRect(2, r4Y, 54, 36, 4, symbols ? KB_AMBER : KB_CYAN);
    tft.setTextColor(TFT_WHITE, symBg);
    tft.drawString(symbols ? "ABC" : "?123", 29, r4Y + 18);

    // Spacebar (X = 60..190)
    tft.fillRoundRect(60, r4Y, 130, 36, 4, KB_KEY_BG);
    tft.drawRoundRect(60, r4Y, 130, 36, 4, KB_KEY_BORDER);
    tft.setTextColor(0x9CD3, KB_KEY_BG);
    tft.drawString("SPACE", 125, r4Y + 18);

    // Clear (X = 194..234)
    tft.fillRoundRect(194, r4Y, 40, 36, 4, KB_KEY_SPEC);
    tft.drawRoundRect(194, r4Y, 40, 36, 4, KB_KEY_BORDER);
    tft.setTextColor(KB_AMBER, KB_KEY_SPEC);
    tft.drawString("CLR", 214, r4Y + 18);

    // DONE / OK (X = 238..318)
    tft.fillRoundRect(238, r4Y, 80, 36, 4, 0x04D3);
    tft.drawRoundRect(238, r4Y, 80, 36, 4, KB_GREEN);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, 0x04D3);
    tft.drawString("DONE", 278, r4Y + 18);

    tft.setTextDatum(TL_DATUM);
}

void KeyboardView::drawFullKeyboard(TFT_eSPI &tft, const char* title, const char* buf,
                                    bool shift, bool symbols) {
    tft.fillScreen(KB_BG);

    // Top Header Bar (Y = 0..22)
    tft.fillRect(0, 0, 320, 22, KB_HEADER_BG);
    tft.setTextFont(2);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(KB_CYAN, KB_HEADER_BG);
    tft.drawString(title ? title : "Keyboard Input", 6, 3);

    tft.setTextFont(1);
    tft.setTextColor(KB_AMBER, KB_HEADER_BG);
    tft.drawString("[DEV]", 214, 7);

    // Cancel Button (X = 252..316, Y = 2..20)
    tft.fillRoundRect(252, 2, 64, 18, 3, 0x7800);
    tft.drawRoundRect(252, 2, 64, 18, 3, TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, 0x7800);
    tft.drawString("CANCEL", 284, 11);
    tft.setTextDatum(TL_DATUM);

    drawInputBox(tft, buf);
    drawKeyGrid(tft, shift, symbols);
}

bool KeyboardView::prompt(const char* title, const char* initialText, char* outBuf, size_t outMaxLen) {
    if (!outBuf || outMaxLen == 0) return false;

    char workBuf[64];
    memset(workBuf, 0, sizeof(workBuf));
    if (initialText) {
        strncpy(workBuf, initialText, sizeof(workBuf) - 1);
    }
    size_t maxChars = (outMaxLen - 1 < sizeof(workBuf) - 1) ? (outMaxLen - 1) : (sizeof(workBuf) - 1);

    // Save current orientation and switch to Horizontal / Landscape (320x240)
    bool prevLandscape = UIManager::isLandscape();
    hal::setDisplayRotation(1);
    hal::setTouchRotation(1);

    TFT_eSPI &tft = hal::getDisplay();
    bool shift   = false;
    bool symbols = false;

    drawFullKeyboard(tft, title, workBuf, shift, symbols);
    LOG_INFO("KEYBOARD", "Opened Horizontal QWERTY Keyboard for '%s'", title ? title : "input");

    // Wait for any initial touch release before accepting key taps
    uint32_t openWaitStart = millis();
    while (millis() - openWaitStart < 250) {
        hal::TouchEvent ev;
        hal::pollTouch(ev);
        if (ev.state == hal::TouchState::NONE) break;
        delay(10);
    }

    uint32_t lastKeyTapMs = millis();
    bool keyArmed = true;
    bool confirmed = false;
    bool finished  = false;

    while (!finished) {
        // Support hardware BOOT power button while keyboard is open
        if (hal::pollPowerButton()) {
            hal::beep(25);
            confirmed = false;
            finished = true;
            break;
        }

        hal::TouchEvent ev;
        bool hasEv = hal::pollTouch(ev);
        if (!hasEv || ev.state == hal::TouchState::UP) {
            keyArmed = true;
            delay(8);
            continue;
        }

        // Only fire once per physical press with a 140ms anti-bounce cooldown
        if (ev.state == hal::TouchState::DOWN && keyArmed && (millis() - lastKeyTapMs >= 140)) {
            keyArmed = false;
            lastKeyTapMs = millis();

            int16_t tx = (int16_t)ev.x;
            int16_t ty = (int16_t)ev.y;

            // 1. Top Header CANCEL button (Y < 24, X >= 248)
            if (ty < 24 && tx >= 248) {
                hal::beep(25);
                confirmed = false;
                finished = true;
                break;
            }

            // 2. Rows 0, 1, 2 (Y = 52..159)
            if (ty >= 50 && ty < 160) {
                int row = (ty - 50) / 36;
                if (row < 0) row = 0;
                if (row > 2) row = 2;
                int col = (tx - 2) / 32;
                if (col >= 0 && col < 10) {
                    const char* r0 = symbols ? ROW0_SYM : ROW0_NORM;
                    const char* r1 = symbols ? ROW1_SYM : (shift ? ROW1_UP : ROW1_LOW);
                    const char* r2 = symbols ? ROW2_SYM : (shift ? ROW2_UP : ROW2_LOW);
                    const char* rows[3] = { r0, r1, r2 };
                    char ch = rows[row][col];
                    size_t curLen = strlen(workBuf);
                    if (curLen < maxChars) {
                        workBuf[curLen] = ch;
                        workBuf[curLen + 1] = '\0';
                        hal::beep(15);
                        drawInputBox(tft, workBuf);
                    }
                }
                continue;
            }

            // 3. Row 3 (Y = 160..195): SHIFT, 7 letters, BKSP
            if (ty >= 160 && ty < 196) {
                if (tx < 47) {
                    shift = !shift;
                    hal::beep(20);
                    drawKeyGrid(tft, shift, symbols);
                } else if (tx >= 270) {
                    size_t curLen = strlen(workBuf);
                    if (curLen > 0) {
                        workBuf[curLen - 1] = '\0';
                        hal::beep(15);
                        drawInputBox(tft, workBuf);
                    }
                } else {
                    int col = (tx - 48) / 32;
                    if (col >= 0 && col < 7) {
                        const char* r3 = symbols ? ROW3_SYM : (shift ? ROW3_UP : ROW3_LOW);
                        char ch = r3[col];
                        size_t curLen = strlen(workBuf);
                        if (curLen < maxChars) {
                            workBuf[curLen] = ch;
                            workBuf[curLen + 1] = '\0';
                            hal::beep(15);
                            drawInputBox(tft, workBuf);
                        }
                    }
                }
                continue;
            }

            // 4. Row 4 (Y >= 196): ?123, SPACE, CLR, DONE
            if (ty >= 196) {
                if (tx < 58) {
                    symbols = !symbols;
                    hal::beep(20);
                    drawKeyGrid(tft, shift, symbols);
                } else if (tx >= 58 && tx < 192) {
                    size_t curLen = strlen(workBuf);
                    if (curLen < maxChars) {
                        workBuf[curLen] = ' ';
                        workBuf[curLen + 1] = '\0';
                        hal::beep(15);
                        drawInputBox(tft, workBuf);
                    }
                } else if (tx >= 192 && tx < 236) {
                    workBuf[0] = '\0';
                    hal::beep(20);
                    drawInputBox(tft, workBuf);
                } else if (tx >= 236) {
                    hal::beep(35);
                    confirmed = true;
                    finished = true;
                    break;
                }
            }
        }
        delay(8);
    }

    if (confirmed) {
        strncpy(outBuf, workBuf, outMaxLen - 1);
        outBuf[outMaxLen - 1] = '\0';
    }

    // Restore previous orientation (Portrait 240x320 if opened from Portrait)
    if (!prevLandscape) {
        hal::setDisplayRotation(0);
        hal::setTouchRotation(0);
    }
    tft.fillScreen(TFT_BLACK);
    return confirmed;
}

} // namespace ui
