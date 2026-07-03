do
local itm
local retartnow = function()
	rtcmem.write32(0, 501)
	table.insert(topub, {dat.clntid..'/ip', wifi.sta.getip(),0})
	dofile 'mqttpub.lua'
	tmr.create():alarm(1500, 0, function() node.restart() end)
end

--print('analise:', #btbl)
if btbl and #btbl ~= 0 then
	while #btbl ~= 0 do
		itm = table.remove(btbl)
        local ltop = string.match(itm[1],"./(%w+)$") 

        if ltop == "light" then
            if itm[2] ~= dat.light then
                dat.light = itm[2]
                dat.lightTimeout = 3600
                light(dat.light)
            end
        end

        if ltop == "lightMoveDetection" then
            if itm[2] ~= dat.lightMoveDetection then
                dat.lightMoveDetection = itm[2]
                if dat.lightMoveDetection == 'OFF' then
                    gpio.trig(pinPIR, "none")
                    createMoveDetectionTimer()
                else
                    gpio.trig(pinPIR, "up", moveDetected)
                    destroyMoveDetectionTimer()
                end
            end
        end

        if ltop == "lightSelected" then
            if itm[2] ~= dat.lightSelected then
                dat.lightSelected = itm[2]
                if dat.lightNow == 'ON' then
                    light(dat.lightNow)
                end
            end
        end
        
        if ltop == 'ide' then
            retartnow()
        end 
        if ltop == 'restart' then node.restart() end 
	end
	brbl = nil
end
end
