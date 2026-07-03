do
m:close()
local conbr
conbr = function(getmq)
    if wifi.sta.status() == 5 then
        getmq:stop()
        getmq:unregister()
        getmq = nil
		m:connect(dat.brok, 1883, false, function(con)
            dat.broker = true
            dat.error_no = 0
			print("Connected to Broker")
            m:subscribe({[dat.topic..'/light']=0,[dat.topic..'/lightMoveDetection']=0,[dat.topic..'/lightSelected']=0,[dat.nodetopic..'/ide']=0,[dat.nodetopic..'/restart']=0}, function(conn)
                print("Subscribed.")
            end)
            m:publish(dat.nodetopic..'/state', "ON", 2, 1)
			conbr = nil
        end,
        function(con, reason)
            dat.error_no = dat.error_no + 1
			print('Fail mqtt! reason:', reason)
            dofile('mqttget.lua')
        end)
    else
        dat.error_no = dat.error_no + 1
        print("Wating for WiFi")
    end
end

tmr.create():alarm(1000, 1, function(t)
    conbr(t)
end)
end
