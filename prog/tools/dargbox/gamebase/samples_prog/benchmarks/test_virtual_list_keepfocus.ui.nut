// Covers the branch the other two focus tests never reach: a window move that
// KEEPS the focused item. test_virtual_list_focus and test_virtual_list_horiz
// both scroll the focused item right out, so an inverted boundary in
// BhvVirtualList::update would release focus on any window move - dropping what
// the user is typing on a small scroll - and both would still pass.
//
// The viewport is sized explicitly, because reaching the branch needs a known
// relationship between the scroll delta, the overscan and where the focused item
// sits: scrolling UP moves the window while leaving the item inside it.
//
// Prints TEST_RESULT ok / TEST_RESULT fail:<reason> and exits with 1 on failure.
// See MULTI_FRAME_TESTS in test.py for the frame budget this needs.
from "%darg/ui_imports.nut" import *

let { exit } = require("dagor.system")

const ROWS = 400
const VISIBLE = 10
const FOCUS_ROW = 14
const WITNESS_ROW = 8

let rowH = hdpx(20)

let heights = []
for (local i = 0; i < ROWS; i++)
  heights.append(rowH)

let scrollHandler = ScrollHandler()

let blurCount = Watched(0)
let focusElem = Watched(null)
let witnessAttaches = Watched(0)

function mkRow(i) {
  if (i == FOCUS_ROW) {
    return @() {
      size = [flex(), rowH]
      rendObj = ROBJ_TEXT
      text = "edit"
      behavior = Behaviors.TextInput
      onAttach = @(elem) focusElem.set(elem)
      onBlur = @() blurCount.set(blurCount.get() + 1)
    }
  }
  if (i == WITNESS_ROW) {
    // attaches again once the window moves back over it, which is how the test
    // proves the move happened rather than assuming it
    return @() {
      size = [flex(), rowH]
      rendObj = ROBJ_TEXT
      text = "witness"
      onAttach = @() witnessAttaches.set(witnessAttaches.get() + 1)
    }
  }
  return @() {
    size = [flex(), rowH]
    rendObj = ROBJ_TEXT
    text = $"row {i}"
  }
}

let rows = []
for (local i = 0; i < ROWS; i++)
  rows.append(mkRow(i))

local frame = 0
local failed = null
local witnessBefore = 0
let finish = function(msg, ok) {
  println(msg)
  exit(ok ? 0 : 1)
}

gui_scene.setUpdateHandler(function(_dt) {
  frame++
  if (frame == 5) {
    scrollHandler.scrollToY(15 * rowH) // window settles around rows 13..28
  }
  else if (frame == 20) {
    if (focusElem.get() == null)
      failed = "focus row was never built"
    else
      set_kb_focus(focusElem.get())
  }
  else if (frame == 25) {
    if (failed == null && blurCount.get() != 0)
      failed = "blur fired before the window moved"
    witnessBefore = witnessAttaches.get()
    scrollHandler.scrollToY(10 * rowH) // moves the window up, FOCUS_ROW stays in
  }
  else if (frame == 60) {
    if (failed == null && witnessAttaches.get() <= witnessBefore)
      failed = $"window did not move, so the keep branch was never reached (witness {witnessAttaches.get()})"
    if (failed == null && blurCount.get() != 0)
      failed = $"focus must survive a window move that keeps the item, got {blurCount.get()} onBlur"
    finish(failed == null ? "TEST_RESULT ok" : $"TEST_RESULT fail:{failed}", failed == null)
  }
})

return {
  size = flex()
  rendObj = ROBJ_SOLID
  color = Color(10, 10, 10)
  halign = ALIGN_LEFT
  valign = ALIGN_TOP
  children = @() {
    size = [flex(), rowH * VISIBLE]
    flow = FLOW_VERTICAL
    clipChildren = true
    behavior = [Behaviors.WheelScroll, Behaviors.VirtualList]
    scrollHandler = scrollHandler

    virtualItems = rows
    virtualItemHeights = heights
    virtualOverscan = 2
    virtualInitialCount = 20
  }
}
