from "%darg/ui_imports.nut" import *
let { colors } = require("style.nut")
let nameFilter = require("nameFilter.nut")
let { makeVertScroll } = require("scrollbar.nut")

const rowMargin = fsh(0.5)
let defRowHeight = calc_str_box({text = "A"})[1] + 2 * rowMargin


// A filter input bound to its text: typing sets it, Esc and Enter drop the
// focus, the X clears it. 'params' may add a placeholder or additionalChildren.
function mkListFilter(filterText, params = null) {
  return nameFilter(filterText, {
    placeholder = "Filter by name"
    onChange = @(text) filterText.set(text)
    onEscape = @() set_kb_focus(null)
    onReturn = @() set_kb_focus(null)
    onClear = function() {
      filterText.set("")
      set_kb_focus(null)
    }
  }.__update(params ?? {}))
}


let rowText = @(text) {
  rendObj = ROBJ_TEXT
  text
  color = colors.TextDefault
  margin = rowMargin
}


// Keyed by list.keyOf(item, idx), so a row keeps its state while the filter
// moves it. The item and the list config are read once: the key names the item,
// and one list has one config table for its whole life.
let ListRow = StatefulComp(function(scope, mountItem, idx, mountList) {
  let list = mountList.get()
  let item = mountItem.get()
  let key = list.keyOf(item, idx.get())
  let selected = list.selected
  let isSelectedFn = list.isSelected
  let isSelected = scope.Computed(@() isSelectedFn(selected.get(), key))
  let stateFlags = scope.Watched(0)
  // shared with row content that has its own Button, so the row stays hovered
  let group = ElemGroup()
  let content = list.mkRow(item, { isSelected, stateFlags, scope, group })

  let onElemState = @(sf) stateFlags.set(sf & S_TOP_HOVER)
  let onClick = list.onClick != null ? @(evt) list.onClick(item, evt) : null
  let onDoubleClick = list.onDoubleClick != null ? @(evt) list.onDoubleClick(item, evt) : null
  let onHover = list.onHover != null ? @(on) list.onHover(item, on) : null

  return function() {
    let color = isSelected.get() ? colors.Active
      : (stateFlags.get() & S_TOP_HOVER) ? colors.GridRowHover
      : colors.GridBg[idx.get() % colors.GridBg.len()]
    return {
      watch = [isSelected, stateFlags, idx]
      rendObj = ROBJ_SOLID
      size = list.rowSize
      valign = ALIGN_CENTER
      color
      group
      behavior = Behaviors.Button
      onElemState
      onClick
      onDoubleClick
      onHover
      children = content
    }
  }
}, @(mountItem, idx, mountList) mountList.keyOf(mountItem, idx))


let listRootBase = {
  size = flex()
  flow = FLOW_VERTICAL
  behavior = Behaviors.Button
}

// Keyed by its config table: a parent rebuild that passes the same table keeps
// the instance and its scroll position.
let FilteredList = StatefulComp(function(scope, mountList) {
  let list = mountList.get()
  let { items, selected } = list
  let scrollHandler = ScrollHandler()

  // Scrolling clamps to the laid out content. A timer set from a subscriber
  // fires before the rebuild of the same update, so when the content is still
  // the shorter old list, wait one more update.
  local retry = false
  function reveal() {
    let elem = scrollHandler.elem
    if (elem == null)
      return
    let curItems = items.get()
    if (retry && elem.getContentHeight() < curItems.len() * defRowHeight - 0.5) {
      retry = false
      gui_scene.resetTimeout(0, reveal)
      return
    }
    let sel = selected.get()
    local idx = null
    foreach (i, item in curItems)
      if (list.isSelected(sel, list.keyOf(item, i))) {
        idx = i
        break
      }
    if (idx == null)
      return
    let top = idx * defRowHeight
    let viewH = elem.getHeight()
    let offs = elem.getScrollOffsY()
    if (top < offs)
      scrollHandler.scrollToY(top)
    else if (top + defRowHeight > offs + viewH)
      scrollHandler.scrollToY(top + defRowHeight - viewH)
  }
  function revealLater() {
    retry = true
    gui_scene.resetTimeout(0, reveal)
  }
  scope.onDetach(@() gui_scene.clearTimer(reveal))
  if (list.revealOnItems)
    scope.subscribe(items, @(_v) revealLater())
  if (list.revealOnSelect)
    scope.subscribe(selected, @(_v) revealLater())
  revealLater()

  let rows = scope.Computed(@() items.get().map(@(item, idx) ListRow(item, idx, list)))
  return makeVertScroll(null, {
    scrollHandler
    rootBase = listRootBase
    virtualItems = rows
    virtualItemHeight = defRowHeight
  })
}, @(mountList) mountList)


// A virtualized list of uniform rows that shows its selection on mount.
// cfg:
//   items         observable array
//   selected      observable the rows compare themselves with
//   mkRow(item, row)  row content, one text line high; row = { isSelected, stateFlags, scope, group }
//   keyOf(item, idx)  row identity, default the item itself
//   isSelected(sel, key)  default sel == key
//   onClick(item, evt), onDoubleClick(item, evt), onHover(item, on)  optional
//   revealOnSelect  scroll to the selection when it changes, default true
//   revealOnItems   scroll to the selection after an items change, default true
function mkFilteredList(cfg) {
  let list = {
    keyOf = @(item, _idx) item
    isSelected = @(sel, key) sel == key
    onClick = null
    onDoubleClick = null
    onHover = null
    revealOnSelect = true
    revealOnItems = true
  }.__update(cfg, { rowSize = [flex(), defRowHeight] })
  return {
    size = flex()
    children = FilteredList(list)
  }
}


return {
  mkFilteredList
  mkListFilter
  rowText
}
