// The horizontal counterpart of test_virtual_list_focus: everything else in the
// suite flows vertically, so the axis-0 paths - the margin side that
// make_run_spacer picks for a skipped run, and the offset mapping in
// wanted_window - are otherwise never exercised.
//
// It also covers the null-item path: cols[NULL_COL] is null, so the probe's
// expected offset holds only if the spacer standing in for it keeps its height.
//
// The geometry check is the load-bearing one. A focus assertion alone does not
// reach it: which items the window builds comes from itemTop and the scroll
// offset, so the items can be misplaced while the bookkeeping stays right - the
// blur assertion below passes with the axis-0 margin side deliberately swapped.
// So the test scrolls a middle item into the window and asserts it sits exactly
// its own itemTop past the list root.
//
// Prints TEST_RESULT ok / TEST_RESULT fail:<reason> and exits with 1 on failure.
// See MULTI_FRAME_TESTS in test.py for the frame budget this needs.
import "math" as math
from "%darg/ui_imports.nut" import *

let { exit } = require("dagor.system")

const COLS = 400
const INPUT_COL = 5
const NULL_COL = 99  // inside the same window as the probe
const PROBE_COL = 100

let colW = hdpx(30)

let widths = []
for (local i = 0; i < COLS; i++)
  widths.append(colW)

let scrollHandler = ScrollHandler()

let blurCount = Watched(0)
let focusedElem = Watched(null)
let probeElem = Watched(null)
let rootElem = Watched(null)

function mkCol(i) {
  if (i == INPUT_COL) {
    return @() {
      size = [colW, flex()]
      rendObj = ROBJ_TEXT
      text = "edit"
      behavior = Behaviors.TextInput
      onAttach = @(elem) focusedElem.set(elem)
      onBlur = @() blurCount.set(blurCount.get() + 1)
    }
  }
  if (i == PROBE_COL) {
    return @() {
      size = [colW, flex()]
      rendObj = ROBJ_TEXT
      text = "probe"
      onAttach = @(elem) probeElem.set(elem)
    }
  }
  return @() {
    size = [colW, flex()]
    rendObj = ROBJ_TEXT
    text = $"{i}"
  }
}

let cols = []
for (local i = 0; i < COLS; i++)
  cols.append(mkCol(i))

// A null item is legal and keeps the height declared for it, unlike a null in
// 'children'. It has to sit in the same window as the probe - a null inside the
// skipped run is never built, so the spacer branch would not run - and then the
// probe's offset below holds only while that spacer occupies its height.
cols[NULL_COL] = null

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
      failed = "input column was never built"
    else
      set_kb_focus(focusedElem.get())
  }
  else if (frame == 10) {
    if (failed == null && blurCount.get() != 0)
      failed = "blur fired before scrolling away"
    // exactly the probe column's own offset, so it must land at the left edge
    scrollHandler.scrollToX(PROBE_COL * colW)
  }
  else if (frame == 40) {
    if (failed == null && blurCount.get() != 1)
      failed = $"expected exactly 1 onBlur after scrolling away, got {blurCount.get()}"
    if (failed == null && probeElem.get() == null)
      failed = "probe column was never built"
    if (failed == null && rootElem.get() == null)
      failed = "list root was never built"
    if (failed == null) {
      // screenPos is the layout position: Element::recalcScreenPositions adds
      // relPos to the parent's screenPos, and the scroll is a render transform.
      // So the probe must sit exactly its own itemTop past the list root.
      let want = PROBE_COL * colW
      let got = probeElem.get().getScreenPosX() - rootElem.get().getScreenPosX()
      if (math.fabs(got - want) > 1.5)
        failed = $"probe column {got} past the root, expected {want} (run or null spacer wrong)"
    }
    finish(failed == null ? "TEST_RESULT ok" : $"TEST_RESULT fail:{failed}", failed == null)
  }
})

return {
  size = flex()
  rendObj = ROBJ_SOLID
  color = Color(10, 10, 10)
  children = @() {
    size = flex()
    flow = FLOW_HORIZONTAL
    clipChildren = true
    behavior = [Behaviors.WheelScroll, Behaviors.VirtualList]
    scrollHandler = scrollHandler
    orientation = O_HORIZONTAL
    onAttach = @(elem) rootElem.set(elem)

    virtualItems = cols
    virtualItemHeights = widths
    virtualOverscan = 2
    virtualInitialCount = 20
  }
}
