// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "statefulComp.h"

#include <daRg/dag_stringKeys.h>
#include <sqstdaux.h>

#include "component.h"
#include "guiScene.h"
#include "scriptUtil.h"
#include "dargDebugUtils.h"

#include <squirrel/sqpcheader.h>
#include <squirrel/sqvm.h>
#include <squirrel/sqstate.h>
#include <squirrel/sqstring.h>
#include <squirrel/sqfuncproto.h>
#include <squirrel/sqclosure.h>

namespace darg
{

using namespace sqfrp;

const char *const stateful_builder_lock_msg =
  "Creating observables, subscribing, or registering onDetach is not allowed in a stateful component builder; do it in the "
  "component ctor";

static const char *const scope_teardown_msg =
  "Creating state or subscribing is not allowed in a scope.onDetach handler; the scope is tearing down";


static WatchedHandle *try_get_observable(const Sqrat::Object &obj)
{
  if (obj.GetType() != OT_INSTANCE)
    return nullptr;
  return Sqrat::ClassType<WatchedHandle>::GetInstanceFromObj(obj.GetObject());
}


// True for a 'mount'-prefixed argument name (mountConfig, mount_config): a
// deliberate mount-time read that opts out of the dead-write report. The word
// boundary keeps unrelated names like 'mountainInfo' out.
static bool is_mount_arg_name(const char *n)
{
  if (!n || strncmp(n, "mount", 5) != 0)
    return false;
  const char c = n[5];
  return c == '\0' || c == '_' || (c >= 'A' && c <= 'Z');
}


static void abandon(Sqrat::Object &obj) { sq_resetobject(&obj.GetObject()); }


void StatefulCompType::abandonScriptRefs()
{
  abandon(ctorFunc);
  abandon(keyFunc);
}


void StatefulCompDesc::abandonScriptRefs()
{
  abandon(typeRef);
  abandon(keyValue);
  for (Sqrat::Object &arg : args)
    abandon(arg);
}


// Sqrat gets a null VM only when the instance dies in the sq_close teardown walk,
// the one case where the script references must be abandoned, not released.
template <typename T>
static SQInteger release_bound_instance(HSQUIRRELVM vm, SQUserPointer ptr, SQInteger size)
{
  if (!vm)
    static_cast<T *>(ptr)->abandonScriptRefs();
  return Sqrat::ClassType<T>::ReleaseOwned(vm, ptr, size);
}


SQInteger StatefulCompType::script_ctor(HSQUIRRELVM vm)
{
  SQInteger top = sq_gettop(vm);
  if (top < 2 || top > 3)
    return sq_throwerror(vm, "Expected StatefulComp(ctor) or StatefulComp(ctor, key)");

  if (sq_gettype(vm, 2) != OT_CLOSURE)
    return sq_throwerror(vm, "StatefulComp ctor must be a script function");

  HSQOBJECT hCtor;
  sq_getstackobj(vm, 2, &hCtor);
  SQFunctionProto *ctorProto = _closure(hCtor)->_function;
  if (ctorProto->_varparams)
    return sq_throwerror(vm, "StatefulComp ctor must not be vararg");
  if (ctorProto->_ndefaultparams > 0)
    return sq_throwerror(vm, "StatefulComp ctor must not have default parameter values (arguments arrive as observables)");

  // '_scope' and '_' keep the static analyzer quiet when the ctor does not use the scope
  const char *scopeParamName = ctorProto->_nparameters >= 2 ? _stringval(ctorProto->_parameters[1]) : "";
  if (strcmp(scopeParamName, "scope") != 0 && strcmp(scopeParamName, "_scope") != 0 && strcmp(scopeParamName, "_") != 0)
    return sq_throwerror(vm, "StatefulComp ctor must take 'scope' (or '_scope', '_' if unused) as its first parameter");

  eastl::unique_ptr<StatefulCompType> self(new StatefulCompType());
  self->ctorFunc = Sqrat::Object(hCtor, vm);
  self->numArgs = ctorProto->_nparameters - 2; // skip 'this' and 'scope'

  self->argDiagSilenced.resize(self->numArgs, false);
  for (int k = 0; k < self->numArgs; ++k)
    if (is_mount_arg_name(_stringval(ctorProto->_parameters[2 + k])))
      self->argDiagSilenced[k] = true;

  if (top == 3 && sq_gettype(vm, 3) != OT_NULL)
  {
    if (sq_gettype(vm, 3) != OT_CLOSURE)
      return sq_throwerror(vm, "StatefulComp key must be a script function or null");

    HSQOBJECT hKey;
    sq_getstackobj(vm, 3, &hKey);
    SQFunctionProto *keyProto = _closure(hKey)->_function;
    if (keyProto->_varparams)
      return sq_throwerror(vm, "StatefulComp key function must not be vararg");
    if (keyProto->_ndefaultparams > 0)
      return sq_throwerror(vm, "StatefulComp key function must not have default parameter values");

    // Bind by name, so that reordering ctor parameters cannot silently
    // re-point the key.
    for (SQInt32 iKey = 1; iKey < keyProto->_nparameters; ++iKey)
    {
      const char *keyParamName = _stringval(keyProto->_parameters[iKey]);
      int argIdx = -1;
      for (SQInt32 iCtor = 2; iCtor < ctorProto->_nparameters; ++iCtor) // the key never sees 'scope'
        if (strcmp(keyParamName, _stringval(ctorProto->_parameters[iCtor])) == 0)
        {
          argIdx = iCtor - 2;
          break;
        }
      if (argIdx < 0)
        return sqstd_throwerrorf(vm, "StatefulComp key parameter '%s' does not name a ctor parameter", keyParamName);
      self->keyArgIndices.push_back(argIdx);
    }
    self->keyFunc = Sqrat::Object(hKey, vm);
  }

  // After SetManagedInstance: it installs the plain ReleaseOwned hook itself.
  Sqrat::ClassType<StatefulCompType>::SetManagedInstance(vm, 1, self.release());
  sq_setreleasehook(vm, 1, &release_bound_instance<StatefulCompType>);
  return 0;
}


SQInteger StatefulCompType::call_mm(HSQUIRRELVM vm)
{
  // stack: 1 = type instance, 2 = call-site 'this', 3.. = arguments
  StatefulCompType *self = Sqrat::ClassType<StatefulCompType>::GetInstance(vm, 1);
  if (!self)
    return SQ_ERROR;

  int nArgs = int(sq_gettop(vm)) - 2;
  if (nArgs != self->numArgs)
  {
    String ctorName;
    get_closure_full_name(self->ctorFunc, ctorName);
    return sqstd_throwerrorf(vm, "%s expects exactly %d argument(s), got %d", ctorName.c_str(), self->numArgs, nArgs);
  }

  HSQOBJECT hType;
  sq_getstackobj(vm, 1, &hType);

  eastl::unique_ptr<StatefulCompDesc> desc(new StatefulCompDesc());
  desc->typeRef = Sqrat::Object(hType, vm);
  desc->type = self;
  desc->args.reserve(nArgs);
  for (int i = 0; i < nArgs; ++i)
  {
    HSQOBJECT hArg;
    sq_getstackobj(vm, 3 + i, &hArg);
    desc->args.push_back(Sqrat::Object(hArg, vm));
  }

  if (!self->keyFunc.IsNull())
  {
    // The key function sees values, not observables: unwrap them.
    sq_pushobject(vm, self->keyFunc.GetObject());
    sq_pushnull(vm);
    for (int k = 0, nk = int(self->keyArgIndices.size()); k < nk; ++k)
    {
      int argIdx = self->keyArgIndices[k];
      const Sqrat::Object &arg = desc->args[argIdx];
      if (WatchedHandle *h = try_get_observable(arg))
      {
        if (!h->graph || !h->graph->resolve(h->id))
        {
          sq_pop(vm, 2 + k);
          return sqstd_throwerrorf(vm, "StatefulComp key: argument %d is a released observable", argIdx + 1);
        }
        sq_pushobject(vm, h->graph->getValue(h->id).GetObject());
      }
      else
        sq_pushobject(vm, arg.GetObject());
    }
    if (SQ_FAILED(sq_call(vm, 1 + int(self->keyArgIndices.size()), SQTrue, SQTrue)))
    {
      sq_pop(vm, 1); // the closure
      return SQ_ERROR;
    }

    HSQOBJECT hKeyVal;
    sq_getstackobj(vm, -1, &hKeyVal);
    desc->keyValue = Sqrat::Object(hKeyVal, vm);
    sq_pop(vm, 2); // result + closure
  }

  auto *cd = Sqrat::ClassType<StatefulCompDesc>::getClassData(vm);
  G_ASSERT_RETURN(cd, sq_throwerror(vm, "StatefulCompDesc class is not registered"));
  sq_pushobject(vm, cd->classObj);
  if (SQ_FAILED(sq_createinstance(vm, -1)))
  {
    sq_pop(vm, 1);
    return sq_throwerror(vm, "Failed to create descriptor instance");
  }
  sq_remove(vm, -2);
  Sqrat::ClassType<StatefulCompDesc>::SetManagedInstance(vm, -1, desc.release());
  sq_setreleasehook(vm, -1, &release_bound_instance<StatefulCompDesc>);
  return 1;
}


StatefulCompDesc *try_get_stateful_desc(const Sqrat::Object &obj)
{
  if (obj.GetType() != OT_INSTANCE)
    return nullptr;
  return Sqrat::ClassType<StatefulCompDesc>::GetInstanceFromObj(obj.GetObject());
}


bool stateful_desc_matches_instance(const StatefulCompDesc *desc, const StatefulInstance *inst)
{
  if (desc->type != inst->type)
    return false;
  HSQUIRRELVM vm = desc->typeRef.GetVM();
  HSQOBJECT a = desc->keyValue.GetObject(), b = inst->keyValue.GetObject();
  return sq_obj_is_equal(vm, &a, &b);
}


// The node is owned by the script handle, so a ctor closure that keeps the
// cell past the instance only ends up with a cell nobody writes any more.
// Immediate + eager pull: reconcile writes must be seen in the same pass.
static Sqrat::Object create_arg_cell(ObservablesGraph *graph, const Sqrat::Object &initial, NodeId &out_id)
{
  HSQUIRRELVM vm = graph->vm;
  out_id = graph->createWatched(initial.GetObject());
  NodeSlot &s = graph->node(out_id);
  s.isImmediate = true;
  s.propagatesImmediate = true;
  s.eagerPull = true;

  auto *cd = Sqrat::ClassType<WatchedHandle>::getClassData(vm);
  G_ASSERT_RETURN(cd, Sqrat::Object());
  SqStackChecker check(vm);
  sq_pushobject(vm, cd->classObj);
  if (SQ_FAILED(sq_createinstance(vm, -1)))
  {
    sq_pop(vm, 1);
    return Sqrat::Object();
  }
  sq_remove(vm, -2);
  Sqrat::ClassType<WatchedHandle>::SetManagedInstance(vm, -1, new WatchedHandle(out_id, graph));
  Sqrat::Var<Sqrat::Object> res(vm, -1);
  sq_pop(vm, 1);
  return res.value;
}


void StatefulScope::abandonScriptRefs()
{
  for (SubEntry &sub : subs)
    abandon(sub.func);
  for (Sqrat::Object &h : detachHandlers)
    abandon(h);
}


void StatefulScope::unsubscribe()
{
  if (!graph)
    return;
  for (SubEntry &sub : subs)
    graph->removeScriptSubscriber(sub.node, sub.func.GetObject());
  subs.clear();

  if (!detachHandlers.empty())
  {
    HSQUIRRELVM vm = graph->vm;
    runningDetachHandlers = true;
    SqStackChecker check(vm);
    for (Sqrat::Object &h : detachHandlers)
    {
      sq_pushobject(vm, h.GetObject());
      sq_pushnull(vm);
      sq_call(vm, 1, SQFalse, SQTrue);
      sq_pop(vm, 1);
    }
    runningDetachHandlers = false;
    detachHandlers.clear();
  }
}


void StatefulScope::dispose()
{
  if (!graph)
    return;
  unsubscribe();
  for (NodeId id : ownedNodes)
    graph->destroyNode(id);
  ownedNodes.clear();
  graph = nullptr;
}


// Creates through the real Watched/Computed class, so every ctor check and the
// source collection stay in one place; the scope only records the node id.
static SQInteger scope_create_node(HSQUIRRELVM vm, const HSQOBJECT &class_obj, bool immediate)
{
  StatefulScope *self = Sqrat::ClassType<StatefulScope>::GetInstance(vm, 1);
  if (!self)
    return SQ_ERROR;
  if (!self->graph)
    return sq_throwerror(vm, "The stateful component scope is already disposed");
  if (self->runningDetachHandlers)
    return sq_throwerror(vm, scope_teardown_msg);

  SQInteger nArgs = sq_gettop(vm) - 1;
  sq_pushobject(vm, class_obj);
  sq_pushnull(vm); // env for the class call
  for (SQInteger i = 0; i < nArgs; ++i)
    sq_push(vm, 2 + i);
  if (SQ_FAILED(sq_call(vm, 1 + nArgs, SQTrue, SQTrue)))
  {
    sq_pop(vm, 1); // the class; the error is already set
    return SQ_ERROR;
  }
  sq_remove(vm, -2); // the class; the new instance stays on top

  HSQOBJECT hInst;
  sq_getstackobj(vm, -1, &hInst);
  WatchedHandle *h = Sqrat::ClassType<WatchedHandle>::GetInstanceFromObj(hInst);
  G_ASSERT_RETURN(h && h->graph == self->graph, sq_throwerror(vm, "Internal error: created observable is invalid"));

  self->ownedNodes.push_back(h->id);
  if (immediate)
  {
    h->setImmediate(true);
    if (!self->graph->node(h->id).isComputed)
      self->graph->node(h->id).eagerPull = true;
  }
  return 1;
}


static SQInteger scope_create_watched(HSQUIRRELVM vm, bool immediate)
{
  auto *cd = Sqrat::ClassType<WatchedHandle>::getClassData(vm);
  G_ASSERT_RETURN(cd, sq_throwerror(vm, "Watched class is not registered"));
  return scope_create_node(vm, cd->classObj, immediate);
}


static SQInteger scope_create_computed(HSQUIRRELVM vm, bool immediate)
{
  auto *cd = Sqrat::ClassType<ComputedHandle>::getClassData(vm);
  G_ASSERT_RETURN(cd, sq_throwerror(vm, "Computed class is not registered"));
  return scope_create_node(vm, cd->classObj, immediate);
}


SQInteger StatefulScope::sqWatched(HSQUIRRELVM vm) { return scope_create_watched(vm, false); }
SQInteger StatefulScope::sqComputed(HSQUIRRELVM vm) { return scope_create_computed(vm, false); }
SQInteger StatefulScope::sqWatchedImmediate(HSQUIRRELVM vm) { return scope_create_watched(vm, true); }
SQInteger StatefulScope::sqComputedImmediate(HSQUIRRELVM vm) { return scope_create_computed(vm, true); }


SQInteger StatefulScope::sqSubscribe(HSQUIRRELVM vm)
{
  StatefulScope *self = Sqrat::ClassType<StatefulScope>::GetInstance(vm, 1);
  if (!self)
    return SQ_ERROR;
  if (!self->graph)
    return sq_throwerror(vm, "The stateful component scope is already disposed");
  if (self->runningDetachHandlers)
    return sq_throwerror(vm, scope_teardown_msg);
  if (self->graph->constructionLockMsg)
    return sq_throwerror(vm, self->graph->constructionLockMsg);

  Sqrat::Var<Sqrat::Object> obsVar(vm, 2);
  WatchedHandle *h = try_get_observable(obsVar.value);
  if (!h || h->graph != self->graph)
    return sq_throwerror(vm, "scope.subscribe expects an observable of this scene");

  SQInteger nparams = 0, nfreevars = 0;
  G_VERIFY(SQ_SUCCEEDED(sq_getclosureinfo(vm, 3, &nparams, &nfreevars)));
  if (nparams != 2 && nparams > -2)
    return sqstd_throwerrorf(vm, "Subscriber function must accept 2 parameters (actual count is %d)", nparams);

  HSQOBJECT func;
  sq_getstackobj(vm, 3, &func);

  switch (self->graph->addScriptSubscriber(h->id, func, /*check_behavior*/ true))
  {
    case ObservablesGraph::SubscribeResult::StaleNode: return sq_throwerror(vm, "Stale observable");
    case ObservablesGraph::SubscribeResult::TooManyNoCheck:
      return sq_throwerror(vm, "Non-checked subscriber count is 255 max. Limit exceeded.");
    case ObservablesGraph::SubscribeResult::Duplicate:
    {
      // The node holds one entry per function identity. A repeated
      // scope.subscribe is a no-op; a callback subscribed outside the scope
      // cannot be owned here - it would silently outlive the instance.
      bool ownedHere = false;
      for (SubEntry &sub : self->subs)
        if (sub.node == h->id && sq_obj_is_equal(vm, &func, &sub.func.GetObject()))
        {
          ownedHere = true;
          break;
        }
      if (!ownedHere)
        return sq_throwerror(vm, "scope.subscribe: this function is already subscribed to the observable outside the scope "
                                 "and would outlive the component; use a distinct function");
      break;
    }
    case ObservablesGraph::SubscribeResult::Added: self->subs.push_back(SubEntry{h->id, Sqrat::Object(func, vm)}); break;
  }

  sq_push(vm, 2); // the observable, for chaining like obs.subscribe
  return 1;
}


SQInteger StatefulScope::sqOnDetach(HSQUIRRELVM vm)
{
  StatefulScope *self = Sqrat::ClassType<StatefulScope>::GetInstance(vm, 1);
  if (!self)
    return SQ_ERROR;
  if (!self->graph)
    return sq_throwerror(vm, "The stateful component scope is already disposed");
  if (self->runningDetachHandlers)
    return sq_throwerror(vm, scope_teardown_msg);
  if (self->graph->constructionLockMsg)
    return sq_throwerror(vm, self->graph->constructionLockMsg);

  HSQOBJECT func;
  sq_getstackobj(vm, 2, &func);
  if (sq_type(func) == OT_CLOSURE)
  {
    SQFunctionProto *proto = _closure(func)->_function;
    int required = proto->_nparameters - 1 - proto->_ndefaultparams;
    if (required > 0)
      return sqstd_throwerrorf(vm, "scope.onDetach handler must be callable with no arguments (%d required)", required);
  }
  else
  {
    SQInteger npc = _nativeclosure(func)->_nparamscheck;
    if (npc > 1 || npc < -1)
      return sq_throwerror(vm, "scope.onDetach handler must be callable with no arguments");
  }
  self->detachHandlers.push_back(Sqrat::Object(func, vm));
  return 0;
}


static Sqrat::Object create_scope_object(ObservablesGraph *graph, StatefulScope *&out_scope)
{
  HSQUIRRELVM vm = graph->vm;
  auto *cd = Sqrat::ClassType<StatefulScope>::getClassData(vm);
  G_ASSERT_RETURN(cd, Sqrat::Object());
  SqStackChecker check(vm);
  sq_pushobject(vm, cd->classObj);
  if (SQ_FAILED(sq_createinstance(vm, -1)))
  {
    sq_pop(vm, 1);
    return Sqrat::Object();
  }
  sq_remove(vm, -2);
  out_scope = new StatefulScope();
  out_scope->graph = graph;
  Sqrat::ClassType<StatefulScope>::SetManagedInstance(vm, -1, out_scope);
  sq_setreleasehook(vm, -1, &release_bound_instance<StatefulScope>);
  Sqrat::Var<Sqrat::Object> res(vm, -1);
  sq_pop(vm, 1);
  return res.value;
}


eastl::unique_ptr<StatefulInstance> stateful_mount(GuiScene *scene, StatefulCompDesc *desc, Component &out_comp)
{
  ObservablesGraph *graph = scene->frpGraph.get();
  HSQUIRRELVM vm = graph->vm;
  const StringKeys *csk = scene->getStringKeys();

  eastl::unique_ptr<StatefulInstance> inst(new StatefulInstance());
  inst->typeRef = desc->typeRef;
  inst->type = desc->type;
  inst->keyValue = desc->keyValue;
  inst->graph = graph;

  inst->scopeRef = create_scope_object(graph, inst->scope);
  if (inst->scopeRef.IsNull())
  {
    darg_immediate_error(vm, "StatefulComp: failed to create the scope object");
    return nullptr;
  }

  // Cells are not owned by the scope, so that disposing the scope cannot
  // release them ahead of their readers.
  inst->argSlots.reserve(desc->args.size());
  for (const Sqrat::Object &arg : desc->args)
  {
    StatefulInstance::ArgSlot slot;
    if (WatchedHandle *h = try_get_observable(arg))
    {
      slot.observable = arg;
      slot.node = h->id;
      slot.pinned = true;
    }
    else
    {
      slot.observable = create_arg_cell(graph, arg, slot.node);
      if (slot.observable.IsNull())
      {
        darg_immediate_error(vm, "StatefulComp: failed to create argument cell");
        return nullptr;
      }
    }
    inst->argSlots.push_back(eastl::move(slot));
  }

  scene->getPerfStats().statefulCtorRuns++;

  Sqrat::Object ctorResult;
  {
    BuilderEvalGuard mutationDeny(vm);
    // The ctor is the place to create state, even when the mount is reached
    // from inside a locked builder (calc_comp_size).
    ConstructionLockGuard unlock(graph, nullptr);

    SqStackChecker check(vm);
    sq_pushobject(vm, inst->type->ctorFunc.GetObject());
    sq_pushnull(vm);
    sq_pushobject(vm, inst->scopeRef.GetObject());
    for (const StatefulInstance::ArgSlot &slot : inst->argSlots)
      sq_pushobject(vm, slot.observable.GetObject());
    if (SQ_FAILED(sq_call(vm, 2 + SQInteger(inst->argSlots.size()), SQTrue, SQTrue)))
    {
      sq_pop(vm, 1); // the closure; the VM has already reported the error
      return nullptr;
    }
    Sqrat::Var<Sqrat::Object> res(vm, -1);
    ctorResult = res.value;
    sq_pop(vm, 2); // result + closure
  }

  String ctorName;
  SQObjectType resType = ctorResult.GetType();
  if (try_get_stateful_desc(ctorResult))
  {
    get_closure_full_name(inst->type->ctorFunc, ctorName);
    darg_immediate_error(vm,
      String(0, "%s: ctor returned a descriptor; return a builder closure or a description table", ctorName.c_str()));
    return nullptr;
  }
  if (resType != OT_CLOSURE && resType != OT_TABLE && resType != OT_CLASS)
  {
    get_closure_full_name(inst->type->ctorFunc, ctorName);
    darg_immediate_error(vm,
      String(0, "%s: ctor must return a builder closure or a description table, got %s", ctorName.c_str(), sq_objtypestr(resType)));
    return nullptr;
  }

  bool built;
  {
    ConstructionLockGuard lock(graph, stateful_builder_lock_msg);
    built = Component::build_component(out_comp, ctorResult, csk, ctorResult);
  }
  if (!built)
    return nullptr; // the error has been reported

  if (!out_comp.uniqueKey.IsNull())
  {
    get_closure_full_name(inst->type->ctorFunc, ctorName);
    darg_immediate_error(vm,
      String(0, "%s: a stateful component description must not set 'key'; identity comes from the StatefulComp key function",
        ctorName.c_str()));
    return nullptr;
  }

  return inst;
}


void stateful_update_args(GuiScene *scene, const StatefulCompDesc *desc, StatefulInstance *inst)
{
  ObservablesGraph *graph = inst->graph;
  HSQUIRRELVM vm = graph->vm;
  G_ASSERT_RETURN(desc->args.size() == inst->argSlots.size(), );

  for (int i = 0, n = int(desc->args.size()); i < n; ++i)
  {
    const Sqrat::Object &arg = desc->args[i];
    StatefulInstance::ArgSlot &slot = inst->argSlots[i];
    WatchedHandle *h = try_get_observable(arg);

    if (slot.pinned)
    {
      if (!h)
        darg_immediate_error(vm, String(0, "StatefulComp: argument %d was an observable at mount but is now a value", i + 1));
      else if (h->id != slot.node)
        darg_immediate_error(vm,
          String(0, "StatefulComp: argument %d is a different observable than at mount; pass the same one", i + 1));
    }
    else
    {
      if (h)
      {
        darg_immediate_error(vm, String(0, "StatefulComp: argument %d was a value at mount but is now an observable", i + 1));
        continue;
      }

#if DAGOR_DBGLEVEL > 0
      NodeSlotData *watched = graph->resolveData(slot.node);
      HSQOBJECT hArg = arg.GetObject();
      bool changed = watched && !sq_obj_is_equal(vm, &watched->value, &hArg);
#endif
      graph->setValue(slot.node, arg); // FRP ignores a write of an equal value
#if DAGOR_DBGLEVEL > 0
      // Nothing reads this observable reactively, so the new value cannot reach
      // the screen: most likely the ctor read it once with get().
      // Says nothing about pinned observables, which are shared with the caller.
      // A 'mount'-prefixed ctor parameter opts out: the cell still updates, only the report is muted.
      G_ASSERT(i < int(inst->type->argDiagSilenced.size()));
      if (changed && !inst->type->argDiagSilenced[i] && !slot.reportedDeadWrite && !graph->nodeHasConsumers(slot.node))
      {
        slot.reportedDeadWrite = true;
        String ctorName;
        get_closure_full_name(inst->type->ctorFunc, ctorName);
        darg_immediate_error(vm, String(0,
                                   "%s: the write to argument %d cannot reach the screen - nothing reads the observable reactively; "
                                   "derive a Computed from it or watch it instead of a one-time get() in the ctor",
                                   ctorName.c_str(), i + 1));
      }
#endif
    }
  }
  G_UNUSED(scene);
}


void StatefulInstance::unsubscribe()
{
  if (scope)
    scope->unsubscribe();
}


void StatefulInstance::dispose()
{
  if (!graph)
    return;
  if (scope)
  {
    scope->dispose();
    scope = nullptr; // freed by the script release hook, possibly right below
  }
  scopeRef.Release();
  argSlots.clear();
  keyValue.Release();
  typeRef.Release();
  graph = nullptr;
}


void bind_stateful_comp(HSQUIRRELVM vm, Sqrat::Table &exports)
{
  ///@class daRg/StatefulComp
  Sqrat::Class<StatefulCompType, Sqrat::NoCopy<StatefulCompType>> typeClass(vm, "StatefulComp");
  typeClass //
    .SquirrelCtor(StatefulCompType::script_ctor, -2, ".c c|o")
    .SquirrelFunc("_call", StatefulCompType::call_mm, -2)
    /**/;

  ///@class daRg/StatefulCompDesc
  Sqrat::Class<StatefulCompDesc, Sqrat::NoConstructor<StatefulCompDesc>> descClass(vm, "StatefulCompDesc");

  ///@class daRg/StatefulScope
  Sqrat::Class<StatefulScope, Sqrat::NoConstructor<StatefulScope>> scopeClass(vm, "StatefulScope");
  scopeClass //
    .SquirrelFuncDeclString(StatefulScope::sqWatched, "instance.Watched([initial: any]): instance")
    .SquirrelFuncDeclString(StatefulScope::sqComputed, "instance.Computed(fn: function): instance")
    .SquirrelFuncDeclString(StatefulScope::sqWatchedImmediate, "instance.WatchedImmediate([initial: any]): instance")
    .SquirrelFuncDeclString(StatefulScope::sqComputedImmediate, "instance.ComputedImmediate(fn: function): instance")
    .SquirrelFuncDeclString(StatefulScope::sqSubscribe, "instance.subscribe(obs: any, handler: function): instance")
    .SquirrelFuncDeclString(StatefulScope::sqOnDetach, "instance.onDetach(handler: function)")
    /**/;

  exports.Bind("StatefulComp", typeClass);
}

} // namespace darg
