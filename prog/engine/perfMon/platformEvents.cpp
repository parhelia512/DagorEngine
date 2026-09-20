// Copyright (C) Gaijin Games KFT.  All rights reserved.

// #include <perfMon/dag_pix.h>

#if _TARGET_PC_WIN || _TARGET_XBOX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
#if USE_PIX
#include <pix3.h>
#endif
#if USE_NVTX
#include <nvtx3/nvtx3.hpp>
#endif
#include <perfMon/dag_pixTimingCapture.h>


void BEGIN_CPU_EVENT([[maybe_unused]] const char *name)
{
#if USE_PIX
  PIXBeginEvent(0, name);
#endif

#if USE_NVTX
  nvtxRangePushA(name);
#endif
}

void END_CPU_EVENT()
{
#if USE_PIX
  PIXEndEvent();
#endif
#if USE_NVTX
  nvtxRangePop();
#endif
}

#if USE_PIX

static bool timing_capture_active = false;

int pix_begin_timing_capture(const char *file_name, int tooling_memory_mb, int cpu_samples_per_sec)
{
  if (timing_capture_active)
    return HRESULT_FROM_WIN32(ERROR_BUSY);

#if _TARGET_PC_WIN
  // not PIXLoadLatestWinPixTimingCapturerLibrary: it finds the install through SHGetKnownFolderPath
  // and would drag ole32 and shell32 into the kernel dll. PIX injects the module on launch/attach.
  if (!GetModuleHandleW(L"WinPixTimingCapturer.dll"))
    return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
#endif

  wchar_t path[1024];
  if (MultiByteToWideChar(CP_UTF8, 0, file_name, -1, path, int(sizeof(path) / sizeof(path[0]))) <= 0)
    return HRESULT_FROM_WIN32(GetLastError());

  PIXCaptureParameters params = {};
  params.TimingCaptureParameters.FileName = path;
  params.TimingCaptureParameters.MaximumToolingMemorySizeMb = tooling_memory_mb;
  params.TimingCaptureParameters.CaptureCallstacks = TRUE;
  params.TimingCaptureParameters.CaptureCpuSamples = TRUE;
  params.TimingCaptureParameters.CpuSamplesPerSecond = cpu_samples_per_sec;
  params.TimingCaptureParameters.CaptureGpuTiming = TRUE;

  const HRESULT hr = PIXBeginCapture(PIX_CAPTURE_TIMING, &params);
  if (FAILED(hr))
    return hr;

  timing_capture_active = true;
  return 0;
}

bool pix_end_timing_capture()
{
  if (!timing_capture_active)
    return true;

  HRESULT hr = S_OK;
#if _TARGET_PC_WIN
  // not PIXEndCapture: it ends whichever capturer it resolves first (pix3_win.h), and the DX12 driver
  // loads the GPU one under its PIX profiles, so ask the timing capturer itself to stop
  typedef HRESULT(WINAPI * EndTimingCaptureFn)(BOOL);
  if (HMODULE lib = GetModuleHandleW(L"WinPixTimingCapturer.dll"))
    if (void *fn = (void *)GetProcAddress(lib, "EndProgrammaticTimingCapture")) // via void *, FARPROC casts warn (C4191)
      hr = reinterpret_cast<EndTimingCaptureFn>(fn)(FALSE);
#else
  hr = PIXEndCapture(FALSE);
#endif

  if (hr == E_PENDING)
    return false;

  timing_capture_active = false;
  return true;
}

#else

int pix_begin_timing_capture(const char *, int, int) { return PIX_TIMING_CAPTURE_UNSUPPORTED; }
bool pix_end_timing_capture() { return true; }

#endif
