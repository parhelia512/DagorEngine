#ifndef LIGHTS_MANAGER_INCLUDED
#define LIGHTS_MANAGER_INCLUDED 1

#define INVALID_SHADOW_VOLUME_ID 0xFFFF

#ifdef __cplusplus
  #define LIGTHS_MANAGER_UINT_32 uint32_t
#else
  #define LIGTHS_MANAGER_UINT_32 uint
#endif
struct ManagedLight
{
  LIGTHS_MANAGER_UINT_32 shadowId_mask;
};

inline LIGTHS_MANAGER_UINT_32 encode_managed_light(LIGTHS_MANAGER_UINT_32 shadow_id, LIGTHS_MANAGER_UINT_32 mask)
{
  return (shadow_id << 16) | mask;
}

inline LIGTHS_MANAGER_UINT_32 decode_managed_shadow_id(LIGTHS_MANAGER_UINT_32 encoded_value)
{
  return (encoded_value >> 16);
}

inline LIGTHS_MANAGER_UINT_32 decode_managed_mask(LIGTHS_MANAGER_UINT_32 encoded_value)
{
  return (encoded_value & 0xFFFF);
}

#undef LIGTHS_MANAGER_UINT_32

#endif
