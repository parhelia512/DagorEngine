//
// Dagor Engine 6.5
// Copyright (C) Gaijin Games KFT.  All rights reserved.
//
#pragma once

// SoA4 node emit for create_bvh_node_sah consumers whose leaves are not triangle quads.
// writeDoubleQuadBVH2 fuses the same SAH walk with its stackless quad-leaf output, and
// soa4Convert's emitSpan owns the stackless-input SoA4 emit; consumers with their own leaf
// encoding (the collision all-nodes TLAS) reuse this walk and node-block emit instead of
// keeping a third private copy of the format.

#include <daBVH/dag_bvhBuild.h> // the SAH entry encoding and its decode helpers live with the builder
#include <daBVH/dag_swBLAS_soa4.h>

namespace soa4
{
// Gather a node's direct children. Returns the count, or 0 when the entry cannot be a
// lane-encoded inner node: an out-of-range node, a leaf (keyed on faceIndex, so its w(bmax)
// stays free for caller payload - swTLAS stores a modelId there), a single-child inner, a
// fanout past 4 (the 2-bit tag carries 2..4 children) -- or a walk that would pass sah_count,
// so a truncated or malformed tree refuses instead of reading past the array.
inline int sahChildren(const bbox3f *sah, int sah_count, int node, int (&kids)[4])
{
  if (node < 0 || node >= sah_count || build_bvh::sahIsLeaf(sah, node))
    return 0; // out of range, or a leaf gathers nothing, whatever its payload lane carries
  const int cnt = build_bvh::sahChildrenCount(sah, node);
  int n = 0;
  int64_t cur = node + 1;
  for (int i = 0; i < cnt && n < 4 && cur < sah_count; ++i)
  {
    kids[n++] = (int)cur;
    cur += build_bvh::sahSpan(sah, (int)cur);
  }
  return (n >= 2 && n == cnt) ? n : 0;
}

// First guard that refused an emit; recovery is uniform (discard the spent state), the reason
// is for the consumer's decline diagnostics. Fanout covers every child count the node encoding
// cannot carry: a past-4 fanout, a single-child inner, an out-of-range node, an empty tree, a
// truncated tree whose bounded walk cannot reach its claimed children -- and the VALID
// single-leaf tree, which the caller must emit as the degenerate root block instead.
enum class SahEmitRefusal : uint8_t
{
  None,
  Fanout,
  Depth,
  Offset,
  Size
};

// Output state one emitFromSah run threads through its recursion.
struct SahEmitState
{
  uint8_t *out = nullptr; // 4-byte aligned (a misaligned base refuses as Offset); must hold outSize bytes
  uint32_t outSize = 0;   // the WHOLE allocation; the guard fits every node's NodeRef word load inside it
  uint32_t outOfs = 0;    // 4-byte aligned at entry; block advances (16n) keep it
  int nodeCount = 0;
  bool ok = true;
  SahEmitRefusal refusal = SahEmitRefusal::None;
};

// Emit the SoA4 node tree for the SAH subtree at `node` and return its tagged child ref, or 0
// with st.ok=false when the tree cannot be encoded: a >4 fanout, a node offset at or past
// NODE_OFS_LIMIT (32 MB, the LeafRef parent-offset envelope; the ref field carries 26 bits),
// a misaligned base or start offset (PTR_OFS_MASK drops the low offset bits, so the stored
// ref would truncate to the aligned-down block, and the u32 lane stores would misalign),
// a node block past outSize, a depth past the traversal stacks -- or the single-leaf tree,
// whose root has one child and must be emitted by the caller as the degenerate root block
// instead. A refused DEEP-form state is spent (offsets advanced, blocks
// partially written); the root form restores its offsets on refusal.
// A child's box lives in its PARENT node, so `parent_packed` (packNodeLane(node_index, lane))
// is only the back-link a refit walks up.
// The root call passes the ~0u sentinel node_sink alone sees.
// Emitted refs carry a ZERO short-lane mask: 4 B short leaf bodies cannot be expressed
// through this emit.
// The tree carries NO leaf-body region (outOfs advances by the 16*N node blocks only), so the
// stock body-decoding readers cannot walk it; only a consumer's own word-based reader can.
// No byte marks that difference (the SoA4 layout has no version field), so a consumer
// must keep emitted buffers apart from converter-produced ones in its own bookkeeping.
// node_index below (in parent_packed and leaf_word) is the EMIT-ORDER index node_sink
// observes, not the SAH preorder index the `node`/`sah_idx` parameters carry.
//   quant_lane(sah_idx, node_index, lane, out_min3, out_max3): the consumer's u16 quantization
//     of one child's box; the emitter owns the lane-strided store (storeLaneBoxU16).
//   leaf_word(sah_face_index, node_index, lane): the consumer payload of a leaf child word;
//     the emitter ORs in BLAS_LEAF_FLAG (bit 31), the key the walkers separate words by, so
//     the payload must keep bit 31 clear (a set bit is absorbed, not payload).
//   node_sink(node_index, node_ofs, parent_packed, n): per-node meta.
// Every callback carries the emit-order node_index, so cross-NODE state keys on it without
// any ordering assumption. Within one node the call order IS fixed: node_sink once, then
// quant_lane for all n lanes in lane order, then the child words in lane order (leaf_word per
// leaf child, recursion per inner child).
// `depth` is the recursion level: the root call passes 0 (the default), each recursion adds 1;
// a nonzero root value shifts the refusal boundary.
template <class QuantLane, class LeafWord, class NodeSink>
inline uint32_t emitFromSah(SahEmitState &st, const bbox3f *sah, int sah_count, int node, uint32_t parent_packed,
  const QuantLane &quant_lane, const LeafWord &leaf_word, const NodeSink &node_sink, int depth = 0)
{
  int kids[4];
  const int n = sahChildren(sah, sah_count, node, kids);
  // Size covers the reader, not just the block: NodeRef's word access loads 16 B at 12n, so a
  // narrow node (n < 4) needs 12n + 16 B of capacity while advancing outOfs by 16n only.
  if (n == 0 || depth > MAX_TREE_DEPTH || st.outOfs >= NODE_OFS_LIMIT || (((uintptr_t)st.out | st.outOfs) & 3u) != 0 ||
      st.outOfs + 12u * (uint32_t)n + 16u > st.outSize)
  {
    st.ok = false;
    st.refusal = n == 0                                                                       ? SahEmitRefusal::Fanout
                 : depth > MAX_TREE_DEPTH                                                     ? SahEmitRefusal::Depth
                 : st.outOfs >= NODE_OFS_LIMIT || (((uintptr_t)st.out | st.outOfs) & 3u) != 0 ? SahEmitRefusal::Offset
                                                                                              : SahEmitRefusal::Size;
    return 0;
  }
  const uint32_t myOfs = st.outOfs;
  st.outOfs += 16u * (uint32_t)n;
  const uint32_t myIdx = (uint32_t)st.nodeCount++;
  node_sink(myIdx, myOfs, parent_packed, n);
  for (int i = 0; i < n; ++i)
  {
    uint16_t mn[3], mx[3];
    quant_lane(kids[i], myIdx, i, mn, mx);
    storeLaneBoxU16(st.out + myOfs, n, i, mn, mx);
  }
  uint32_t *w = (uint32_t *)(st.out + myOfs + 12u * (uint32_t)n);
  for (int i = 0; i < n; ++i)
  {
    const int f = build_bvh::sahFaceIndex(sah, kids[i]);
    w[i] = !build_bvh::sahIsLeaf(sah, kids[i])
             ? emitFromSah(st, sah, sah_count, kids[i], packNodeLane(myIdx, i), quant_lane, leaf_word, node_sink, depth + 1)
             : BLAS_LEAF_FLAG | leaf_word(f, myIdx, i);
    if (!st.ok)
      return 0;
  }
  return makeNodeRef(myOfs, n, 0); // no short-leaf bodies through this emit
}

// Root form: emit the whole tree from SAH entry 0, which must BE the tree root -- true for a
// fresh Tab only (create_bvh_node_sah appends, so a reused Tab puts the root at the pre-call
// size and this form would silently emit the wrong subtree). The ~0u parent is the sentinel
// node_sink alone sees.
// A refused root call restores outOfs and nodeCount, and every root call starts with a clean
// ok/refusal, so a refused state may be re-emitted (the partial bytes are dead and the next
// emit overwrites them).
template <class QuantLane, class LeafWord, class NodeSink>
inline uint32_t emitFromSah(SahEmitState &st, const bbox3f *sah, int sah_count, const QuantLane &quant_lane, const LeafWord &leaf_word,
  const NodeSink &node_sink)
{
  const uint32_t entryOfs = st.outOfs;
  const int entryCount = st.nodeCount;
  st.ok = true;
  st.refusal = SahEmitRefusal::None;
  const uint32_t ref = emitFromSah(st, sah, sah_count, 0, ~0u, quant_lane, leaf_word, node_sink);
  if (!st.ok)
  {
    st.outOfs = entryOfs;
    st.nodeCount = entryCount;
  }
  return ref;
}
} // namespace soa4
