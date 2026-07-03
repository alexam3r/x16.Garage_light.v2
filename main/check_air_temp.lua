do
    local function getAirTemp()
        i2c.setup(0, 2, 1, i2c.SLOW) -- call i2c.setup() only once
        am2320.setup()
        local rh, t = am2320.read()
        dat.airTemp = math.floor((t/10-5)*10)/10
        dat.hum = math.floor((rh/10)*10)/10
        rh, t = nil, nil
    end


    local status, err = pcall(getAirTemp);
    if status == true then
        print('airTemp='..dat.airTemp..', hum='..dat.hum)
        table.insert(topub, {'airTemp', dat.airTemp})
        table.insert(topub, {'hum', dat.hum})
    else
        dat.message = "AM2320 [Runtime error: "..(err or "#err#").."]"
        print(dat.message)
        table.insert(topub, {'message', dat.message})
    end
end
