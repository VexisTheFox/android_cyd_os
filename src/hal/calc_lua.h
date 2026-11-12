#pragma once
#include <Arduino.h>

static const char BUNDLED_CALC_LUA[] PROGMEM = R"lua(
-- ===================================================================
-- CYD Android OS - Zero-Flicker Touch Calculator [DEV BUILD]
-- Target Hardware: ESP32-2432S028 (240x320 Portrait)
-- ===================================================================
-- @name: Calculator
-- @icon: calc
-- @orientation: portrait

local calc = {
    expr = "0",
    result = "",
    pressedR = 0,
    pressedC = 0,
    needsReset = false,
    theme = {
        bg = 0x0821,
        displayBg = 0x18E3,
        displayText = 0xFFFF,
        resText = 0x07E0,
        btnNum = 0x2124,
        btnOp = 0xFD20,
        btnAction = 0x4208,
        btnEqual = 0x04F5,
        btnText = 0xFFFF
    }
}

local VP_X = 0
local VP_Y = 20
local VP_W = 240
local VP_H = 274

local COLS = 4
local ROWS = 5
local BTN_GAP = 4
local BTN_W = math.floor((VP_W - 16 - (BTN_GAP * (COLS - 1))) / COLS) -- 53px
local BTN_H = 38
local GRID_START_Y = VP_Y + 56

local buttons = {
    { "C",   "(",   ")",   "/" },
    { "7",   "8",   "9",   "*" },
    { "4",   "5",   "6",   "-" },
    { "1",   "2",   "3",   "+" },
    { "0",   ".",   "DEL", "=" }
}

local function evaluateExpr(e)
    if not string.match(e, "^[%s%d%+%-%*%/%.%(%)]+$") then
        return "Error"
    end
    local f = load("return " .. e)
    if not f then return "Error" end
    local ok, res = pcall(f)
    if not ok or res == nil then return "Error" end
    if type(res) == "number" then
        if math.floor(res) == res then
            return string.format("%d", res)
        else
            return string.format("%.4f", res):gsub("%.?0+$", "")
        end
    end
    return tostring(res)
end

-- Redraw ONLY the calculator readout window (zero screen flicker)
function calc.drawDisplay()
    gui.fillRect(VP_X + 9, VP_Y + 7, VP_W - 18, 42, calc.theme.displayBg)
    local dispExpr = calc.expr
    if #dispExpr > 24 then
        dispExpr = string.sub(dispExpr, -24)
    end
    gui.drawText(VP_X + 14, VP_Y + 10, dispExpr, calc.theme.displayText, 2)
    if calc.result ~= "" then
        gui.drawText(VP_X + 14, VP_Y + 28, "= " .. calc.result, calc.theme.resText, 2)
    end
end

-- Redraw ONLY a single keypad button at (r, c)
function calc.drawButton(r, c, isPressed)
    if r < 1 or r > ROWS or c < 1 or c > COLS then return end
    local label = buttons[r][c]
    local bx = VP_X + 8 + (c - 1) * (BTN_W + BTN_GAP)
    local by = GRID_START_Y + (r - 1) * (BTN_H + BTN_GAP)

    local color = calc.theme.btnNum
    if label == "=" then
        color = calc.theme.btnEqual
    elseif label == "/" or label == "*" or label == "-" or label == "+" then
        color = calc.theme.btnOp
    elseif label == "C" or label == "DEL" or label == "(" or label == ")" then
        color = calc.theme.btnAction
    end

    if isPressed then
        color = 0xFFFF
    end

    gui.fillRect(bx, by, BTN_W, BTN_H, color)
    gui.drawRect(bx, by, BTN_W, BTN_H, isPressed and 0x07FF or 0x0000)

    local textColor = isPressed and 0x0000 or calc.theme.btnText
    local tx = bx + math.floor((BTN_W - (#label * 8)) / 2)
    local ty = by + 11
    gui.drawText(tx, ty, label, textColor, 2)
end

-- Initial full draw (called ONCE on startup)
function calc.drawFull()
    gui.fillRect(VP_X, VP_Y, VP_W, VP_H, calc.theme.bg)
    gui.fillRect(VP_X + 8, VP_Y + 6, VP_W - 16, 44, calc.theme.displayBg)
    gui.drawRect(VP_X + 8, VP_Y + 6, VP_W - 16, 44, 0x4208)
    calc.drawDisplay()

    for r = 1, ROWS do
        for c = 1, COLS do
            calc.drawButton(r, c, false)
        end
    end
end

function calc.onButtonPress(btn)
    if btn == "C" then
        calc.expr = "0"
        calc.result = ""
        calc.needsReset = false
    elseif btn == "DEL" then
        if #calc.expr > 1 then
            calc.expr = string.sub(calc.expr, 1, -2)
        else
            calc.expr = "0"
        end
        calc.result = ""
    elseif btn == "=" then
        calc.result = evaluateExpr(calc.expr)
        calc.needsReset = true
    else
        if calc.needsReset then
            calc.expr = ""
            calc.result = ""
            calc.needsReset = false
        end
        if calc.expr == "0" and btn ~= "." and btn ~= "/" and btn ~= "*" and btn ~= "+" and btn ~= "-" then
            calc.expr = btn
        else
            calc.expr = calc.expr .. btn
        end
    end
end

function calc.handleTouch(x, y)
    for r = 1, ROWS do
        for c = 1, COLS do
            local bx = VP_X + 8 + (c - 1) * (BTN_W + BTN_GAP)
            local by = GRID_START_Y + (r - 1) * (BTN_H + BTN_GAP)

            if x >= bx and x <= (bx + BTN_W) and y >= by and y <= (by + BTN_H) then
                local label = buttons[r][c]
                calc.pressedR = r
                calc.pressedC = c
                calc.onButtonPress(label)
                if sys and sys.beep then
                    sys.beep(1200, 15)
                end
                calc.drawButton(r, c, true)
                calc.drawDisplay()
                return true
            end
        end
    end
    return false
end

function app_main()
    calc.drawFull()
    local lastTouched = false

    while true do
        local tx, ty, touched = input.getTouch()
        if touched and not lastTouched then
            calc.handleTouch(tx, ty)
            lastTouched = true
        elseif not touched and lastTouched then
            if calc.pressedR > 0 and calc.pressedC > 0 then
                calc.drawButton(calc.pressedR, calc.pressedC, false)
                calc.pressedR = 0
                calc.pressedC = 0
            end
            lastTouched = false
        end
        sys.delay(16)
    end
end

function onResume() calc.drawFull() end
app_main()
)lua";
