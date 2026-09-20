// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <recastTools/navmeshExportType.h>

enum NavmeshAreaType
{
  NM_AREATYPE_MAIN = 0,
  NM_AREATYPE_DET,
  NM_AREATYPE_RECT,
  NM_AREATYPE_POLY
};

NavmeshExportType navmesh_export_type_name_to_enum(const char *name);
const char *navmesh_export_type_to_string(NavmeshExportType type);
