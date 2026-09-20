from "eventbus" import eventbus_subscribe
from "frp" import Watched

let controlsGeneration = Watched({})
let controlsGenerationUpdate = @(...) controlsGeneration.set({})
const CONTROLS_SETUP_CHANGED_EVENT_ID = "controls_setup_changed"
eventbus_subscribe(CONTROLS_SETUP_CHANGED_EVENT_ID, controlsGenerationUpdate)

return freeze({ CONTROLS_SETUP_CHANGED_EVENT_ID, controlsGeneration, controlsGenerationUpdate })
