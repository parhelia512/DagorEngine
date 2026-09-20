// Baseline for bench_virtual_list: the same 2000 rows and the same scroll
// sweep, but every row is built.
from "%darg/ui_imports.nut" import *

let { mkBenchRunner } = require("samples_prog/benchmarks/bench_stats.nut")
let { rows, mkScroller } = require("samples_prog/benchmarks/bench_virtual_common.nut")

const WARMUP = 120
const CAPTURE = 600

let scrollHandler = ScrollHandler()

mkBenchRunner("bench_virtual_list_off", {
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
    behavior = Behaviors.WheelScroll
    scrollHandler = scrollHandler

    children = rows
  }
}
