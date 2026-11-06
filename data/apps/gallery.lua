-- ===================================================================
-- CYD Android OS - SD Card & Flash Picture Gallery (Development Build)
-- Target Hardware: ESP32-2432S028 (240x320 LCD + XPT2046 Touch)
-- ===================================================================
-- Metadata Header:
-- @name: Gallery
-- @icon: gallery
-- @author: CYD Team
-- @version: 2.0.0
-- @description: Load and view .bmp and .jpg pictures from MicroSD card

local gallery = {
    images = {},
    currentIdx = 1,
    lastW = 0,
    lastH = 0,
    statusMsg = "",
    theme = {
        bg = 0x0821,
        headerBg = 0x18C3,
        cardBg = 0x10A2,
        border = 0x324B,
        accent = 0x07E0,
        cyan = 0x07FF,
        amber = 0xFFE0,
        white = 0xFFFF,
        muted = 0x9CD3
    }
}

local VP_X = 0
local VP_Y = 20
local VP_W = 240
local VP_H = 274

function gallery.scan(autoCreateSamples)
    if sys and sys.isSDMounted and not sys.isSDMounted() then
        if sys.probeSD then sys.probeSD() end
    end

    gallery.images = {}
    if file and file.listImages then
        gallery.images = file.listImages() or {}
    end

    if #gallery.images == 0 and autoCreateSamples then
        if gui and gui.createSampleImages then
            gui.createSampleImages()
            if file and file.listImages then
                gallery.images = file.listImages() or {}
            end
        end
    end

    if gallery.currentIdx > #gallery.images then
        gallery.currentIdx = math.max(1, #gallery.images)
    end
end

function gallery.render()
    gui.fillRect(VP_X, VP_Y, VP_W, VP_H, gallery.theme.bg)

    -- Header Bar
    gui.fillRect(VP_X, VP_Y, VP_W, 24, gallery.theme.headerBg)
    gui.drawFastHLine(VP_X, VP_Y + 24, VP_W, gallery.theme.cyan)
    gui.drawText(VP_X + 6, VP_Y + 4, "SD Gallery", gallery.theme.white, 2)
    gui.drawText(VP_X + 94, VP_Y + 8, "[DEV BUILD]", gallery.theme.amber, 1)

    local sd = sys and sys.isSDMounted and sys.isSDMounted() or false
    gui.drawText(VP_X + 178, VP_Y + 8, sd and "[SD:OK]" or "[FLASH]", sd and gallery.theme.accent or gallery.theme.cyan, 1)

    -- Picture Frame (X=6, Y=VP_Y+28, W=228, H=176)
    local frameX = VP_X + 6
    local frameY = VP_Y + 28
    local frameW = 228
    local frameH = 176
    gui.fillRect(frameX, frameY, frameW, frameH, 0x0000)
    gui.drawRect(frameX, frameY, frameW, frameH, gallery.theme.border)

    local total = #gallery.images
    if total == 0 then
        gui.drawText(frameX + 18, frameY + 46, "No .bmp / .jpg found", gallery.theme.amber, 2)
        gui.drawText(frameX + 14, frameY + 74, "Place .bmp or .jpg files in:", gallery.theme.muted, 1)
        gui.drawText(frameX + 14, frameY + 88, "/sd, /sd/pictures, /sd/dcim", gallery.theme.cyan, 1)
        gui.drawText(frameX + 14, frameY + 108, "Or tap [+SAMPLES] below to", gallery.theme.white, 1)
        gui.drawText(frameX + 14, frameY + 120, "generate sample BMP photos!", gallery.theme.white, 1)
    else
        local item = gallery.images[gallery.currentIdx]
        local ok, w, h = false, 0, 0
        if gui and gui.drawImage and item and item.path then
            ok, w, h = gui.drawImage(item.path, frameX + 2, frameY + 2, frameW - 4, frameH - 4)
        end

        -- Metadata Bar (Y=VP_Y+208, H=26)
        gui.fillRect(VP_X + 6, VP_Y + 208, 228, 26, gallery.theme.cardBg)
        gui.drawRect(VP_X + 6, VP_Y + 208, 228, 26, gallery.theme.border)

        local srcTag = (item and item.isSD) and "SD" or "FLASH"
        local nameStr = item and item.name or "image"
        if #nameStr > 18 then nameStr = string.sub(nameStr, 1, 16) .. ".." end
        gui.drawText(VP_X + 12, VP_Y + 212, string.format("%d/%d: %s [%s]", gallery.currentIdx, total, nameStr, srcTag), gallery.theme.white, 1)

        if ok then
            local kb = math.max(1, math.floor((item.size or 0) / 1024))
            gui.drawText(VP_X + 12, VP_Y + 223, string.format("Res: %dx%d px | Size: %d KB", w or 0, h or 0, kb), gallery.theme.cyan, 1)
        else
            gui.drawText(VP_X + 12, VP_Y + 223, "Format error (use 24-bit BMP or JPG)", gallery.theme.amber, 1)
        end
    end

    -- Bottom Action Bar (Y=VP_Y+238, H=30)
    local by = VP_Y + 238
    -- 1. < PREV (X=6, W=52)
    gui.fillRect(VP_X + 6, by, 52, 30, gallery.theme.cardBg)
    gui.drawRect(VP_X + 6, by, 52, 30, gallery.theme.cyan)
    gui.drawText(VP_X + 14, by + 11, "< PREV", gallery.theme.cyan, 1)

    -- 2. NEXT > (X=62, W=52)
    gui.fillRect(VP_X + 62, by, 52, 30, gallery.theme.cardBg)
    gui.drawRect(VP_X + 62, by, 52, 30, gallery.theme.cyan)
    gui.drawText(VP_X + 70, by + 11, "NEXT >", gallery.theme.cyan, 1)

    -- 3. SD SCAN (X=118, W=56)
    gui.fillRect(VP_X + 118, by, 56, 30, 0x18C3)
    gui.drawRect(VP_X + 118, by, 56, 30, gallery.theme.accent)
    gui.drawText(VP_X + 124, by + 11, "SCAN SD", gallery.theme.accent, 1)

    -- 4. +SAMPLES (X=178, W=56)
    gui.fillRect(VP_X + 178, by, 56, 30, 0x2124)
    gui.drawRect(VP_X + 178, by, 56, 30, gallery.theme.amber)
    gui.drawText(VP_X + 182, by + 11, "+SAMPLES", gallery.theme.amber, 1)
end

function gallery.handleTouch(x, y)
    local by = VP_Y + 238
    if y >= by and y <= (by + 32) then
        -- 1. < PREV
        if x >= (VP_X + 6) and x <= (VP_X + 58) then
            if #gallery.images > 0 then
                gallery.currentIdx = gallery.currentIdx - 1
                if gallery.currentIdx < 1 then gallery.currentIdx = #gallery.images end
                if sys and sys.beep then sys.beep(1000, 15) end
                gallery.render()
            end
            return true
        -- 2. NEXT >
        elseif x >= (VP_X + 62) and x <= (VP_X + 114) then
            if #gallery.images > 0 then
                gallery.currentIdx = (gallery.currentIdx % #gallery.images) + 1
                if sys and sys.beep then sys.beep(1200, 15) end
                gallery.render()
            end
            return true
        -- 3. SCAN SD
        elseif x >= (VP_X + 118) and x <= (VP_X + 174) then
            if sys and sys.probeSD then sys.probeSD() end
            if sys and sys.beep then sys.beep(1400, 25) end
            gallery.scan(false)
            gallery.render()
            return true
        -- 4. +SAMPLES
        elseif x >= (VP_X + 178) and x <= (VP_X + 236) then
            if gui and gui.createSampleImages then
                gui.createSampleImages()
            end
            if sys and sys.beep then sys.beep(1600, 35) end
            gallery.scan(false)
            gallery.render()
            return true
        end
    end

    -- Tap on picture frame cycles to next image
    if y >= (VP_Y + 28) and y <= (VP_Y + 204) and #gallery.images > 1 then
        gallery.currentIdx = (gallery.currentIdx % #gallery.images) + 1
        if sys and sys.beep then sys.beep(1100, 15) end
        gallery.render()
        return true
    end

    return false
end

function app_main()
    gallery.scan(true)
    gallery.render()
    local lastTouched = false

    while true do
        if input and input.getTouch then
            local tx, ty, touched = input.getTouch()
            if touched and not lastTouched then
                gallery.handleTouch(tx, ty)
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
