// Windowed list: 2000 rows of two fixed heights, only the viewport window is
// built. Compare against bench_virtual_list_off, which builds all of them.
from "%darg/ui_imports.nut" import *

let { mkBenchRunner } = require("samples_prog/benchmarks/bench_stats.nut")
let { rows, heights, mkScroller } = require("samples_prog/benchmarks/bench_virtual_common.nut")

const WARMUP = 120
const CAPTURE = 600

let scrollHandler = ScrollHandler()

mkBenchRunner("bench_virtual_list", {
  warmup = WARMUP
  capture = CAPTURE
  onFrame = mkScroller(scrollHandler, CAPTURE)
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
    virtualOverscan = 3
  }
}
