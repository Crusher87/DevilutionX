local events = require("devilutionx.events")
local render = require("devilutionx.render")

-- Phase 0 smoke test: proves the mod is loaded in-game.
events.GameDrawComplete.add(function()
  render.string("POE2 Mashup v0.0.1", 6, 6)
end)
