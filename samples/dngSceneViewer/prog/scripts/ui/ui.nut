#default:forbid-root-table

from "math" import min
from "%darg/ui_imports.nut" import *
import "console" as console

require("ui_config.nut")
let ecs = require("%dngscripts/ecs.nut")
ecs.clear_vm_entity_systems()
let {licenseWnd, showLicense} = require("licenseTxt.nut")

let {editor, showUIinEditor, editorIsActive} = require("editor.nut")
let {take_screenshot_nogui, take_screenshot} = require("screencap")
let inspectorToggle = require("%darg/helpers/inspector.nut")
let { exit_game } =require("app")


let eventHandlers = freeze({
  ["Global.Screenshot"] = @(...) take_screenshot(),
  ["Global.ScreenshotNoGUI"] = @(...) take_screenshot_nogui()
})

let isFreeCamera = Watched(false)
ecs.register_es("free_cam_ui", {
  [["onInit", "onChange"]] = function(_, _eid, comps){
      isFreeCamera.set(comps.camera__active && comps.free_cam__move)
  }
  onDestroy = @(...) isFreeCamera.set(false)
}, {comps_track = [["camera__active", ecs.TYPE_BOOL], ["free_cam__move", ecs.TYPE_POINT2]]})

console.register_command(@() inspectorToggle(), "ui.inspector")

let hintStyle = {
  fontFxFactor = min(24, hdpx(24))
  fontFxColor = 0xA0000000
  fontFx = FFT_SHADOW
}
let fpsBar = {
  behavior = Behaviors.FpsBar
  size = [sw(20), SIZE_TO_CONTENT]
  hplace = ALIGN_RIGHT
  vplace = ALIGN_BOTTOM
  rendObj = ROBJ_TEXT
  fontSize = hdpx(14)
  halign = ALIGN_RIGHT
}.__update(hintStyle)

let controls = @() {
  rendObj = ROBJ_TEXT
  text = editorIsActive.get()
    ? "Press F12 to disable editor. Press Space to toggle free camera."
    : isFreeCamera.get() ? "Press F10 to disable free camera" : null
  watch = [isFreeCamera, editorIsActive]
  hplace = ALIGN_LEFT
  vplace = ALIGN_BOTTOM
}.__update(hintStyle)

console.command("camera.free 0")

let hints = freeze({
  size = flex()
  margin = hdpx(10)
  children = [controls, fpsBar]
})

return function(){
  let children = [hints]
  if (editorIsActive.get()) {
    if (!showUIinEditor.get())
      children.clear()
  }
  children.append(editor)
  if (showLicense.get())
    children.append(licenseWnd)

  return {
    watch = [editorIsActive, showUIinEditor, showLicense]
    size = flex()
    hotkeys = [["L.Cmd Q", @() exit_game()]]
    children
    eventHandlers
  }
}