
topub = {}
--sens = 1
sent = {}
dat = { count = 0 }
dat.clntid = node.chipid()
print('Client: ',dat.clntid)
dat.topic = 'garage/light'
print('Topic: ',dat.topic)
dat.nodetopic = dat.topic..'/'..dat.clntid
dat.brok = '10.0.2.1'
dat.user = '...'
dat.pass = '...'

dat.pinLightMain = 'OFF'
dat.lightNow = 'OFF'
dat.lightMoveDetection = 'ON'
dat.lightSelected = 'MAIN'
dat.lightTimeout = 600
dat.moveDetectionTimout = 6870947
dat.airTemp = 99
dat.hum = 99
dat.error_no = 0
dat.temp = '---'

pinPIR = 7
pinLightMain = 5
pinLightEdison = 6

gpio.mode(pinPIR, gpio.INT)
gpio.mode(pinLightMain,gpio.OUTPUT)
gpio.mode(pinLightEdison,gpio.OUTPUT)


rtctime.set(0, 0)
dofile('mqttset.lua')
dofile('main.lua')
