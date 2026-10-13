local events = require("devilutionx.events")
local render = require("devilutionx.render")
local poe2 = require("devilutionx.poe2")

poe2.enable()

-- Shows the mod version so it's obvious in game that the mod is loaded.
events.GameDrawComplete.add(function()
  render.string("POE2 Mashup v0.9.0", 6, 6)
end)
