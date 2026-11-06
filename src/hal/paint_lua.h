#pragma once
#include <Arduino.h>

static const char BUNDLED_PAINT_LUA[] PROGMEM = R"lua(
-- ===================================================================
-- CYD Android OS - Touch Drawing & Paint Application
-- Target Hardware: ESP32-2432S028 (320x240 LCD + XPT2046 Touch)
-- ===================================================================
-- Metadata Header:
-- @name: Paint
-- @icon: paint
-- @author: CYD Team
-- @version: 1.0.0
-- @description: Interactive touch canvas with multi-color palette

local paint = {
    selectedColor = 0xF800, -- Default Red
    brushSize = 3,          -- Radius in pixels
    lastX = -1,
    lastY = -1,
    theme = {
        bg = 0x0000,        -- Black drawing canvas
        toolbarBg = 0x18E3, -- Slate gray toolbar
        border = 0x4208
    }
}

-- Viewport bounds (Portrait: Y: 20 to 294 -> 240x274)
local VP_X = 0
local VP_Y = 20
local VP_W = 240
local VP_H = 274

local TOOLBAR_H = 28
local CANVAS_Y = VP_Y + TOOLBAR_H
local CANVAS_H = VP_H - TOOLBAR_H

-- Color Swatches (16-bit RGB565)
local swatches = {
    { color = 0xFFFF, label = "W" },   -- White
    { color = 0xF800, label = "R" },   -- Red
    { color = 0x07E0, label = "G" },   -- Green
    { color = 0x001F, label = "B" },   -- Blue
    { color = 0xFFE0, label = "Y" },   -- Yellow
    { color = 0x07FF, label = "C" },   -- Cyan
    { color = 0xF81F, label = "M" },   -- Magenta
    { color = 0x0000, label = "CLS" }  -- Clear / Eraser
}

-- Draw top toolbar and palette
function paint.drawToolbar()
    if gui and gui.fillRect then
        -- Toolbar background
        gui.fillRect(VP_X, VP_Y, VP_W, TOOLBAR_H, paint.theme.toolbarBg)
        gui.drawFastHLine(VP_X, CANVAS_Y - 1, VP_W, paint.theme.border)

        -- Swatch boxes (fit 240px width)
        local swW = 20
        local swH = 20
        local startX = VP_X + 4
        local startY = VP_Y + 4

        for i, sw in ipairs(swatches) do
            local bx = startX + (i - 1) * (swW + 3)
            gui.fillRect(bx, startY, swW, swH, sw.color)
            
            -- Draw border (highlight selected color)
            if sw.color == paint.selectedColor and sw.label ~= "CLS" then
                gui.drawRect(bx - 1, startY - 1, swW + 2, swH + 2, 0xFFFF)
                gui.drawRect(bx - 2, startY - 2, swW + 4, swH + 4, 0x07FF)
            else
                gui.drawRect(bx, startY, swW, swH, 0x0000)
            end

            -- Text label for clear / eraser
            if sw.label == "CLS" then
                gui.drawText(bx + 2, startY + 6, "CLS", 0xFFFF, 1)
            end
        end

        -- Brush size indicator
        local brushBtnX = startX + (#swatches * (swW + 3)) + 4
        gui.fillRect(brushBtnX, startY, 42, swH, 0x2124)
        gui.drawRect(brushBtnX, startY, 42, swH, 0x8410)
        local sizeLabel = string.format("R:%d", paint.brushSize)
        gui.drawText(brushBtnX + 8, startY + 6, sizeLabel, 0xFFFF, 1)
    end
end

-- Clear entire canvas area
function paint.clearCanvas()
    if gui and gui.fillRect then
        gui.fillRect(VP_X, CANVAS_Y, VP_W, CANVAS_H, paint.theme.bg)
    end
end

-- Main entry point
function app_main()
    paint.clearCanvas()
    paint.drawToolbar()

    local drawing = false

    while true do
        if input and input.getTouch then
            local tx, ty, touched = input.getTouch()

            if touched then
                -- Check if touching the toolbar
                if ty >= VP_Y and ty < CANVAS_Y then
                    local swW = 20
                    local swH = 20
                    local startX = VP_X + 4
                    local startY = VP_Y + 4

                    for i, sw in ipairs(swatches) do
                        local bx = startX + (i - 1) * (swW + 3)
                        if tx >= bx and tx <= (bx + swW) and ty >= startY and ty <= (startY + swH) then
                            if sw.label == "CLS" then
                                paint.clearCanvas()
                                if sys and sys.beep then sys.beep(600, 30) end
                            else
                                paint.selectedColor = sw.color
                                if sys and sys.beep then sys.beep(1000, 15) end
                            end
                            paint.drawToolbar()
                            break
                        end
                    end

                    -- Check brush size toggle
                    local brushBtnX = startX + (#swatches * (swW + 3)) + 4
                    if tx >= brushBtnX and tx <= (brushBtnX + 42) and ty >= startY and ty <= (startY + swH) then
                        if paint.brushSize == 2 then
                            paint.brushSize = 4
                        elseif paint.brushSize == 4 then
                            paint.brushSize = 7
                        else
                            paint.brushSize = 2
                        end
                        if sys and sys.beep then sys.beep(1400, 20) end
                        paint.drawToolbar()
                    end

                    drawing = false
                    paint.lastX = -1
                    paint.lastY = -1

                -- Touching the canvas area: draw!
                elseif ty >= CANVAS_Y + paint.brushSize and ty < (VP_Y + VP_H - paint.brushSize) then
                    if gui and gui.fillCircle then
                        if drawing and paint.lastX >= 0 and paint.lastY >= 0 then
                            local dx = tx - paint.lastX
                            local dy = ty - paint.lastY
                            local dist = math.sqrt(dx * dx + dy * dy)
                            local stepDist = math.max(1, math.floor(paint.brushSize / 2))
                            local steps = math.floor(dist / stepDist)
                            if steps > 1 then
                                for s = 1, steps do
                                    local curX = math.floor(paint.lastX + (dx * s) / steps)
                                    local curY = math.floor(paint.lastY + (dy * s) / steps)
                                    if curY >= (CANVAS_Y + paint.brushSize) and curY < (VP_Y + VP_H - paint.brushSize) then
                                        gui.fillCircle(curX, curY, paint.brushSize, paint.selectedColor)
                                    end
                                end
                            else
                                gui.fillCircle(tx, ty, paint.brushSize, paint.selectedColor)
                            end
                            if gui.drawLine then
                                gui.drawLine(paint.lastX, paint.lastY, tx, ty, paint.selectedColor)
                            end
                        else
                            gui.fillCircle(tx, ty, paint.brushSize, paint.selectedColor)
                        end
                    end
                    paint.lastX = tx
                    paint.lastY = ty
                    drawing = true
                end
            else
                drawing = false
                paint.lastX = -1
                paint.lastY = -1
            end
        end

        if sys and sys.delay then
            sys.delay(10)
        end
    end
end

-- Invoke entry point
app_main()
)lua";
