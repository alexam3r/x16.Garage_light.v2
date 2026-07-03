node.LFS._init()

if rtcmem.read32(0) == 501 then
    print("Start Server!")
    rtcmem.write32(0, 0)
    node.LFS.get('http')()
else
    status,err = pcall(function() 
        if not file.exists('to_lfs') and file.exists('LFS.img') then
            to_lfs = file.open("to_lfs", "w")
            to_lfs:close()
            to_lfs = nil
            print('Reloading LFS.img')
            node.LFS.reload('LFS.img')
        else
            if file.exists('to_lfs') then file.remove("to_lfs") end
            if file.exists('LFS.img') then file.remove("LFS.img") end
        end
    end)
    tmr.create():alarm(5000, 0,  function()
        if status then 
            for k,v in pairs(node.LFS.list()) do print(k,v) end
            node.LFS.setglobals()
        else
            print("error:", err)
            print("Start HTTP")
            rtcmem.write32(0, 501)
            node.restart()
        end
        status,err = nil,nil
    end)
end
