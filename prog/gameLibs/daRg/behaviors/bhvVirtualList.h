// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <daRg/dag_behavior.h>

#include <generic/dag_tab.h>


namespace darg
{


// Builds only the items inside the scroll viewport, from 'virtualItems' rather
// than 'children'; each skipped run becomes a zero-size spacer whose margin
// carries its length, so the extent still covers the whole list.
class BhvVirtualList : public darg::Behavior
{
public:
  BhvVirtualList();

  virtual void onElemSetup(Element *, SetupMode setup_mode) override;
  virtual void onDetach(Element *, DetachMode) override;
  virtual void contributeChildren(Element *, dag::Vector<Sqrat::Object, framemem_allocator> &children) override;
  virtual int update(UpdateStage stage, Element *elem, float dt) override;
};


struct BhvVirtualListData
{
  // params
  int axis = 1;
  int overscan = 3;
  int initialCount = 32;

  // itemTop[i] is item i's offset past the element's padding, itemTop[n] the
  // total. Exact only while items lay out at their declared height, with no
  // flow-axis margin and no container gap.
  Tab<float> itemTop;

  // the window contributeChildren() emits; count < 0 until it is first seeded
  int first = 0;
  int count = -1;

  // item index per contributed child, or NOT_AN_ITEM for a spacer or tail entry:
  // the children are not one per item, since runs collapse into spacers.
  Tab<int> builtItems;

  // latches the inert-item warning, so it is not repeated every act
  bool warnedInertItem = false;

  // how many children contributeChildren() emitted, or -1 when not all of them.
  // A different count means builtItems no longer lines up with the children.
  int contributed = -1;

  int nItems() const { return itemTop.size() > 0 ? int(itemTop.size()) - 1 : 0; }
  float totalSize() const { return itemTop.empty() ? 0.0f : itemTop.back(); }
};


extern BhvVirtualList bhv_virtual_list;


} // namespace darg
