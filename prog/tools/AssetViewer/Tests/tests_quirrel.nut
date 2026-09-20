let {Point2} = require("dagor.math")

let test_runtime = require("test_runtime")

let {Mouse, Keys, Modifier} = test_runtime
local tr = test_runtime.getInstance()
println($"Success! Test runtime found: {tr}")

tr.registerTest("quirrel_test", function(_runtime) {
  println("Success!")
})

tr.registerTest("quirrel_yield", function(runtime) {
  println("Yield #1")
  runtime.yieldTime(1.0)
  println("Yield #2")
  runtime.yieldTime(2.0)
  println("Yield #3")
  runtime.yieldFrame()
  println("Success, Yield!")
})

tr.registerTest("quirrel_fail", function(runtime) {
  runtime.markFailed()
  println("Success, Test FAILED!")
})

function printItemInfo(msg, item) {
  local max = item.bb.Max
  local min = item.bb.Min
  local labelStr = item.label != null ? $", label = {item.label}" : ", label = <null>"
  local displayNameStr = item.displayName != null ? $", displayName = {item.displayName}" : ", displayName = <null>"
  local controlTypeStr = item.controlType != null ? $", controlType = {item.controlType}" : ", controlType = <null>"
  local bbStr = $"[Min: ({min.x}, {min.y}), Max: ({max.x}, {max.y})]"
  println("".concat($"{msg}: id = {item.id}, bb = {bbStr}, parentId = {item.parentId}",
    labelStr, displayNameStr, controlTypeStr, $", controlId = {item.controlId}"))
}

tr.registerTest("quirrel_query_item", function(runtime) {
  local item = null
  item = runtime.queryItem("Root/*/*/Assets Tree/Collapse all")
  if (item != null) {
    printItemInfo("Query item", item);
  } else {
    runtime.markFailed();
  }
  item = runtime.queryItem("Root/*/*/No such GUI Item")
  if (item != null) {
    runtime.markFailed();
  }
  println($"Success, Query Item!")
})

tr.registerTest("quirrel_query_children", function(runtime) {
  local children = null
  children = runtime.queryChildren("Root/*/*/Assets Tree")
  if (children != null) {
    println($"Query children len = {children.len()}")
    foreach (item in children) {
      printItemInfo("Query children", item);
    }
  } else {
    runtime.markFailed();
  }
  children = runtime.queryChildren("Root/*/*/No such GUI Item")
  if (children != null) {
    runtime.markFailed();
  }
  println($"Success, Query Children!")
})

tr.registerTest("quirrel_mouse_move", function(runtime) {
  local positions = [Point2(100, 200), Point2(200, 200), Point2(200, 100), Point2(100, 100)];
  runtime.input().mousePosition(positions[0]);
  runtime.yieldTime(1.0);
  for (local i = 0; i < positions.len(); i++) {
    runtime.input().mouseMove(positions[i], positions[(i+1)%positions.len()], 1.0);
    runtime.yieldTime(1.0);
  }
  runtime.input().mouseMoveItem("Root/*/*/Assets Tree/Collapse all", 1.0);
  runtime.yieldTime(1.0);
  runtime.input().mouseMoveItem("Root/*/*/Assets Tree/Expand all", 1.0);
  runtime.yieldTime(1.0);
  runtime.input().mouseMoveItem("Root/*/*/Assets Tree/All", 1.0);
  println("Success, Mouse Move!")
})

tr.registerTest("quirrel_mouse_click", function(runtime) {
  runtime.input().mouseClickItem("Root/*/*/Assets Tree/Collapse all", Mouse.Left);
  runtime.yieldTime(1.0);
  runtime.input().mouseClickItem("Root/*/*/Assets Tree/Expand all", Mouse.Left);
  runtime.yieldTime(1.0);
  runtime.input().mouseClickItem("Root/*/*/Assets Tree/All", Mouse.Left);
  runtime.yieldTime(1.0);
  runtime.input().mouseClick(Mouse.Right);
  println("Success, Mouse Click (Item)!")
})

tr.registerTest("quirrel_mouse_modifier_click", function(runtime) {
  local assets = runtime.queryItem("Root/*/*/Assets Tree");
  local collapse = runtime.queryItem("Root/*/*/Assets Tree/Collapse all");
  if (assets != null && collapse != null) {
    local cSize = collapse.bb.Max - collapse.bb.Min;
    local x = collapse.bb.Max.x - (cSize.y / 2);
    local top = Point2(x, collapse.bb.Max.y + cSize.y);
    local bottom = Point2(x, assets.bb.Max.y - cSize.y * 2);
    local shiftClickScroll = function(pos) {
      runtime.input().mousePosition(pos);
      runtime.yieldFrame();
      runtime.input().mouseClickModifier(Mouse.Left, Modifier.Shift);
      runtime.yieldTime(1.0);
    }
    runtime.yieldTime(1.0);
    shiftClickScroll(top);
    local checkLastAsset = function(should) {
      local item = runtime.queryItem("**/cdk_color_tex_palette");
      return (should && item != null) || (!should && item == null);
    }
    local checkTop = checkLastAsset(false);
    shiftClickScroll(bottom);
    local checkBottom = checkLastAsset(true);
    shiftClickScroll(top);
    local checkTopAgain = checkLastAsset(false);
    if (checkTop && checkBottom && checkTopAgain) {
      println("Success, Mouse Modifier Click!");
    } else {
      runtime.markFailed();
    }
  } else {
    runtime.markFailed();
  }
})

tr.registerTest("quirrel_keys", function(runtime) {
  runtime.input().mouseMoveItem("Root/*/*/Viewport", 1.0);
  runtime.yieldTime(1.0);
  runtime.input().mouseClick(Mouse.Left);
  runtime.yieldTime(1.0);
  runtime.input().keyPress(Keys.Z)
  runtime.yieldTime(1.0);
  local changePerspective = function(k, m) {
    runtime.input().keyPressModifier(k, m)
    runtime.yieldTime(0.1);
    runtime.input().keyPress(Keys.Z)
    runtime.yieldTime(1.0);
  }
  changePerspective(Keys.F, Modifier.Shift)
  runtime.input().keyPress(Keys.Z)
  runtime.yieldTime(1.0);
  changePerspective(Keys.K, Modifier.Shift)
  changePerspective(Keys.T, Modifier.Shift)
  changePerspective(Keys.B, Modifier.Shift)
  changePerspective(Keys.L, Modifier.Shift)
  changePerspective(Keys.R, Modifier.Shift)
  changePerspective(Keys.P, Modifier.Shift)
  println("Success, Keys!")
})

tr.registerTest("quirrel_character_input", function(runtime) {
  runtime.input().mouseMoveItem("Root/*/*/Assets Tree/##searchInput", 1.0);
  runtime.yieldTime(1.0);
  runtime.input().mouseClick(Mouse.Left);
  runtime.yieldTime(1.0);
  runtime.input().characterInput("cdk_building_doors", 0.2)
  runtime.yieldTime(1.0);
  runtime.input().mouseClick(Mouse.Left);
  runtime.yieldTime(1.0);
  runtime.input().keyPress(Keys.Home);
  runtime.yieldTime(1.0);
  runtime.input().keyHeldModifier(Keys.RightArrow, Modifier.Shift, 3.0);
  runtime.yieldTime(1.0);
  runtime.input().keyPress(Keys.LeftArrow);
  runtime.yieldTime(1.0);
  runtime.input().keyHeld(Keys.Delete, 5.0);
  println("Success, Character Input!")
})

tr.registerTest("quirrel_mouse_drag", function(runtime) {
  local viewport = runtime.queryItem("Root/*/*/Viewport")
  if (viewport == null) {
    runtime.markFailed()
    return
  }
  local min = viewport.bb.Min
  local max = viewport.bb.Max
  local startx = (max.x - min.x) * 0.1
  local cy = min.y + (max.y - min.y) / 2.0
  local from = Point2(min.x + startx, cy)
  local to = Point2(max.x - startx, cy)
  runtime.input().mouseDrag(Mouse.Middle, from, to, 5.0)
  println("Success, Mouse drag!")
})
