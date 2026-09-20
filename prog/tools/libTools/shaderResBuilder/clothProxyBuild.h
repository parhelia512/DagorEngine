// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

class ILogWriter;
class DynamicRenderableSceneLodsResSrc;

// lod{ proxy:i= } names a known proxy type; reports and returns false when it does not.
bool validate_cloth_proxy_type(int proxy_int, const char *lod_file_name, ILogWriter *log);

// Picks the cage lod, audits it, and bakes the render-vertex-to-cage bindings into every cloth render lod.
// Runs after addNode: it reads and patches the packed vertex data.
void build_cloth_proxy_bindings(DynamicRenderableSceneLodsResSrc &res, ILogWriter *log);
