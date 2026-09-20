// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <util/dag_globDef.h>
#include <libTools/util/undo.h>
#include <memory/dag_mem.h>
#include <util/dag_hash.h>
#include <ska_hash_map/flat_hash_map2.hpp>

namespace
{
struct UndoMergeKeyHash
{
  size_t operator()(const UndoMergeKey &k) const
  {
    // only spreads the buckets, the container still compares the keys, so a collision costs nothing
    const HashVal<64> kindHash = mem_hash_fnv1<64>(reinterpret_cast<const char *>(&k.kind), sizeof(k.kind));
    return static_cast<size_t>(mem_hash_fnv1<64>(reinterpret_cast<const char *>(&k.target), sizeof(k.target), kindHash));
  }
};

using MergedOpMap = ska::flat_hash_map<UndoMergeKey, UndoRedoObject *, UndoMergeKeyHash>;

// Offers `op` to the object that already covers its key; true when it was folded in.
bool fold_into_earlier_op(MergedOpMap &survivors, UndoRedoObject *op)
{
  const UndoMergeKey key = op->get_merge_key();
  if (!key.is_mergeable())
    return false;

  // Looked up before inserting: a hit is the common case in a drag, and it must not build an entry.
  const auto it = survivors.find(key);
  if (it != survivors.end())
    return it->second->merge(*op);

  survivors.insert({key, op});
  return false;
}

// Closes and reverts `o` on the spot, which is all that can be done for it when no operation is open to
// record it into.
void apply_and_revert(UndoRedoObject &o)
{
  o.accepted();
  o.restore(false);
}
} // namespace


class UndoRedoHolder : public UndoRedoObject
{
public:
  DAG_DECLARE_NEW(midmem)

  Tab<UndoRedoObject *> obj;
  String name;
  // Opaque owner of this top-level operation, stamped from UndoSystemImpl::curOwner at accept().
  // The undo system never dereferences it; the editor app uses it to attribute the op to a plugin.
  void *owner = nullptr;

  // The objects that each cover a slice, by merge key, so put() can find the one a new object would fold
  // into. Used only when this holder starts a fold scope; empty on the root, the only one that is trimmed.
  MergedOpMap survivors;

  // How many operations are open inside this one that only group what is put into them. They record into
  // this holder directly, so they need no holder of their own, see UndoSystemImpl::begin.
  int groupDepth = 0;
  bool canCancel = true;

  UndoRedoHolder() : obj(midmem), name(strmem) {}

  ~UndoRedoHolder() override
  {
    for (int i = 0; i < obj.size(); ++i)
      delete (obj[i]);
  }

  void put(UndoRedoObject *o)
  {
    if (!o)
      return;
    obj.push_back(o);
  }

  void clear_after(int p)
  {
    if (p >= obj.size())
      return;
    if (p < 0)
      p = 0;
    for (int i = p; i < obj.size(); ++i)
      delete (obj[i]);
    safe_erase_items(obj, p, obj.size() - p);
  }

  // called to undo changes to database
  // if save_redo_data is true, save data necessary to restore
  //   database to its current state in redo operation
  void restore(bool save) override
  {
    for (int i = obj.size() - 1; i >= 0; --i)
      obj[i]->restore(save);
  }

  // redo undone changes
  void redo() override
  {
    for (int i = 0; i < obj.size(); ++i)
      obj[i]->redo();
  }

  // get approximate size of this object (bytes)
  // used to keep undo data size under reasonable limit
  size_t size() override
  {
    size_t sz = 0;
    for (int i = 0; i < obj.size(); ++i)
      sz += obj[i]->size();
    return sz;
  }

  // called when this object is accepted in UndoSystem
  //   as a result of accept() or cancel()
  void accepted() override
  {
    for (int i = 0; i < obj.size(); ++i)
      obj[i]->accepted();
  }

  // for debugging
  void get_description(String &s) override { s = name; }

  bool is_operation_group() const override { return true; }

  // Offers everything this operation holds to `outer`, the operation around it, which is only safe once
  // this one is accepted. Walks in insertion order, so the earliest object of a key survives.
  void merge_parts(MergedOpMap &outer)
  {
    // Compacts in one pass: erasing each folded object on its own would shift the whole tail every time.
    int keep = 0;
    for (int i = 0; i < obj.size(); ++i)
    {
      UndoRedoObject *o = obj[i];
      if (o->is_operation_group())
      {
        UndoRedoHolder *nested = static_cast<UndoRedoHolder *>(o);
        nested->merge_parts(outer);
        if (!nested->obj.size())
        {
          delete nested; // everything it held folded into an operation further out
          continue;
        }
      }
      else if (fold_into_earlier_op(outer, o))
      {
        delete o;
        continue;
      }
      obj[keep++] = o;
    }

    if (keep < obj.size())
      erase_items(obj, keep, obj.size() - keep);
  }
};


// ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ//


class UndoSystemImpl : public UndoSystem
{
public:
  DAG_DECLARE_NEW(inimem)

  Tab<UndoRedoHolder *> stack;
  String name;
  int curop;
  int max_size;
  IUndoRedoWndClient *wnd;

  bool dirtyFlag;
  int openOps = 0;

  // Stamped onto each operation at accept(); see set_op_owner.
  void *curOwner = nullptr;


  UndoSystemImpl(const char *nm, int sz, IUndoRedoWndClient *_wnd) :
    wnd(_wnd), stack(tmpmem), name(inimem), curop(0), max_size(sz), dirtyFlag(false)
  {
    name = nm;
    UndoRedoHolder *h = new UndoRedoHolder;
    stack.push_back(h);
  }

  ~UndoSystemImpl() override
  {
    for (int i = stack.size() - 1; i >= 0; --i)
      delete (stack[i]);
  }

  void clear() override
  {
    openOps = 0;

    for (int i = stack.size() - 1; i > 0; --i)
      delete (stack[i]);
    safe_erase_items(stack, 1, stack.size() - 1);
    stack[0]->clear_after(0);
    curop = 0;
    if (wnd)
      wnd->updateUndoRedoMenu();
  }

  void set_op_owner(void *owner) override { curOwner = owner; }

  void *get_undo_owner() override
  {
    if (!can_undo())
    {
      return NULL;
    }
    return static_cast<UndoRedoHolder *>(stack.back()->obj[curop - 1])->owner;
  }

  void *get_redo_owner() override
  {
    if (!can_redo())
    {
      return NULL;
    }
    return static_cast<UndoRedoHolder *>(stack.back()->obj[curop])->owner;
  }

  void remove_ops_by_owner(void *owner) override
  {
    if (is_holding())
    {
      return;
    }
    UndoRedoHolder *h = stack.back();
    for (int i = h->obj.size() - 1; i >= 0; --i)
    {
      if (static_cast<UndoRedoHolder *>(h->obj[i])->owner != owner)
      {
        continue;
      }
      delete h->obj[i];
      erase_items(h->obj, i, 1);
      // Removing an op at or below curop shifts the undo position down by one.
      if (i < curop)
      {
        --curop;
      }
    }
    if (curop > h->obj.size())
    {
      curop = h->obj.size();
    }
    if (wnd)
    {
      wnd->updateUndoRedoMenu();
    }
  }

  void set_max_size(int sz) override { max_size = sz; }

  int get_max_size() override { return max_size; }

  void check_size()
  {
    if (stack.size() != 1)
      return;
    UndoRedoHolder &h = *stack[0];

    // calc size
    size_t sz = 0;
    int i;
    for (i = 0; i < curop - 1; ++i)
      sz += h.obj[i]->size();

    // remove operations until size is ok
    for (i = 0; i < curop - 1 && sz > max_size; ++i)
      sz -= h.obj[i]->size();

    if (i <= 0)
      return;

    // really remove operations
    for (int j = 0; j < i; ++j)
      delete (h.obj[j]);
    erase_items(h.obj, 0, i);
  }

  // call this method to start operation,
  //   then call accept() or cancel() to end it.
  // NOTE: operations can be nested
  void begin(bool can_cancel) override
  {
    openOps++;

    // An operation that only groups adds nothing to the history: its name is never shown, it cannot be
    // cancelled, and what it holds would have folded into the operation around it at its accept anyway.
    // So it gets no holder, and records straight into the one around it, which is what lets its puts fold
    // as they arrive. A top level one still needs a holder, it is the history entry.
    if (!can_cancel && stack.size() > 1)
    {
      (stack.back())->groupDepth++;
      return;
    }

    UndoRedoHolder *h = new UndoRedoHolder;
    h->canCancel = can_cancel;
    stack.push_back(h);

    // dirtyFlag=true;
  }

  // put object into current operation.
  // if no operation was started (is_holding() is false),
  //   the passed object will be canceled and destroyed.
  void put(UndoRedoObject *o) override
  {
    if (!o)
      return;
    if (!is_holding())
    {
      apply_and_revert(*o);
      delete o;
      return;
    }

    UndoRedoHolder *h = stack.back();

    // Only against what this operation already covers. The survivor then holds the state this operation
    // started from, so a cancel() of it reverts `o` too and nothing has to be kept. It must not restore()
    // here, that would undo the change the caller is making.
    if (fold_into_earlier_op(h->survivors, o))
    {
      delete o;
      return;
    }

    h->put(o);
  }

  using UndoSystem::put; // the raw put() above would hide the template one otherwise

  bool take_probe(UndoRedoObject &probe, const UndoMergeKey &key) override
  {
    if (!is_holding())
    {
      apply_and_revert(probe);
      return true;
    }

    if (!key.is_mergeable())
      return false;

    // Looked up, never inserted: `probe` is on the caller's stack, so it must not become the object that
    // covers the slice. record_put() inserts the heap copy instead.
    const MergedOpMap &survivors = (stack.back())->survivors;
    const auto it = survivors.find(key);
    return it != survivors.end() && it->second->merge(probe);
  }

  void record_put(UndoRedoObject *o, const UndoMergeKey &key) override
  {
    if (!o)
      return;
    if (!is_holding())
    {
      apply_and_revert(*o);
      delete o;
      return;
    }

    UndoRedoHolder *h = stack.back();
    if (key.is_mergeable())
      h->survivors.insert({key, o}); // no effect when the slice is covered already, so the earliest wins
    h->put(o);
  }

  // accept current operation and leave database in its modified state
  // NOTE: operations can be nested
  void accept(const char *nm) override
  {
    if (!nm)
      nm = "(operation)";
    if (stack.size() <= 1)
      DAG_FATAL("accept '%s' without begin in '%s'", nm, name);
    openOps--;

    UndoRedoHolder *h = stack.back();

    if (h->groupDepth > 0)
    {
      h->groupDepth--; // it had no holder, so there is nothing to close but the count
      return;
    }

    if (!h->obj.size())
    {
      // Dropped rather than cancelled: it recorded nothing, so there is nothing to revert, and cancel()
      // is not allowed on an operation opened with can_cancel false.
      h->accepted();
      stack.pop_back();
      delete h;
      if (wnd)
        wnd->updateUndoRedoMenu();
      return;
    }

    dirtyFlag = true;

    h->name = nm;
    h->owner = curOwner;
    h->accepted();
    stack.pop_back();

    if (stack.size() == 1)
    {
      // clear redo ops if top level! nothing folds into the root, so one edit never folds into another
      (stack.back())->clear_after(curop);
    }
    else
    {
      // h cannot be cancelled any more, so what it holds may now fold into the operation around it
      h->merge_parts((stack.back())->survivors);
      if (!h->obj.size())
      {
        delete h; // all of it folded into the operation around it
        if (wnd)
          wnd->updateUndoRedoMenu();
        return;
      }
    }

    // Released, not just cleared: nothing looks at its map again, and one sized for a whole selection is
    // not small.
    MergedOpMap().swap(h->survivors);
    (stack.back())->put(h);

    if (stack.size() == 1)
    {
      curop = (stack.back())->obj.size();
      check_size();
    }
    if (wnd)
      wnd->updateUndoRedoMenu();
  }

  // cancel current operation and restore database
  //   to its state before last begin(), destroy UndoRedoObjects
  // NOTE: operations can be nested
  void cancel() override
  {
    if (stack.size() <= 1)
      DAG_FATAL("cancel without begin in '%s'", name);
    openOps--;
    UndoRedoHolder *h = stack.back();

    // This was opened without can_cancel, so what it holds is already recorded in the
    // operation around it and reverting that one would undo work it had committed. Closing it anyway is
    // what keeps the begin and accept count even.
    if (h->groupDepth > 0)
    {
      G_ASSERT_FAIL("cancel of an operation opened without can_cancel in '%s'", name);
      h->groupDepth--;
      return;
    }
    if (!h->canCancel)
    {
      // Nothing outside it covers what it holds, so this drops the record and leaves the change applied.
      G_ASSERT_FAIL("cancel of a top level operation opened without can_cancel in '%s'", name);
      h->accepted();
      stack.pop_back();
      delete h;
      if (wnd)
        wnd->updateUndoRedoMenu();
      return;
    }

    h->accepted();
    h->restore(false);
    stack.pop_back();
    delete h; // its survivors named only its own objects, so nothing outside it is left dangling
    if (wnd)
      wnd->updateUndoRedoMenu();
  }

  // returns true if system is saving undo data
  // (begin() was called without matching accept() or cancel())
  bool is_holding() override { return stack.size() > 1; }

  int open_operation_count() override { return openOps; }


  bool can_undo() override
  {
    if (is_holding())
      return false;
    UndoRedoHolder *h = stack.back();
    if (curop > h->obj.size())
      curop = h->obj.size();
    if (curop <= 0)
      return false;
    return true;
  }

  // undo last operation
  void undo() override
  {
    if (!can_undo())
      return;
    UndoRedoHolder *h = stack.back();
    dirtyFlag = true;
    h->obj[--curop]->restore(true);

    if (wnd)
    {
      String opName;
      h->obj[curop]->get_description(opName);
      wnd->onUndoRedo(opName, true);
    }
  }

  bool can_redo() override
  {
    if (is_holding())
      return false;
    UndoRedoHolder *h = stack.back();
    if (curop >= h->obj.size())
      return false;
    if (curop < 0)
      curop = 0;
    return true;
  }

  // redo last undone operation
  void redo() override
  {
    if (!can_redo())
      return;
    UndoRedoHolder *h = stack.back();
    dirtyFlag = true;
    h->obj[curop++]->redo();

    if (wnd)
    {
      String opName;
      h->obj[curop - 1]->get_description(opName);
      wnd->onUndoRedo(opName, false);
    }
  }

  // returns number of (top-level) operations that can be undone
  int undo_level() override
  {
    if (is_holding())
      return 0;
    return curop;
  }

  // returns i-th undo operation name (0 is last operation,
  //    1 is operation before it, etc...), or NULL if no such operation
  const char *get_undo_name(int i) override
  {
    if (is_holding())
      return NULL;
    if (i < 0)
      i = 0;
    UndoRedoHolder *h = stack.back();
    if (curop - 1 - i < 0)
      return NULL;
    return ((UndoRedoHolder *)h->obj[curop - 1 - i])->name;
  }

  // returns number of (top-level) operations that can be redone
  int redo_level() override
  {
    if (is_holding())
      return 0;
    UndoRedoHolder *h = stack.back();
    return h->obj.size() - curop;
  }

  // returns i-th redo operation name (0 is current operation,
  //    1 is operation after it, etc...), or NULL if no such operation
  const char *get_redo_name(int i) override
  {
    if (is_holding())
      return NULL;
    if (i < 0)
      i = 0;
    UndoRedoHolder *h = stack.back();
    if (curop + i >= h->obj.size())
      return NULL;
    return ((UndoRedoHolder *)h->obj[curop + i])->name;
  }


  bool isDirty() const override { return dirtyFlag; }

  void setDirty(bool dirty) override { dirtyFlag = dirty; }
};


// ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ//


UndoSystem *create_undo_system(const char *name, int maxsz, IUndoRedoWndClient *wnd) { return new UndoSystemImpl(name, maxsz, wnd); }
