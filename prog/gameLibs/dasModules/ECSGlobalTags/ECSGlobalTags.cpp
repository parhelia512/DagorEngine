// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <daScript/daScript.h>
#include <dasModules/dasModulesCommon.h>
#include <dasModules/aotEcs.h>
#include <dasModules/aotECSGlobalTags.h>
#include <dasModules/aotECSGlobalTagsDas.h>

namespace bind_dascript
{
bool ecs_has_tag_in_context(const char *tag, das::Context *context)
{
  if (!tag)
    return false;
  ecs::EntityManager *mgr = nullptr;
  if (EsContext *esCtx = context ? safe_cast_es_context(context) : nullptr)
    mgr = esCtx->mgr;
  if (mgr)
    return ecs_has_tag_in_mgr(tag, *mgr);
  return ecs_has_global_tag(tag);
}

class ECSGlobalTagsModule final : public das::Module
{
public:
  ECSGlobalTagsModule() : das::Module("ECSGlobalTags")
  {
    das::ModuleLibrary lib(this);
    addBuiltinDependency(lib, require("ecs"));

    das::addExtern<DAS_BIND_FUN(ecs_has_tag_in_context)>(*this, lib, "ecs_has_tag", das::SideEffects::accessExternal,
      "bind_dascript::ecs_has_tag_in_context");

    verifyAotReady();
  }
  virtual das::ModuleAotType aotRequire(das::TextWriter &tw) const override
  {
    tw << "#include <dasModules/aotECSGlobalTagsDas.h>\n";
    return das::ModuleAotType::cpp;
  }
};
} // namespace bind_dascript
REGISTER_MODULE_IN_NAMESPACE(ECSGlobalTagsModule, bind_dascript);
