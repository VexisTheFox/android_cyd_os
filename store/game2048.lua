-- @name: 2048 Game
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
