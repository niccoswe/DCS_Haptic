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
    -- Your initialization code
    udp:setpeername(host, port)
end

-- Modify LuaExportStop
function LuaExportStop()
    if originalLuaExportStop then originalLuaExportStop() end
    -- Your cleanup code
    udp:close()
end

-- Modify LuaExportActivityNextEvent
function LuaExportActivityNextEvent(t)
    local tNext = t
    
    -- Call the original function if it exists
    if originalLuaExportActivityNextEvent then 
        tNext = originalLuaExportActivityNextEvent(t)
    end

    -- Your telemetry export code
    local IAS = LoGetIndicatedAirSpeed()
    local AoA = LoGetAngleOfAttack()
    local airframe = LoGetSelfData().Name  -- Get the name of the airframe

    if IAS and AoA and airframe then
        local data = string.format("%.2f,%.2f,%s", IAS, AoA, airframe)
        udp:send(data)
    end

    -- Return the next call time (use the original if available, otherwise schedule for 1 second later)
    return tNext or (t + 1.0)
end

-- END Export telemetry to own haptic app