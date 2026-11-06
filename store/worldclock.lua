-- @name: WorldTime
-- @bg: 0x1928
-- @icon_b64: ACqoAAKVVoAJQUFgJAPAGCQDwBiQA8AGkAPABpUD/xaVA/8WkAAABpAAAAYkAAAYJAFAGAlBQWAClVaAACqoAA==

local VP_X, VP_Y, VP_W, VP_H = 0, 20, 240, 274
local ICON_B64 = "ACqoAAKVVoAJQUFgJAPAGCQDwBiQA8AGkAPABpUD/xaVA/8WkAAABpAAAAYkAAAYJAFAGAlBQWAClVaAACqoAA=="

local function fmtZone(epoch, offsetHours)
    local z = epoch + offsetHours * 3600
    local s = z % 60
    local m = math.floor(z / 60) % 60
    local h = math.floor(z / 3600) % 24
    return string.format("%02d:%02d:%02d", h, m, s)
end

gui.fillRect(VP_X, VP_Y, VP_W, VP_H, 0x0821)
gui.fillRect(VP_X, VP_Y, VP_W, 24, 0x1928)
gui.drawFastHLine(VP_X, VP_Y + 24, VP_W, 0x07FF)
if gui.drawBase64 then gui.drawBase64(14, VP_Y + 12, ICON_B64, 1, 0xFFE0) end
gui.drawText(28, VP_Y + 8, "NTP WORLD CLOCK", 0xFFFF, 1)

gui.fillRect(20, VP_Y + 224, 200, 38, 0x04D3)
gui.drawRect(20, VP_Y + 224, 200, 38, 0x07FF)
gui.drawText(68, VP_Y + 239, "SYNC NTP NOW", 0xFFFF, 1)

local lastSec = -1
local wasT = false
while true do
    local tx, ty, pressed = input.getTouch()
    if pressed and not wasT and ty >= VP_Y + 224 then
        sys.beep(1250, 25)
        sys.syncNTP(2000)
        lastSec = -1
    end
    wasT = pressed

    local tStr, synced, epoch, h, m, s = sys.getTime()
    if s ~= lastSec then
        lastSec = s
        gui.fillRect(12, VP_Y + 32, 216, 182, 0x10A2)
        gui.drawRect(12, VP_Y + 32, 216, 182, 0x3186)
        gui.drawText(20, VP_Y + 40, "Local (" .. sys.getRegion() .. ")", 0x07FF, 1)
        gui.drawText(20, VP_Y + 54, tStr, 0xFFE0, 4)
        gui.drawText(152, VP_Y + 62, synced and "[NTP OK]" or "[UPTIME]", 0x07E0, 1)

        gui.drawText(20, VP_Y + 94,  "UTC / GMT      : " .. fmtZone(epoch, 0), 0xFFFF, 1)
        gui.drawText(20, VP_Y + 114, "Prague (CET)   : " .. fmtZone(epoch, 1), 0xFFFF, 1)
        gui.drawText(20, VP_Y + 134, "New York (EST) : " .. fmtZone(epoch, -5), 0xFFFF, 1)
        gui.drawText(20, VP_Y + 154, "Tokyo (JST)    : " .. fmtZone(epoch, 9), 0xFFFF, 1)
        gui.drawText(20, VP_Y + 174, "Sydney (AEST)  : " .. fmtZone(epoch, 10), 0xFFFF, 1)
        gui.drawText(20, VP_Y + 194, "Epoch: " .. tostring(epoch), 0x9CD3, 1)
    end
    sys.delay(40)
end
