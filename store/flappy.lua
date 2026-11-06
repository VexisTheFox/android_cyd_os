-- @name: Flappy CYD
-- @bg: 0x1386
-- @icon_b64: AAAAAAAP/AAA//UAA//WQA9X1kANVfUADVX/qA9X/qoD//+oAP//AAAP8AAAAAAAqqqqqgAAAAAAAAAAAAAAAA==

local VP_X, VP_Y, VP_W, VP_H = 0, 20, 240, 274
local birdY, birdV = 140, 0
local pipeX, gapY = 220, 120
local score, best = 0, 0
local prevBirdY, prevPipeX = 140, 220

gui.fillRect(VP_X, VP_Y, VP_W, VP_H, 0x0821)
gui.fillRect(VP_X, VP_Y, VP_W, 20, 0x1386)
gui.drawText(8, VP_Y + 6, "FLAPPY CYD - TAP TO FLAP", 0xFFE0, 1)

while true do
    local tx, ty, pressed = input.getTouch()
    if pressed and birdV > -2 then
        birdV = -4.5
        sys.beep(1100, 10)
    end

    gui.fillRect(36, math.floor(prevBirdY), 14, 12, 0x0821)
    gui.fillRect(prevPipeX, VP_Y + 22, 22, gapY - (VP_Y + 22), 0x0821)
    gui.fillRect(prevPipeX, gapY + 64, 22, (VP_Y + VP_H) - (gapY + 64), 0x0821)

    birdV = birdV + 0.45
    birdY = birdY + birdV
    pipeX = pipeX - 4

    if pipeX < -22 then
        pipeX = 236
        gapY = math.random(VP_Y + 44, VP_Y + 180)
        score = score + 1
        if score > best then best = score end
        gui.fillRect(148, VP_Y + 4, 88, 12, 0x1386)
        gui.drawText(150, VP_Y + 6, "S:" .. score .. " B:" .. best, 0x07FF, 1)
    end

    if birdY < VP_Y + 24 or birdY > VP_Y + VP_H - 14 or
       (pipeX < 50 and pipeX + 22 > 36 and (birdY < gapY or birdY + 12 > gapY + 64)) then
        sys.beep(260, 80)
        score = 0
        birdY, birdV, pipeX = 140, 0, 236
        gui.fillRect(VP_X, VP_Y + 22, VP_W, VP_H - 22, 0x0821)
        gui.fillRect(148, VP_Y + 4, 88, 12, 0x1386)
        gui.drawText(150, VP_Y + 6, "S:0 B:" .. best, 0x07FF, 1)
    end

    if pipeX >= 0 and pipeX < 238 then
        gui.fillRect(pipeX, VP_Y + 22, 20, gapY - (VP_Y + 22), 0x07E0)
        gui.fillRect(pipeX, gapY + 64, 20, (VP_Y + VP_H) - (gapY + 64), 0x07E0)
    end
    gui.fillRect(36, math.floor(birdY), 14, 12, 0xFFE0)

    prevBirdY = birdY
    prevPipeX = pipeX
    sys.delay(28)
end
