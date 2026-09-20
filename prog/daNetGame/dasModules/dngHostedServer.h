// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <daScript/daScript.h>
#include <dasModules/dasModulesCommon.h>
#include <daECS/core/entityManager.h>
#include <util/dag_delayedAction.h>
#include <ioSys/dag_dataBlock.h>
#include <startup/dag_globalSettings.h>
#include <string.h>
#include <EASTL/string.h>
#include <dag/dag_vector.h>
#include <debug/dag_log.h>

#include "main/hostedServerLauncher.h"
#include "net/net.h"


namespace bind_dascript
{

inline bool is_hosted_internal_server_active() { return ::is_hosted_internal_server_active(); }

inline const char *get_hosted_internal_server_uid() { return ::get_hosted_internal_server_uid(); }

inline const char *allocate_hosted_server_uid() { return ::allocate_hosted_server_uid(); }

inline void set_hosted_server_start_uid(const char *uid) { ::set_hosted_server_start_uid(uid); }

inline bool is_main_thread_network() { return ::is_main_thread_network(); }


inline void request_start_hosted_server(const das::TArray<char *> &cmds,
  const char *main_das_path,
  const char *circuit,
  const char *uid,
  const char *scene,
  int tickrate,
  const char *game_execution_mode,
  das::Context * /*ctx*/,
  das::LineInfoArg * /*at*/)
{
  dag::Vector<eastl::string> argv;
  argv.reserve(cmds.size + 14);
  if (main_das_path && *main_das_path)
    argv.emplace_back(main_das_path);
  if (circuit && *circuit)
  {
    eastl::string s = "-config:circuit:t=";
    s += circuit;
    argv.emplace_back(eastl::move(s));
  }

  auto add_cfg_b = [&](const char *path, bool v) {
    argv.emplace_back(eastl::string(eastl::string::CtorSprintf(), "-config:%s:b=%s", path, v ? "yes" : "no"));
  };

  const DataBlock &hostDebug = *dgs_get_settings()->getBlockByNameEx("debug");
  add_cfg_b("debug/allowUnsafeDasCode", hostDebug.getBool("allowUnsafeDasCode", false));
  if (hostDebug.paramExists("useAddonVromSrc"))
    add_cfg_b("debug/useAddonVromSrc", hostDebug.getBool("useAddonVromSrc", false));
  if (hostDebug.paramExists("vromfsFirstPriority"))
    add_cfg_b("debug/vromfsFirstPriority", hostDebug.getBool("vromfsFirstPriority", false));
  add_cfg_b("game_das_enable_serialization", dgs_get_settings()->getBool("game_das_enable_serialization", false));

  // Project scene/tickrate/mode are typed params so project cmds stay free of -config:*.
  if (scene && *scene)
    argv.emplace_back(eastl::string(eastl::string::CtorSprintf(), "-config:scene:t=%s", scene));
  if (tickrate > 0)
    argv.emplace_back(eastl::string(eastl::string::CtorSprintf(), "-config:tickrate:i=%d", tickrate));
  if (game_execution_mode && *game_execution_mode)
    argv.emplace_back(eastl::string(eastl::string::CtorSprintf(), "-config:gameExecutionMode:t=%s", game_execution_mode));

  if (::is_main_thread_network())
    argv.emplace_back("-force_main_net");
  else
    argv.emplace_back("-force_user_net");

  for (uint32_t i = 0; i < cmds.size; ++i)
  {
    const char *c = cmds[i] ? cmds[i] : "";
    if (strncmp(c, "-config:", 8) == 0)
    {
      logwarn("request_start_hosted_server: dropping project cmd '%s' (host stamps -config)", c);
      continue;
    }
    argv.emplace_back(c);
  }

  uid = resolve_hosted_server_start_uid(uid);
  argv.emplace_back(eastl::string(eastl::string::CtorSprintf(), "%s%s", TEST_SERVER_LOG_UID_STAMP_PREFIX, uid ? uid : ""));

  if (!try_begin_hosted_server_start(uid))
    return;

  run_action_on_main_thread([argv = eastl::move(argv)]() mutable {
    if (!is_hosted_server_start_pending())
      return;
    ecs::List<ecs::string> cmdList;
    cmdList.reserve(argv.size());
    for (const eastl::string &a : argv)
      cmdList.emplace_back(a.c_str());
    g_entity_mgr->broadcastEvent(EventHostedInternalServerToStart{eastl::move(cmdList)});
  });
}

inline void request_stop_hosted_server(const char *uid, das::Context *, das::LineInfoArg *)
{
  eastl::string id = uid ? uid : "";
  run_action_on_main_thread(
    [id = eastl::move(id)]() { g_entity_mgr->broadcastEvent(EventHostedInternalServerToStop{ecs::string(id.c_str())}); });
}

} // namespace bind_dascript
