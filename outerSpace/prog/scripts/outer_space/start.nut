
#default:forbid-root-table

import "%dngscripts/ecs.nut" as ecs
from "dagor.system" import DBGLEVEL
from "dagor.debug" import logerr
from "dagor.fs" import scan_folder

ecs.clear_vm_entity_systems()
require("%scripts/globs/sqevents.nut")

let use_realfs = (DBGLEVEL > 0) ? true: false

let files = scan_folder({root=$"%scripts/outer_space/es", vromfs = true, realfs = use_realfs, recursive = true, files_suffix=".nut"})

foreach(i in files) {
  try {
    print($"require: {i}\n")
    require(i)
  } catch (e) {
    logerr($"Module {i} was not loaded - see log for details")
  }
}
require("%scripts/outer_space/report_logerr.nut")
