-- Ticks the map items the overlay finds done while it reads the game's
-- traffic. A find it can't pin to one map item waits with the ones it can be
-- (its maybes, shown with a "?" on the map). It's ticked once one is left and
-- no other object may own it, or when as many waiting finds as map items
-- share a set of them. The player's own tap on a maybe takes one find that
-- waited on it; other ticks only leave it out of the finds. Kept in finds.json.

local collected = require("collected")

local finds = {}

local FILE = "finds.json"
-- waiting finds kept, the oldest go first
local KEEP = 200
-- waiting finds that share map items, tried in every split up to this many
local SPLIT = 12

-- the newest find taken, so none is taken twice, also after a restart
local last = 0
-- { maybe = { point ids }, rivals = { objects } }
local waiting = {}
-- point id -> true while a waiting find may be it
finds.maybe = {}
-- goes up when maybe changes
finds.version = 0

local function ids(list)
    local out = {}
    for _, id in ipairs(type(list) == "table" and list or {}) do
        local value = math.tointeger(id)
        if value and value > 0 then out[#out + 1] = value end
    end
    return out
end

local function texts(list)
    local out = {}
    for _, each in ipairs(type(list) == "table" and list or {}) do
        if type(each) == "string" and each ~= "" then out[#out + 1] = each end
    end
    return out
end

local function remake()
    finds.maybe = {}
    for _, w in ipairs(waiting) do
        for _, id in ipairs(w.maybe) do finds.maybe[id] = true end
    end
    finds.version = finds.version + 1
end

local function save()
    WriteFile(FILE, JSON_Encode({ last = last, waiting = waiting }))
end

-- the indexes of waiting finds whose map items must all be done: one left
-- of a find with no rivals, or a few such finds with only as many between them
local function sure()
    local exact = {}
    for i, w in ipairs(waiting) do
        if #w.rivals == 0 then exact[#exact + 1] = i end
    end
    -- the exact ones that share map items, together
    local groupOf, groups = {}, {}
    for _, i in ipairs(exact) do
        local into
        for _, id in ipairs(waiting[i].maybe) do
            local g = groupOf[id]
            if g and g ~= into then
                if not into then
                    into = g
                else
                    for _, j in ipairs(groups[g]) do groups[into][#groups[into] + 1] = j end
                    for each, h in pairs(groupOf) do if h == g then groupOf[each] = into end end
                    groups[g] = nil
                end
            end
        end
        if not into then
            into = #groups + 1
            while groups[into] do into = into + 1 end
            groups[into] = {}
        end
        groups[into][#groups[into] + 1] = i
        for _, id in ipairs(waiting[i].maybe) do groupOf[id] = into end
    end
    for _, group in pairs(groups) do
        local n = #group
        if n <= SPLIT then
            for mask = 1, (1 << n) - 1 do
                local union, count, picked = {}, 0, {}
                for b = 0, n - 1 do
                    if mask & (1 << b) ~= 0 then
                        picked[#picked + 1] = group[b + 1]
                        for _, id in ipairs(waiting[group[b + 1]].maybe) do
                            if not union[id] then union[id] = true count = count + 1 end
                        end
                    end
                end
                if count == #picked then return picked, union end
            end
        else
            for _, i in ipairs(group) do
                if #waiting[i].maybe == 1 then return { i }, { [waiting[i].maybe[1]] = true } end
            end
        end
    end
    return nil
end

local function drop(indexes)
    local gone = {}
    for _, i in ipairs(indexes) do gone[i] = true end
    local kept = {}
    for i, w in ipairs(waiting) do
        if not gone[i] then kept[#kept + 1] = w end
    end
    waiting = kept
end

local function settle()
    while true do
        -- done ones are out of every find, a find with none left is dropped
        local kept = {}
        for _, w in ipairs(waiting) do
            local left = {}
            for _, id in ipairs(w.maybe) do
                if not collected.has(id) then left[#left + 1] = id end
            end
            w.maybe = left
            if #left > 0 then kept[#kept + 1] = w end
        end
        waiting = kept
        local picked, union = sure()
        if not picked then break end
        local add = {}
        for id in pairs(union) do add[#add + 1] = id end
        table.sort(add)
        drop(picked)
        collected.change(add, {}, "auto")
    end
    remake()
end

-- the finds of GetFinds, oldest first
function finds.take(list)
    local took = false
    for _, f in ipairs(type(list) == "table" and list or {}) do
        local id = math.tointeger(f.id)
        if id and id > last then
            last, took = id, true
            -- an object found is no other find's rival any more
            if type(f.object) == "string" and f.object ~= "" then
                for _, w in ipairs(waiting) do
                    for r = #w.rivals, 1, -1 do
                        if w.rivals[r] == f.object then table.remove(w.rivals, r) end
                    end
                end
            end
            local point = math.tointeger(f.point)
            if point and point > 0 then
                collected.set(point, true, "auto")
            else
                local maybe = ids(f.maybe)
                if #maybe > 0 then waiting[#waiting + 1] = { maybe = maybe, rivals = texts(f.rivals) } end
            end
        end
    end
    if not took then return end
    while #waiting > KEEP do table.remove(waiting, 1) end
    settle()
    save()
end

function finds.load()
    last, waiting = 0, {}
    local saved = FileExists(FILE) and JSON_Decode(ReadFile(FILE))
    if type(saved) == "table" then
        last = math.tointeger(saved.last) or 0
        -- ids go by the clock, one past now was taken while the clock was ahead
        if last > GetOSTimeMS() * 1000 then last = 0 end
        for _, w in ipairs(type(saved.waiting) == "table" and saved.waiting or {}) do
            if type(w) == "table" then
                local maybe = ids(w.maybe)
                if #maybe > 0 then waiting[#waiting + 1] = { maybe = maybe, rivals = texts(w.rivals) } end
            end
        end
    end
    settle()
end

-- the newest finds, and the ones that came before the extension started
function finds.update()
    finds.take(GetFinds(last))
end

-- set when a map item a find waits on is ticked by a sync, a file or a Get
local unsettled = false

-- once a frame: the waiting finds after such ticks, in one go
function finds.tick()
    if not unsettled then return end
    unsettled = false
    settle()
    save()
end

collected.listen(function(id, on, from)
    if not on or from == "auto" or not finds.maybe[id] then return end
    if from ~= "tap" then
        unsettled = true
        return
    end
    -- the player's tick says which one a find was: the one with the fewest
    -- maybes that holds it, one without rivals first
    local best
    for i, w in ipairs(waiting) do
        for _, each in ipairs(w.maybe) do
            if each == id then
                local b = best and waiting[best]
                if not b or (#w.rivals == 0 and #b.rivals > 0)
                    or ((#w.rivals == 0) == (#b.rivals == 0) and #w.maybe < #b.maybe) then
                    best = i
                end
                break
            end
        end
    end
    if best then drop({ best }) end
    settle()
    save()
end)

return finds
