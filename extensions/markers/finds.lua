-- Ticks the map items the overlay finds done while it reads the game's
-- traffic, while auto is on. A find it can't pin to one map item waits with
-- the ones it can be (its maybes, shown with a "?" on the map). It's ticked
-- once one is left and no other object may own it, or when as many waiting
-- finds as map items share a set of them. The player's own tap on a maybe
-- takes one find that waited on it; other ticks only leave it out of the
-- finds. A find of a thing that grows back has back_at, and its tick comes
-- off by itself then, also after a restart, or sooner when the overlay sees
-- its object again. Kept in finds.json. The ticks and what waits to grow back
-- go to the overlay, so its finds pass over them.

local collected = require("collected")

local finds = {}

local FILE = "finds.json"
-- waiting finds kept, the oldest go first
local KEEP = 200
-- waiting finds that share map items, tried in every split up to this many
local SPLIT = 12

-- the newest find taken, so none is taken twice, also after a restart
local last = 0
-- { maybe = { point ids }, rivals = { objects }, back = back_at or nil, object = "group/config" or nil }
local waiting = {}
-- point id -> when its tick comes off, in Unix seconds
local back = {}
-- object -> { at = back_at, points = { point ids } }, the objects those ticks came from
local objects = {}
-- the last second the backs were looked at
local looked = 0
-- what the overlay was given last: collected's version and ours
local givenCollected, givenOurs, ours = -1, -1, 0
-- true while the finds tick map items
finds.auto = true
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

local function text(value)
    return type(value) == "string" and value ~= "" and value or nil
end

local function remake()
    finds.maybe = {}
    for _, w in ipairs(waiting) do
        for _, id in ipairs(w.maybe) do finds.maybe[id] = true end
    end
    finds.version = finds.version + 1
end

local function save()
    local backs = {}
    for id, at in pairs(back) do backs[#backs + 1] = { point = id, at = at } end
    table.sort(backs, function(a, b) return a.point < b.point end)
    local objs = {}
    for object, o in pairs(objects) do objs[#objs + 1] = { object = object, at = o.at, points = o.points } end
    table.sort(objs, function(a, b) return a.object < b.object end)
    WriteFile(FILE, JSON_Encode({ last = last, auto = finds.auto, waiting = waiting, back = backs, objects = objs }))
    ours = ours + 1
end

local function backAt(value)
    local at = math.tointeger(value)
    return at and at > 0 and at or nil
end

-- ticks it with its back time, the later one when it has one already, and
-- keeps which objects it came from
local function tickUntil(list, at, from)
    if at then
        for _, id in ipairs(list) do
            if not back[id] or back[id] < at then back[id] = at end
        end
        for _, object in ipairs(from or {}) do
            local o = objects[object] or { at = at, points = {} }
            if o.at < at then o.at = at end
            local has = {}
            for _, id in ipairs(o.points) do has[id] = true end
            for _, id in ipairs(list) do
                if not has[id] then o.points[#o.points + 1] = id end
            end
            objects[object] = o
        end
    end
    collected.change(list, {}, "auto")
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
        local add, at, from = {}, nil, {}
        for id in pairs(union) do add[#add + 1] = id end
        table.sort(add)
        for _, i in ipairs(picked) do
            local w = waiting[i]
            if w.back and (not at or w.back > at) then at = w.back end
            if w.back and w.object then from[#from + 1] = w.object end
        end
        drop(picked)
        tickUntil(add, at, from)
    end
    remake()
end

-- an object waiting to grow back was seen again: its ticks come off now, and
-- a find of it still waiting is let go
local function grewBack(object)
    local o = objects[object]
    objects[object] = nil
    local off = {}
    for _, id in ipairs(o and o.points or {}) do
        if back[id] then
            back[id] = nil
            off[#off + 1] = id
        end
    end
    for i = #waiting, 1, -1 do
        if waiting[i].object == object and waiting[i].back then table.remove(waiting, i) end
    end
    if #off > 0 then
        table.sort(off)
        collected.change({}, off, "auto")
    end
end

-- the finds of GetFinds, oldest first; with auto off they're passed over,
-- but what grew back still comes off
function finds.take(list)
    local took = false
    for _, f in ipairs(type(list) == "table" and list or {}) do
        local id = math.tointeger(f.id)
        if id and id > last then
            last, took = id, true
            local object = text(f.object)
            if f.how == "back" then
                if object then grewBack(object) end
            elseif finds.auto then
                -- an object found is no other find's rival any more
                if object then
                    for _, w in ipairs(waiting) do
                        for r = #w.rivals, 1, -1 do
                            if w.rivals[r] == object then table.remove(w.rivals, r) end
                        end
                    end
                end
                local point = math.tointeger(f.point)
                if point and point > 0 then
                    tickUntil({ point }, backAt(f.back_at), { object })
                else
                    local maybe = ids(f.maybe)
                    if #maybe > 0 then
                        waiting[#waiting + 1] = { maybe = maybe, rivals = texts(f.rivals), back = backAt(f.back_at), object = object }
                    end
                end
            end
        end
    end
    if not took then return end
    while #waiting > KEEP do table.remove(waiting, 1) end
    settle()
    save()
end

function finds.load()
    last, waiting, back, objects, looked = 0, {}, {}, {}, 0
    finds.auto = true
    local saved = FileExists(FILE) and JSON_Decode(ReadFile(FILE))
    if type(saved) == "table" then
        last = math.tointeger(saved.last) or 0
        -- ids go by the clock, one past now was taken while the clock was ahead
        if last > GetOSTimeMS() * 1000 then last = 0 end
        if saved.auto == false then finds.auto = false end
        for _, w in ipairs(type(saved.waiting) == "table" and saved.waiting or {}) do
            if type(w) == "table" then
                local maybe = ids(w.maybe)
                if #maybe > 0 then
                    waiting[#waiting + 1] = { maybe = maybe, rivals = texts(w.rivals), back = backAt(w.back), object = text(w.object) }
                end
            end
        end
        for _, b in ipairs(type(saved.back) == "table" and saved.back or {}) do
            local id = type(b) == "table" and math.tointeger(b.point)
            local at = type(b) == "table" and backAt(b.at)
            if id and id > 0 and at then back[id] = at end
        end
        for _, o in ipairs(type(saved.objects) == "table" and saved.objects or {}) do
            local object = type(o) == "table" and text(o.object)
            local at = type(o) == "table" and backAt(o.at)
            local points = type(o) == "table" and ids(o.points) or {}
            if object and at and #points > 0 then objects[object] = { at = at, points = points } end
        end
    end
    settle()
end

function finds.setAuto(on)
    if finds.auto == on then return end
    finds.auto = on
    save()
end

-- the ticks of things grown back by now come off, in one go
local function grown(now)
    local off = {}
    for id, at in pairs(back) do
        if at <= now then off[#off + 1] = id end
    end
    for object, o in pairs(objects) do
        if o.at <= now then objects[object] = nil end
    end
    if #off == 0 then return false end
    table.sort(off)
    for _, id in ipairs(off) do back[id] = nil end
    collected.change({}, off, "auto")
    return true
end

-- when a tick comes off by itself, nil when it doesn't
function finds.backAt(id)
    return back[id]
end

-- the newest finds, and the ones that came before the extension started
function finds.update()
    finds.take(GetFinds(last))
end

-- the ticks and what waits to grow back, to the overlay: by object where it's
-- known, the rest by point
local function give()
    local growing, byObject = {}, {}
    for object, o in pairs(objects) do
        local points = {}
        for _, id in ipairs(o.points) do
            if back[id] then
                points[#points + 1] = id
                byObject[id] = true
            end
        end
        growing[#growing + 1] = { object = object, points = points, back_at = o.at }
    end
    for id, at in pairs(back) do
        if not byObject[id] then growing[#growing + 1] = { points = { id }, back_at = at } end
    end
    SetFindsDone(collected.list(), growing)
    givenCollected, givenOurs = collected.version, ours
end

-- set when a map item a find waits on is ticked by a sync, a file or a Get
local unsettled = false

-- once a frame: the waiting finds after such ticks, in one go
function finds.tick()
    local now = math.floor(GetOSTimeMS() / 1000)
    local changed = false
    if now ~= looked then
        looked = now
        changed = grown(now)
    end
    if unsettled or changed then
        unsettled = false
        settle()
        save()
    end
    if givenCollected ~= collected.version or givenOurs ~= ours then give() end
end

collected.listen(function(id, on, from)
    -- a tick taken off by hand or by a site doesn't come back off later
    if not on and from ~= "auto" and back[id] then
        back[id] = nil
        for object, o in pairs(objects) do
            for i = #o.points, 1, -1 do
                if o.points[i] == id then table.remove(o.points, i) end
            end
            if #o.points == 0 then objects[object] = nil end
        end
        unsettled = true
    end
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
