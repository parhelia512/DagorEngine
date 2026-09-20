//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

// Programmatic PIX timing (CPU) capture. Real only where USE_PIX is compiled in (perfMon/jamfile),
// stubs elsewhere, so callers need no guards of their own.

// returned by the stub, so a build without PIX can be told apart from a PIX that said no
static constexpr int PIX_TIMING_CAPTURE_UNSUPPORTED = -1;

// file_name is utf8, at most 1023 characters, and reaches PIX as is: a bare name lands wherever PIX
// resolves it, on windows the process directory. Returns 0, or the HRESULT the capture was refused
// with - reporting is left to the caller, since this lives in the kernel dll and must not pull the log in.
int pix_begin_timing_capture(const char *file_name, int tooling_memory_mb, int cpu_samples_per_sec);

// PIX writes the capture out after the ask, so this answers false while that is still going. Keep
// calling until it is true: no other capture may start, and the process may not exit, before then.
bool pix_end_timing_capture();
