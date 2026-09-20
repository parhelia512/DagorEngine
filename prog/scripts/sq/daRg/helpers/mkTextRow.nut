/*
  this function takes text (for example from localization)
  and replace some tokens with darg components or strings, and all left text with mkText functions, returning list of components
  example:
    let text = "hello {user}, current time: {time}!"
    let mkText = @(text) {rendObj = ROBJ_TEXT, text=text}
    let curUser = Watched("Bob")
    let replaceTable = {
      user = {text=curUser.get(), rendObj = ROBJ_TEXT, color = Color(255,200,200)},
      time = {text=curTime.get(), rendObj = ROBJ_TEXT, color = Color(200,255,200)}
    }

    let greeting = @(){
      children = mkTextRow(text, mkText, replaceTable)
      watch = [curUser, curTime]
      flow = FLOW_HORIZONTAL
      gap = hdpx(5)
    }
    result will be text that will be automatically update text with time and username, and time can be disaplayed with clocks widget
*/

from "%darg/ui_imports.nut" import *
from "types" import String, Array

function mkTextRow(fullText, mkText, replaceTable): array {
  let plainTextSubsts = replaceTable.filter(@(v) v instanceof String)
  if (plainTextSubsts.len() > 0) {
    fullText = fullText.subst(plainTextSubsts)
    replaceTable = replaceTable.filter(@(v) !(v instanceof String))
  }
  local res = [fullText]
  foreach(id, comp in replaceTable) {
    let key = "".concat("{", id, "}")
    let curList = res
    res = []
    foreach(text in curList) {
      if (!(text instanceof String)) {
        res.append(text)
        continue
      }
      local nextIdx = 0
      local idx = text.indexof(key)
      while (idx != null) {
        if (idx > nextIdx)
          res.append(text.slice(nextIdx, idx))
        if (comp instanceof Array)
          res.extend(comp)
        else
          res.append(comp)
        nextIdx = idx + key.len()
        idx = text.indexof(key, nextIdx)
      }
      if (nextIdx < text.len())
        res.append(text.slice(nextIdx))
    }
  }
  return res.map(@(t) t instanceof String ? mkText(t) : t)
}

return mkTextRow