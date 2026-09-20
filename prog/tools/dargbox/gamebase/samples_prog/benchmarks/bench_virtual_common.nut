// Shared list for bench_virtual_list (windowed) and bench_virtual_list_off
// (every row built). Rows come in two fixed heights so the bench exercises
// virtualItemHeights rather than the uniform shortcut, and carry no margin on
// the flow axis - the VirtualList contract, since a spacer child's margin stands
// in for the rows it skips, and a margin only reproduces what the heights
// declared.
from "%darg/ui_imports.nut" import *

const ROWS = 2000
const TALL_EVERY = 7

let rowH = hdpx(20)
let tallH = hdpx(34)

let isTall = @(i) (i % TALL_EVERY) == 0

let heights = []
for (local i = 0; i < ROWS; i++)
  heights.append(isTall(i) ? tallH : rowH)

function mkRow(i) {
  let h = heights[i]
  return @() {
    size = [flex(), h]
    rendObj = ROBJ_SOLID
    color = (i % 2) ? Color(24, 24, 28) : Color(18, 18, 22)
    flow = FLOW_HORIZONTAL
    valign = ALIGN_CENTER
    children = [
      { rendObj = ROBJ_TEXT, text = $"item {i}", size = [flex(), SIZE_TO_CONTENT] }
      { rendObj = ROBJ_TEXT, text = $"value {i}", size = [flex(), SIZE_TO_CONTENT] }
    ]
  }
}

let rows = []
for (local i = 0; i < ROWS; i++)
  rows.append(mkRow(i))

let totalHeight = heights.reduce(@(a, b) a + b, 0)

// sweeps the whole list down and back over the capture window, so the window
// moves on most frames instead of sitting still
function mkScroller(scroll_handler, frames) {
  return function(frame) {
    let phase = (frame.tofloat() % frames) / frames
    let t = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0
    scroll_handler.scrollToY(t * totalHeight)
  }
}

return { rows, heights, mkScroller }
