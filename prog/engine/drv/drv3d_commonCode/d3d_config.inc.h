// Copyright (C) Gaijin Games KFT.  All rights reserved.

// Common part of config for every D3D driver with multi-driver support.
// Rules for config:
// - Define MULTI_DRIVER_NAME as multi_<driver_name>
// - Include this file in the config
// - Include driver's config for each TU before all other includes (e.g. use -FId3d_config.h in jam)
#if _TARGET_D3D_MULTI
#define _ADD_MULTI_NAMESPACE ::MULTI_DRIVER_NAME
#define _MULTI_INTERFACE     ::inline MULTI_DRIVER_NAME
#undef _TARGET_D3D_MULTI
// stub needs to know if it was imported as part of multi or not
#define _TARGET_WAS_MULTI 1
#else
#define _ADD_MULTI_NAMESPACE
#endif
