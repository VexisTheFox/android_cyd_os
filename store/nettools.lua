-- @name: NetTools
-- @bg: 0x0352
-- @icon_b64: ACqoAAKBQoAIJBggIIQSCCIQBIiFV9VShB/0EpVf9VaVX/VWhB/0EoVX1VIiEASIIIQSCAgkGCACgUKAACqoAA==

local VP_X, VP_Y, VP_W, VP_H = 0, 20, 240, 274
local r1, r2, r3 = "Not tested", "Not tested", "Not tested"
local pubInfo = "Tap HTTP IP TEST"

local function drawUI()
    gui.fillRect(VP_X, VP_Y, VP_W, VP_H, 0x0821)
    gui.fillRect(VP_X, VP_Y, VP_W, 22, 0x0352)
    gui.drawFastHLine(VP_X, VP_Y + 22, VP_W, 0x07FF)
    gui.drawText(8, VP_Y + 7, "NET PING & HTTP SUITE", 0xFFFF, 1)

    gui.fillRect(10, VP_Y + 30, 220, 44, 0x10A2)
    gui.drawRect(10, VP_Y + 30, 220, 44, 0x3186)
    gui.drawText(16, VP_Y + 36, "WiFi: " .. wifi.getStatus() .. " (" .. wifi.getIP() .. ")", 0x07FF, 1)
    local tStr, synced = sys.getTime()
    gui.drawText(16, VP_Y + 52, "Time: " .. tStr .. (synced and " [NTP OK]" or " [LOCAL]"), 0xFFE0, 1)

    gui.fillRect(10, VP_Y + 80, 220, 82, 0x10A2)
    gui.drawRect(10, VP_Y + 80, 220, 82, 0x3186)
    gui.drawText(16, VP_Y + 86,  "1.1.1.1 (Cloudflare): " .. r1, 0xFFFF, 1)
    gui.drawText(16, VP_Y + 104, "8.8.8.8 (Google DNS): " .. r2, 0xFFFF, 1)
    gui.drawText(16, VP_Y + 122, "github.com (TCP 80) : " .. r3, 0xFFFF, 1)
    gui.drawText(16, VP_Y + 142, "WAN: " .. string.sub(pubInfo, 1, 30), 0x07E0, 1)

    gui.fillRect(10, VP_Y + 172, 106, 40, 0x04D3)
    gui.drawRect(10, VP_Y + 172, 106, 40, 0x07FF)
    gui.drawText(26, VP_Y + 188, "RUN PINGS", 0xFFFF, 1)

    gui.fillRect(124, VP_Y + 172, 106, 40, 0x2C9F)
    gui.drawRect(124, VP_Y + 172, 106, 40, 0x07FF)
    gui.drawText(142, VP_Y + 188, "HTTP WAN IP", 0xFFFF, 1)

    gui.fillRect(10, VP_Y + 220, 220, 38, 0x18E3)
    gui.drawRect(10, VP_Y + 220, 220, 38, 0xFFE0)
    gui.drawText(66, VP_Y + 235, "SYNC NTP CLOCK NOW", 0xFFE0, 1)
end

drawUI()
local wasT = false
while true do
    local tx, ty, pressed = input.getTouch()
    if pressed and not wasT then
        if ty >= VP_Y + 172 and ty <= VP_Y + 212 then
            sys.beep(1100, 20)
            if tx < 120 then
                r1 = "Pinging..."
                drawUI()
                local ok1, ms1 = net.ping("1.1.1.1", 53)
                r1 = ok1 and (ms1 .. " ms") or "FAIL"
                local ok2, ms2 = net.ping("8.8.8.8", 53)
                r2 = ok2 and (ms2 .. " ms") or "FAIL"
                local ok3, ms3 = net.ping("github.com", 80)
                r3 = ok3 and (ms3 .. " ms") or "FAIL"
                drawUI()
            else
                pubInfo = "Fetching ip-api.com..."
                drawUI()
                local b, c = http.get("http://ip-api.com/line/?fields=query,country")
                if b and c == 200 then
                    pubInfo = string.gsub(b, "[\r\n]+", " ")
                else
                    pubInfo = "HTTP Err " .. tostring(c)
                end
                drawUI()
            end
        elseif ty >= VP_Y + 220 and ty <= VP_Y + 258 then
            sys.beep(1300, 25)
            sys.syncNTP(2500)
            drawUI()
        end
    end
    wasT = pressed
    sys.delay(25)
end
