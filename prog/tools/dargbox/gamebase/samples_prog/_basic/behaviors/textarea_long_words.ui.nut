from "%darg/ui_imports.nut" import *
from "math" import max

let cursors = require("samples_prog/_cursors.nut")

let text = "Short words, then a <color=#88ff88>Donaudampfschifffahrtsgesellschaftskapitaenswitwenrentenversicherungsantragsformular</color> that is wider than the box, then more words. Also a URL-like https://example.com/a/very/long/path/without/any/spaces/in/it/that/never/ends and the end."

function label(txt) {
  return {
    rendObj = ROBJ_TEXT
    color = Color(198,198,128)
    text = txt
  }
}

function framed(params) {
  return {
    rendObj = ROBJ_FRAME
    size = const [hdpx(300), hdpx(150)]
    padding = hdpx(5)
    children = {
      size = flex()
      rendObj = ROBJ_TEXTAREA
      text
      behavior = Behaviors.TextArea
    }.__update(params)
  }
}

let frameState = Watched({ size = [hdpx(400), hdpx(150)] })

let resizable = @() {
  watch = frameState
  rendObj = ROBJ_FRAME
  color = Color(200,100,100)
  size = frameState.get().size
  padding = hdpx(10)
  behavior = Behaviors.MoveResize
  moveResizeCursors = cursors.moveResizeCursors
  onMoveResize = function(_dx, _dy, dw, dh) {
    let w = frameState.get()
    w.size = [max(hdpx(20), w.size[0]+dw), max(hdpx(20), w.size[1]+dh)]
    return w
  }
  children = {
    size = flex()
    rendObj = ROBJ_TEXTAREA
    text = text
    behavior = [Behaviors.TextArea, Behaviors.WheelScroll]
  }
}

return {
  rendObj = ROBJ_SOLID
  color = Color(80,80,80)
  cursor = cursors.normal
  size = flex()
  padding = sh(2)
  flow = FLOW_VERTICAL
  gap = hdpx(10)
  children = [
    label("breakLongWords = false: a word wider than the area overflows it")
    framed({ breakLongWords = false })
    label("Default (breakLongWords = true): the word is broken at any character")
    {
      flow = FLOW_HORIZONTAL
      gap = hdpx(10)
      children = [
        framed({})
        framed({ indent = hdpx(30), hangingIndent = hdpx(15) })
        framed({ halign = ALIGN_RIGHT })
      ]
    }
    label("Resizable: drag the frame edge to see the word re-break")
    resizable
  ]
}
