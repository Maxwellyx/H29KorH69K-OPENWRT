module("luci.controller.hinlinkfan", package.seeall)

function index()
    entry({"admin", "system", "hinlinkfan"}, cbi("/hinlinkfan"),"调温大师",100)
end
