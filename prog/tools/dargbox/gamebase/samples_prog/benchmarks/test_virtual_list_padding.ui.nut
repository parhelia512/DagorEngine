// Covers two delivered paths no other scene reaches: flow-axis padding on the
// list root, and the uniform virtualItemHeight input (every other scene passes
// the virtualItemHeights array).
//
// Padding narrows the viewport rather than shifting the item range: item i is on
// screen while scrollOffs <= itemTop[i] <= scrollOffs + size - padTop - padBottom.
// Getting that wrong widens the window instead, so the witness row below - just
// past the correct trailing edge - gets built when it should not. The witness is
// outside the initial seed too, so any attach at all means the window is wrong.
//
// Prints TEST_RESULT ok / TEST_RESULT fail:<reason> and exits with 1 on failure.
// See MULTI_FRAME_TESTS in test.py for the frame budget this needs.
from "%darg/ui_imports.nut" import *

let { exit } = require("dagor.system")

const ROWS = 400
const SEED = 20
const PAD_ROWS = 3
const BOX_ROWS = 16
const SCROLL_ROW = 20
const INSIDE_ROW = 25   // must be built once the window settles
const WITNESS_ROW = 34  // must not be: past the trailing edge, outside the seed

let rowH = hdpx(20)

let scrollHandler = ScrollHandler()

let insideAttaches = Watched(0)
let witnessAttaches = Watched(0)

function mkRow(i) {
  if (i == INSIDE_ROW || i == WITNESS_ROW) {
    let counter = i == INSIDE_ROW ? insideAttaches : witnessAttaches
    return @() {
      size = [flex(), rowH]
      rendObj = ROBJ_TEXT
      text = $"row {i}"
      onAttach = @() counter.set(counter.get() + 1)
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
let finish = function(msg, ok) {
  println(msg)
  exit(ok ? 0 : 1)
}

gui_scene.setUpdateHandler(function(_dt) {
  frame++
  if (frame == 5) {
    scrollHandler.scrollToY(SCROLL_ROW * rowH)
  }
  else if (frame == 40) {
    if (insideAttaches.get() == 0)
      failed = "window never settled over the scrolled range"
    if (failed == null && witnessAttaches.get() != 0)
      failed = $"row {WITNESS_ROW} is past the clipped viewport but was built ({witnessAttaches.get()} attaches)"
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
    size = [flex(), rowH * BOX_ROWS]
    padding = [rowH * PAD_ROWS, 0, rowH * PAD_ROWS, 0]
    flow = FLOW_VERTICAL
    clipChildren = true
    behavior = [Behaviors.WheelScroll, Behaviors.VirtualList]
    scrollHandler = scrollHandler

    virtualItems = rows
    virtualItemHeight = rowH  // the uniform input, not the per-item array
    virtualOverscan = 2
    virtualInitialCount = SEED
  }
}
