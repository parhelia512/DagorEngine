// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <daECS/core/event.h>
#include <daECS/core/componentTypes.h>
#include <generic/dag_enumBitMask.h>
#include <util/dag_string.h>

class DataBlock;

namespace Sqrat
{
class Table;
}

ECS_BROADCAST_EVENT_TYPE(EventHostedInternalServerDidStart);
ECS_BROADCAST_EVENT_TYPE(EventHostedInternalServerDidStop);
ECS_BROADCAST_EVENT_TYPE(EventHostedInternalServerToStart, ecs::List<ecs::string> /*cmd*/);
// uid: the same -test_log_uid string. A late stop must not take down a newer start.
ECS_BROADCAST_EVENT_TYPE(EventHostedInternalServerToStop, ecs::string /*uid*/);

void bind_hosting_internal_server(Sqrat::Table &ns);
void shutdown_internal_server_on_host_exit();
void schedule_new_internal_server_with_args(int external_argc, char **external_argv);
void kill_internal_server(bool wait);
// No-op when current HIS uid is empty or not uid. Process-exit paths keep untargeted kill.
void kill_internal_server_uid(const char *uid, bool wait);
// Drops pending relaunch. Pair with kill_internal_server() on app-stopped paths.
void cancel_scheduled_internal_server_start();
void prelaunch_internal_server_if_needed();

bool is_hosted_internal_server_active();
const char *get_hosted_internal_server_uid();
const char *allocate_hosted_server_uid();
void set_hosted_server_start_uid(const char *uid);
const char *resolve_hosted_server_start_uid(const char *passed);

bool try_begin_hosted_server_start(const char *uid);
void clear_hosted_server_start_pending();
bool is_hosted_server_start_pending();

bool poll_hosted_ready_watchdog();

void hosted_internal_server_management_update();

// HIS file-log mirror (daNetGame default path, no Eden).
// Host stores a forwarder into the DLL; DagorWinMain arms an in-DLL on_log above
// visuallog that chains prev then calls the host forwarder (default signature below).
//
// Eden override: set_hosted_log_mirror(eden_fn, /*dll_side_override_expect_later*/ true).
// Launcher passes that flag into the DLL so default arm is skipped. Eden dedic does not
// install another on_log: its existing console_output_listener casts the stored void*
// to the Eden host signature (with from_das) and calls it.
typedef void(__cdecl *HostedServerLogForwarder)(int lev_tag, const char *message, const char *filename, int code_line);

void __cdecl default_hosted_server_log_forwarder(int lev_tag, const char *message, const char *filename, int code_line);
// Host-side registration. fn is HostedServerLogForwarder when expect_later=false;
// otherwise an opaque override (Eden-typed). dll_side_override_expect_later: skip DLL default arm.
void set_hosted_log_mirror(void *fn, bool dll_side_override_expect_later = false);
void *get_hosted_log_mirror();
bool hosted_log_mirror_expects_dll_override();

#if DAGOR_HOSTED_INTERNAL_SERVER
// Dedic: opaque host forwarder stored in the DLL (cast to HostedServerLogForwarder or Eden type).
void *hosted_server_get_log_forwarder();
void hosted_server_store_instance_uid(const char *uid);
#endif
void hosted_server_arm_log_mirror_callback(); // HIS DLL: after visual_err_log_setup (default path only)

// Hosted-ready latch: each path signals a flag; ready when (got & required) == required.
// Default required = Entities (DNG). Callers set the mask with hosted_server_set_ready_required.
enum class HostedReadyFlags : uint32_t
{
  None = 0,
  Entities = 1u << 0,
  Scripts = 1u << 1,
};
DAGOR_ENABLE_ENUM_BITMASK(HostedReadyFlags)

// After Eden Game on_initialize (and net-command publish). Latch ES signals Scripts.
ECS_BROADCAST_EVENT_TYPE(OnEdenScriptsInitialized);

void hosted_server_set_ready_required(HostedReadyFlags mask);
void hosted_server_signal_ready(HostedReadyFlags flags);

inline constexpr const char HOSTED_DEDIC_LOG_TAG[] = "[ded]";
inline void format_hosted_dedic_log_line(String &out, const char *message)
{
  out.printf(0, "%s %s", HOSTED_DEDIC_LOG_TAG, message ? message : "");
}

// debug/mirrorHostedDedicLogs (applies on next HIS start). Default off; Eden sets its own default.
void set_mirror_hosted_dedic_logs(bool on);
bool get_mirror_hosted_dedic_logs();

// test_engine log discovery: first -test_log_uid line in a log wins.
inline constexpr const char TEST_LOG_UID_ARG[] = "test_log_uid";
inline constexpr const char TEST_SERVER_LOG_UID_ARG[] = "test_server_log_uid";
inline constexpr const char TEST_LOG_UID_STAMP_PREFIX[] = "-test_log_uid:";
inline constexpr const char TEST_SERVER_LOG_UID_STAMP_PREFIX[] = "-test_server_log_uid:";
void stamp_test_log_uid_if_present();
// True for host-only identity argv (-test_log_uid / -test_server_log_uid, : or =).
bool is_host_identity_uid_argv(const char *arg);
void append_relayed_test_log_uid_arg(DataBlock &start_params);
