// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

// tri_ref tokens are minted by the walks from validated trees, so decoding one back needs no
// checks. Dev PC builds still run the paranoid validation (daBVH/dag_swBLAS_soa4Validate.h) to
// catch stale/forged refs early. One expression, shared by the runtime and the unit test's
// crafted-token battery, so the production gate and the test's skip guard cannot drift apart.
#define VALIDATE_TRI_REF_TOKENS (_TARGET_PC_WIN && DAGOR_DBGLEVEL > 0)
