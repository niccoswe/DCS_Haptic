-- Export telemetry to own haptic app

local socket = require("socket")
local host, port = "127.0.0.1", 12345
local udp = socket.udp()

-- Store original export functions
local originalLuaExportStart = LuaExportStart
local originalLuaExportStop = LuaExportStop
local originalLuaExportActivityNextEvent = LuaExportActivityNextEvent

-- Modify LuaExportStart
function LuaExportStart()
    if originalLuaExportStart then originalLuaExportStart() end
    udp:setpeername(host, port)
end

-- Modify LuaExportStop
function LuaExportStop()
    if originalLuaExportStop then originalLuaExportStop() end
    udp:close()
end

-- Modify LuaExportActivityNextEvent
function LuaExportActivityNextEvent(t)
    local tNext = t
    
    if originalLuaExportActivityNextEvent then 
        tNext = originalLuaExportActivityNextEvent(t)
    end

    local IAS = LoGetIndicatedAirSpeed()
    local AoA = LoGetAngleOfAttack()
    local selfData = LoGetSelfData()
    local airframe = selfData and selfData.Name or ""

    if IAS and AoA and airframe ~= "" then
        local data = string.format("%.2f,%.2f,%s", IAS, AoA, airframe)
        udp:send(data)
    end

    return tNext or (t + 0.5)
end

-- END Export telemetry to own haptic app