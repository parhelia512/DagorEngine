// Copyright (C) Gaijin Games KFT.  All rights reserved.
#pragma once

#include <gameRes/dag_collisionResource.h>
#include <daBVH/dag_swBLAS_soa4.h>
#include "shapes/CollisionBlasShape.h"

// physJolt's reader of a collision resource's chunk internals (a friend of the resource): a node's
// chunk parsed into the shape's ChunkRef, and the claim that keeps the Data block a chunk lives in
// alive while a shape reads it. The resource's own class surface carries none of this.
struct CollResJoltChunk
{
  // One node's chunk, validated and parsed once for a consumer that keeps it: empty unless the
  // whole chunk fits in the resource's blob. The fields come from the resource's own chunk
  // readers (getNodeOccluderBlas), so the chunk layout keeps one owner.
  struct View
  {
    explicit operator bool() const { return chunk.tree != nullptr; }
    soa4::ChunkRef chunk;             // tree, treeBytes, root, bmin, invScale, triCount
    uint32_t hdrFlags = 0;            // NodeBlasChunkHeader::flags (HAS_EDGE_FLAGS), read past validation
    const uint8_t *verts21 = nullptr; // the node's vert21 stream (JoltMeshShapeBuilder's feed)
    uint32_t vertOffset = 0;          // verts21 - tree, 8-aligned
  };
  static View viewOf(const CollisionResource &res, const CollisionNode &node)
  {
    View v;
    const dag::ConstSpan<uint8_t> blas = res.data->nodeBlasData();
    if (node.nodeBlasOfs == ~0u || node.nodeBlasOfs >= blas.size())
      return v;
    const size_t left = blas.size() - node.nodeBlasOfs;
    if (left < sizeof(CollisionResource::NodeBlasChunkHeader))
      return v;
    const auto *hdr = (const CollisionResource::NodeBlasChunkHeader *)(blas.data() + node.nodeBlasOfs);
    if (!hdr->rootRef.valid() || CollisionResource::nodeChunkBytes(hdr, node.verticesCount) > left)
      return v;
    const CollisionResource::NodeOccluderBlas b = res.getNodeOccluderBlas(node);
    v.chunk = {b.blasData, hdr->treeBytes, b.rootRef, b.bmin, b.invScale, node.indicesCount / 3u};
    v.hdrFlags = hdr->flags;
    v.verts21 = b.blasData + b.vertOffset;
    v.vertOffset = b.vertOffset;
    return v;
  }

  // A share of the Data block for a long-lived consumer of a chunk (a physics shape): the block and
  // every pointer into it stay alive, its geometry immutable. The one write a shared block takes is
  // a first tree bind's geomNodeId stamps; the per-node bind claim (setNodeGeomNodeId) is refused
  // while a share exists, and a later bind with a foreign layout moves the resource to its own
  // clone and leaves the share on the frozen block: the isolation two deepCopies have. The
  // resource object itself may be freed.
  class Share
  {
  public:
    explicit Share(const CollisionResource &res) : block(res.data) {}
    size_t sharedBytes() const { return block->bytesPerHolder(); }

  private:
    Ptr<CollisionResource::Data> block;
  };

  // The shape over node_id's chunk with a Share of the block, or an error when the node has none.
  // compound_children as the shape's bare entry, plus 0 = one child per node of the resource.
  static JPH::Shape::ShapeResult createShape(const CollisionResource &res, int node_id, JPH::uint32 compound_children = 0,
    bool force_descent_ids = false);
};
