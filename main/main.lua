
function createLightTimer()
    if not lightTimer then
        lightTimer = tmr.create()
        lightTimer:register(dat.lightTimeout*1000, tmr.ALARM_SINGLE, function() 
            light("OFF")
            destroyLightTimer()
            dat.lightSelected = 'MAIN'
            table.insert(topub, {'lightSelected', dat.lightSelected})
        end)
        lightTimer:start()
    else
        lightTimer:stop()
        lightTimer:interval(dat.lightTimeout*1000)
        lightTimer:start()
    end
end

function destroyLightTimer()
    if(lightTimer) then
        lightTimer:stop()
        lightTimer:unregister()
        lightTimer = nil
    end
end

function createMoveDetectionTimer()
    if not moveDetectionTimer then
        moveDetectionTimer = tmr.create()
        moveDetectionTimer:register(dat.moveDetectionTimout, tmr.ALARM_SINGLE, function() -- (6870947) 1:54:30.947
            gpio.trig(pinPIR, "up", moveDetected)
            dat.lightSelected = 'MAIN'
            table.insert(topub, {'lightSelected', dat.lightSelected})
            dat.lightMoveDetection = 'ON'
            table.insert(topub, {'lightMoveDetection', dat.lightMoveDetection})
            destroyMoveDetectionTimer()
        end)
        moveDetectionTimer:start()
    else
        moveDetectionTimer:stop()
        moveDetectionTimer:interval(dat.moveDetectionTimout) --6870947
        moveDetectionTimer:start()
    end
end

function destroyMoveDetectionTimer()
    if(moveDetectionTimer) then
        moveDetectionTimer:stop()
        moveDetectionTimer:unregister()
        moveDetectionTimer = nil
    end
end

function light(_light)
    if dat.lightNow ~= _light then
        table.insert(topub, {'lightNow', _light})
    end
    dat.lightNow = _light
    if dat.lightNow == "ON" then
        if dat.lightSelected == 'MAIN' then
            gpio.write(pinLightMain,1)
            gpio.write(pinLightEdison,0)
        else
            gpio.write(pinLightMain,0)
            gpio.write(pinLightEdison,1)
        end
        createLightTimer()
        print('Light ON')
    else
        print('Light OFF')
        if dat.lightSelected == 'MAIN' then
            gpio.write(pinLightMain,0)
        else
            gpio.write(pinLightEdison,0)
        end
        dat.light = 'OFF'
        table.insert(topub, {'light', dat.light})
        destroyLightTimer()
    end
end

function moveDetected()
    print("MOVE!!!")
    if dat.lightNow == "ON" then
        createLightTimer()
    else
        dat.lightTimeout = 600
        createLightTimer()
        light("ON")
    end
end

gpio.trig(pinPIR, "up", moveDetected)

dofile("check_air_temp.lua")
tmr.create():alarm(1000, tmr.ALARM_AUTO,  function()
    dat.count = dat.count + 1

    if dat.count == 30 and not lightTimer then
        gpio.write(pinLightMain,0)
    end
    
    if dat.count >= 60 then
        table.insert(topub, {dat.clntid..'/heap', node.heap()})
        local uptime = rtctime.get()
        table.insert(topub, {dat.clntid..'/uptime', uptime})
--        if table.getn(dat.gasLastValues) >= 5 then
--            table.insert(topub, {'gas', getAvgTable(dat.gasLastValues)})
--        end
        dofile("check_air_temp.lua")
        dat.count = 0
        if dat.error_no > 100 then node.restart() end
    end
    if (topub and #topub ~= 0) then dofile 'mqttpub.lua' end
end) 

