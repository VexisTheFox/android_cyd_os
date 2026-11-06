-- @name: CyberSynth
-- @bg: 0x4812
-- @icon_b64: APAPAAMMMMD8A8A/AAAAAKqqqqpUUVFFVFFRRVRRUUVUUVFFVAFQBVUVVFVVFVRVVRVUVVUVVFWqqqqqAAAAAA==

local VP_X, VP_Y, VP_W, VP_H = 0, 20, 240, 274
local notes = {
    { name = "C4", freq = 262 }, { name = "D4", freq = 294 },
    { name = "E4", freq = 330 }, { name = "F4", freq = 349 },
    { name = "G4", freq = 392 }, { name = "A4", freq = 440 },
    { name = "B4", freq = 494 }, { name = "C5", freq = 523 }
}
local octave = 1
local activeKey = -1

local function drawHeader(freqText)
    gui.fillRect(VP_X, VP_Y, VP_W, 48, 0x10A2)
    gui.drawFastHLine(VP_X, VP_Y + 48, VP_W, 0xF81F)
    gui.drawText(8, VP_Y + 6, "CYBERSYNTH FM [OCT x" .. octave .. "]", 0x07FF, 1)
    gui.drawText(8, VP_Y + 24, freqText or "Touch keys below to play", 0xFFE0, 1)
    gui.fillRect(172, VP_Y + 8, 60, 30, 0x4812)
    gui.drawRect(172, VP_Y + 8, 60, 30, 0x07FF)
    gui.drawText(184, VP_Y + 19, "OCTAVE", 0xFFFF, 1)
end

local function drawKey(idx, pressed)
    local kw = 30
    local x = (idx - 1) * kw
    local y = VP_Y + 54
    local h = 212
    local bg = pressed and 0x07FF or ((idx % 2 == 0) and 0x18E3 or 0x2965)
    local fg = pressed and 0x0000 or 0xFFFF
    gui.fillRect(x + 1, y, kw - 2, h, bg)
    gui.drawRect(x + 1, y, kw - 2, h, 0xF81F)
    gui.drawText(x + 8, y + h - 24, notes[idx].name, fg, 1)
end

gui.fillRect(VP_X, VP_Y, VP_W, VP_H, 0x0000)
drawHeader()
for i = 1, 8 do drawKey(i, false) end

local wasT = false
while true do
    local tx, ty, pressed = input.getTouch()
    if pressed then
        if not wasT and ty >= VP_Y + 6 and ty <= VP_Y + 42 and tx >= 170 then
            octave = (octave == 1) and 2 or 1
            sys.beep(880 * octave, 25)
            drawHeader("Octave switched")
        elseif ty >= VP_Y + 54 then
            local k = math.floor(tx / 30) + 1
            if k < 1 then k = 1 elseif k > 8 then k = 8 end
            if k ~= activeKey then
                if activeKey > 0 then drawKey(activeKey, false) end
                activeKey = k
                drawKey(activeKey, true)
                local f = notes[k].freq * octave
                drawHeader("Note: " .. notes[k].name .. " (" .. f .. " Hz)")
                sys.beep(f, 70)
            end
        end
    elseif wasT and activeKey > 0 then
        drawKey(activeKey, false)
        activeKey = -1
    end
    wasT = pressed
    sys.delay(18)
end
