from "%darg/ui_imports.nut" import *

let mkBox = @(pos = null) {
  rendObj = ROBJ_SOLID
  size = [sh(2), sh(2)]
  color = 0xCCFFCC66
  pos
}

let mkFloating = @(pos = null) {
  size = [sh(2), sh(2)]
  children = mkBox(pos)
}

let mkList = @(flow) {
   rendObj = ROBJ_FRAME
   color = 0x006688AA
   flow
   gap = sh(1)
   children = [
     mkBox()
     mkBox([sh(0.5), 0]) // change their position only in direction opposite to container's flow
     mkBox([sh(-0.5), 0])
     mkBox([0, sh(0.5)])
     mkBox([0, sh(-0.5)])
     mkBox()
     mkBox([sh(1.5), sh(1.5)])
     mkBox()
     mkFloating([sh(-1.5), sh(-1.5)]) // pos works because it is applied to a container without flow
   ]
}

let vertList = mkList(FLOW_HORIZONTAL)
let horList = mkList(FLOW_VERTICAL)

return {
  hplace = ALIGN_CENTER
  vplace = ALIGN_CENTER
  flow = FLOW_HORIZONTAL
  gap = sh(10)
  children = [vertList, horList]
}
