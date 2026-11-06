-- ===================================================================
-- CYD Android OS - Lua IDE & Scratchpad Terminal
-- Target Hardware: ESP32-2432S028 (320x240 LCD + XPT2046 Touch)
-- ===================================================================
-- Metadata Header:
-- @name: Lua IDE
-- @icon: terminal
-- @author: CYD Team
-- @version: 1.0.0
-- @description: Dynamic Lua 5.4 runtime scratchpad and test runner

local ide = {
    selectedProgram = 1,
    consoleLog = {
        "Lua 5.4.6 runtime initialized (32-bit).",
        "Ready. Select a program below and tap RUN."
    },
    theme = {
        bg = 0x0821,
        headerBg = 0x1CE6,  -- Terminal Green
        terminalBg = 0x0000,
        cardBg = 0x18C3,
        cardBorder = 0x4208,
        cyan = 0x07FF,
        green = 0x07E0,
        yellow = 0xFFE0,
        white = 0xFFFF
    },
    programs = {
        { name = "1. Play Chime", desc = "Tone arpeggio test" },
        { name = "2. Rainbow LED", desc = "Hardware RGB cycle" },
        { name = "3. Draw Canvas", desc = "Vector graphics demo" },
        { name = "4. Benchmark",  desc = "Compute & heap meter" }
    }
}

local VP_X = 0
local VP_Y = 20
local VP_W = 240
local VP_H = 274

function ide.log(msg)
    table.insert(ide.consoleLog, msg)
    if #ide.consoleLog > 11 then
        table.remove(ide.consoleLog, 1)
    end
end

function ide.render()
    if not gui or not gui.fillRect then return end

    -- Background
    gui.fillRect(VP_X, VP_Y, VP_W, VP_H, ide.theme.bg)

    -- Header
    gui.fillRect(VP_X, VP_Y, VP_W, 24, 0x10A2)
    gui.drawFastHLine(VP_X, VP_Y + 24, VP_W, ide.theme.headerBg)
    gui.drawText(VP_X + 6, VP_Y + 4, "Lua 5.4 IDE", ide.theme.headerBg, 2)
    local heap = sys and sys.getHeap and math.floor(sys.getHeap() / 1024) or 160
    gui.drawText(VP_X + 162, VP_Y + 8, string.format("Heap:%dK", heap), ide.theme.cyan, 1)

    -- Program Selection Grid (2x2)
    local btnW = 110
    local btnH = 28
    for i, prog in ipairs(ide.programs) do
        local col = (i - 1) % 2
        local row = math.floor((i - 1) / 2)
        local bx = VP_X + 6 + col * (btnW + 8)
        local by = VP_Y + 30 + row * (btnH + 4)

        local isSel = (i == ide.selectedProgram)
        local bgCol = isSel and 0x2965 or ide.theme.cardBg
        local borderCol = isSel and ide.theme.green or ide.theme.cardBorder

        gui.fillRect(bx, by, btnW, btnH, bgCol)
        gui.drawRect(bx, by, btnW, btnH, borderCol)
        gui.drawText(bx + 4, by + 4, prog.name, isSel and ide.theme.white or 0xDEFB, 1)
        gui.drawText(bx + 4, by + 15, prog.desc, ide.theme.cyan, 1)
    end

    -- Run Button
    local runY = VP_Y + 98
    gui.fillRect(VP_X + 6, runY, VP_W - 12, 24, 0x1B06)
    gui.drawRect(VP_X + 6, runY, VP_W - 12, 24, ide.theme.green)
    gui.drawText(VP_X + 64, runY + 8, "RUN SELECTED SCRIPT", ide.theme.green, 1)

    -- Terminal Console Window
    local conY = VP_Y + 128
    local conH = VP_H - 134
    gui.fillRect(VP_X + 6, conY, VP_W - 12, conH, ide.theme.terminalBg)
    gui.drawRect(VP_X + 6, conY, VP_W - 12, conH, 0x3186)
    gui.drawText(VP_X + 10, conY + 4, "CONSOLE LOG:", ide.theme.yellow, 1)

    for idx, line in ipairs(ide.consoleLog) do
        local dispLine = line
        if #dispLine > 34 then dispLine = string.sub(dispLine, 1, 32) .. ".." end
        gui.drawText(VP_X + 10, conY + 16 + (idx - 1) * 10, "> " .. dispLine, ide.theme.green, 1)
    end
end

function ide.executeSelected()
    local idx = ide.selectedProgram
    ide.log("Executing Program #" .. idx .. "...")

    if idx == 1 then
        -- Melody
        if sys and sys.beep then
            sys.beep(523, 70); if sys.delay then sys.delay(80) end
            sys.beep(659, 70); if sys.delay then sys.delay(80) end
            sys.beep(784, 70); if sys.delay then sys.delay(80) end
            sys.beep(1046, 120)
        end
        ide.log("Chime finished (C5-E5-G5-C6).")

    elseif idx == 2 then
        -- RGB LED
        if sys and sys.setLED then
            sys.setLED(1, 0, 0); if sys.delay then sys.delay(120) end
            sys.setLED(0, 1, 0); if sys.delay then sys.delay(120) end
            sys.setLED(0, 0, 1); if sys.delay then sys.delay(120) end
            sys.setLED(0, 0, 0)
        end
        ide.log("RGB LED cycled: R -> G -> B.")

    elseif idx == 3 then
        -- Graphics
        if gui and gui.fillRect then
            gui.fillRect(60, 100, 120, 60, 0x18C3)
            gui.drawRect(60, 100, 120, 60, 0x07FF)
            gui.fillCircle(120, 130, 20, 0xF800)
            gui.drawText(72, 124, "Lua Vector OK", 0xFFFF, 1)
            if sys and sys.delay then sys.delay(350) end
        end
        ide.log("Vector graphics drawn to canvas.")

    elseif idx == 4 then
        -- Benchmark
        local count = 0
        for i = 1, 10000 do
            count = count + i
        end
        local hp = sys and sys.getHeap and sys.getHeap() or 0
        ide.log(string.format("10k loops OK. Free=%d B", hp))
    end

    ide.render()
end

function ide.handleTouch(x, y)
    -- Program selection buttons
    local btnW = 110
    local btnH = 28
    for i = 1, #ide.programs do
        local col = (i - 1) % 2
        local row = math.floor((i - 1) / 2)
        local bx = VP_X + 6 + col * (btnW + 8)
        local by = VP_Y + 30 + row * (btnH + 4)

        if x >= bx and x <= (bx + btnW) and y >= by and y <= (by + btnH) then
            ide.selectedProgram = i
            if sys and sys.beep then sys.beep(1200, 15) end
            ide.render()
            return true
        end
    end

    -- Run Button
    local runY = VP_Y + 98
    if x >= (VP_X + 6) and x <= (VP_X + VP_W - 6) and y >= runY and y <= (runY + 26) then
        if sys and sys.beep then sys.beep(1500, 25) end
        ide.executeSelected()
        return true
    end

    return false
end

function app_main()
    ide.render()
    local lastTouched = false

    while true do
        if input and input.getTouch then
            local tx, ty, touched = input.getTouch()
            if touched and not lastTouched then
                ide.handleTouch(tx, ty)
                lastTouched = true
            elseif not touched and lastTouched then
                lastTouched = false
            end
        end

        if sys and sys.delay then
            sys.delay(30)
        end
    end
end

-- Run application
app_main()
