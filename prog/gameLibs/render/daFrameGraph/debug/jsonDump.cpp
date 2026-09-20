// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "jsonDump.h"
#include "jsonDumpCommon.h"

#include <debug/dag_log.h>
#include <osApiWrappers/dag_files.h>
#include <osApiWrappers/dag_miscApi.h>
#include <util/dag_console.h>


namespace dafg
{

namespace jsondump
{

Buffer sb;
Writer w(sb);

} // namespace jsondump

void dump_state_to_json(const InternalRegistry &registry, const intermediate::Graph &ir_graph, const sd::NodeStateDeltas &state_deltas)
{
  jsondump::sb.Clear();
  jsondump::w.Reset(jsondump::sb);
  jsondump::w.SetIndent(' ', 2);

  {
    jsondump::w.StartObject();

    jsondump::dump_registry_to_json(registry);
    jsondump::dump_ir_graph_to_json(registry, ir_graph);
    jsondump::dump_state_deltas_to_json(ir_graph, state_deltas);

    jsondump::w.EndObject();
  }

  if (!jsondump::w.IsComplete())
  {
    logerr("daFG: failed to dump, json is broken!");
    return;
  }

  DagorDateTime dt;
  get_local_time(&dt);
  String filename(0, "dafgDump_%02d-%02d-%02d.json", dt.hour, dt.minute, dt.second);

  if (file_ptr_t f = df_open(filename, DF_WRITE | DF_CREATE))
  {
    int fileLen = static_cast<int>(jsondump::sb.GetSize());
    int bytesWritten = df_write(f, jsondump::sb.GetString(), fileLen);
    if (bytesWritten == fileLen)
      debug("daFG: framegraph dump saved to '%s'", filename);
    else
      logerr("daFG: failed to dump, wrote %d bytes instead of %d", bytesWritten, fileLen);

    df_close(f);
  }
  else
  {
    logerr("daFG: failed to open '%s' for writing", filename);
  }
}

} // namespace dafg
