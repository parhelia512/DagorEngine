// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <render/puddle_query/puddleQueryManager.h>
#include <debug/dag_assert.h>

PuddleQueryManager *get_puddle_query_mgr() { G_ASSERT_RETURN(false, nullptr); }

int PuddleQueryManager::queryPoint(const Point3 &) { G_ASSERT_RETURN(false, -1); }

GpuReadbackResultState PuddleQueryManager::getQueryValue(int, float &) { G_ASSERT_RETURN(false, GpuReadbackResultState::FAILED); }
