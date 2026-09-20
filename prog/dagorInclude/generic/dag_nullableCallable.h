//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

#include <cstddef>
#include <EASTL/type_traits.h>


namespace dag
{
namespace detail
{

// callables that hold their own empty state: eastl::function, function pointers, ...
template <typename T, typename = void>
struct IsNullableCallable : public eastl::false_type
{};

template <typename T>
struct IsNullableCallable<T,
  eastl::enable_if_t<eastl::is_constructible<T, std::nullptr_t>::value && eastl::is_constructible<bool, const T &>::value>>
  : public eastl::true_type
{};

template <typename T>
constexpr bool is_nullable_callable_v = IsNullableCallable<T>::value;

} // namespace detail
} // namespace dag
