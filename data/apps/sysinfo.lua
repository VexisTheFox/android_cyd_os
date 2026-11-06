-- ===================================================================
-- CYD Android OS - Hardware Telemetry & Kernel Log Viewer (Dev Build)
-- Target Hardware: ESP32-2432S028 (240x320 LCD + XPT2046 Touch)
-- ===================================================================
-- Metadata Header:
-- @name: SysInfo
-- @icon: sysinfo
-- @author: CYD Team
-- @version: 2.1.0
-- @description: Live system telemetry, memory, storage, and developer kernel.log

local sysinfo = {
    ledState = 0, -- 0=Off, 1=Red, 2=Green, 3=Blue
    viewMode = "telemetry", -- "telemetry" or "log"
    logLines = {},
    logColors = {},
    logScroll = 1,
    linesPerPage = 18,
    theme = {
        bg = 0x0821,
        cardBg = 0x18E3,
        cardBorder = 0x4208,
        titleText = 0x07FF,
        label = 0xBDD7,
        value = 0xFFFF,
        accent = 0x07E0,
        amber = 0xFFE0
    }
}

local VP_X = 0
local VP_Y = 20
local VP_W = 240
local VP_H = 274

local function drawCard(x, y, w, h, title)
    gui.fillRect(x, y, w, h, sysinfo.theme.cardBg)
    gui.drawRect(x, y, w, h, sysinfo.theme.cardBorder)
    gui.drawText(x + 6, y + 4, title, sysinfo.theme.titleText, 1)
    gui.drawFastHLine(x, y + 16, w, sysinfo.theme.cardBorder)
end

local function drawBar(x, y, w, h, percent, fillColor)
    gui.drawRect(x, y, w, h, 0x4208)
    local fillW = math.floor((w - 2) * math.min(1.0, math.max(0.0, percent / 100.0)))
    if fillW > 0 then
        gui.fillRect(x + 1, y + 1, fillW, h - 2, fillColor)
    end
    if fillW < (w - 2) then
        gui.fillRect(x + 1 + fillW, y + 1, (w - 2) - fillW, h - 2, 0x0000)
    end
end

function sysinfo.loadKernelLog()
    sysinfo.logLines = {}
    sysinfo.logColors = {}

    if sys and sys.syncKernelLog then
        sys.syncKernelLog()
    end

    local raw = ""
    if sys and sys.getKernelLog then
        raw = sys.getKernelLog() or ""
    elseif file and file.read then
        raw = file.read("/kernel.log") or ""
    end

    if raw == "" then
        table.insert(sysinfo.logLines, "(kernel.log is empty / deleted)")
        table.insert(sysinfo.logColors, 0xBDD7)
        sysinfo.logScroll = 1
        return
    end

    for line in string.gmatch(raw .. "\n", "([^\r\n]*)\r?\n") do
        if #line > 0 then
            local col = 0xCE79
            if string.find(line, "%[FATAL%]") then
                col = 0xF800
            elseif string.find(line, "%[ERROR%]") then
                col = 0xFA68
            elseif string.find(line, "%[WARN%]") then
                col = 0xFFE0
            elseif string.find(line, "%[INFO%]") then
                col = 0x07E0
            end

            local pos = 1
            while pos <= #line do
                local chunk = string.sub(line, pos, pos + 35)
                if pos > 1 then
                    chunk = "  " .. chunk
                end
                table.insert(sysinfo.logLines, chunk)
                table.insert(sysinfo.logColors, col)
                pos = pos + 36
            end
        end
    end

    local maxScroll = math.max(1, #sysinfo.logLines - sysinfo.linesPerPage + 1)
    sysinfo.logScroll = maxScroll
end

function sysinfo.renderLogViewer()
    gui.fillRect(VP_X, VP_Y, VP_W, VP_H, 0x0000)

    gui.fillRect(VP_X, VP_Y, VP_W, 20, 0x18E3)
    gui.drawFastHLine(VP_X, VP_Y + 20, VP_W, 0x07FF)
    gui.drawText(VP_X + 6, VP_Y + 6, "KERNEL.LOG [DEV]", 0x07FF, 1)

    local sdMounted = sys and sys.isSDMounted and sys.isSDMounted() or false
    if sdMounted then
        gui.drawText(VP_X + 114, VP_Y + 6, "[SYNCED: /sd/kernel.log]", 0x07E0, 1)
    else
        gui.drawText(VP_X + 130, VP_Y + 6, "[FLASH: /kernel.log]", 0xFFE0, 1)
    end

    local total = #sysinfo.logLines
    local startIdx = sysinfo.logScroll
    local endIdx = math.min(total, startIdx + sysinfo.linesPerPage - 1)

    local y = VP_Y + 24
    for i = startIdx, endIdx do
        gui.drawText(VP_X + 4, y, sysinfo.logLines[i], sysinfo.logColors[i] or 0xFFFF, 1)
        y = y + 12
    end

    gui.drawFastHLine(VP_X, VP_Y + 241, VP_W, 0x4208)

    -- 1. < BACK (X=6, W=52)
    gui.fillRect(VP_X + 6, VP_Y + 244, 52, 26, 0x2124)
    gui.drawRect(VP_X + 6, VP_Y + 244, 52, 26, 0x07FF)
    gui.drawText(VP_X + 14, VP_Y + 253, "< BACK", 0x07FF, 1)

    -- 2. UP (X=62, W=52)
    gui.fillRect(VP_X + 62, VP_Y + 244, 52, 26, 0x18E3)
    gui.drawRect(VP_X + 62, VP_Y + 244, 52, 26, 0xBDD7)
    gui.drawText(VP_X + 76, VP_Y + 253, "UP ^", 0xFFFF, 1)

    -- 3. DOWN (X=118, W=52)
    gui.fillRect(VP_X + 118, VP_Y + 244, 52, 26, 0x18E3)
    gui.drawRect(VP_X + 118, VP_Y + 244, 52, 26, 0xBDD7)
    gui.drawText(VP_X + 126, VP_Y + 253, "DOWN v", 0xFFFF, 1)

    -- 4. DELETE LOGS (X=174, W=60)
    gui.fillRect(VP_X + 174, VP_Y + 244, 60, 26, 0x4000)
    gui.drawRect(VP_X + 174, VP_Y + 244, 60, 26, 0xF800)
    gui.drawText(VP_X + 186, VP_Y + 253, "DELETE", 0xFFFF, 1)
end

function sysinfo.render()
    if not gui or not gui.fillRect then return end

    if sysinfo.viewMode == "log" then
        sysinfo.renderLogViewer()
        return
    end

    gui.fillRect(VP_X, VP_Y, VP_W, VP_H, sysinfo.theme.bg)

    -- Card 1: SoC & Memory (Top: X=6, Y=VP_Y+4, W=228, H=66)
    drawCard(VP_X + 6, VP_Y + 4, 228, 66, "CPU & RAM [DEVELOPMENT BUILD]")
    gui.drawText(VP_X + 12, VP_Y + 23, "ESP32 Dual-Core 240MHz | DEV-2.0", sysinfo.theme.amber, 1)

    local heap = sys and sys.getHeap and sys.getHeap() or 180000
    local maxHeap = 327680
    local heapPct = math.floor((heap / maxHeap) * 100)
    gui.drawText(VP_X + 12, VP_Y + 37, string.format("Free: %d KB (%d%% Avail)", math.floor(heap / 1024), heapPct), sysinfo.theme.value, 1)
    drawBar(VP_X + 12, VP_Y + 51, 216, 12, heapPct, 0x07E0)

    -- Card 2: Storage Status (Middle: X=6, Y=VP_Y+74, W=228, H=74)
    drawCard(VP_X + 6, VP_Y + 74, 228, 74, "STORAGE & LOG MIRROR")
    local totalKb = sys and sys.getStorageTotal and math.floor(sys.getStorageTotal() / 1024) or 1920
    local usedKb = sys and sys.getStorageUsed and math.floor(sys.getStorageUsed() / 1024) or 120
    local storePct = math.floor((usedKb / math.max(1, totalKb)) * 100)

    gui.drawText(VP_X + 12, VP_Y + 93, string.format("LittleFS: %d / %d KB (%d%%)", usedKb, totalKb, storePct), sysinfo.theme.value, 1)
    drawBar(VP_X + 12, VP_Y + 107, 216, 10, storePct, 0x07FF)

    local sdMounted = sys and sys.isSDMounted and sys.isSDMounted() or false
    if sdMounted then
        gui.drawText(VP_X + 12, VP_Y + 123, "SD: MOUNTED -> /sd/kernel.log", 0x07E0, 1)
    else
        gui.drawText(VP_X + 12, VP_Y + 123, "SD: NONE (Flash /kernel.log)", 0xFD20, 1)
    end

    -- Card 3: Peripherals & Sensors (Bottom: X=6, Y=VP_Y+152, W=228, H=82)
    drawCard(VP_X + 6, VP_Y + 152, 228, 82, "PERIPHERALS & SENSORS")

    local ldr = sys and sys.getLdr and sys.getLdr() or 2048
    local ldrPct = math.floor((ldr / 4095) * 100)
    local sec = sys and sys.getUptime and sys.getUptime() or 0
    local hrs = math.floor(sec / 3600)
    local mins = math.floor((sec % 3600) / 60)
    local secs = sec % 60
    gui.drawText(VP_X + 12, VP_Y + 171, string.format("LDR: %d (%d%%)  Up: %02d:%02d:%02d", ldr, ldrPct, hrs, mins, secs), sysinfo.theme.value, 1)
    drawBar(VP_X + 12, VP_Y + 185, 216, 8, ldrPct, 0xFFE0)

    -- Button 1: LED Cycle (X=12, Y=VP_Y+200, W=104, H=26)
    local ledLabels = { [0] = "LED: OFF", [1] = "LED: RED", [2] = "LED: GRN", [3] = "LED: BLU" }
    local ledColors = { [0] = 0x4208, [1] = 0xF800, [2] = 0x07E0, [3] = 0x001F }
    gui.fillRect(VP_X + 12, VP_Y + 200, 104, 26, ledColors[sysinfo.ledState])
    gui.drawRect(VP_X + 12, VP_Y + 200, 104, 26, 0xFFFF)
    gui.drawText(VP_X + 34, VP_Y + 209, ledLabels[sysinfo.ledState], 0xFFFF, 1)

    -- Button 2: Chime Test (X=124, Y=VP_Y+200, W=104, H=26)
    gui.fillRect(VP_X + 124, VP_Y + 200, 104, 26, 0x2124)
    gui.drawRect(VP_X + 124, VP_Y + 200, 104, 26, 0x07FF)
    gui.drawText(VP_X + 144, VP_Y + 209, "PLAY CHIME", 0x07FF, 1)

    -- Button 3: VIEW KERNEL.LOG (gated by Developer Mode!)
    local devUnlocked = (sys and sys.isDevMode and sys.isDevMode()) or false
    if devUnlocked then
        gui.fillRect(VP_X + 6, VP_Y + 239, 228, 29, 0x0210)
        gui.drawRect(VP_X + 6, VP_Y + 239, 228, 29, 0x07E0)
        gui.drawText(VP_X + 30, VP_Y + 249, "VIEW / DELETE KERNEL.LOG [DEV]", 0x07E0, 1)
    else
        gui.fillRect(VP_X + 6, VP_Y + 239, 228, 29, 0x2124)
        gui.drawRect(VP_X + 6, VP_Y + 239, 228, 29, 0xFFE0)
        gui.drawText(VP_X + 18, VP_Y + 249, "LOGS LOCKED: TAP BUILD 5X IN SET", 0xFFE0, 1)
    end
end

function sysinfo.handleTouch(x, y)
    if sysinfo.viewMode == "log" then
        if y >= (VP_Y + 240) and y <= (VP_Y + 274) then
            if x >= (VP_X + 6) and x <= (VP_X + 58) then
                sysinfo.viewMode = "telemetry"
                if sys and sys.beep then sys.beep(900, 20) end
                sysinfo.render()
                return true
            elseif x >= (VP_X + 62) and x <= (VP_X + 114) then
                sysinfo.logScroll = math.max(1, sysinfo.logScroll - 8)
                if sys and sys.beep then sys.beep(1100, 15) end
                sysinfo.renderLogViewer()
                return true
            elseif x >= (VP_X + 118) and x <= (VP_X + 170) then
                local maxScroll = math.max(1, #sysinfo.logLines - sysinfo.linesPerPage + 1)
                sysinfo.logScroll = math.min(maxScroll, sysinfo.logScroll + 8)
                if sys and sys.beep then sys.beep(1100, 15) end
                sysinfo.renderLogViewer()
                return true
            elseif x >= (VP_X + 174) and x <= (VP_X + 234) then
                if sys and sys.clearKernelLog then
                    sys.clearKernelLog()
                end
                if sys and sys.beep then sys.beep(600, 40) end
                sysinfo.loadKernelLog()
                sysinfo.renderLogViewer()
                return true
            end
        end
        return false
    end

    -- Button 1: LED Cycle
    if x >= (VP_X + 12) and x <= (VP_X + 116) and y >= (VP_Y + 200) and y <= (VP_Y + 228) then
        sysinfo.ledState = (sysinfo.ledState + 1) % 4
        if sys and sys.setLED then
            if sysinfo.ledState == 0 then sys.setLED(0, 0, 0)
            elseif sysinfo.ledState == 1 then sys.setLED(1, 0, 0)
            elseif sysinfo.ledState == 2 then sys.setLED(0, 1, 0)
            elseif sysinfo.ledState == 3 then sys.setLED(0, 0, 1)
            end
        end
        if sys and sys.beep then sys.beep(1000, 20) end
        sysinfo.render()
        return true
    end

    -- Button 2: Audio Chime
    if x >= (VP_X + 124) and x <= (VP_X + 228) and y >= (VP_Y + 200) and y <= (VP_Y + 228) then
        if sys and sys.beep then
            sys.beep(523, 60)
            if sys.delay then sys.delay(70) end
            sys.beep(659, 60)
            if sys.delay then sys.delay(70) end
            sys.beep(784, 100)
        end
        return true
    end

    -- Button 3: VIEW KERNEL.LOG (gated by Developer Mode)
    if x >= (VP_X + 6) and x <= (VP_X + 234) and y >= (VP_Y + 236) and y <= (VP_Y + 272) then
        local devUnlocked = (sys and sys.isDevMode and sys.isDevMode()) or false
        if not devUnlocked then
            if sys and sys.beep then sys.beep(400, 60) end
            return true
        end
        if sys and sys.beep then sys.beep(1200, 25) end
        sysinfo.loadKernelLog()
        sysinfo.viewMode = "log"
        sysinfo.render()
        return true
    end

    if y >= VP_Y and y < (VP_Y + 198) then
        if sys and sys.beep then sys.beep(1200, 15) end
        sysinfo.render()
        return true
    end

    return false
end

function app_main()
    sysinfo.render()
    local lastTouched = false

    while true do
        if input and input.getTouch then
            local tx, ty, touched = input.getTouch()
            if touched and not lastTouched then
                sysinfo.handleTouch(tx, ty)
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

app_main()
