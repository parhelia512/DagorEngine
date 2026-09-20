// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <EASTL/string.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// `decimals` places with trailing zeros trimmed, the way the JS editor writes a number.
inline void append_rounded(eastl::string &out, double v, int decimals)
{
  char buf[64];
  snprintf(buf, sizeof(buf), "%.*f", decimals, v);
  char *dot = strchr(buf, '.');
  if (dot)
  {
    char *last = buf + strlen(buf) - 1;
    while (last > dot && *last == '0')
    {
      *last-- = 0;
    }
    if (last == dot)
    {
      *last = 0;
    }
  }
  out += (strcmp(buf, "-0") == 0) ? "0" : buf;
}

// The shortest text that still parses back to the same float. Plain %.9g round-trips too, but pads
// clean values (0.333 -> 0.333000004) into every .blk diff.
inline void append_shortest(eastl::string &out, float v)
{
  char buf[32];
  for (int precision = 6; precision <= 9; ++precision)
  {
    snprintf(buf, sizeof(buf), "%.*g", precision, v);
    if (strtof(buf, nullptr) == v)
    {
      break;
    }
  }
  out += buf;
}
