m = mqtt.Client(dat.clntid, 60, dat.user, dat.pass)
m:lwt(dat.topic..'/'..dat.clntid..'/state', "OFF", 0, 1)
m:on("offline", function(con)
    m:close()
	dat.broker = false
	dofile('mqttget.lua')
end)
m:on("message", function(conn, topic, dt)
	print('Got broker:', topic, dt)
	if not _G.btbl then _G.btbl = {} end
	table.insert(_G.btbl, {topic,dt})
	dofile('mqttanalise.lua')
end)
dofile('mqttget.lua')
