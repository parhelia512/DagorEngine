// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <stdio.h>
#include <windows.h>
#include <shlobj.h>
#include <maxversion.h>
#include <string>
#include <format>
#include <fstream>
#include <filesystem>
#include "debug.h"
#include "common.h"

namespace fs = std::filesystem;

static std::ofstream debugfile;
static bool debug_file_available = true;

void debug_formatted(std::string_view fmt, std::format_args args)
{
  try
  {
    debug_out(std::vformat(fmt, args));
  }
  catch (const std::format_error &)
  {
    debug_out(fmt);
  }
}

void debug_formatted(std::wstring_view fmt, std::wformat_args args)
{
  try
  {
    debug_out(std::vformat(fmt, args));
  }
  catch (const std::format_error &)
  {
    debug_out(fmt);
  }
}

void debug_out(std::string_view text) { debug_out(strToWide(text)); }

void debug_out(std::wstring_view text)
{
  std::wstring line(text);
  line += L'\n';

  OutputDebugStringW(line.c_str());
  if (!debug_file_available)
    return;

  if (!debugfile.is_open())
  {
    fs::path debugfile_name =
      std::format(L"dagor2_plugin_max{}.{}.{}.log", MAX_PRODUCT_VERSION_MAJOR, MAX_PRODUCT_VERSION_MINOR, MAX_PRODUCT_VERSION_POINT);

    TCHAR folder[MAX_PATH];
    fs::path debugfile_path;
    if (SUCCEEDED(SHGetFolderPath(NULL, (CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE), NULL, 0, folder)))
      debugfile_path = fs::path(folder) / debugfile_name;
    else
      debugfile_path = fs::path(L"d:\\") / debugfile_name;

    debugfile.open(debugfile_path);
    if (!debugfile)
    {
      debug_file_available = false;
      OutputDebugStringW(std::format(L"failed to create debug file: {}\n", debugfile_path.c_str()).c_str());
      return;
    }
  }

  debugfile << wideToStr(line);

  debugfile.flush();
}
