// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <format>
#include <string_view>

void debug_out(std::string_view text);
void debug_out(std::wstring_view text);

void debug_formatted(std::string_view fmt, std::format_args args);
void debug_formatted(std::wstring_view fmt, std::wformat_args args);

#if __cpp_lib_format >= 202207L

template <typename... Args>
void debug(std::format_string<Args...> fmt, Args &&...args)
{
  debug_formatted(fmt.get(), std::make_format_args(args...));
}

template <typename... Args>
void debug(std::wformat_string<Args...> fmt, Args &&...args)
{
  debug_formatted(fmt.get(), std::make_wformat_args(args...));
}

#else

template <typename... Args>
void debug(std::string_view fmt, Args &&...args)
{
  debug_formatted(fmt, std::make_format_args(args...));
}

template <typename... Args>
void debug(std::wstring_view fmt, Args &&...args)
{
  debug_formatted(fmt, std::make_wformat_args(args...));
}

#endif
