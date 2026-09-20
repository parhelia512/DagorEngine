// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <driver.h>
#include <constants.h>

#include <util/dag_globDef.h>

#include <EASTL/algorithm.h>
#include <EASTL/string_view.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


namespace drv3d_dx12::debug
{

#if DX12_NAME_OBJECTS || DX12_HAVE_GET_OBJECT_NAME
struct ObjectName
{
  char storage[MAX_OBJECT_NAME_LENGTH];
  uint32_t length;

  constexpr bool empty() const { return length == 0; }
  constexpr const char *c_str() const { return storage; }
  constexpr operator eastl::string_view() const { return {storage, length}; }
};

#else

struct ObjectName
{
  constexpr bool empty() const { return true; }
  constexpr const char *c_str() const { return ""; }
  constexpr operator eastl::string_view() const { return {}; }
};

#endif

#if DX12_NAME_OBJECTS

PRINTF_LIKE inline ObjectName format_object_name(const char *format, ...)
{
  ObjectName result;
  va_list args;
  va_start(args, format);
  const int written = _vsnprintf_s(result.storage, _TRUNCATE, format, args);
  va_end(args);
  // _TRUNCATE reports -1 for a cut name, so measure the terminated buffer instead.
  result.length =
    written >= 0 ? static_cast<uint32_t>(written) : static_cast<uint32_t>(strnlen(result.storage, MAX_OBJECT_NAME_LENGTH));
  return result;
}

uint64_t next_pool_object_serial();

// '#' plus the 20 digits of a uint64.
inline constexpr size_t max_object_serial_length = 21;

inline ObjectName make_object_name(eastl::string_view prefix, uint64_t serial)
{
  const int prefixLength = min<int>(prefix.size(), MAX_OBJECT_NAME_LENGTH - max_object_serial_length - 1);
  return format_object_name("%.*s#%llu", prefixLength, prefix.data(), serial);
}

inline ObjectName make_pool_object_name(eastl::string_view prefix) { return make_object_name(prefix, next_pool_object_serial()); }

void name_resource(ID3D12Resource *resource, eastl::string_view name);
void name_object(ID3D12Object *object, eastl::string_view name);

inline void name_resource_or_pool(ID3D12Resource *resource, const char *name, eastl::string_view pool_kind)
{
  if (name && *name)
    name_resource(resource, name);
  else
    name_resource(resource, make_pool_object_name(pool_kind));
}

#else

template <class... Args>
constexpr ObjectName format_object_name(const Args &...)
{
  return {};
}

constexpr uint64_t next_pool_object_serial() { return 0; }

constexpr ObjectName make_object_name(auto &&, uint64_t) { return {}; }
constexpr ObjectName make_pool_object_name(auto &&) { return {}; }

constexpr void name_resource(ID3D12Resource *, auto &&) {}
constexpr void name_object(ID3D12Object *, auto &&) {}
constexpr void name_resource_or_pool(ID3D12Resource *, const char *, auto &&) {}

#endif

#if DX12_HAVE_GET_OBJECT_NAME

// NOTE: This is intended for debug only, this is possibly slow, so use with care!
ObjectName get_object_name(ID3D12Object *object);

#else

constexpr ObjectName get_object_name(ID3D12Object *) { return {}; }

#endif

inline void set_object_name(ID3D12Object *object, eastl::string_view name)
{
  // lazy way of converting to wchar, this assumes name is not multi byte encoding
  wchar_t wcharName[MAX_OBJECT_NAME_LENGTH];
  *eastl::copy(name.data(), min(name.data() + name.size(), name.data() + MAX_OBJECT_NAME_LENGTH - 1), wcharName) = L'\0';
  object->SetName(wcharName);
}

inline void set_object_name(ID3D12Object *object, eastl::wstring_view name)
{
  // technically not correct, when name is a sub-string...
  object->SetName(name.data());
}

} // namespace drv3d_dx12::debug
