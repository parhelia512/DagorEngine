from "%darg/ui_imports.nut" import *
from "math" import PI, sin, cos, min, max, clamp
from "dagor.time" import get_time_msec

const HOLD_TIME_DEF = 2.0
const DONE_PULSE_MS = 400.0
const PIECE_OVERLAP = 0.5
const HALF_PI = PI * 0.5

const defStyle = {
  text = {
    normal = { fontSize = hdpx(18), color = Color(210, 215, 220) }
    hover = { color = Color(240, 245, 250) }
    active = { color = Color(255, 255, 255) }
    done = { color = Color(255, 255, 255) }
  }
  box = {
    normal = { borderRadius = hdpx(10), borderWidth = hdpx(2), borderColor = Color(70, 80, 90), fillColor = Color(38, 44, 52) }
    hover = { borderColor = Color(110, 125, 140), fillColor = Color(48, 56, 66) }
    active = { borderColor = Color(110, 125, 140), fillColor = Color(28, 32, 38) }
    done = { borderColor = Color(120, 230, 140), fillColor = Color(44, 78, 56) }
  }
  ring = {
    width = hdpx(3)
    trackColor = mul_color(Color(255, 255, 255), 0.12, 10)
    color = Color(90, 200, 255)
    headColor = Color(255, 255, 255)
    doneColor = Color(120, 230, 140)
  }
  padding = [hdpx(10), hdpx(24)]
}

function mergeStyle(defaults, ovr) {
  if (ovr == null)
    return defaults
  let res = clone defaults
  foreach (section, val in ovr) {
    if (typeof val != "table" || typeof defaults?[section] != "table") {
      res[section] <- val
      continue
    }
    let merged = clone defaults[section]
    foreach (k, v in val)
      merged[k] <- (typeof v == "table" && typeof merged?[k] == "table") ? merged[k].__merge(v) : v
    res[section] <- merged
  }
  return res
}

const idleHold = { startMs = -1, firedMs = -1 }

const ignoreClick = @() null

const releaseTriggered = @(hk) hk.startswith("^") ? hk : $"^{hk}"

function outlinePath(w, h, radius, lineWidth) {
  let x0 = lineWidth * 0.5
  let x1 = w - x0
  let y1 = h - x0
  let rc = clamp(radius - x0, 0.0, min(x1 - x0, y1 - x0) * 0.5)
  let topHalf = x1 - rc - w * 0.5
  let sideV = y1 - x0 - 2 * rc
  let sideH = x1 - x0 - 2 * rc
  let arc = HALF_PI * rc
  let pieces = [
    { len = topHalf, line = [w * 0.5, x0, x1 - rc, x0] }
    { len = arc, arc = [x1 - rc, x0 + rc, -HALF_PI] }
    { len = sideV, line = [x1, x0 + rc, x1, y1 - rc] }
    { len = arc, arc = [x1 - rc, y1 - rc, 0.0] }
    { len = sideH, line = [x1 - rc, y1, x0 + rc, y1] }
    { len = arc, arc = [x0 + rc, y1 - rc, HALF_PI] }
    { len = sideV, line = [x0, y1 - rc, x0, x0 + rc] }
    { len = arc, arc = [x0 + rc, x0 + rc, PI] }
    { len = topHalf, line = [x0 + rc, x0, w * 0.5, x0] }
  ]
  return { w, h, rc, pieces, total = pieces.reduce(@(sum, p) sum + p.len, 0.0) }
}

function drawSpan(ctx, path, a, b, width, color) {
  local start = 0.0
  foreach (p in path.pieces) {
    let pieceStart = start
    start += p.len
    if (p.len <= 0.0)
      continue
    local ia = max(a, pieceStart)
    local ib = min(b, start)
    if (ib - ia <= 0.01)
      continue
    if (ia > a)
      ia -= PIECE_OVERLAP
    if (ib < b)
      ib += PIECE_OVERLAP
    let t0 = (ia - pieceStart) / p.len
    let t1 = (ib - pieceStart) / p.len
    if ("line" in p) {
      let [px0, py0, px1, py1] = p.line
      ctx.line([px0 + (px1 - px0) * t0, py0 + (py1 - py0) * t0, px0 + (px1 - px0) * t1, py0 + (py1 - py0) * t1], width, color)
    }
    else {
      let [cx, cy, a0] = p.arc
      ctx.sector(cx, cy, path.rc, path.rc, a0 + t0 * HALF_PI, a0 + t1 * HALF_PI, width, color, color, 0)
    }
  }
}

function pointAt(path, dist) {
  local start = 0.0
  foreach (p in path.pieces) {
    if (p.len > 0.0 && dist <= start + p.len) {
      let t = clamp((dist - start) / p.len, 0.0, 1.0)
      if ("line" in p) {
        let [px0, py0, px1, py1] = p.line
        return [px0 + (px1 - px0) * t, py0 + (py1 - py0) * t]
      }
      let [cx, cy, a0] = p.arc
      let ang = a0 + t * HALF_PI
      return [cx + path.rc * cos(ang), cy + path.rc * sin(ang)]
    }
    start += p.len
  }
  let last = path.pieces.top().line
  return [last[2], last[3]]
}

function drawHoldRing(ctx, hold, holdMs, path, ring) {
  let { startMs, firedMs } = hold
  if (startMs < 0)
    return
  let now = get_time_msec()
  let width = ring.width
  drawSpan(ctx, path, 0.0, path.total, width, ring.trackColor)
  if (firedMs >= 0) {
    let k = clamp((now - firedMs) / DONE_PULSE_MS, 0.0, 1.0)
    drawSpan(ctx, path, 0.0, path.total, width * (1.0 + 3.0 * (1.0 - k)), mul_color(ring.doneColor, 0.45 * (1.0 - k),2))
    drawSpan(ctx, path, 0.0, path.total, width, ring.doneColor)
    return
  }
  let done = clamp((now - startMs) / holdMs, 0.0, 1.0) * path.total
  drawSpan(ctx, path, 0.0, done, width, ring.color)
  let [hx, hy] = pointAt(path, done)
  let glowR = width * 2.6
  let headR = width * 1.1
  let glow = mul_color(ring.headColor, 0.25)
  ctx.ellipse(hx, hy, glowR, glowR, 1, glow, glow, glow)
  ctx.ellipse(hx, hy, headR, headR, 1, ring.headColor, ring.headColor, ring.headColor)
}

function holdableButton(text, onClick, params = null) {
  let { holdTime = HOLD_TIME_DEF, hotkeys = null, key = null, style = null } = params ?? {}
  let st = mergeStyle(defStyle, style)
  let ring = st.ring
  let holdMs = max(holdTime, 0.05) * 1000.0
  let holdHotkeys = hotkeys == null ? null : freeze(hotkeys.map(releaseTriggered))

  let stateFlags = Watched(0)
  let hold = Watched(idleHold)
  let timerId = {}

  function fire() {
    hold.set(freeze({ startMs = hold.get().startMs, firedMs = get_time_msec() }))
    onClick()
  }

  function onElemState(sf) {
    let wasHeld = (stateFlags.get() & S_ACTIVE) != 0
    let isHeld = (sf & S_ACTIVE) != 0
    stateFlags.set(sf)
    if (isHeld == wasHeld)
      return
    if (isHeld) {
      hold.set(freeze({ startMs = get_time_msec(), firedMs = -1 }))
      gui_scene.resetTimeout(holdMs / 1000.0, fire, timerId)
    }
    else {
      gui_scene.clearTimer(timerId)
      hold.set(idleHold)
    }
  }

  function resetHold() {
    gui_scene.clearTimer(timerId)
    hold.set(idleHold)
    stateFlags.set(0)
  }

  local path = null
  let progressRing = {
    rendObj = ROBJ_VECTOR_CANVAS
    size = FLEX
    draw = function(ctx, rect) {
      if (path == null || path.w != rect.w || path.h != rect.h)
        path = outlinePath(rect.w, rect.h, st.box.normal.borderRadius, ring.width)
      drawHoldRing(ctx, hold.get(), holdMs, path, ring)
    }
  }

  return function() {
    let sf = stateFlags.get()
    local state = "normal"
    if (hold.get().firedMs >= 0)
      state = "done"
    else if ((sf & S_ACTIVE) != 0)
      state = "active"
    else if ((sf & S_HOVER) != 0)
      state = "hover"
    return {
      watch = [stateFlags, hold]
      key = key ?? timerId
      rendObj = ROBJ_BOX
      halign = ALIGN_CENTER
      valign = ALIGN_CENTER
      behavior = Behaviors.Button
      hotkeys = holdHotkeys
      onClick = ignoreClick
      onElemState
      onDetach = resetHold
      children = [
        { rendObj = ROBJ_TEXT, text, padding = st.padding }.__update(st.text.normal, st.text?[state] ?? {})
        progressRing
      ]
    }.__update(st.box.normal, st.box?[state] ?? {})
  }
}

return freeze({
  holdableButton
  outlinePath
  drawHoldRing
  defHoldableStyle = defStyle
})
