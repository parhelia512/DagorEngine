from "%darg/ui_imports.nut" import *
from "holdableButton.nut" import holdableButton
import "samples_prog/_cursors.nut" as cursors

let firedCount = Watched(0)
let events = Watched([])

function report(what) {
  firedCount.modify(@(v) v + 1)
  events.modify(function(v) {
    let res = [$"{firedCount.get()}. {what}"]
    res.extend(v)
    return res.slice(0, 6)
  })
}

const txt = @(text, ovr = null) { rendObj = ROBJ_TEXT, text, color = Color(200, 205, 210) }.__update(ovr ?? {})

const dangerStyle = {
  box = {
    normal = { borderWidth = 0, fillColor = Color(96, 34, 34) }
    hover = { fillColor = Color(122, 44, 44) }
    active = { fillColor = Color(72, 24, 24) }
    done = { fillColor = Color(170, 60, 50) }
  }
  ring = { color = Color(255, 130, 90), doneColor = Color(255, 210, 120) }
}

const pillStyle = {
  box = {
    normal = { size = [hdpx(300), hdpx(64)], borderRadius = hdpx(32), borderWidth = hdpx(3), borderColor = Color(60, 110, 140) }
  }
  ring = { width = hdpx(5), color = Color(255, 220, 90), headColor = Color(255, 240, 200) }
  text = { normal = { fontSize = hdpx(22) } }
}

const quickStyle = {padding = [hdpx(8), hdpx(10)], box = { normal = { borderWidth = 0, fillColor = 0 }}
}

let buttons = [
  holdableButton("Hold to confirm", @() report("confirm"))
  holdableButton("Quick, 0.5 s", @() report("quick"), { holdTime = 0.5 })
  holdableButton("Launch, 3 s (Space, J:Y)", @() report("launch"), { holdTime = 3.0, hotkeys = ["Space", "J:Y"] })
  holdableButton("Delete (Delete, J:X)", @() report("delete"), { hotkeys = ["Delete", "J:X"], style = dangerStyle })
  holdableButton("Normal, no border (Esc, J:Start)", @() report("normal"), { hotkeys = ["Esc", "J:Start"], holdTime = 1.0, style = quickStyle})
]

let eventLog = @() {
  watch = [firedCount, events]
  flow = FLOW_VERTICAL
  gap = hdpx(4)
  halign = ALIGN_CENTER
  size = const [SIZE_TO_CONTENT, hdpx(170)]
  children = [txt($"fired {firedCount.get()} times", { fontSize = hdpx(18) })]
    .extend(events.get().map(@(e) txt(e, { color = Color(150, 160, 170) })))
}

return {
  rendObj = ROBJ_SOLID
  color = Color(30, 40, 50)
  size = flex()
  cursor = cursors.normal
  flow = FLOW_VERTICAL
  halign = ALIGN_CENTER
  valign = ALIGN_CENTER
  gap = hdpx(28)
  children = [
    txt("Keep the mouse button, the gamepad click or the hotkey down until the ring closes; release early to cancel", { fontSize = hdpx(16) })
    {
      flow = FLOW_HORIZONTAL
      gap = hdpx(20)
      valign = ALIGN_CENTER
      children = buttons
    }
    holdableButton("Wide pill, 1.5 s (Enter)", @() report("pill"), { holdTime = 1.5, hotkeys = ["Enter"], style = pillStyle })
    eventLog
  ]
}
