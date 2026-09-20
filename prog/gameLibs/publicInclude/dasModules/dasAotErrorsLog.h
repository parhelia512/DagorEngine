//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <daScript/daScript.h>
#include <osApiWrappers/dag_files.h>
#include <osApiWrappers/dag_critSec.h>
#include <debug/dag_logSys.h>
#include <util/dag_string.h>
#include <perfMon/dag_cpuFreq.h>

inline constexpr const char *das_aot_errors_file_name = "das_aot_errors.log";

// One AOT link error report is a whole syntax tree, which drowns the main log, so the reports
// go to their own file; when that file cannot be written they fall back to the main log. The
// file restarts on a process's first write and once it outgrows a cap, so it stays bounded.
inline void das_log_aot_link_errors(const char *fname, const das::vector<das::Error> &errors, bool log_reports)
{
  if (errors.empty())
    return;
  bool inFile = false;
  const char *logDir = get_log_directory();
  if (log_reports && *logDir)
  {
    // the report text builds outside the lock: formatting a syntax tree per error is the slow part
    const int t = get_time_msec();
    String text(0, "\n==== %s [%d.%02d] ====\n", fname, t / 1000, (t % 1000) / 10);
    for (auto &err : errors)
      text += das::reportError(err.at, err.what, err.extra, err.fixme, err.cerr).c_str();

    constexpr int maxFileSize = 8 << 20;
    static WinCritSec cs; // scripts load on several threads, and the first writer truncates
    static bool started = false;
    WinAutoLock lock(cs);
    // the explicit separator covers mobile log dirs, which come without a trailing slash
    String path(0, "%s/%s", logDir, das_aot_errors_file_name);
    file_ptr_t f = df_open(path, DF_WRITE | DF_CREATE | (started ? DF_APPEND : 0));
    if (f && df_length(f) > maxFileSize)
    {
      df_close(f);
      f = df_open(path, DF_WRITE | DF_CREATE);
    }
    if (f)
    {
      started = true;
      inFile = df_write(f, text.str(), text.length()) == text.length();
      inFile = (df_close(f) == 0) && inFile;
      if (!inFile)
      {
        // a failed write truncates the whole file, so the next block starts clean; the fallback
        // keeps this block's reports in the main log
        if (file_ptr_t ft = df_open(path, DF_WRITE | DF_CREATE))
          df_close(ft);
      }
    }
  }
  if (inFile)
    logwarn("daScript: failed to link cpp aot <%s>: %d errors, see %s", fname, (int)errors.size(), das_aot_errors_file_name);
  else
  {
    logwarn("daScript: failed to link cpp aot <%s>: %d errors", fname, (int)errors.size());
    if (log_reports)
      for (auto &err : errors)
        logwarn("%s", das::reportError(err.at, err.what, err.extra, err.fixme, err.cerr).c_str());
  }
}
