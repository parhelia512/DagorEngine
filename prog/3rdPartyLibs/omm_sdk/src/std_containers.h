/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include <functional>
#include <string>

#include <EASTL/fixed_vector.h>
#include <EASTL/hash_map.h>
#include <EASTL/vector_map.h>
#include <EASTL/vector_set.h>

#include "omm.h"

#include "std_allocator.h"

namespace omm
{
    // Local modification. The GPU baker builds its dispatch chain from scratch on every bake, with
    // several small containers per pass, so the per-bake cost is the allocator, not the work. The
    // containers below keep their small case in inline storage and spill to the heap past
    // kInlineElements. A spilled one still moves element-wise, thus a vector of these pays a copy on
    // each growth; only the CPU baker's near-duplicate detection does that, and no caller enables it.
    static constexpr size_t kInlineElements = 16;

    // The SDK memory interface behind the EASTL allocator contract. Converts from the allocator
    // forms the SDK hands to its containers, so the container constructors take them as before.
    class EastlAllocator
    {
    public:
        EastlAllocator(const char* = nullptr) { CheckAndSetDefaultAllocator(m_Interface); }
        EastlAllocator(const StdMemoryAllocatorInterface& memoryAllocator) : m_Interface(memoryAllocator)
        { CheckAndSetDefaultAllocator(m_Interface); }
        template<class U>
        EastlAllocator(const StdAllocator<U>& allocator) : EastlAllocator(allocator.GetInterface()) {}

        void* allocate(size_t n, int = 0) { return m_Interface.Allocate(m_Interface.UserArg, n, kDefaultAlignment); }
        void* allocate(size_t n, size_t alignment, size_t, int = 0) { return m_Interface.Allocate(m_Interface.UserArg, n, alignment); }
        void deallocate(void* p, size_t) { m_Interface.Free(m_Interface.UserArg, p); }

        const char* get_name() const { return "omm"; }
        void set_name(const char*) {}

        bool operator==(const EastlAllocator& o) const { return m_Interface == o.m_Interface; }
        bool operator!=(const EastlAllocator& o) const { return !(*this == o); }

    private:
        static constexpr size_t kDefaultAlignment = 16;
        StdMemoryAllocatorInterface m_Interface;
    };

    template<class TKey, class TVal>
    using hash_map = eastl::hash_map<TKey, TVal, eastl::hash<TKey>, eastl::equal_to<TKey>, EastlAllocator>;

    template<class TVal>
    using vector = eastl::fixed_vector<TVal, kInlineElements, true, EastlAllocator>;

    // std::less, not eastl::less: the dispatch chain is built from the iteration order of these maps.
    template<class TKey, class TVal>
    using map = eastl::vector_map<TKey, TVal, std::less<TKey>, EastlAllocator, vector<eastl::pair<TKey, TVal>>>;

    template<class TKey>
    using set = eastl::vector_set<TKey, std::less<TKey>, EastlAllocator, vector<TKey>>;

    using string = std::basic_string<char, std::char_traits<char>, StdAllocator<char>>;
}
