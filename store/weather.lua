-- @name: Weather
-- @bg: 0x0276
-- @icon_b64: APDwAAw/wwAD//wAP/9VAAP1VVAPVVVUAVVVVQVVVVUFVVVVAVVVVAAAAAAAoKCgAoKCgAKCgoAAAAAAAAAAAA==

local VP_X, VP_Y, VP_W, VP_H = 0, 20, 240, 274
local ICON_B64 = "APDwAAw/wwAD//wAP/9VAAP1VVAPVVVUAVVVVQVVVVUFVVVVAVVVVAAAAAAAoKCgAoKCgAKCgoAAAAAAAAAAAA=="

local city = "Prague"
local lat, lon = "50.08", "14.43"
local reg = sys.getRegion and sys.getRegion() or "Czechia"
if string.find(reg, "USA") then city, lat, lon = "New York", "40.71", "-74.01"
elseif string.find(reg, "Albania") then city, lat, lon = "Tirana", "41.33", "19.82"
elseif string.find(reg, "Germany") then city, lat, lon = "Berlin", "52.52", "13.41"
elseif string.find(reg, "UK") then city, lat, lon = "London", "51.51", "-0.13"
elseif string.find(reg, "Japan") then city, lat, lon = "Tokyo", "35.68", "139.69"
end

local temp = "--.- C"
local wind = "-- km/h"
local status = "Tap FETCH LIVE to query Open-Meteo"

local function drawUI()
    gui.fillRect(VP_X, VP_Y, VP_W, VP_H, 0x0821)
    gui.fillRect(VP_X, VP_Y, VP_W, 24, 0x0276)
    gui.drawFastHLine(VP_X, VP_Y + 24, VP_W, 0x07FF)
    gui.drawText(8, VP_Y + 7, "LIVE WEATHER (" .. city .. ")", 0xFFFF, 1)

    gui.fillRect(16, VP_Y + 36, 208, 120, 0x10A2)
    gui.drawRect(16, VP_Y + 36, 208, 120, 0x07FF)
    if gui.drawBase64 then
        gui.drawBase64(54, VP_Y + 84, ICON_B64, 3, 0xFFE0)
    end
    gui.drawText(98, VP_Y + 54, "TEMP:", 0x9CD3, 1)
    gui.drawText(98, VP_Y + 68, temp, 0xFFE0, 2)
    gui.drawText(98, VP_Y + 98, "WIND:", 0x9CD3, 1)
    gui.drawText(98, VP_Y + 112, wind, 0x07FF, 2)
    gui.drawText(26, VP_Y + 140, "Coords: " .. lat .. ", " .. lon, 0x9CD3, 1)

    gui.fillRect(16, VP_Y + 166, 208, 36, 0x10A2)
    gui.drawRect(16, VP_Y + 166, 208, 36, 0x3186)
    gui.drawText(24, VP_Y + 174, "Status:", 0x07E0, 1)
    gui.drawText(24, VP_Y + 188, string.sub(status, 1, 32), 0xFFFF, 1)

    gui.fillRect(24, VP_Y + 216, 192, 42, 0x04D3)
    gui.drawRect(24, VP_Y + 216, 192, 42, 0x07FF)
    gui.drawText(66, VP_Y + 230, "FETCH LIVE WEATHER", 0xFFFF, 1)
end

local function fetchWeather()
    status = "Querying api.open-meteo.com..."
    drawUI()
    local url = "https://api.open-meteo.com/v1/forecast?latitude=" .. lat .. "&longitude=" .. lon .. "&current_weather=true"
    local body, code, err = http.get(url)
    if body and code == 200 then
        local cw = string.match(body, '"current_weather"%s*:%s*(%b{})') or body
        local t = string.match(cw, '"temperature"%s*:%s*([%-0-9%.]+)')
        local w = string.match(cw, '"windspeed"%s*:%s*([%-0-9%.]+)')
        if t then temp = t .. " C" end
        if w then wind = w .. " km/h" end
        status = "HTTP 200 OK (Open-Meteo API)"
    else
        status = "Err " .. tostring(code) .. ": " .. tostring(err or "No WiFi")
    end
    drawUI()
end

drawUI()
local wasT = false
while true do
    local tx, ty, pressed = input.getTouch()
    if pressed and not wasT then
        if ty >= VP_Y + 216 and ty <= VP_Y + 258 and tx >= 24 and tx <= 216 then
            sys.beep(1200, 25)
            fetchWeather()
        end
    end
    wasT = pressed
    sys.delay(25)
end
