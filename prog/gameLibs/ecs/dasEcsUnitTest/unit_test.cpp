// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <perfMon/dag_cpuFreq.h>
#include <vecmath/dag_vecMathDecl.h>
#include <math/random/dag_random.h>
#include <vecmath/dag_vecMath.h>
#include <math/dag_Point3.h>
#include <math/dag_TMatrix4.h>
#include <debug/dag_logSys.h>
#include <osApiWrappers/dag_basePath.h>
#include <osApiWrappers/dag_direct.h>
#include <daECS/core/entityManager.h>
#include <daECS/core/componentTypes.h>
#include <daECS/core/updateStage.h>
#include <daECS/core/entitySystem.h>
#include <daECS/core/coreEvents.h>
#include <osApiWrappers/dag_miscApi.h>
#include <osApiWrappers/dag_threads.h>
#include <osApiWrappers/dag_files.h>
#include <perfMon/dag_statDrv.h>
#include <daECS/core/internal/performQuery.h>
#include <daECS/core/internal/typesAndLimits.h>
#include <ecs/scripts/dasEs.h>
#include <startup/dag_globalSettings.h>
#include <daECS/core/sharedComponent.h>
#include <generic/dag_tab.h>
#include <ioSys/dag_findFiles.h>
#include <debug/dag_hwExcept.h>
#include <debug/dag_except.h>
#include <string.h>

#include <ioSys/dag_dataBlock.h>
#include <daECS/io/blk.h>

#include <daScript/misc/platform.h>
#include <daScript/daScriptModule.h>
#include <dasModules/dasFsFileAccess.h>
#include "unitModule.h"

extern bool NEED_DAS_AOT_COMPILE;

static constexpr int DEF_RUNS = 100;

struct SomeComponent
{
  SomeComponent() { debug("created some"); }
  static void requestResources(const char *, const ecs::resource_request_cb_t &res_cb)
  {
    res_cb("some_name", 0);
    debug("requested");
  }
};
ECS_DECLARE_RELOCATABLE_TYPE(SomeComponent);
ECS_REGISTER_RELOCATABLE_TYPE(SomeComponent, nullptr);

ECS_REGISTER_TYPE(TMatrix4, nullptr); // declared in unitModule.h


ECS_BROADCAST_EVENT_TYPE(EventCPPSimpleTestEvent, float, int)
ECS_REGISTER_EVENT(EventCPPSimpleTestEvent)

ECS_BROADCAST_EVENT_TYPE(EventCPPTestEvent, float, int, SimpleString)
ECS_BROADCAST_EVENT_TYPE(EventStart)
ECS_BROADCAST_EVENT_TYPE(EventStart2)
ECS_BROADCAST_EVENT_TYPE(EventEnd)

ECS_REGISTER_EVENT(EventCPPTestEvent)
ECS_REGISTER_EVENT(EventStart)
ECS_REGISTER_EVENT(EventStart2)
ECS_REGISTER_EVENT(EventEnd)

ECS_BROADCAST_EVENT_TYPE(EventCPPTestStringEvent, SimpleString)
ECS_REGISTER_EVENT(EventCPPTestStringEvent)

ECS_BROADCAST_EVENT_TYPE(EventEmpty)
ECS_REGISTER_EVENT(EventEmpty)

static bool had_errors = 0;


void os_debug_break()
{
  logerr("script break");
  had_errors = true;
}
void os_message_box(const char *s, const char *s2, int) { logerr("%s:%s", s, s2); }
static void my_fatal_handler(const char *title, const char *msg, const char *call_stack)
{
  printf("%s:%s\n%s", title, msg, call_stack);
  exit(1);
}

static bool count_expected_errors = false;
static int expected_errors_count = 0;
// matches only the Template::validateSets logerr; unrelated errors keep failing the run
static const char *validate_sets_err_marker = "is not a component of any known template";
// swallows only the expected broken _component fixture logerr at load
static bool swallow_component_block_err = false;

static int log_callback(int lev_tag, const char *fmt, const void * /*arg*/, int /*anum*/, const char * /*ctx_file*/, int /*ctx_line*/)
{
  if (count_expected_errors && lev_tag == LOGLEVEL_ERR && fmt && strstr(fmt, validate_sets_err_marker))
  {
    expected_errors_count++;
    return 1;
  }
  if (swallow_component_block_err && lev_tag == LOGLEVEL_ERR && fmt && strstr(fmt, "_component block at") && strstr(fmt, "is unknown"))
    return 1;
  if (!ignore_log_errors && (lev_tag == LOGLEVEL_ERR || lev_tag == LOGLEVEL_FATAL))
    had_errors = true;
  return 1;
}

#include <osApiWrappers/dag_symHlp.h>
#include <osApiWrappers/dag_dbgStr.h> //set_debug_console_handle
#if _TARGET_PC_WIN
#include <windows.h> //set_debug_console_handle
#endif
extern bool dgs_execute_quiet;

static int count_entities_with_query()
{
  ecs::ComponentDesc eidComp{ECS_HASH("eid"), ecs::ComponentTypeInfo<ecs::EntityId>()};
  ecs::NamedQueryDesc desc{
    "free_per_thread_query_data_test",
    dag::ConstSpan<ecs::ComponentDesc>(),
    dag::ConstSpan<ecs::ComponentDesc>(&eidComp, 1),
    dag::ConstSpan<ecs::ComponentDesc>(),
    dag::ConstSpan<ecs::ComponentDesc>(),
  };
  ecs::QueryId qid = g_entity_mgr->createQuery(desc);
  int count = 0;
  ecs::perform_query(g_entity_mgr, qid, [&count](const ecs::QueryView &qv) { count += qv.end() - qv.begin(); });
  g_entity_mgr->destroyQuery(qid);
  return count;
}

// an exiting thread frees its query TLS node; the later main thread clear() must then free only live-TLS nodes
static void test_free_per_thread_query_data()
{
  const int64_t mainThreadId = get_current_thread_id();
  const int expected = count_entities_with_query();
  struct QueryThread final : public DaThread
  {
    int count = -1;
    QueryThread() : DaThread("freeQueryDataTest") {}
    void execute() override
    {
      g_entity_mgr->setOwnerThreadId(get_current_thread_id());
      count = count_entities_with_query();
      g_entity_mgr->freePerThreadQueryData();
    }
  } thread;
  G_VERIFY(thread.start());
  thread.terminate(true /*wait*/);
  g_entity_mgr->setOwnerThreadId(mainThreadId);
  G_ASSERTF(thread.count == expected, "%d != %d", thread.count, expected);
  count_entities_with_query(); // re-create the main thread node so the final clear() frees it with live TLS
  printf("freePerThreadQueryData test passed (%d entities)\n", expected);
}

int myMain2(int startArgC)
{
  if (df_get_real_name("entities.blk"))
  {
    ecs::TemplateRefs trefs(*g_entity_mgr);
    SimpleString fname("entities.blk");
    ecs::load_templates_blk(*g_entity_mgr, make_span_const(&fname, 1), trefs);
    g_entity_mgr->addTemplates(trefs);
  }

  if (dd_dir_exist(dgs_argv[startArgC]))
  {
    Tab<SimpleString> file_list;
    if (find_files_in_folder(file_list, dgs_argv[startArgC], "*.das", false, true, true))
    {
      for (auto &s : file_list)
      {
        if (!bind_dascript::load_das_script(s.c_str()))
        {
          printf("Can't compile <%s>\n", s.c_str());
          return 1;
        }
        printf("Successfully compiled <%s>\n", s.c_str());
      }
    }
    else
    {
      printf("Successfully compiled <%s>\n", dgs_argv[startArgC]);
      return 1;
    }
  }
  else
  {
    // check compilation
    if (bind_dascript::load_das_script(dgs_argv[startArgC]))
    {
      printf("Successfully compiled <%s>\n", dgs_argv[startArgC]);
      return 0;
    }
    else
    {
      printf("Error compiling <%s>\n", dgs_argv[startArgC]);
      return 1;
    }
  }

  if (df_get_real_name("scene.blk"))
    ecs::create_entities_blk(*g_entity_mgr, DataBlock("scene.blk"), NULL);
  printf("scene loaded entities\n");
  for (int i = 0; i < 100; ++i)
    g_entity_mgr->tick();
  g_entity_mgr->broadcastEventImmediate(EventStart());
  printf("EventStart sent\n");
  debug("EventStart");
  g_entity_mgr->broadcastEventImmediate(EventCPPTestEvent(2.f, 10, "test_string"));


  g_entity_mgr->tick();
  for (int i = 0; i < DEF_RUNS; ++i)
    g_entity_mgr->update(ecs::UpdateStageInfoAct(0.1, 0.1));

  printf("%d acts called\n", DEF_RUNS);
  for (int i = 0; i < 100; ++i)
    g_entity_mgr->tick();

  g_entity_mgr->broadcastEventImmediate(EventStart2());
  printf("EventStart2 sent\n");
  debug("EventStart2");

  printf("%d acts called\n", DEF_RUNS);
  for (int i = 0; i < 100; ++i)
    g_entity_mgr->tick();

  g_entity_mgr->broadcastEventImmediate(EventEnd());
  printf("EventEnd sent\n");

  G_ASSERT(get_test_value("EventStartTriggered") == 1);
  G_ASSERT(get_test_value("EventEndTriggered") == 1);
  {
    // the replicated half of tests/trackedInherited.das; das observes only change events
    const ecs::Template *t = g_entity_mgr->getTemplateDB().getTemplateByName("trackedInheritedChild");
    G_ASSERT(t && t->isReplicated(ECS_HASH("inherited_track_val").hash, g_entity_mgr->getTemplateDB().data()));
    G_UNUSED(t);
  }
  {
    // validateSets: a never declared _tracked name logerrs at instantiate; a tag
    // filtered component and a descendant declared one do not (see validate_sets.blk)
    // a component created by code exists only in DataComponents (like net synced ones)
    G_VERIFY(g_entity_mgr->createComponent(ECS_HASH("code_registered_comp"),
               g_entity_mgr->getComponentTypes().findType(ecs::ComponentTypeInfo<int>::type), dag::Span<ecs::component_t>(), nullptr,
               0) != ecs::INVALID_COMPONENT_INDEX);
    ecs::TemplateRefs vtrefs(*g_entity_mgr);
    swallow_component_block_err = true; // the failed_registered _component logerrs its unknown type by design
    G_VERIFY(ecs::load_templates_blk_file(*g_entity_mgr, "validate_sets.blk", vtrefs, &g_entity_mgr->getTemplateDB().info()));
    swallow_component_block_err = false;
    g_entity_mgr->addTemplates(vtrefs);
    // the dev suite must compile the validation it counts; rel builds skip the counts
#if DAGOR_DBGLEVEL > 0
    G_STATIC_ASSERT(DAECS_EXTENSIVE_CHECKS);
#endif
    // one instantiation per case; the counter resets per case, failures name the template
    auto expectErrors = [&](const char *tname, int expected) {
      G_UNUSED(expected);
      expected_errors_count = 0;
      count_expected_errors = true;
      G_VERIFY(g_entity_mgr->createEntitySync(tname) != ecs::INVALID_ENTITY_ID);
      count_expected_errors = false;
#if DAECS_EXTENSIVE_CHECKS && DAGOR_DBGLEVEL > 0
      G_ASSERTF(expected_errors_count == expected, "%s: %d != %d", tname, expected_errors_count, expected);
#endif
    };
    // exempt through the components name map only: entities.blk loads without info, so
    // noinfo_declared_comp is not in componentTags; pin that premise first
#if DAGOR_DBGLEVEL > 0
    G_ASSERT(g_entity_mgr->getTemplateDB().info().componentTags.count(ECS_HASH("noinfo_declared_comp").hash) == 0);
#endif
    expectErrors("tracksNoInfoDeclared", 0);
    expectErrors("tracksCodeRegistered", 0);
    // the instantiation consumes the parent's sets, so the parent's dangling name
    // reports here, once
    expectErrors("childOfDangling", 1);
    // the second child must not re-report the parent: validateSets memoizes per template
    expectErrors("childOfDangling2", 0);
    // the ancestor walk reports every set; a parent's bad replicated name too
    expectErrors("childOfDanglingRepl", 1);
    // the walk is transitive: a grandparent's bad name reports through the grandchild
    expectErrors("grandChildOfDangling", 1);
    // exempt through componentTags: _component blocks and template components are
    // recorded before the tag gate, a failed registration included
    expectErrors("tracksTagFilteredComponent", 0);
    expectErrors("tracksFailedRegistered", 0);
    expectErrors("tracksPlainComponent", 0);
    expectErrors("trackedTagFiltered", 0);
    expectErrors("baseTracksChildComp", 0);
    expectErrors("sameTemplTagSkipped", 0);
    {
      // tag skipped names (param and block form) must leave the sets at parse; a
      // loaded component's name must stay
      const ecs::Template *st = g_entity_mgr->getTemplateDB().getTemplateByName("sameTemplTagSkipped");
      G_ASSERT(st && st->trackedSet().count(ECS_HASH("tag_skipped_own").hash) == 0);
      G_ASSERT(st && st->trackedSet().count(ECS_HASH("tag_skipped_block").hash) == 0);
      G_ASSERT(st && st->trackedSet().count(ECS_HASH("loaded_block").hash) == 1);
      G_ASSERT(st && st->trackedSet().count(ECS_HASH("skip_own_plain").hash) == 1);
      G_ASSERT(st && st->ignoredSet().count(ECS_HASH("tag_skipped_own").hash) == 0);
      G_ASSERT(st && st->ignoredSet().count(ECS_HASH("skip_own_plain").hash) == 1);
      G_ASSERT(st && st->replicatedSet().count(ECS_HASH("tag_skipped_own").hash) == 0);
      G_ASSERT(st && st->replicatedSet().count(ECS_HASH("skip_own_plain").hash) == 1);
      // per template isolation: the parent's tag skips do not drop the child's list name
      const ecs::Template *ct = g_entity_mgr->getTemplateDB().getTemplateByName("trackedTagFiltered");
      G_ASSERT(ct && ct->trackedSet().count(ECS_HASH("tag_filtered_comp").hash) == 1);
      G_UNUSED(ct);
      G_UNUSED(st);
    }
    expectErrors("typoTracked", 1);
    expectErrors("typoReplicated", 1);
    expectErrors("typoIgnored", 1);
    // every bad name of a set is reported, not only the first
    expectErrors("typoTrackedTwo", 2);
    // late load order: the first instantiation reports the not yet declared name once;
    // after the declaring load a second create adds no report
    expectErrors("tracksLateDeclared", 1);
    {
      ecs::TemplateRefs ltrefs(*g_entity_mgr);
      G_VERIFY(ecs::load_templates_blk_file(*g_entity_mgr, "validate_sets_late.blk", ltrefs, &g_entity_mgr->getTemplateDB().info()));
      g_entity_mgr->addTemplates(ltrefs);
    }
    expectErrors("tracksLateDeclared", 0);
    // the net sync shape: the first instantiation runs before the component exists in
    // DataComponents and reports once; after creation a second create adds no report
    expectErrors("tracksLateCodeRegistered", 1);
    G_VERIFY(g_entity_mgr->createComponent(ECS_HASH("late_code_registered_comp"),
               g_entity_mgr->getComponentTypes().findType(ecs::ComponentTypeInfo<int>::type), dag::Span<ecs::component_t>(), nullptr,
               0) != ecs::INVALID_COMPONENT_INDEX);
    expectErrors("tracksLateCodeRegistered", 0);
    {
      // membership branch: a code created template is in no load registry, so only
      // hasComponent exempts its own tracked component
      ecs::ComponentsMap cmap;
      cmap[ECS_HASH("code_tracked_comp")] = ecs::ChildComponent(0);
      ecs::Template::component_set tracked;
      tracked.insert(ECS_HASH("code_tracked_comp").hash);
      g_entity_mgr->addTemplate(ecs::Template("codeTracked", eastl::move(cmap), eastl::move(tracked), ecs::Template::component_set(),
        ecs::Template::component_set(), false));
      // parent walk half: the child tracks the parent's code declared component; the
      // child instantiates first, so only the hierarchy walk can exempt the name
      ecs::ComponentsMap childCmap;
      ecs::Template::component_set childTracked;
      childTracked.insert(ECS_HASH("code_tracked_comp").hash);
      const char *codeParents[] = {"codeTracked"};
      dag::ConstSpan<const char *> pspan(codeParents, 1);
      G_VERIFY(
        g_entity_mgr->getTemplateDB().addTemplate(ecs::Template("codeTrackedChild", eastl::move(childCmap), eastl::move(childTracked),
                                                    ecs::Template::component_set(), ecs::Template::component_set(), false),
          &pspan) == ecs::TemplateDB::AR_OK);
      expectErrors("codeTrackedChild", 0);
      expectErrors("codeTracked", 0);
      // the inverse direction: a blk parent's name that only the code created child's
      // component map declares is legal through the instantiating template's view
      ecs::ComponentsMap invCmap;
      invCmap[ECS_HASH("code_child_declared_comp")] = ecs::ChildComponent(0);
      const char *invParents[] = {"parentTracksCodeChildComp"};
      dag::ConstSpan<const char *> invSpan(invParents, 1);
      G_VERIFY(g_entity_mgr->getTemplateDB().addTemplate(
                 ecs::Template("codeChildOfTrackingParent", eastl::move(invCmap), ecs::Template::component_set(),
                   ecs::Template::component_set(), ecs::Template::component_set(), false),
                 &invSpan) == ecs::TemplateDB::AR_OK);
      expectErrors("codeChildOfTrackingParent", 0);
    }
  }
  test_free_per_thread_query_data();
  int64_t reft = ref_time_ticks();
  g_entity_mgr->clear();
  debug("clear in %dus", get_time_usec(reft));
  printf("closed%s\n", had_errors ? " with unhandled errors (see debug file, run with -debug)" : "");
  return had_errors ? 1 : 0;
}

void require_project_specific_debugger_modules() {} // stub for dng debugger

int myMain(int startArgC)
{
  dgs_execute_quiet = true;
  g_entity_mgr.demandInit();
  // bind_dascript::set_das_root("../../../1stPartyLibs/daScript/"); // use current dir as root path
  bind_dascript::init_systems(NEED_DAS_AOT_COMPILE ? bind_dascript::AotMode::AOT : bind_dascript::AotMode::NO_AOT,
    bind_dascript::HotReload::ENABLED, bind_dascript::LoadDebugCode::YES,
    DAGOR_DBGLEVEL > 0 ? bind_dascript::LogAotErrors::YES : bind_dascript::LogAotErrors::NO, bind_dascript::DasSyntax::V1_5);
  NEED_MODULE(DagorFiles)
  NEED_MODULE(DasEcsUnitTest)
  das::Module::Initialize();

  int ret = myMain2(startArgC);

  bind_dascript::shutdown_systems();
  g_entity_mgr->getTemplateDB().clear();
  g_entity_mgr.demandDestroy();
  return ret;
}

extern void default_crt_init_kernel_lib();
extern void default_crt_init_core_lib();

extern "C" int main(int argc, char **argv)
{
  G_STATIC_ASSERT(ecs::ecs_data_alignment(alignof(Point3)) == 4);
  G_STATIC_ASSERT(ecs::ecs_data_alignment(alignof(void *)) == sizeof(size_t));
  default_crt_init_kernel_lib();
  default_crt_init_core_lib();
  symhlp_init_default();
  ::dgs_argc = argc;
  ::dgs_argv = argv;
  dgs_report_fatal_error = my_fatal_handler;
  debug_set_log_callback(&log_callback);
  int startArgC = 1;
  bool debugFile = false;
  bool verbose = false;
  while (dgs_argc - startArgC > 1)
  {
    if (strcmp(dgs_argv[startArgC], "-debug") == 0)
    {
      debugFile = true;
      startArgC++;
    }
    else if (strcmp(dgs_argv[startArgC], "-verbose") == 0)
    {
      verbose = true;
      startArgC++;
    }
    else
    {
      startArgC = dgs_argc;
      break;
    }
  }
  if (dgs_argc - startArgC < 1)
  {
    printf("Usage: [-debug] [-verbose] file_name|dir_name; file_name.das or dir_name/*.das will be checked\n");
    return 1;
  }
#if _TARGET_PC_WIN
  if (verbose)
    set_debug_console_handle((intptr_t)::GetStdHandle(STD_OUTPUT_HANDLE));
#endif
  start_classic_debug_system(debugFile ? "debug" : nullptr, false);
  dd_get_fname(""); //== pull in directoryService.obj
  char buf[512];
  eastl::string loc;
  if (strstr(dgs_argv[0], "/") || strstr(dgs_argv[0], "\\"))
    loc = eastl::string(dd_get_fname_location(buf, dgs_argv[0]));
  else
    loc = "../../";
  dd_set_named_mount_path("daslibEcs", (loc + "../../prog/gameLibs/das/ecs").c_str());
  dd_set_named_mount_path("daslib", (loc + "../../prog/1stPartyLibs/daScript/daslib").c_str());
  bind_dascript::set_das_root((loc + "../../prog/1stPartyLibs/daScript").c_str()); // use exe dir as root path
  printf("daScript+daECS unit test\n");
  debug_flush(true);
  dd_add_base_path("");
  DagorHwException::setHandler("main");
  int retcode = 0;

  DAGOR_TRY { retcode = myMain(startArgC); }
  DAGOR_CATCH(DagorException e)
  {
#ifdef DAGOR_EXCEPTIONS_ENABLED
    DagorHwException::reportException(e, true);
#endif
    printf("exception");
    return 1;
  }

  DagorHwException::cleanup();

  TIME_PROFILER_SHUTDOWN();
  return retcode;
}

#include <startup/dag_leakDetector.inc.cpp>

// extern bool dgs_execute_quiet;
// #include <windows.h>
// #include <startup/dag_leakDetector.inc.cpp>
// #include <startup/dag_mainCon.inc.cpp>
