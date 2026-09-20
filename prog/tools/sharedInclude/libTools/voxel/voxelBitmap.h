// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <util/dag_bitArray.h>
#include <EASTL/deque.h>
#include <libTools/util/makeBindump.h>


struct VoxelBitmap
{
  Bitarray map;
  IPoint3 size = IPoint3::ZERO;

  VoxelBitmap() : map(tmpmem) {}
  VoxelBitmap(const IPoint3 &s) : map(tmpmem), size(s) { map.resize(s.x * s.y * s.z); }

  void reset() { map.reset(); }

  inline bool outside(int x, int y, int z) const
  {
    return uint32_t(x) >= uint32_t(size.x) or uint32_t(y) >= uint32_t(size.y) or uint32_t(z) >= uint32_t(size.z);
  }

  inline uint32_t bitIndex(int x, int y, int z) const { return (y * size.z + z) * size.x + x; }

  bool get(int x, int y, int z) const
  {
    if (outside(x, y, z))
      return false;
    return map[bitIndex(x, y, z)];
  }
  inline bool get(const IPoint3 &p) const { return get(P3D(p)); }

  void set(int x, int y, int z, bool v = true)
  {
    if (outside(x, y, z))
      return;
    map.set(bitIndex(x, y, z), v);
  }
  inline void set(const IPoint3 &p, bool v = true) { set(P3D(p), v); }

  void fill(int x0, int y0, int z0, int x1, int y1, int z1, bool v)
  {
    if (x0 > x1)
      eastl::swap(x0, x1);
    if (y0 > y1)
      eastl::swap(y0, y1);
    if (z0 > z1)
      eastl::swap(z0, z1);

    x0 = max(x0, 0);
    x1 = min(x1, size.x - 1);
    y0 = max(y0, 0);
    y1 = min(y1, size.y - 1);
    z0 = max(z0, 0);
    z1 = min(z1, size.z - 1);

    for (int y = y0; y <= y1; y++)
      for (int z = z0; z <= z1; z++)
      {
        uint32_t b0 = bitIndex(x0, y, z);
        uint32_t b1 = bitIndex(x1, y, z);
        map.fill(b0, b1, v);
      }
  }
  inline void fill(const IPoint3 &b0, const IPoint3 &b1, bool v) { fill(P3D(b0), P3D(b1), v); }

  void floodClear(int seed_x, int seed_y, int seed_z)
  {
    if (outside(seed_x, seed_y, seed_z))
      return;

    uint32_t numFilled = 0;
    eastl::deque<IPoint3> queue;
    queue.emplace_back(seed_x, seed_y, seed_z);

    while (!queue.empty())
    {
      auto [x, y, z] = queue.front();
      queue.pop_front();
      if (!map[bitIndex(x, y, z)])
        continue;

      int x0 = x;
      for (; x0 > 0 and map[bitIndex(x0 - 1, y, z)]; x0--) {}
      int x1 = x;
      for (; x1 + 1 < size.x and map[bitIndex(x1 + 1, y, z)]; x1++) {}

      map.fill(bitIndex(x0, y, z), bitIndex(x1, y, z), false);
      numFilled += x1 - x0 + 1;

      if (y > 0)
      {
        y--;
        for (x = x0; x <= x1; x++)
          if (map[bitIndex(x, y, z)])
          {
            queue.emplace_back(x, y, z);
            for (x++; x <= x1 and map[bitIndex(x, y, z)]; x++) {}
          }
        y++;
      }

      if (y + 1 < size.y)
      {
        y++;
        for (x = x0; x <= x1; x++)
          if (map[bitIndex(x, y, z)])
          {
            queue.emplace_back(x, y, z);
            for (x++; x <= x1 and map[bitIndex(x, y, z)]; x++) {}
          }
        y--;
      }

      if (z > 0)
      {
        z--;
        for (x = x0; x <= x1; x++)
          if (map[bitIndex(x, y, z)])
          {
            queue.emplace_back(x, y, z);
            for (x++; x <= x1 and map[bitIndex(x, y, z)]; x++) {}
          }
        z++;
      }

      if (z + 1 < size.z)
      {
        z++;
        for (x = x0; x <= x1; x++)
          if (map[bitIndex(x, y, z)])
          {
            queue.emplace_back(x, y, z);
            for (x++; x <= x1 and map[bitIndex(x, y, z)]; x++) {}
          }
        z--;
      }
    }
  }

  void clearOutside()
  {
    // yx
    for (int z : {0, size.z - 1})
      for (int y = 0; y < size.y; y++)
        for (int x = 0; x < size.x;)
        {
          bool v = map[bitIndex(x, y, z)];
          int x0 = x;
          if (v)
          {
            for (x++; x < size.x and map[bitIndex(x, y, z)]; x++) {}
            floodClear(x0, y, z);
          }
          for (x++; x < size.x and !map[bitIndex(x, y, z)]; x++) {}
        }
    // zx
    for (int y : {0, size.y - 1})
      for (int z = 0; z < size.z; z++)
        for (int x = 0; x < size.x;)
        {
          bool v = map[bitIndex(x, y, z)];
          int x0 = x;
          if (v)
          {
            for (x++; x < size.x and map[bitIndex(x, y, z)]; x++) {}
            floodClear(x0, y, z);
          }
          for (x++; x < size.x and !map[bitIndex(x, y, z)]; x++) {}
        }
    // yz
    for (int x : {0, size.x - 1})
      for (int y = 0; y < size.y; y++)
        for (int z = 0; z < size.z;)
        {
          bool v = map[bitIndex(x, y, z)];
          int z0 = z;
          if (v)
          {
            for (z++; z < size.z and map[bitIndex(x, y, z)]; z++) {}
            floodClear(x, y, z0);
          }
          for (z++; z < size.z and !map[bitIndex(x, y, z)]; z++) {}
        }
  }

  void save(mkbindump::BinDumpSaveCB &cwr) { cwr.writeTabData32e(make_span_const(map.getPtr(), map.dataSize() >> 2)); }
};
