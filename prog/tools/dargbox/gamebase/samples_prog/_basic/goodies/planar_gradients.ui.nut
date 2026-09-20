from "%darg/ui_imports.nut" import *
from "math" import fabs

import "samples_prog/_cursors.nut" as cursors

let {
  makePlanarGradient, make2DGradient, makeHermitEasingFunc, gradientEasings,
  mkSmoothBWGradientX, mkSmoothBWGradientY, mkColoredGradientX, gradRadial
} = require("%darg/helpers/mkGradientImg.nut")

// unit checks below run on load, both under csq (CI) and dargbox

let approx = @[pure] (a:number, b:number, eps:number = 1e-4) fabs(a - b) <= eps

function expectThrow(fn:function, msg:string) {
  local thrown = false
  try {
    fn()
  }
  catch (_e) {
    thrown = true
  }
  assert(thrown, msg)
}

// hermite easing passes through its points, clamps outside [0..1]
let ease = makeHermitEasingFunc(0.0, [0.5, 0.8], 1.0)
assert(approx(ease(0.0), 0.0) && approx(ease(1.0), 1.0), "hermite endpoints")
assert(approx(ease(0.5), 0.8), "hermite mid point")
assert(ease(-1.0) == 0.0 && ease(2.0) == 1.0, "hermite clamps argument")
local prev = ease(0.0)
for (local i = 1; i <= 50; i++) {
  let v = ease(i / 50.0)
  assert(v >= prev - 1e-9, "hermite must be monotone for monotone points")
  prev = v
}
expectThrow(@() makeHermitEasingFunc(0.0), "hermite needs at least 2 points")
expectThrow(@() makeHermitEasingFunc(0.0, [0.7, 0.1], [0.5, 0.2], 1.0), "hermite x must increase")

foreach (name, fn in gradientEasings)
  assert(approx(fn(0.0), 0.0) && approx(fn(1.0), 1.0), $"easing {name} endpoints")

// B&W planar gradient interpolates corners and extra points exactly
let bw = makePlanarGradient({tl = 0.0, tr = 1.0, bl = 1.0, br = 0.0}, [0.5, 0.5, 0.25])
assert(approx(bw(0, 0), 0.0) && approx(bw(1, 0), 1.0), "B&W top corners")
assert(approx(bw(0, 1), 1.0) && approx(bw(1, 1), 0.0), "B&W bottom corners")
assert(approx(bw(0.5, 0.5), 0.25), "B&W extra point")
for (local y = 0; y <= 10; y++)
  for (local x = 0; x <= 10; x++) {
    let v = bw(x / 10.0, y / 10.0)
    assert(v >= 0.0 && v <= 1.0, "B&W output must stay in [0..1]")
  }
assert(bw(-5, 0.5) == bw(0, 0.5) && bw(0.5, 5) == bw(0.5, 1), "arguments clamp to [0..1]")

// colored planar gradient reproduces corner colors (Oklab roundtrip, +-1 per channel)
let channelsClose = @[pure] (c1:int, c2:int) fabs(((c1 >> 24) & 0xFF) - ((c2 >> 24) & 0xFF)) <= 1
  && fabs(((c1 >> 16) & 0xFF) - ((c2 >> 16) & 0xFF)) <= 1
  && fabs(((c1 >> 8) & 0xFF) - ((c2 >> 8) & 0xFF)) <= 1
  && fabs((c1 & 0xFF) - (c2 & 0xFF)) <= 1

let cTl = 0xFFFF4020
let cTr = 0xFF20FF80
let cBl = 0xFF2040FF
let cBr = 0x80FFFFFF
let colored = makePlanarGradient({colored = true, tl = cTl, tr = cTr, bl = cBl, br = cBr})
assert(channelsClose(colored(0, 0), cTl) && channelsClose(colored(1, 0), cTr), "colored top corners")
assert(channelsClose(colored(0, 1), cBl) && channelsClose(colored(1, 1), cBr), "colored bottom corners")

// input validation
expectThrow(@() makePlanarGradient({tl = 0.0, tr = 1.0, bl = 0.0, br = 1.0}, [0.0, 0.0, 0.5]),
  "duplicate point must fail")
expectThrow(@() makePlanarGradient({tl = 0.0, tr = 1.0, bl = 0.0, br = 1.0}, [1.5, 0.0, 0.5]),
  "out of range point must fail")
expectThrow(@() makePlanarGradient({tl = 0.0, tr = 1.0, bl = 0.0, br = 1.0}, [0.5, 0.5]),
  "malformed extra point must fail")
expectThrow(@() makePlanarGradient({tl = 2.0, tr = 1.0, bl = 0.0, br = 1.0}),
  "out of range B&W value must fail")
expectThrow(@() makePlanarGradient({tl = null, tr = 1.0, bl = 0.0, br = 1.0}),
  "non-number B&W value must fail")
expectThrow(@() makePlanarGradient({colored = true, tl = 0.5, tr = cTr, bl = cBl, br = cBr}),
  "float value in colored mode must fail")
expectThrow(@() make2DGradient({tl = 0.0, tr = 1.0, bl = 0.0, br = 1.0, width = 1, height = 8}),
  "too small bitmap must fail")

// picture factories build without errors
assert(make2DGradient({tl = 0.0, tr = 1.0, bl = 1.0, br = 0.0}) != null, "make2DGradient B&W")
assert(mkSmoothBWGradientX({width = 16}) != null && mkSmoothBWGradientY({height = 16}) != null, "smooth B&W gradients")
assert(mkColoredGradientX({colorLeft = cTl, colorRight = cTr}) != null, "colored gradient X")
let radialPic = gradRadial()
assert(radialPic != null && radialPic == gradRadial(), "gradRadial is cached")

// visual part

let mkImage = @(picture, label) {
  flow = FLOW_VERTICAL
  gap = hdpx(4)
  halign = ALIGN_CENTER
  children = [
    { rendObj = ROBJ_IMAGE, image = picture, size = hdpx(200) }
    { rendObj = ROBJ_TEXT, text = label }
  ]
}

return {
  size = flex()
  rendObj = ROBJ_SOLID
  color = Color(30, 30, 30)
  valign = ALIGN_CENTER
  halign = ALIGN_CENTER
  flow = FLOW_HORIZONTAL
  gap = sh(3)
  cursor = cursors.normal
  children = [
    mkImage(make2DGradient({colored = true, tl = cTl, tr = cTr, bl = cBl, br = cBr, width = 64, height = 64}),
      "colored corners")
    mkImage(make2DGradient({colored = true, tl = cTl, tr = cTr, bl = cBl, br = cBr, width = 64, height = 64},
      [0.5, 0.5, 0xFF000000]), "extra black center")
    mkImage(make2DGradient({tl = 0.0, tr = 1.0, bl = 1.0, br = 0.0, width = 64, height = 64}, [0.5, 0.5, 0.25]),
      "B&W with center point")
    mkImage(mkSmoothBWGradientX({width = 64, easing = makeHermitEasingFunc(0.0, [0.3, 0.9], 1.0)}),
      "hermite easing X")
    mkImage(gradRadial(), "radial spot")
  ]
}
