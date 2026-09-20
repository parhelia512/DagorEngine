from "math" import min, max, clamp
from "%sqstd/frp.nut" import WatchedRo
from "%dngscripts/localizations.nut" import loc
from "console" import register_command, command
from "dagor.workcycle" import defer

enum Layers {
  Default
  ComboPopup
  MsgBox
  Tooltip
  Inspector
}

let export = {
  loc
  console_register_command = register_command
  console_command = command
  defer
  Layers
}

let log= require("%sqstd/log.nut")()
let logs = {
  log_for_user = log.dlog //warning disable: -dlog-warn
  dlog = log.dlog //warning disable: -dlog-warn
  log = log.log
  with_prefix = log.with_prefix
  dlogsplit = log.dlogsplit
  vlog = log.vlog
  console_print = log.console_print
  wlog = log.wlog
  wdlog = log.wdlog //disable: -dlog-warn
  debugTableData = log.debugTableData
}

return export.__update(
  {min, max, clamp, WatchedRo},
  require("daRg"),
  require("frp"),
  logs,
  require("%darg/darg_library.nut"),
  require("%sqstd/functools.nut")
  {DngBhv = require("dng.behaviors")}
)
