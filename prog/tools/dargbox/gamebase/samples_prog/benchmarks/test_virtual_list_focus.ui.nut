// Regression test for the two VirtualList focus paths that can lose user data:
//
// KbFocus::onElementDetached() does not run the script onBlur handler, so if a
// focused text input were simply windowed out, an in-progress edit would vanish.
// The behavior must release focus itself, which fires onBlur.
//
// A tail child, on the other hand, is built whatever the window, so its focus
// must survive every window move - the behavior maps a focused child back to its
// item index, and the tail sits past the last one.
//
// Prints TEST_RESULT ok / TEST_RESULT fail:<reason> and exits with 1 on failure,
// so test.py gates on it - see MULTI_FRAME_TESTS there for the frame budget this
// needs, since one render frame does not reach the checks below.
from "%darg/ui_imports.nut" import *

let { exit } = require("dagor.system")

const ROWS = 400
const INPUT_ROW = 5

let rowH = hdpx(20)

let heights = []
for (local i = 0; i < ROWS; i++)
  heights.append(rowH)

let scrollHandler = ScrollHandler()

let blurCount = Watched(0)
let focusedElem = Watched(null)
let tailAttaches = Watched(0)
let tailDetaches = Watched(0)
let tailBlurCount = Watched(0)
let tailInputElem = Watched(null)

// virtualTail children are always built, whatever the window is doing
let tailRow = {
  key = "tail"
  size = [flex(), hdpx(40)]
  rendObj = ROBJ_TEXT
  text = "tail"
  onAttach = @() tailAttaches.set(tailAttaches.get() + 1)
  onDetach = @() tailDetaches.set(tailDetaches.get() + 1)
}

let tailInput = {
  key = "tailInput"
  size = [flex(), hdpx(20)]
  rendObj = ROBJ_TEXT
  text = "tail editable"
  behavior = Behaviors.TextInput
  onAttach = @(elem) tailInputElem.set(elem)
  onBlur = @() tailBlurCount.set(tailBlurCount.get() + 1)
}

function mkRow(i) {
  if (i != INPUT_ROW) {
    return @() {
      size = [flex(), rowH]
      rendObj = ROBJ_TEXT
      text = $"row {i}"
    }
  }
  return @() {
    size = [flex(), rowH]
    rendObj = ROBJ_TEXT
    text = "editable"
    behavior = Behaviors.TextInput
    onAttach = @(elem) focusedElem.set(elem)
    onBlur = @() blurCount.set(blurCount.get() + 1)
  }
}

let rows = []
for (local i = 0; i < ROWS; i++)
  rows.append(mkRow(i))

local frame = 0
local failed = null
let finish = function(msg, ok) {
  println(msg)
  exit(ok ? 0 : 1)
}

gui_scene.setUpdateHandler(function(_dt) {
  frame++
  if (frame == 5) {
    if (focusedElem.get() == null)
      failed = "input row was never built"
    else
      set_kb_focus(focusedElem.get())
  }
  else if (frame == 10) {
    if (failed == null && blurCount.get() != 0)
      failed = "blur fired before scrolling away"
    // scroll far past the input row so it must leave the window
    scrollHandler.scrollToY(ROWS * rowH)
  }
  else if (frame == 40) {
    if (failed == null && blurCount.get() != 1)
      failed = $"expected exactly 1 onBlur after scrolling away, got {blurCount.get()}"
    if (failed == null && tailAttaches.get() != 1)
      failed = $"tail should attach once and stay, attaches={tailAttaches.get()}"
    if (failed == null && tailDetaches.get() != 0)
      failed = $"tail should never detach, detaches={tailDetaches.get()}"
    if (tailInputElem.get() == null)
      failed = failed ?? "tail input was never built"
    else
      set_kb_focus(tailInputElem.get())
  }
  else if (frame == 45) {
    scrollHandler.scrollToY(0) // walk the window back over every item
  }
  else if (frame == 80) {
    if (failed == null && tailBlurCount.get() != 0)
      failed = $"tail focus must survive window moves, got {tailBlurCount.get()} onBlur"
    finish(failed == null ? "TEST_RESULT ok" : $"TEST_RESULT fail:{failed}", failed == null)
  }
})

return {
  size = flex()
  rendObj = ROBJ_SOLID
  color = Color(10, 10, 10)
  children = @() {
    size = flex()
    flow = FLOW_VERTICAL
    clipChildren = true
    behavior = [Behaviors.WheelScroll, Behaviors.VirtualList]
    scrollHandler = scrollHandler

    virtualItems = rows
    virtualItemHeights = heights
    virtualTail = [tailRow, tailInput]
    virtualOverscan = 2
    virtualInitialCount = 20
  }
}
