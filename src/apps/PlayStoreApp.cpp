/**
 * @file PlayStoreApp.cpp
 * @brief Native Android-Style Play Store & OTA Lua App Downloader for CYD OS
 */

#include "PlayStoreApp.h"
#include "../core/AppRegistry.h"
#include "../core/Config.h"
#include "../core/Logger.h"
#include "../core/LuaIconCodec.h"
#include "../hal/BoardHAL.h"
#include "../hal/StorageHAL.h"
#include "../ui/UIManager.h"
#include "../ui/KeyboardView.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <cstring>

namespace apps {

// =============================================================================
// BUNDLED CLOUD STORE APPS (PROGMEM + GitHub Cloud Sync)
// Each script includes -- @name, -- @bg, and -- @icon_b64 in its header!
// =============================================================================

static const char STORE_WEATHER_LUA[] PROGMEM = R"LUA(-- @name: Weather
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
)LUA";

static const char STORE_NETTOOLS_LUA[] PROGMEM = R"LUA(-- @name: NetTools
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
)LUA";

static const char STORE_CYBERSYNTH_LUA[] PROGMEM = R"LUA(-- @name: CyberSynth
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
)LUA";

static const char STORE_FLAPPY_LUA[] PROGMEM = R"LUA(-- @name: Flappy CYD
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
)LUA";

static const char STORE_2048_LUA[] PROGMEM = R"LUA(-- @name: 2048 Game
-- @bg: 0x5180
-- @icon_b64: P/wqqP//qqr1X6Qa8B+kGvVfpVr0D6Aa9V+gGj/8KqgqqD/8qqr//6Va9V+kCvQfpVr1X6Qa9B+lWvVfKqg//A==

local VP_X, VP_Y, VP_W, VP_H = 0, 20, 240, 274
local grid = {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}}
local score = 0

local function spawn()
    local empty = {}
    for r = 1, 4 do
        for c = 1, 4 do
            if grid[r][c] == 0 then table.insert(empty, {r, c}) end
        end
    end
    if #empty > 0 then
        local pick = empty[math.random(1, #empty)]
        grid[pick[1]][pick[2]] = (math.random(1, 10) == 1) and 4 or 2
    end
end

local function drawBoard()
    gui.fillRect(VP_X, VP_Y, VP_W, 24, 0x5180)
    gui.drawText(8, VP_Y + 7, "2048 PUZZLE | SCORE: " .. score, 0xFFFF, 1)
    for r = 1, 4 do
        for c = 1, 4 do
            local v = grid[r][c]
            local x = 12 + (c - 1) * 54
            local y = VP_Y + 30 + (r - 1) * 48
            local bg = (v == 0) and 0x18E3 or ((v <= 8) and 0x2C9F or ((v <= 64) and 0x04D3 or 0xFD00))
            gui.fillRect(x, y, 50, 44, bg)
            gui.drawRect(x, y, 50, 44, 0x3186)
            if v > 0 then
                gui.drawText(x + 12, y + 14, tostring(v), 0xFFFF, 2)
            end
        end
    end
    local by = VP_Y + 228
    local labels = {"LEFT", "UP", "DOWN", "RIGHT"}
    for i = 1, 4 do
        local bx = 8 + (i - 1) * 57
        gui.fillRect(bx, by, 53, 38, 0x10A2)
        gui.drawRect(bx, by, 53, 38, 0x07FF)
        gui.drawText(bx + 10, by + 15, labels[i], 0xFFE0, 1)
    end
end

local function slideLine(line)
    local out = {}
    for i = 1, 4 do if line[i] ~= 0 then table.insert(out, line[i]) end end
    local i = 1
    local merged = {}
    while i <= #out do
        if i < #out and out[i] == out[i+1] then
            local val = out[i] * 2
            score = score + val
            table.insert(merged, val)
            i = i + 2
        else
            table.insert(merged, out[i])
            i = i + 1
        end
    end
    while #merged < 4 do table.insert(merged, 0) end
    return merged
end

local function move(dir)
    for idx = 1, 4 do
        local line = {}
        for k = 1, 4 do
            if dir == 1 then line[k] = grid[idx][k]
            elseif dir == 4 then line[k] = grid[idx][5 - k]
            elseif dir == 2 then line[k] = grid[k][idx]
            else line[k] = grid[5 - k][idx] end
        end
        local res = slideLine(line)
        for k = 1, 4 do
            if dir == 1 then grid[idx][k] = res[k]
            elseif dir == 4 then grid[idx][5 - k] = res[k]
            elseif dir == 2 then grid[k][idx] = res[k]
            else grid[5 - k][idx] = res[k] end
        end
    end
    spawn()
    drawBoard()
end

gui.fillRect(VP_X, VP_Y, VP_W, VP_H, 0x0821)
spawn(); spawn(); drawBoard()

local wasT = false
while true do
    local tx, ty, pressed = input.getTouch()
    if pressed and not wasT and ty >= VP_Y + 224 then
        local btn = math.floor((tx - 8) / 57) + 1
        if btn >= 1 and btn <= 4 then
            sys.beep(950, 18)
            move(btn)
        end
    end
    wasT = pressed
    sys.delay(25)
end
)LUA";

static const char STORE_WORLDCLOCK_LUA[] PROGMEM = R"LUA(-- @name: WorldTime
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
)LUA";

static const StoreCatalogItem STORE_CATALOG[] = {
    {
        "weather", "Weather", "Live HTTP API",
        "Open-Meteo live temp & wind",
        "/apps/weather.lua",
        "https://raw.githubusercontent.com/VexisTheFox/android_cyd_os/main/store/weather.lua",
        "APDwAAw/wwAD//wAP/9VAAP1VVAPVVVUAVVVVQVVVVUFVVVVAVVVVAAAAAAAoKCgAoKCgAKCgoAAAAAAAAAAAA==",
        0x0276, STORE_WEATHER_LUA
    },
    {
        "nettools", "NetTools", "Network & Ping",
        "TCP Ping, WAN IP & NTP test",
        "/apps/nettools.lua",
        "https://raw.githubusercontent.com/VexisTheFox/android_cyd_os/main/store/nettools.lua",
        "ACqoAAKBQoAIJBggIIQSCCIQBIiFV9VShB/0EpVf9VaVX/VWhB/0EoVX1VIiEASIIIQSCAgkGCACgUKAACqoAA==",
        0x0352, STORE_NETTOOLS_LUA
    },
    {
        "cybersynth", "CyberSynth", "Audio Synth",
        "8-key FM synthesizer & octaves",
        "/apps/cybersynth.lua",
        "https://raw.githubusercontent.com/VexisTheFox/android_cyd_os/main/store/cybersynth.lua",
        "APAPAAMMMMD8A8A/AAAAAKqqqqpUUVFFVFFRRVRRUUVUUVFFVAFQBVUVVFVVFVRVVRVUVVUVVFWqqqqqAAAAAA==",
        0x4812, STORE_CYBERSYNTH_LUA
    },
    {
        "flappy", "Flappy CYD", "Arcade Game",
        "Smooth 35 FPS arcade game",
        "/apps/flappy.lua",
        "https://raw.githubusercontent.com/VexisTheFox/android_cyd_os/main/store/flappy.lua",
        "AAAAAAAP/AAA//UAA//WQA9X1kANVfUADVX/qA9X/qoD//+oAP//AAAP8AAAAAAAqqqqqgAAAAAAAAAAAAAAAA==",
        0x1386, STORE_FLAPPY_LUA
    },
    {
        "game2048", "2048 Game", "Puzzle Game",
        "Classic 4x4 2048 tile puzzle",
        "/apps/game2048.lua",
        "https://raw.githubusercontent.com/VexisTheFox/android_cyd_os/main/store/game2048.lua",
        "P/wqqP//qqr1X6Qa8B+kGvVfpVr0D6Aa9V+gGj/8KqgqqD/8qqr//6Va9V+kCvQfpVr1X6Qa9B+lWvVfKqg//A==",
        0x5180, STORE_2048_LUA
    },
    {
        "worldclock", "WorldTime", "NTP Clock",
        "Multi-timezone live NTP clock",
        "/apps/worldclock.lua",
        "https://raw.githubusercontent.com/VexisTheFox/android_cyd_os/main/store/worldclock.lua",
        "ACqoAAKVVoAJQUFgJAPAGCQDwBiQA8AGkAPABpUD/xaVA/8WkAAABpAAAAYkAAAYJAFAGAlBQWAClVaAACqoAA==",
        0x1928, STORE_WORLDCLOCK_LUA
    }
};

static constexpr int CATALOG_COUNT = sizeof(STORE_CATALOG) / sizeof(STORE_CATALOG[0]);

PlayStoreApp::PlayStoreApp()
    : m_activeTab(0),
      m_scrollY(0),
      m_selectedIndex(0),
      m_wasPressed(false),
      m_isDragging(false),
      m_touchStartY(0),
      m_scrollStartY(0),
      m_lastTouchY(0),
      m_lastScrollDrawMs(0),
      m_needsFullRedraw(true),
      m_needsListRedraw(true),
      m_needsFooterRedraw(true) {
    strncpy(m_customUrl, "https://raw.githubusercontent.com/VexisTheFox/android_cyd_os/main/store/weather.lua", sizeof(m_customUrl) - 1);
    m_customUrl[sizeof(m_customUrl) - 1] = '\0';
    strncpy(m_customFile, "custom_app.lua", sizeof(m_customFile) - 1);
    m_customFile[sizeof(m_customFile) - 1] = '\0';
    strncpy(m_statusLine, "Ready to download via Wi-Fi / Cloud", sizeof(m_statusLine) - 1);
    m_statusLine[sizeof(m_statusLine) - 1] = '\0';
}

void PlayStoreApp::onCreate() {
    m_needsFullRedraw = true;
}

void PlayStoreApp::onResume() {
    m_needsFullRedraw = true;
    m_needsListRedraw = true;
    m_needsFooterRedraw = true;
}

void PlayStoreApp::onUpdate() {}

void PlayStoreApp::onPause() {
    m_wasPressed = false;
    m_isDragging = false;
}

void PlayStoreApp::onDestroy() {}

bool PlayStoreApp::isInstalled(int idx) const {
    if (idx < 0 || idx >= CATALOG_COUNT) return false;
    return StorageHAL::fileExists(STORE_CATALOG[idx].localPath);
}

int PlayStoreApp::getMaxScroll(int listH) const {
    int totalH = CATALOG_COUNT * ROW_H + 8;
    int maxS = totalH - listH;
    return (maxS > 0) ? maxS : 0;
}

void PlayStoreApp::onRender(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    if (m_needsFullRedraw) {
        drawHeader(display, vx, vy, vw);
        if (m_activeTab == 0) {
            drawListArea(display, vx, vy, vw, vh);
            drawFooter(display, vx, vy, vw, vh);
        } else {
            drawUrlTab(display, vx, vy, vw, vh);
        }
        m_needsFullRedraw = false;
        m_needsListRedraw = false;
        m_needsFooterRedraw = false;
        return;
    }

    if (m_activeTab == 0) {
        if (m_needsListRedraw) {
            drawListArea(display, vx, vy, vw, vh);
            m_needsListRedraw = false;
        }
        if (m_needsFooterRedraw) {
            drawFooter(display, vx, vy, vw, vh);
            m_needsFooterRedraw = false;
        }
    }
}

void PlayStoreApp::drawHeader(TFT_eSPI &display, int vx, int vy, int vw) {
    display.fillRect(vx, vy, vw, 22, 0x0352);
    display.setTextFont(1);
    display.setTextDatum(TL_DATUM);
    display.setTextColor(0x07FF, 0x0352);
    display.drawString("CYD PLAY STORE", vx + 6, vy + 7);

    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    display.setTextDatum(TR_DATUM);
    display.setTextColor(wifiOk ? 0x07E0 : 0xFFE0, 0x0352);
    display.drawString(wifiOk ? "CLOUD ONLINE" : "OFFLINE CACHE", vx + vw - 6, vy + 7);

    int tabY = vy + 22;
    int halfW = vw / 2;
    uint16_t t0Bg = (m_activeTab == 0) ? 0x18E3 : 0x0821;
    uint16_t t1Bg = (m_activeTab == 1) ? 0x18E3 : 0x0821;

    display.fillRect(vx, tabY, halfW, 24, t0Bg);
    display.fillRect(vx + halfW, tabY, vw - halfW, 24, t1Bg);
    display.drawFastHLine(vx, tabY + 23, vw, 0x3186);

    if (m_activeTab == 0) {
        display.fillRect(vx + 6, tabY + 21, halfW - 12, 2, 0x07E0);
    } else {
        display.fillRect(vx + halfW + 6, tabY + 21, halfW - 12, 2, 0x07FF);
    }

    display.setTextDatum(MC_DATUM);
    display.setTextColor((m_activeTab == 0) ? TFT_WHITE : 0x8410, t0Bg);
    display.drawString("FEATURED (6)", vx + halfW / 2, tabY + 12);

    display.setTextColor((m_activeTab == 1) ? TFT_WHITE : 0x8410, t1Bg);
    display.drawString("URL / GITHUB", vx + halfW + halfW / 2, tabY + 12);
    display.setTextDatum(TL_DATUM);
}

void PlayStoreApp::drawListArea(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int listTop = vy + HEADER_H;
    int listH   = vh - HEADER_H - FOOTER_H;

    TFT_eSprite band(&display);
    band.setColorDepth(16);
    const int BAND_H = 64;
    bool hasSprite = (band.createSprite(vw, BAND_H) != nullptr);

    for (int yOff = 0; yOff < listH; yOff += BAND_H) {
        int curH = (yOff + BAND_H > listH) ? (listH - yOff) : BAND_H;
        TFT_eSPI &canvas = hasSprite ? (TFT_eSPI&)band : display;
        int baseX = hasSprite ? 0 : vx;
        int baseY = hasSprite ? 0 : (listTop + yOff);

        canvas.fillRect(baseX, baseY, vw, curH, 0x0821);

        for (int i = 0; i < CATALOG_COUNT; i++) {
            int rowDocY = 4 + i * ROW_H;
            int rowScreenY = rowDocY - m_scrollY - yOff;
            if (rowScreenY + ROW_H <= 0 || rowScreenY >= curH) continue;

            int ry = baseY + rowScreenY;
            bool sel = (i == m_selectedIndex);
            bool inst = isInstalled(i);
            const StoreCatalogItem &item = STORE_CATALOG[i];

            uint16_t cardBg = sel ? 0x1949 : 0x10A2;
            uint16_t border = sel ? 0x07FF : 0x2965;

            canvas.fillRoundRect(baseX + 6, ry, vw - 14, ROW_H - 4, 5, cardBg);
            canvas.drawRoundRect(baseX + 6, ry, vw - 14, ROW_H - 4, 5, border);

            // Render custom Base64 logo inside 28x28 rounded badge
            canvas.fillRoundRect(baseX + 11, ry + 5, 28, 28, 5, item.iconBg);
            canvas.drawRoundRect(baseX + 11, ry + 5, 28, 28, 5, 0x4A69);
            core::LuaIconCodec::drawBase64Icon(canvas, item.iconB64, baseX + 25, ry + 19, TFT_WHITE, 1);

            canvas.setTextFont(1);
            canvas.setTextDatum(TL_DATUM);
            canvas.setTextColor(TFT_WHITE, cardBg);
            canvas.drawString(item.title, baseX + 45, ry + 6);

            canvas.setTextColor(0x9CD3, cardBg);
            canvas.drawString(item.desc, baseX + 45, ry + 20);

            // Status pill
            uint16_t pillBg = inst ? 0x03E0 : 0x0276;
            uint16_t pillFg = inst ? TFT_WHITE : 0x07FF;
            canvas.fillRoundRect(baseX + vw - 66, ry + 6, 52, 14, 3, pillBg);
            canvas.setTextDatum(MC_DATUM);
            canvas.setTextColor(pillFg, pillBg);
            canvas.drawString(inst ? "INSTALLED" : "GET APP", baseX + vw - 40, ry + 13);
            canvas.setTextDatum(TL_DATUM);
        }

        if (hasSprite) {
            band.pushSprite(vx, listTop + yOff, 0, 0, vw, curH);
        }
    }

    if (hasSprite) {
        band.deleteSprite();
    }
}

void PlayStoreApp::drawFooter(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int footY = vy + vh - FOOTER_H;
    display.fillRect(vx, footY, vw, FOOTER_H, 0x10A2);
    display.drawFastHLine(vx, footY, vw, 0x3186);

    bool inst = isInstalled(m_selectedIndex);

    // Button 1: INSTALL or UPDATE
    display.fillRoundRect(vx + 6, footY + 5, 86, 26, 5, 0x04D3);
    display.drawRoundRect(vx + 6, footY + 5, 86, 26, 5, 0x07E0);
    display.setTextFont(1);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(TFT_WHITE, 0x04D3);
    display.drawString(inst ? "REINSTALL" : "INSTALL", vx + 49, footY + 18);

    // Button 2: OPEN
    uint16_t openBg = inst ? 0x2C9F : 0x18E3;
    display.fillRoundRect(vx + 98, footY + 5, 66, 26, 5, openBg);
    display.drawRoundRect(vx + 98, footY + 5, 66, 26, 5, inst ? 0x07FF : 0x3186);
    display.setTextColor(inst ? TFT_WHITE : 0x8410, openBg);
    display.drawString("OPEN", vx + 131, footY + 18);

    // Button 3: UNINSTALL
    uint16_t delBg = inst ? 0x7800 : 0x18E3;
    display.fillRoundRect(vx + 170, footY + 5, 64, 26, 5, delBg);
    display.drawRoundRect(vx + 170, footY + 5, 64, 26, 5, inst ? 0xF800 : 0x3186);
    display.setTextColor(inst ? TFT_WHITE : 0x8410, delBg);
    display.drawString("REMOVE", vx + 202, footY + 18);
    display.setTextDatum(TL_DATUM);
}

void PlayStoreApp::drawUrlTab(TFT_eSPI &display, int vx, int vy, int vw, int vh) {
    int bodyY = vy + HEADER_H;
    int bodyH = vh - HEADER_H;
    display.fillRect(vx, bodyY, vw, bodyH, 0x0821);

    display.setTextFont(1);
    display.setTextDatum(TL_DATUM);

    // Card 1: Custom URL
    display.fillRoundRect(8, bodyY + 8, vw - 16, 62, 5, 0x10A2);
    display.drawRoundRect(8, bodyY + 8, vw - 16, 62, 5, 0x3186);
    display.setTextColor(0x07FF, 0x10A2);
    display.drawString("1. Lua Script URL (Tap to Edit):", 14, bodyY + 14);
    display.setTextColor(TFT_WHITE, 0x10A2);
    char u1[36], u2[36];
    strncpy(u1, m_customUrl, 34); u1[34] = '\0';
    size_t uLen = strlen(m_customUrl);
    if (uLen > 34) {
        strncpy(u2, m_customUrl + 34, 34); u2[34] = '\0';
    } else {
        u2[0] = '\0';
    }
    display.drawString(u1, 14, bodyY + 30);
    display.setTextColor(0x9CD3, 0x10A2);
    display.drawString(u2, 14, bodyY + 44);

    // Card 2: Target Filename
    display.fillRoundRect(8, bodyY + 76, vw - 16, 38, 5, 0x10A2);
    display.drawRoundRect(8, bodyY + 76, vw - 16, 38, 5, 0x3186);
    display.setTextColor(0xFFE0, 0x10A2);
    display.drawString("2. Save As (/apps/):", 14, bodyY + 84);
    display.setTextColor(TFT_WHITE, 0x10A2);
    display.drawString(m_customFile, 14, bodyY + 98);

    // Status Card
    display.fillRoundRect(8, bodyY + 120, vw - 16, 36, 5, 0x10A2);
    display.drawRoundRect(8, bodyY + 120, vw - 16, 36, 5, 0x2965);
    display.setTextColor(0x07E0, 0x10A2);
    display.drawString("Status:", 14, bodyY + 126);
    display.setTextColor(TFT_WHITE, 0x10A2);
    display.drawString(m_statusLine, 14, bodyY + 140);

    // Download & Install Button
    display.fillRoundRect(16, bodyY + 168, vw - 32, 42, 6, 0x04D3);
    display.drawRoundRect(16, bodyY + 168, vw - 32, 42, 6, 0x07E0);
    display.setTextDatum(MC_DATUM);
    display.setTextColor(TFT_WHITE, 0x04D3);
    display.drawString("DOWNLOAD & INSTALL TO HOME", vx + vw / 2, bodyY + 189);
    display.setTextDatum(TL_DATUM);
}

bool PlayStoreApp::downloadUrlToPath(const char* url, const char* destPath) {
    if (WiFi.status() != WL_CONNECTED) return false;
    if (xPortGetFreeHeapSize() < 48000) return false;

    HTTPClient http;
    http.setConnectTimeout(3500);
    http.setTimeout(3500);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    bool isHttps = (strncasecmp(url, "https://", 8) == 0);
    WiFiClient client;
    WiFiClientSecure secureClient;
    bool begun = false;
    if (isHttps) {
        secureClient.setInsecure();
        begun = http.begin(secureClient, url);
    } else {
        begun = http.begin(client, url);
    }
    if (!begun) return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    StorageHAL::ensureDirectory("/apps");
    File f = LittleFS.open(destPath, "w");
    if (!f) {
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buff[512];
    int total = 0;
    int len = http.getSize();
    uint32_t lastAct = millis();

    while ((http.connected() || (stream && stream->available() > 0)) && (len > 0 || len == -1)) {
        size_t avail = stream ? stream->available() : 0;
        if (avail) {
            int c = stream->readBytes(buff, (avail > sizeof(buff)) ? sizeof(buff) : avail);
            if (c > 0) {
                f.write(buff, c);
                total += c;
                if (len > 0) len -= c;
                lastAct = millis();
            }
        } else if (millis() - lastAct > 3500) {
            break;
        }
        delay(1);
    }
    f.flush();
    f.close();
    http.end();
    return (total > 16);
}

void PlayStoreApp::installFeaturedApp(int idx) {
    if (idx < 0 || idx >= CATALOG_COUNT) return;
    const StoreCatalogItem &item = STORE_CATALOG[idx];

    ui::UIManager::showToast("Installing from Play Store...", 900);

    bool cloudOk = false;
    if (WiFi.status() == WL_CONNECTED) {
        cloudOk = downloadUrlToPath(item.cloudUrl, item.localPath);
    }

    if (!cloudOk) {
        // Offline / Instant PROGMEM package write
        StorageHAL::ensureDirectory("/apps");
        File f = LittleFS.open(item.localPath, "w");
        if (f) {
            size_t len = strlen_P(item.progmemSrc);
            const uint8_t* ptr = (const uint8_t*)item.progmemSrc;
            uint8_t chunk[128];
            size_t written = 0;
            while (written < len) {
                size_t n = (len - written > sizeof(chunk)) ? sizeof(chunk) : (len - written);
                memcpy_P(chunk, ptr + written, n);
                f.write(chunk, n);
                written += n;
            }
            f.flush();
            f.close();
        }
    }

    core::AppRegistry::scanFilesystemApps();
    LOG_INFO("STORE", "Installed %s (%s) -> %s", item.title, cloudOk ? "Cloud" : "Bundled", item.localPath);

    char msg[44];
    snprintf(msg, sizeof(msg), "Installed %s to Home!", item.title);
    ui::UIManager::showToast(msg, 1500);
    m_needsFullRedraw = true;
}

void PlayStoreApp::uninstallFeaturedApp(int idx) {
    if (idx < 0 || idx >= CATALOG_COUNT) return;
    const StoreCatalogItem &item = STORE_CATALOG[idx];
    if (!isInstalled(idx)) return;

    LittleFS.remove(item.localPath);
    core::AppRegistry::rescan();
    LOG_INFO("STORE", "Uninstalled %s (%s)", item.title, item.localPath);

    char msg[44];
    snprintf(msg, sizeof(msg), "Removed %s", item.title);
    ui::UIManager::showToast(msg, 1400);
    m_needsFullRedraw = true;
}

void PlayStoreApp::openFeaturedApp(int idx) {
    if (idx < 0 || idx >= CATALOG_COUNT) return;
    if (!isInstalled(idx)) {
        installFeaturedApp(idx);
    }
    const StoreCatalogItem &item = STORE_CATALOG[idx];
    int appIdx = core::AppRegistry::findAppIndex(item.id);
    if (appIdx < 0) appIdx = core::AppRegistry::findAppIndex(item.title);
    if (appIdx >= 0) {
        core::App* app = core::AppRegistry::getApp((size_t)appIdx);
        if (app) ui::UIManager::launchApp(app);
    }
}

void PlayStoreApp::onTouch(uint16_t x, uint16_t y, bool pressed) {
    int vw = 240;
    int vh = 274;
    int listH = vh - HEADER_H - FOOTER_H;

    if (pressed && !m_wasPressed) {
        m_wasPressed   = true;
        m_isDragging   = false;
        m_touchStartY  = (int16_t)y;
        m_lastTouchY   = (int16_t)y;
        m_scrollStartY = m_scrollY;
        return;
    }

    if (pressed && m_wasPressed) {
        if (m_activeTab == 0 && y >= HEADER_H && y < (vh - FOOTER_H)) {
            int dy = (int)m_touchStartY - (int)y;
            if (!m_isDragging && abs(dy) > 7) {
                m_isDragging = true;
            }
            if (m_isDragging) {
                int maxS = getMaxScroll(listH);
                int newS = m_scrollStartY + dy;
                if (newS < 0) newS = 0;
                if (newS > maxS) newS = maxS;
                uint32_t now = millis();
                if (newS != m_scrollY && (now - m_lastScrollDrawMs >= 20)) {
                    m_scrollY = (int16_t)newS;
                    m_lastScrollDrawMs = now;
                    m_needsListRedraw = true;
                }
            }
        }
        m_lastTouchY = (int16_t)y;
        return;
    }

    if (!pressed && m_wasPressed) {
        m_wasPressed = false;
        if (!m_isDragging) {
            handleTap((int16_t)x, (int16_t)y, vw, vh);
        }
        m_isDragging = false;
    }
}

void PlayStoreApp::handleTap(int16_t rx, int16_t ry, int vw, int vh) {
    // 1. Tab Bar
    if (ry >= 22 && ry < HEADER_H) {
        uint8_t newTab = (rx < vw / 2) ? 0 : 1;
        if (newTab != m_activeTab) {
            hal::beep(20);
            m_activeTab = newTab;
            m_needsFullRedraw = true;
        }
        return;
    }

    if (m_activeTab == 0) {
        // List area tap
        if (ry >= HEADER_H && ry < (vh - FOOTER_H)) {
            int docY = (ry - HEADER_H) + m_scrollY - 4;
            int idx = docY / ROW_H;
            if (idx >= 0 && idx < CATALOG_COUNT) {
                hal::beep(20);
                m_selectedIndex = (int16_t)idx;
                if (rx >= vw - 70) {
                    installFeaturedApp(idx);
                } else {
                    m_needsListRedraw = true;
                    m_needsFooterRedraw = true;
                }
            }
            return;
        }

        // Footer buttons
        if (ry >= (vh - FOOTER_H)) {
            hal::beep(25);
            if (rx < 94) {
                installFeaturedApp(m_selectedIndex);
            } else if (rx < 168) {
                openFeaturedApp(m_selectedIndex);
            } else {
                uninstallFeaturedApp(m_selectedIndex);
            }
            return;
        }
    } else {
        // Tab 1: Custom URL Installer
        int bodyY = HEADER_H;
        if (ry >= bodyY + 8 && ry <= bodyY + 70) {
            hal::beep(20);
            char outUrl[128] = {0};
            if (ui::KeyboardView::prompt("Enter .lua Script URL", m_customUrl, outUrl, sizeof(outUrl))) {
                if (outUrl[0] != '\0') {
                    strncpy(m_customUrl, outUrl, sizeof(m_customUrl) - 1);
                    m_customUrl[sizeof(m_customUrl) - 1] = '\0';
                }
            }
            m_needsFullRedraw = true;
        } else if (ry >= bodyY + 76 && ry <= bodyY + 114) {
            hal::beep(20);
            char outFile[32] = {0};
            if (ui::KeyboardView::prompt("Filename in /apps/", m_customFile, outFile, sizeof(outFile))) {
                if (outFile[0] != '\0') {
                    strncpy(m_customFile, outFile, sizeof(m_customFile) - 1);
                    m_customFile[sizeof(m_customFile) - 1] = '\0';
                }
            }
            m_needsFullRedraw = true;
        } else if (ry >= bodyY + 168 && ry <= bodyY + 214) {
            hal::beep(30);
            snprintf(m_statusLine, sizeof(m_statusLine), "Downloading via HTTP/TLS...");
            m_needsFullRedraw = true;
            onRender(hal::getDisplay(), 0, 20, 240, 274);

            char dest[48];
            snprintf(dest, sizeof(dest), "/apps/%s", m_customFile);
            if (downloadUrlToPath(m_customUrl, dest)) {
                core::AppRegistry::scanFilesystemApps();
                snprintf(m_statusLine, sizeof(m_statusLine), "Installed %s to Home!", dest);
                ui::UIManager::showToast("URL App Installed!", 1600);
            } else {
                snprintf(m_statusLine, sizeof(m_statusLine), "Download failed (Check Wi-Fi/URL)");
                ui::UIManager::showToast("Download failed", 1600);
            }
            m_needsFullRedraw = true;
        }
    }
}

} // namespace apps
