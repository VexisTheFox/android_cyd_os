-- ===================================================================
-- CYD Android OS - File Manager & SD Card App Installer (Dev Build)
-- Target Hardware: ESP32-2432S028 (240x320 LCD + XPT2046 Touch)
-- ===================================================================
-- Metadata Header:
-- @name: Files
-- @icon: files
-- @author: CYD Team
-- @version: 2.1.0
-- @description: LittleFS & SD file browser with Install to Home Screen & Local Storage

local files = {
    currentTab = "sd",     -- "sd" or "lfs"
    selectedIdx = 1,
    pageOffset = 1,
    viewMode = "list",     -- "list" or "viewer"
    viewPath = "",
    viewContent = "",
    fileList = {},
    statusMsg = "",
    theme = {
        bg = 0x0821,
        headerBg = 0x18C3,
        tabActive = 0xFD00,   -- Amber / Gold
        tabInactive = 0x2124,
        cardBg = 0x10A2,
        cardBorder = 0x324B,
        cardSel = 0x2965,
        green = 0x07E0,
        cyan = 0x07FF,
        white = 0xFFFF,
        yellow = 0xFFE0,
        muted = 0x9CD3,
        danger = 0xF800
    }
}

local VP_X = 0
local VP_Y = 20
local VP_W = 240
local VP_H = 274

-- Scan current directory
function files.refresh()
    files.fileList = {}
    local path = (files.currentTab == "sd") and "/sd" or "/apps"

    -- Auto-probe SD if on SD tab and not currently mounted
    if files.currentTab == "sd" and sys and sys.isSDMounted and not sys.isSDMounted() then
        if sys.probeSD then
            sys.probeSD()
        end
    end

    if file and file.list then
        local raw = file.list(path)
        if raw and #raw > 0 then
            files.fileList = raw
        end
    end

    if #files.fileList == 0 and files.currentTab == "lfs" then
        files.fileList = {
            { name = "gallery.lua", path = "/apps/gallery.lua", size = 5400, isLua = true, isSD = false },
            { name = "files.lua", path = "/apps/files.lua", size = 5200, isLua = true, isSD = false },
            { name = "sysinfo.lua", path = "/apps/sysinfo.lua", size = 6650, isLua = true, isSD = false },
            { name = "calc.lua", path = "/apps/calc.lua", size = 6349, isLua = true, isSD = false },
            { name = "paint.lua", path = "/apps/paint.lua", size = 6800, isLua = true, isSD = false },
            { name = "terminal.lua", path = "/apps/terminal.lua", size = 4800, isLua = true, isSD = false }
        }
    end

    if files.selectedIdx > #files.fileList then
        files.selectedIdx = math.max(1, #files.fileList)
    end
end

function files.installSelected(item)
    if not item or not item.isLua or not item.path then return end
    if sys and sys.installApp then
        local ok, dest = sys.installApp(item.path)
        if ok then
            files.statusMsg = "INSTALLED TO HOME: " .. (item.name or "")
            if sys.beep then
                sys.beep(1046, 40)
                if sys.delay then sys.delay(45) end
                sys.beep(1568, 70)
            end
        else
            files.statusMsg = "Install failed: " .. tostring(dest)
            if sys.beep then sys.beep(400, 80) end
        end
    end
    files.render()
end

function files.render()
    if not gui or not gui.fillRect then return end

    -- Background
    gui.fillRect(VP_X, VP_Y, VP_W, VP_H, files.theme.bg)

    -- Header / Title Bar (Y: 20..44)
    gui.fillRect(VP_X, VP_Y, VP_W, 24, files.theme.headerBg)
    gui.drawFastHLine(VP_X, VP_Y + 24, VP_W, files.theme.tabActive)
    gui.drawText(VP_X + 6, VP_Y + 4, "Files & Installer", files.theme.white, 2)
    gui.drawText(VP_X + 124, VP_Y + 8, "[DEV]", files.theme.yellow, 1)

    -- Storage badge
    local sd = sys and sys.isSDMounted and sys.isSDMounted() or false
    local sdCol = sd and files.theme.green or files.theme.yellow
    local sdLabel = sd and "[SD:OK]" or "[NO SD]"
    gui.drawText(VP_X + 188, VP_Y + 8, sdLabel, sdCol, 1)

    -- Tab Bar (Y: 48..70)
    local isSdTab = (files.currentTab == "sd")
    gui.fillRect(VP_X + 6, VP_Y + 28, 110, 22, isSdTab and files.theme.tabActive or files.theme.tabInactive)
    gui.drawRect(VP_X + 6, VP_Y + 28, 110, 22, isSdTab and files.theme.white or files.theme.cardBorder)
    gui.drawText(VP_X + 18, VP_Y + 35, "SD CARD (/sd)", isSdTab and 0x0000 or files.theme.white, 1)

    local isLfsTab = (files.currentTab == "lfs")
    gui.fillRect(VP_X + 124, VP_Y + 28, 110, 22, isLfsTab and files.theme.tabActive or files.theme.tabInactive)
    gui.drawRect(VP_X + 124, VP_Y + 28, 110, 22, isLfsTab and files.theme.white or files.theme.cardBorder)
    gui.drawText(VP_X + 136, VP_Y + 35, "FLASH (/apps)", isLfsTab and 0x0000 or files.theme.white, 1)

    if files.viewMode == "viewer" then
        files.renderViewer()
        return
    end

    if files.currentTab == "sd" and not sd then
        files.renderNoSD()
        return
    end

    files.renderList()
end

function files.renderNoSD()
    local boxY = VP_Y + 58
    gui.fillRect(VP_X + 8, boxY, VP_W - 16, 126, files.theme.cardBg)
    gui.drawRect(VP_X + 8, boxY, VP_W - 16, 126, files.theme.cardBorder)

    gui.drawText(VP_X + 20, boxY + 12, "SD Card Not Detected", files.theme.yellow, 2)
    gui.drawText(VP_X + 18, boxY + 36, "Insert FAT32 MicroSD card,", files.theme.muted, 1)
    gui.drawText(VP_X + 18, boxY + 50, "place .lua apps in /sd or /apps", files.theme.muted, 1)
    gui.drawText(VP_X + 18, boxY + 64, "to run or install to Home Screen:", files.theme.muted, 1)

    gui.fillRect(VP_X + 20, boxY + 86, 200, 28, 0x1B06)
    gui.drawRect(VP_X + 20, boxY + 86, 200, 28, files.theme.green)
    gui.drawText(VP_X + 38, boxY + 96, "MOUNT / PROBE SD CARD", files.theme.green, 1)
end

function files.renderList()
    local startY = VP_Y + 54
    local itemH = 26
    local maxDisplay = 5
    local total = #files.fileList

    if total == 0 then
        gui.fillRect(VP_X + 8, startY, VP_W - 16, 40, files.theme.cardBg)
        gui.drawRect(VP_X + 8, startY, VP_W - 16, 40, files.theme.cardBorder)
        gui.drawText(VP_X + 16, startY + 14, "No .lua files found on volume.", files.theme.muted, 1)
    else
        for i = 1, maxDisplay do
            local itemIdx = files.pageOffset + (i - 1)
            if itemIdx <= total then
                local item = files.fileList[itemIdx]
                local iy = startY + (i - 1) * (itemH + 3)
                local isSel = (itemIdx == files.selectedIdx)
                local bgCol = isSel and files.theme.cardSel or files.theme.cardBg
                local bdrCol = isSel and files.theme.tabActive or files.theme.cardBorder

                gui.fillRect(VP_X + 6, iy, VP_W - 12, itemH, bgCol)
                gui.drawRect(VP_X + 6, iy, VP_W - 12, itemH, bdrCol)

                local badgeBg = item.isLua and files.theme.green or 0x4208
                gui.fillRect(VP_X + 10, iy + 3, 24, 20, badgeBg)
                gui.drawText(VP_X + 13, iy + 9, "LUA", 0x0000, 1)

                local dispName = item.name or ""
                if #dispName > 16 then dispName = string.sub(dispName, 1, 14) .. ".." end
                local nameCol = isSel and files.theme.white or 0xDEFB
                gui.drawText(VP_X + 40, iy + 5, dispName, nameCol, 2)

                local szStr = string.format("%dB", item.size or 0)
                if (item.size or 0) >= 1024 then
                    szStr = string.format("%.1fK", (item.size or 0) / 1024)
                end
                gui.drawText(VP_X + 190, iy + 9, szStr, files.theme.cyan, 1)
            else
                local iy = startY + (i - 1) * (itemH + 3)
                gui.fillRect(VP_X + 6, iy, VP_W - 12, itemH, files.theme.bg)
            end
        end
    end

    -- Status / Pagination Row (Y: VP_Y + 204)
    local py = VP_Y + 204
    gui.fillRect(VP_X + 6, py, VP_W - 12, 22, files.theme.bg)
    if files.pageOffset > 1 then
        gui.fillRect(VP_X + 6, py, 46, 20, files.theme.cardBg)
        gui.drawRect(VP_X + 6, py, 46, 20, files.theme.cyan)
        gui.drawText(VP_X + 12, py + 6, "< UP", files.theme.cyan, 1)
    end
    if (files.pageOffset + maxDisplay - 1) < total then
        gui.fillRect(VP_X + 56, py, 50, 20, files.theme.cardBg)
        gui.drawRect(VP_X + 56, py, 50, 20, files.theme.cyan)
        gui.drawText(VP_X + 62, py + 6, "DOWN >", files.theme.cyan, 1)
    end

    if files.statusMsg ~= "" then
        local msg = files.statusMsg
        if #msg > 21 then msg = string.sub(msg, 1, 21) end
        gui.drawText(VP_X + 110, py + 6, msg, files.theme.green, 1)
    else
        gui.drawText(VP_X + 110, py + 6, "DEV BUILD INSTALLER", files.theme.muted, 1)
    end

    -- Bottom Action Bar (4 Buttons: Y = VP_Y + VP_H - 30)
    local by = VP_Y + VP_H - 30
    local selItem = files.fileList[files.selectedIdx]

    -- Button 1: RUN (X=6, W=54)
    gui.fillRect(VP_X + 6, by, 54, 26, (selItem and selItem.isLua) and 0x03E0 or files.theme.cardBg)
    gui.drawRect(VP_X + 6, by, 54, 26, files.theme.white)
    gui.drawText(VP_X + 14, by + 9, "> RUN", (selItem and selItem.isLua) and 0x0000 or files.theme.muted, 1)

    -- Button 2: INSTALL TO HOME & FLASH (X=64, W=64)
    local canInstall = (selItem and selItem.isLua and files.currentTab == "sd")
    gui.fillRect(VP_X + 64, by, 64, 26, canInstall and 0x04D3 or files.theme.cardBg)
    gui.drawRect(VP_X + 64, by, 64, 26, canInstall and files.theme.cyan or files.theme.cardBorder)
    gui.drawText(VP_X + 70, by + 9, "+INSTALL", canInstall and files.theme.white or files.theme.muted, 1)

    -- Button 3: VIEW CODE (X=132, W=48)
    gui.fillRect(VP_X + 132, by, 48, 26, files.theme.cardBg)
    gui.drawRect(VP_X + 132, by, 48, 26, files.theme.tabActive)
    gui.drawText(VP_X + 142, by + 9, "VIEW", files.theme.white, 1)

    -- Button 4: +DEMO SD or REFRESH (X=184, W=50)
    if files.currentTab == "sd" then
        gui.fillRect(VP_X + 184, by, 50, 26, 0x10A2)
        gui.drawRect(VP_X + 184, by, 50, 26, files.theme.green)
        gui.drawText(VP_X + 188, by + 9, "+DEMO", files.theme.green, 1)
    else
        gui.fillRect(VP_X + 184, by, 50, 26, 0x10A2)
        gui.drawRect(VP_X + 184, by, 50, 26, files.theme.cyan)
        gui.drawText(VP_X + 192, by + 9, "SCAN", files.theme.cyan, 1)
    end
end

function files.renderViewer()
    local vy = VP_Y + 54
    local vh = VP_H - 90
    gui.fillRect(VP_X + 6, vy, VP_W - 12, vh, 0x0000)
    gui.drawRect(VP_X + 6, vy, VP_W - 12, vh, files.theme.cardBorder)

    gui.drawText(VP_X + 12, vy + 4, "File: " .. files.viewPath, files.theme.cyan, 1)
    gui.drawFastHLine(VP_X + 6, vy + 16, VP_W - 12, files.theme.cardBorder)

    local lines = {}
    local p = files.viewContent
    if p and #p > 0 then
        for line in string.gmatch(p, "[^\r\n]+") do
            table.insert(lines, line)
            if #lines >= 14 then break end
        end
    else
        table.insert(lines, "(File is empty or binary)")
    end

    for idx, l in ipairs(lines) do
        if #l > 32 then l = string.sub(l, 1, 30) .. ".." end
        gui.drawText(VP_X + 10, vy + 20 + (idx - 1) * 11, string.format("%d:%s", idx, l), files.theme.green, 1)
    end

    local by = VP_Y + VP_H - 30
    -- Run button
    gui.fillRect(VP_X + 6, by, 72, 26, 0x03E0)
    gui.drawRect(VP_X + 6, by, 72, 26, files.theme.white)
    gui.drawText(VP_X + 18, by + 9, "> RUN", 0x0000, 1)

    -- Install to Home button
    gui.fillRect(VP_X + 82, by, 76, 26, 0x04D3)
    gui.drawRect(VP_X + 82, by, 76, 26, files.theme.cyan)
    gui.drawText(VP_X + 90, by + 9, "+INSTALL", files.theme.white, 1)

    -- Back to list button
    gui.fillRect(VP_X + 162, by, 72, 26, files.theme.cardBg)
    gui.drawRect(VP_X + 162, by, 72, 26, files.theme.tabActive)
    gui.drawText(VP_X + 176, by + 9, "< BACK", files.theme.white, 1)
end

function files.launchSelected(targetPath)
    if not targetPath or targetPath == "" then return end
    if sys and sys.beep then sys.beep(1600, 40) end
    if sys and sys.launch then
        sys.launch(targetPath)
    elseif sys and sys.runScript then
        sys.runScript(targetPath)
    end
end

function files.handleTouch(x, y)
    -- 1. Tab Bar Touches
    if y >= (VP_Y + 26) and y <= (VP_Y + 52) then
        if x >= (VP_X + 6) and x <= (VP_X + 118) then
            files.currentTab = "sd"
            files.pageOffset = 1
            files.selectedIdx = 1
            files.viewMode = "list"
            files.statusMsg = ""
            if sys and sys.beep then sys.beep(1200, 15) end
            files.refresh()
            files.render()
            return true
        elseif x >= (VP_X + 122) and x <= (VP_X + 236) then
            files.currentTab = "lfs"
            files.pageOffset = 1
            files.selectedIdx = 1
            files.viewMode = "list"
            files.statusMsg = ""
            if sys and sys.beep then sys.beep(1200, 15) end
            files.refresh()
            files.render()
            return true
        end
    end

    -- 2. Viewer Mode Touches
    if files.viewMode == "viewer" then
        local by = VP_Y + VP_H - 30
        if y >= by and y <= (by + 28) then
            if x >= (VP_X + 6) and x <= (VP_X + 78) then
                files.launchSelected(files.viewPath)
                return true
            elseif x >= (VP_X + 82) and x <= (VP_X + 158) then
                local selItem = files.fileList[files.selectedIdx]
                if selItem then
                    files.viewMode = "list"
                    files.installSelected(selItem)
                end
                return true
            elseif x >= (VP_X + 162) and x <= (VP_X + 236) then
                files.viewMode = "list"
                if sys and sys.beep then sys.beep(800, 15) end
                files.render()
                return true
            end
        end
        return false
    end

    -- 3. SD Not Mounted screen touch
    local sd = sys and sys.isSDMounted and sys.isSDMounted() or false
    if files.currentTab == "sd" and not sd then
        local boxY = VP_Y + 58
        if y >= (boxY + 80) and y <= (boxY + 120) and x >= (VP_X + 16) and x <= (VP_X + 224) then
            if sys and sys.probeSD then sys.probeSD() end
            if sys and sys.beep then sys.beep(1000, 30) end
            files.refresh()
            files.render()
            return true
        end
        return false
    end

    -- 4. File List Selection
    local startY = VP_Y + 54
    local itemH = 26
    local maxDisplay = 5
    for i = 1, maxDisplay do
        local itemIdx = files.pageOffset + (i - 1)
        if itemIdx <= #files.fileList then
            local iy = startY + (i - 1) * (itemH + 3)
            if x >= (VP_X + 6) and x <= (VP_X + VP_W - 6) and y >= iy and y <= (iy + itemH) then
                files.selectedIdx = itemIdx
                if sys and sys.beep then sys.beep(1100, 15) end
                files.renderList()
                return true
            end
        end
    end

    -- 5. Pagination Touches
    local py = VP_Y + 204
    if y >= py and y <= (py + 24) then
        if x >= (VP_X + 6) and x <= (VP_X + 54) and files.pageOffset > 1 then
            files.pageOffset = math.max(1, files.pageOffset - 5)
            if sys and sys.beep then sys.beep(900, 15) end
            files.renderList()
            return true
        elseif x >= (VP_X + 56) and x <= (VP_X + 108) and (files.pageOffset + maxDisplay - 1) < #files.fileList then
            files.pageOffset = files.pageOffset + 5
            if sys and sys.beep then sys.beep(900, 15) end
            files.renderList()
            return true
        end
    end

    -- 6. Bottom Action Bar Touches
    local by = VP_Y + VP_H - 30
    if y >= by and y <= (by + 28) then
        local selItem = files.fileList[files.selectedIdx]

        -- Button 1: RUN
        if x >= (VP_X + 6) and x <= (VP_X + 60) then
            if selItem and selItem.isLua then
                files.launchSelected(selItem.path)
                return true
            end
        -- Button 2: INSTALL TO HOME & FLASH
        elseif x >= (VP_X + 64) and x <= (VP_X + 128) then
            if selItem and selItem.isLua and files.currentTab == "sd" then
                files.installSelected(selItem)
                return true
            end
        -- Button 3: VIEW CODE
        elseif x >= (VP_X + 132) and x <= (VP_X + 180) then
            if selItem then
                files.viewPath = selItem.path
                files.viewContent = ""
                if file and file.read then
                    local c = file.read(selItem.path)
                    files.viewContent = c or ""
                end
                files.viewMode = "viewer"
                if sys and sys.beep then sys.beep(1200, 20) end
                files.render()
                return true
            end
        -- Button 4: +DEMO or SCAN
        elseif x >= (VP_X + 184) and x <= (VP_X + 236) then
            if files.currentTab == "sd" then
                local sampleCode = "-- @name: SD Demo\n-- CYD SD Demo App (Development Build)\nfunction app_main()\n  gui.fillRect(0,20,240,274,0x0821)\n  gui.drawText(14,36,'Installed SD App!',0x07E0,2)\n  gui.drawText(14,62,'[DEVELOPMENT BUILD]',0xFFE0,1)\n  gui.fillCircle(120,160,38,0xFD00)\n  sys.beep(1200,50)\n  while true do\n    sys.delay(50)\n  end\nend\napp_main()\n"
                if file and file.write then
                    file.write("/sd/apps/demo_sd.lua", sampleCode)
                end
                files.statusMsg = "Created /sd/apps/demo_sd.lua"
                if sys and sys.beep then sys.beep(1500, 40) end
                files.refresh()
                files.render()
                return true
            else
                if sys and sys.beep then sys.beep(1000, 20) end
                files.refresh()
                files.render()
                return true
            end
        end
    end

    return false
end

function app_main()
    files.refresh()
    files.render()
    local lastTouched = false

    while true do
        if input and input.getTouch then
            local tx, ty, touched = input.getTouch()
            if touched and not lastTouched then
                files.handleTouch(tx, ty)
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
