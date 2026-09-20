//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

// The emulated-backend read window is anchored at the last PS T register and fills top down, so a read's
// register does not move when another read is added after it. Every side supplies its own register count.
inline constexpr int subpass_read_register(int max_t_registers, int read_index) { return max_t_registers - 1 - read_index; }
inline constexpr int subpass_read_window_base(int max_t_registers, int read_count) { return max_t_registers - read_count; }
inline constexpr bool subpass_read_fits_window(int max_t_registers, int read_index)
{
  return read_index >= 0 && read_index < max_t_registers;
}
