//
// Dagor Engine 6.5 - Game Libraries
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <daScript/daScript.h>
#include <daNet/bitStream.h>
#include <util/dag_string.h>
#include <gameMath/quantization.h>
#include <debug/dag_debug.h>


//! Ends a data walk and reports why, instead of unwinding the das call the way
//! DataWalker::error does. For a walk over data this program did not produce - a peer's frame, a
//! file - the caller reads the outcome back through cancel() and decides.
inline void abort_data_walk(das::DataWalker &walker, das::LineInfo *at, const char *message)
{
  walker._cancel = true;
  if (walker.context)
    walker.context->to_err(at, message);
  else
    logerr("%s", message);
}

enum class BitStreamWalkerMode
{
  Read,
  Write,
};

template <BitStreamWalkerMode mode>
struct BitStreamWalker : das::DataWalker
{
  static inline uint32_t bytes2bits(uint32_t by) { return by << 3; }

  typedef std::conditional_t<mode == BitStreamWalkerMode::Write, danet::BitStream, const danet::BitStream> BitStreamType;
  BitStreamType *stream;
  das::LineInfo *debugInfo;
  bool compressNext = false;
  int packedUnitVectorNext = -1;
  float quantizedRange = 0.f; // @quantized32|64 = range: the float3 lies in [-range, range] per axis; 0 = raw
  int quantizedBits = 0;      // the wire size the annotation named

  BitStreamWalker(BitStreamType *stream, das::Context *context, das::LineInfo *debugInfo) : stream(stream), debugInfo(debugInfo)
  {
    this->context = context;
  }

  void abortWalk(const char *message) { abort_data_walk(*this, debugInfo, message); }

  bool canVisitHandle(char *, das::TypeInfo *) override { return false; }  // TODO
  bool canVisitVariant(char *, das::TypeInfo *) override { return false; } // TODO
  bool canVisitTable(char *, das::TypeInfo *) override { return false; }   // TODO
  bool canVisitTableData(das::TypeInfo *) override { return false; }
  bool canVisitPointer(das::TypeInfo *) override { return false; }
  bool canVisitLambda(das::TypeInfo *) override { return false; }
  bool canVisitIterator(das::TypeInfo *) override { return false; }

  bool isCompressible(das::TypeInfo *ti)
  {
    const das::Type type = ti->type == das::Type::tArray ? ti->firstType->type : ti->type;
    // all 2 and 4 byte int types
    return type == das::Type::tInt16 || type == das::Type::tUInt16 || type == das::Type::tInt || type == das::Type::tUInt ||
           type == das::Type::tInt2 || type == das::Type::tUInt2 || type == das::Type::tInt3 || type == das::Type::tUInt3 ||
           type == das::Type::tInt4 || type == das::Type::tUInt4 || type == das::Type::tRange || type == das::Type::tURange ||
           type == das::Type::tBitfield16 || type == das::Type::tBitfield || type == das::Type::tEnumeration16 ||
           type == das::Type::tEnumeration;
  }

  bool isPackableUnitVector(das::TypeInfo *ti)
  {
    const das::Type type = ti->type == das::Type::tArray ? ti->firstType->type : ti->type;
    return type == das::Type::tFloat3;
  }

  void beforeStructureField(char *, das::StructInfo *, char *, das::VarInfo *vi, bool) override
  {
    // TODO: allow @compressed for nested arrays, like array<array<int>>
    if (vi->annotation_argument_count != 0)
    {
      const bool typeIsCompressible = isCompressible(vi);
      const bool typeIsPackable = isPackableUnitVector(vi);
      const bool typeIsQuantized = typeIsPackable; // currently only float3 is supported
      for (uint32_t ai = 0; ai < vi->annotation_argument_count; ++ai)
      {
        const das::AnnotationArgumentInfo &ann = vi->annotation_arguments[ai];
        if (strcmp(ann.name, "packedUnitVector") == 0)
        {
          if (!typeIsPackable)
          {
            class String err(0, "packedUnitVector can only be used with float3 type (%s)", vi->name);
            abortWalk(err.c_str());
            return;
          }
          if (DAGOR_UNLIKELY(ann.type != das::Type::tInt))
          {
            class String err(0, "packedUnitVector value must be int (%s)", vi->name);
            abortWalk(err.c_str());
            return;
          }
          packedUnitVectorNext = ann.iValue;
          if (!(packedUnitVectorNext == 16 || packedUnitVectorNext == 24 || packedUnitVectorNext == 32))
          {
            class String err(0, "packedUnitVector value must be 16, 24 or 32 bits (%d)", packedUnitVectorNext);
            abortWalk(err.c_str());
            return;
          }
          continue;
        }
        if (strcmp(ann.name, "compressed") == 0)
        {
          if (!typeIsCompressible)
          {
            class String err(0, "compressed can only be used with signed and unsigned int types (%s)", vi->name);
            abortWalk(err.c_str());
            return;
          }
          if (DAGOR_UNLIKELY(ann.type != das::Type::tBool))
          {
            class String err(0, "compressed value must be bool (%s)", vi->name);
            abortWalk(err.c_str());
            return;
          }
          compressNext = ann.bValue;
          continue;
        }
        const int bits = strcmp(ann.name, "quantized32") == 0 ? 32 : strcmp(ann.name, "quantized64") == 0 ? 64 : 0;
        if (bits != 0)
        {
          if (!typeIsQuantized)
          {
            class String err(0, "%s can only be used with float3 type (%s)", ann.name, vi->name);
            abortWalk(err.c_str());
            return;
          }
          if (quantizedBits != 0)
          {
            class String err(0, "one quantized annotation per field (%s)", vi->name);
            abortWalk(err.c_str());
            return;
          }
          if (ann.type == das::Type::tFloat)
            quantizedRange = ann.fValue;
          else if (ann.type == das::Type::tInt)
            quantizedRange = float(ann.iValue);
          else
          {
            class String err(0,
              "%s value must be the range in world units, a number; a module constant needs a [net_command] or a "
              "[replicated] component, whose macro folds it (%s)",
              ann.name, vi->name);
            abortWalk(err.c_str());
            return;
          }
          if (!(quantizedRange > 0.f))
          {
            class String err(0, "%s range must be positive (%s)", ann.name, vi->name);
            abortWalk(err.c_str());
            return;
          }
          quantizedBits = bits;
          continue;
        }
      }
      if (packedUnitVectorNext >= 0 && quantizedBits != 0)
      {
        class String err(0, "packedUnitVector and quantized32/64 cannot share a field (%s)", vi->name);
        abortWalk(err.c_str());
        return;
      }
    }
  }
  void afterStructureField(char *, das::StructInfo *, char *, das::VarInfo *, bool) override
  {
    compressNext = false;
    packedUnitVectorNext = -1;
    quantizedRange = 0.f;
    quantizedBits = 0;
  }
  void beforeArray(das::Array *pa, das::TypeInfo *ti) override
  {
    if constexpr (mode == BitStreamWalkerMode::Write)
    {
      stream->Write((uint32_t)pa->size);
    }
    else
    {
      uint32_t size;
      if (!stream->Read(size))
      {
        class String err(0, "Failed to read array size %@bits (%@/%@)", bytes2bits(sizeof(size)), stream->GetReadOffset(),
          stream->GetNumberOfBitsUsed());
        abortWalk(err.c_str());
        return;
      }
      // the count comes off the stream, and array_resize throws on a negative or oversized one:
      // no array can hold more elements than the stream has bits left to describe them
      if (size > stream->GetNumberOfUnreadBits())
      {
        class String err(0, "array of %@ elements, with %@bits left to read", size, stream->GetNumberOfUnreadBits());
        abortWalk(err.c_str());
        return;
      }
      builtin_array_clear(*pa, context, nullptr);
      builtin_array_resize(*pa, (int)size, ti->firstType->size, context, nullptr);
    }
  }

  template <typename TT>
  __forceinline void process(TT &data)
  {
    if constexpr (mode == BitStreamWalkerMode::Write)
    {
      stream->Write(data);
    }
    else
    {
      if (!stream->Read(data))
      {
        class String err(0, "Failed to read %@bits (%@/%@)", bytes2bits(sizeof(data)), stream->GetReadOffset(),
          stream->GetNumberOfBitsUsed());
        abortWalk(err.c_str());
        return;
      }
    }
  }
  template <typename TT>
  __forceinline void processCompressible(TT &data)
  {
    if constexpr (mode == BitStreamWalkerMode::Write)
    {
      compressNext ? stream->WriteCompressed(data) : stream->Write(data);
    }
    else
    {
      const bool ok = compressNext ? stream->ReadCompressed(data) : stream->Read(data);
      if (!ok)
      {
        class String err(0, "Failed to read %@bits (%@/%@)", bytes2bits(sizeof(data)), stream->GetReadOffset(),
          stream->GetNumberOfBitsUsed());
        abortWalk(err.c_str());
        return;
      }
    }
  }
  template <int dim, typename TT>
  __forceinline void processCompressibleVector(TT &data)
  {
    processCompressible(data.x);
    processCompressible(data.y);
    if constexpr (dim >= 3)
    {
      processCompressible(data.z);
    }
    if constexpr (dim == 4)
    {
      processCompressible(data.w);
    }
  }

  // x | z << XBits | y << (XBits + ZBits), each axis packed as value / quantizedRange in [-1, 1];
  // a layout that leaves the top bit free (the 64-bit one) flags a clamped input there.
  template <typename RT, size_t XBits, size_t YBits, size_t ZBits>
  void processQuantized(Point3 &pos)
  {
    static constexpr RT CLAMPED_BIT = (XBits + YBits + ZBits < sizeof(RT) * 8) ? (RT(1) << (sizeof(RT) * 8 - 1)) : RT(0);
    if constexpr (mode == BitStreamWalkerMode::Write)
    {
      bool clamped = false;
      RT q = RT(gamemath::pack_scalar_signed<RT, float>(pos.x, XBits, quantizedRange, &clamped));
      q |= RT(gamemath::pack_scalar_signed<RT, float>(pos.z, ZBits, quantizedRange, &clamped)) << XBits;
      q |= RT(gamemath::pack_scalar_signed<RT, float>(pos.y, YBits, quantizedRange, &clamped)) << (XBits + ZBits);
      if (clamped)
        q |= CLAMPED_BIT;
      stream->Write(q);
    }
    else
    {
      RT q;
      if (!stream->Read(q))
      {
        class String err(0, "Failed to read %@bits (%@/%@)", bytes2bits(sizeof(q)), stream->GetReadOffset(),
          stream->GetNumberOfBitsUsed());
        abortWalk(err.c_str());
        return;
      }
      q &= ~CLAMPED_BIT;
      pos.x = gamemath::unpack_scalar_signed<RT, float>(q & FVAL_BITS_MASK(RT, XBits), XBits, quantizedRange);
      pos.z = gamemath::unpack_scalar_signed<RT, float>((q >> XBits) & FVAL_BITS_MASK(RT, ZBits), ZBits, quantizedRange);
      pos.y = gamemath::unpack_scalar_signed<RT, float>((q >> (XBits + ZBits)) & FVAL_BITS_MASK(RT, YBits), YBits, quantizedRange);
    }
  }

  template <int dim, typename TT>
  void processPackableVector(TT &data)
  {
    if (quantizedBits != 0)
    {
      G_ASSERTF(dim == 3, "quantized32/64 can only be 3D");
      if (quantizedBits == 32)
        processQuantized<uint32_t, 12, 8, 12>(reinterpret_cast<Point3 &>(data));
      else
        processQuantized<uint64_t, 22, 19, 22>(reinterpret_cast<Point3 &>(data));
      return;
    }
    if (packedUnitVectorNext >= 0)
    {
      G_ASSERTF(dim == 3, "packedUnitVector can only be 3D");
      if constexpr (mode == BitStreamWalkerMode::Write)
      {
        if (packedUnitVectorNext <= 16)
        {
          uint16_t val = gamemath::pack_unit_vec<uint16_t>(reinterpret_cast<Point3 &>(data), packedUnitVectorNext);
          stream->Write(val);
        }
        else
        {
          uint32_t val = gamemath::pack_unit_vec<uint32_t>(reinterpret_cast<Point3 &>(data), packedUnitVectorNext);
          stream->Write(val);
        }
        return;
      }
      else
      {
        if (packedUnitVectorNext <= 16)
        {
          uint16_t val;
          if (!stream->Read(val))
          {
            class String err(0, "Failed to read %@bits (%@/%@)", bytes2bits(sizeof(val)), stream->GetReadOffset(),
              stream->GetNumberOfBitsUsed());
            abortWalk(err.c_str());
            return;
          }
          reinterpret_cast<Point3 &>(data) = gamemath::unpack_unit_vec<uint16_t>(val, packedUnitVectorNext);
        }
        else
        {
          uint32_t val;
          if (!stream->Read(val))
          {
            class String err(0, "Failed to read %@bits (%@/%@)", bytes2bits(sizeof(val)), stream->GetReadOffset(),
              stream->GetNumberOfBitsUsed());
            abortWalk(err.c_str());
            return;
          }
          reinterpret_cast<Point3 &>(data) = gamemath::unpack_unit_vec<uint32_t>(val, packedUnitVectorNext);
        }
        return;
      }
    }
    process(data);
  }

  void Bool(bool &data) override { process(data); }
  void Int8(int8_t &data) override { process(data); }
  void UInt8(uint8_t &data) override { process(data); }
  void Int16(int16_t &data) override { processCompressible(data); }
  void UInt16(uint16_t &data) override { processCompressible(data); }
  void Int64(int64_t &data) override { process(data); }
  void UInt64(uint64_t &data) override { process(data); }
  void String(char *&data) override
  {
    if constexpr (mode == BitStreamWalkerMode::Write)
    {
      stream->Write(data);
    }
    else
    {
      eastl::string str;
      if (!stream->Read(str))
      {
        class String err(0, "Failed to read string %@/%@", stream->GetReadOffset(), stream->GetNumberOfBitsUsed());
        abortWalk(err.c_str());
        return;
      }
      data = context->allocateString(str.c_str(), str.size(), debugInfo);
    }
  }
  void Double(double &data) override { process(data); }
  void Float(float &data) override { process(data); }
  void Int(int32_t &data) override { processCompressible(data); }
  void UInt(uint32_t &data) override { processCompressible(data); }
  void Bitfield(uint32_t &data, das::TypeInfo *) override { processCompressible(data); }
  void Bitfield8(uint8_t &data, das::TypeInfo *) override { process(data); }
  void Bitfield16(uint16_t &data, das::TypeInfo *) override { processCompressible(data); }
  void Bitfield64(uint64_t &data, das::TypeInfo *) override { process(data); }
  void Int2(das::int2 &data) override { processCompressibleVector<2>(data); }
  void Int3(das::int3 &data) override { processCompressibleVector<3>(data); }
  void Int4(das::int4 &data) override { processCompressibleVector<4>(data); }
  void UInt2(das::uint2 &data) override { processCompressibleVector<2>(data); }
  void UInt3(das::uint3 &data) override { processCompressibleVector<3>(data); }
  void UInt4(das::uint4 &data) override { processCompressibleVector<4>(data); }
  void Float2(das::float2 &data) override { process(data); }
  void Float3(das::float3 &data) override { processPackableVector<3>(data); }
  void Float4(das::float4 &data) override { process(data); }
  void Range(das::range &data) override { processCompressibleVector<2>(data); }
  void URange(das::urange &data) override { processCompressibleVector<2>(data); }
  void Range64(das::range64 &data) override { process(data); }
  void URange64(das::urange64 &data) override { process(data); }
  void WalkEnumeration(int32_t &data, das::EnumInfo *) override { processCompressible(data); }
  void WalkEnumeration8(int8_t &data, das::EnumInfo *) override { process(data); }
  void WalkEnumeration16(int16_t &data, das::EnumInfo *) override { processCompressible(data); }
  void WalkEnumeration64(int64_t &data, das::EnumInfo *) override { process(data); }
};