// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "blasQuery.h"
#include <dag_noise/dag_uint_noise.h>
#include <util/dag_parallelFor.h>
#include <stdio.h>
#include <ioSys/dag_zstdIo.h>

static constexpr uint32_t block_size = SDF_BLOCK_SIZE * SDF_BLOCK_SIZE * SDF_BLOCK_SIZE;

template <class T>
static Point3 point3(T a)
{
  return Point3(a, a, a);
}

static constexpr __forceinline int divide_and_round_up(int v, int divisor) { return (v + divisor - 1) / divisor; }

static uint32_t calc_voxel_index(IPoint3 coord, IPoint3 dim) { return (coord.z * dim.y + coord.y) * dim.x + coord.x; }

class SDFBlockTask
{
public:
  SDFBlockTask(const MeshBLAS &blas, const dag::Span<Point3> &rayStabDirections, float maxTraceDist_, BBox3 volumeBox_,
    float invMaxExt_, Point2 decodeSDFMulAdd_, IPoint3 blockCoord_, IPoint3 indirectionDim_) :
    blas(blas),
    rayStabDirections(rayStabDirections),
    maxTraceDist(maxTraceDist_),
    volumeBox(volumeBox_),
    invMaxExt(invMaxExt_),
    decodeSDFMulAdd(decodeSDFMulAdd_),
    blockCoord(blockCoord_),
    indirectionDim(indirectionDim_)
  {}

  void doJob();

  const MeshBLAS &blas;
  const dag::Span<Point3> &rayStabDirections;
  float maxTraceDist;
  BBox3 volumeBox;
  float invMaxExt;
  Point2 decodeSDFMulAdd;
  IPoint3 blockCoord;
  IPoint3 indirectionDim;

  struct Result
  {
    uint8_t blockMaxDist = 0;
    uint8_t blockMinDist = 255;
    dag::Vector<uint8_t> SDFData;
  } result;
  int negs = 0, traces = 0, hits = 0;
};

void SDFBlockTask::doJob()
{
  const Point3 indirectionVoxelSize = div(volumeBox.width(), point3(indirectionDim));
  const Point3 SDFVoxelSize = indirectionVoxelSize / float(SDF_INTERNAL_BLOCK_SIZE);
  const Point3 blockPos = volumeBox[0] + mul(point3(blockCoord), indirectionVoxelSize);
  // debug("%@..%@ pos %@ coord %@ dim %@", volumeBox[0], volumeBox[1], blockPos, blockCoord, indirectionDim);
  result.SDFData.clear();
  result.SDFData.resize(SDF_BLOCK_SIZE * SDF_BLOCK_SIZE * SDF_BLOCK_SIZE);
  auto sdf = result.SDFData.data();

  for (int32_t zI = 0; zI < SDF_BLOCK_SIZE; zI++)
  {
    for (int32_t yI = 0; yI < SDF_BLOCK_SIZE; yI++)
    {
      for (int32_t xI = 0; xI < SDF_BLOCK_SIZE; xI++, sdf++)
      {

        const Point3 voxelPos = mul(Point3(xI, yI, zI), SDFVoxelSize) + blockPos;

        float minDist = maxTraceDist;

        if (blas_closest_distance(blas, v_ldu_p3(&voxelPos.x), maxTraceDist, minDist))
        {
          traces++;
          int32_t hit = 0;
          int32_t hitBack = 0;
          int32_t raysFired = 0, totalRays = rayStabDirections.size();
          constexpr int backThreshold = 4;

          for (const Point3 rayDir : rayStabDirections)
          {
            ++raysFired;
            const float eps = 1.e-4f;
            // Pull back the starting position slightly to make sure we hit a triangle that voxelPos is exactly on.
            const Point3 rayStart = voxelPos - eps * maxTraceDist * rayDir;

            bool normalOpposes = false;
            if (blas_ray_hit(blas, v_ldu_p3(&rayStart.x), v_ldu_p3(&rayDir.x), maxTraceDist + eps, normalOpposes))
            {
              hit++;
              hits++;

              if (normalOpposes)
              {
                if ((++hitBack) * backThreshold > totalRays) // we are already hit too much backs
                  break;
              }
            }
            // we can stop earlier if too much rays are already facing forward and there is no chance to be negative dist even if all
            // other rays are negative
            const int raysLeft = totalRays - raysFired;
            if ((raysLeft + hitBack) * backThreshold <= totalRays)
              break;
          }

          // Consider this voxel 'inside' an object if we hit too much backfaces
          if (hit > 0 && hitBack * backThreshold > totalRays)
          {
            minDist = -minDist;
            ++negs;
          }
        }

        const float volumeSpaceDistance = minDist * invMaxExt;
        const uint8_t quantizedDist = clamp(int((volumeSpaceDistance - decodeSDFMulAdd.y) / decodeSDFMulAdd.x * 255.0f + .5f), 0, 255);
        *sdf = quantizedDist;
        result.blockMaxDist = max(result.blockMaxDist, quantizedDist);
        result.blockMinDist = min(result.blockMinDist, quantizedDist);
      }
    }
  }
}

inline Point3 uniform_sample_sphere(float EX, float EY)
{
  float phi = 2 * PI * EY;
  float cosTheta = 1 - 2 * EX;
  float sinTheta = sqrt(1 - cosTheta * cosTheta);

  Point3 H;
  H.x = sinTheta * cosf(phi);
  H.y = sinTheta * sinf(phi);
  H.z = cosTheta;
  return H;
}

void generate_sphere_samples(Point3 *dir, uint32_t dim, uint32_t seed)
{
  for (int i = 0, IndexX = 0; IndexX < dim; IndexX++)
  {
    for (int IndexY = 0; IndexY < dim; IndexY++, dir++, i += 2)
    {
      const float U1 = (uint_noise1D(i, seed) & 0xFFFFFF) / float(0xFFFFFF);
      const float U2 = (uint_noise1D(i + 1, seed) & 0xFFFFFF) / float(0xFFFFFF);
      *dir = uniform_sample_sphere((IndexX + U1) / (float)dim, (IndexY + U2) / (float)dim);
    }
  }
}

static dag::Vector<Point3> rayStabDirections;
static int total_bricks, bricks, totalBytes, uncompressedBytes;

void init_sdf_generate()
{
  int dim = 7;
  rayStabDirections.resize(dim * dim);
  generate_sphere_samples(rayStabDirections.data(), dim, 0);
}

void close_sdf_generate()
{
  rayStabDirections.clear();
  rayStabDirections.shrink_to_fit();
}

static IPoint3 mip_indirection(const IPoint3 &mip0IndirectionDim, int mip)
{
  return IPoint3(divide_and_round_up(mip0IndirectionDim.x, 1 << mip), divide_and_round_up(mip0IndirectionDim.y, 1 << mip),
    divide_and_round_up(mip0IndirectionDim.z, 1 << mip));
}

static IPoint3 mip0_indirection(const BBox3 &meshBounds, float voxel_density, int maxBlocksDim)
{
  const Point3 idealDim = Point3(meshBounds.width() * (voxel_density / (float)SDF_INTERNAL_BLOCK_SIZE));
  return IPoint3(clamp(float2int_near(idealDim.x), 1, maxBlocksDim), clamp(float2int_near(idealDim.y), 1, maxBlocksDim),
    clamp(float2int_near(idealDim.z), 1, maxBlocksDim));
}

void generate_sdf(const MeshBLAS &blas, MippedMeshSDF &meshSDF, float voxel_density, int mesh_max_res)
{
  BBox3 meshBounds = blas.box;
  const int32_t maxBlocksDim = min(divide_and_round_up(mesh_max_res, SDF_INTERNAL_BLOCK_SIZE), SDF_MAX_INDIRECTION_DIM - 1);

  // Make sure the mesh bounding box has positive extents to handle thin planes
  {
    Point3 meshBoundsCenter = meshBounds.center();
    Point3 meshBoundsExtent = max(meshBounds[1] - meshBoundsCenter, Point3(0.5f, 0.5f, 0.5f));
    meshBounds[0] = meshBoundsCenter - meshBoundsExtent;
    meshBounds[1] = meshBoundsCenter + meshBoundsExtent;
  }

  // we need to analyze flags for meshes!
  constexpr bool mostlyTwoSided = false;

  if (mostlyTwoSided)
  {
    const IPoint3 mip0IndirectionDim = mip0_indirection(meshBounds, voxel_density, maxBlocksDim);

    const float centralDifferencingExpandInVoxels = .25f;
    const Point3 texelObjectSpaceSize =
      div(meshBounds.width(), point3(mip0IndirectionDim * SDF_INTERNAL_BLOCK_SIZE) - point3(2 * centralDifferencingExpandInVoxels));
    meshBounds[0] -= texelObjectSpaceSize;
    meshBounds[1] += texelObjectSpaceSize;
  }

  Point3 ext = meshBounds.width() * .5f;
  const float invMaxExt = 1.0f / max(max(ext.x, ext.y), ext.z);
  IPoint3 mip0IndirectionDim = mip0_indirection(meshBounds, voxel_density, maxBlocksDim);
  if (maxBlocksDim < min<int>(mesh_max_res, SDF_MAX_INDIRECTION_DIM - 1) &&
      lengthSq(meshBounds.width()) > (SDF_INTERNAL_BLOCK_SIZE * SDF_INTERNAL_BLOCK_SIZE) / (voxel_density * voxel_density) &&
      mip_indirection(mip0IndirectionDim, SDF_NUM_MIPS - 1) == mip_indirection(mip0IndirectionDim, SDF_NUM_MIPS - 2))
  {
    // increase voxel density
    voxel_density *= 2;
    mip0IndirectionDim = mip0_indirection(meshBounds, voxel_density, maxBlocksDim);
  }

  dag::Vector<uint8_t> compressedMipData;
  dag::Vector<uint32_t> indirectionLUT;
  dag::Vector<SDFBlockTask *> validBlocks;
  dag::Vector<SDFBlockTask> blockTasks;
  dag::Vector<uint8_t> SDFBlocksData;
  int32_t mipCount = 0;

  IPoint3 lastIndirectionDim(0, 0, 0);
  for (; mipCount < SDF_NUM_MIPS; mipCount++)
  {
    const IPoint3 indirectionDim = mip_indirection(mip0IndirectionDim, mipCount);
    if (mipCount && lastIndirectionDim == indirectionDim) // resolution stopped change
      break;
    lastIndirectionDim = indirectionDim;
    // Expand to one voxel for trilinear filtering
    const Point3 texelObjectSpaceSize =
      div(meshBounds.width(), point3(indirectionDim * SDF_INTERNAL_BLOCK_SIZE) - point3(2 * SDF_VOLUME_BORDER));
    BBox3 SDFVolume;
    SDFVolume[0] = meshBounds[0] - texelObjectSpaceSize;
    SDFVolume[1] = meshBounds[1] + texelObjectSpaceSize;

    const Point3 indirectionVoxelSize = div(SDFVolume.width(), point3(indirectionDim));

    const Point3 volumeSpaceSDFVoxelSize = indirectionVoxelSize * invMaxExt / float(SDF_INTERNAL_BLOCK_SIZE);
    const float maxEncodeDist = length(volumeSpaceSDFVoxelSize) * SDF_MAX_VOXELS_DIST;
    const float maxTraceDist = invMaxExt < 1.f ? maxEncodeDist / invMaxExt : maxEncodeDist;
    const Point2 decodeSDFMulAdd(2.0f * maxEncodeDist, -maxEncodeDist);

    blockTasks.clear();
    blockTasks.reserve(indirectionDim.x * indirectionDim.y * indirectionDim.z);
    for (int32_t zI = 0; zI < indirectionDim.z; zI++)
    {
      for (int32_t yI = 0; yI < indirectionDim.y; yI++)
      {
        for (int32_t xI = 0; xI < indirectionDim.x; xI++)
        {
          blockTasks.emplace_back(blas, make_span(rayStabDirections), maxTraceDist, SDFVolume, invMaxExt, decodeSDFMulAdd,
            IPoint3(xI, yI, zI), indirectionDim);
        }
      }
    }

    threadpool::parallel_for(0, blockTasks.size(), 1, [&blockTasks](uint32_t tbegin, uint32_t tend, uint32_t) {
      for (uint32_t i = tbegin; i != tend; ++i)
        blockTasks[i].doJob();
    });

    auto &outMip = meshSDF.mipInfo[mipCount];
    indirectionLUT.clear();
    indirectionLUT.resize(indirectionDim.x * indirectionDim.y * indirectionDim.z, SDF_INVALID_LUT_INDEX);

    validBlocks.clear();
    validBlocks.reserve(blockTasks.size());

    int negs = 0, traces = 0, hits = 0;
    static int totalBlocks, totalNegBlocks;
    for (auto &t : blockTasks)
    {
      negs += t.negs;
      traces += t.traces;
      hits += t.hits;
      if (t.result.blockMinDist < 255 && t.result.blockMaxDist > 0)
      {
        validBlocks.push_back(&t);
        totalBlocks++;
        totalNegBlocks += (t.result.blockMinDist <= 127);
      }
    }
    // debug("maxTraceDist %f invMaxExt =%f maxEncodeDist = %f, negs/traces %d/%d traces/voxels = %d/%d hits/traces %d/%d",
    //   maxTraceDist, invMaxExt,maxEncodeDist,
    //   negs, traces,
    //   traces, blockTasks.size()*blockTasks[0].result.SDFData.size(), hits,
    //  traces*rayStabDirections.size());

    SDFBlocksData.resize(block_size * validBlocks.size());

    for (int32_t blockIndex = 0; blockIndex < validBlocks.size(); blockIndex++)
    {
      const auto &block = *validBlocks[blockIndex];
      indirectionLUT[calc_voxel_index(block.blockCoord, indirectionDim)] = blockIndex;

      G_ASSERT(block_size == block.result.SDFData.size());
      memcpy(&SDFBlocksData[blockIndex * block_size], block.result.SDFData.data(), block_size);
    }

    const int32_t indirectionLUTBytes = indirectionLUT.size() * sizeof(uint32_t);
    const int32_t mipDataSize = indirectionLUTBytes + SDFBlocksData.size();
    total_bricks += blockTasks.size();
    bricks += validBlocks.size();

    compressedMipData.resize(mipDataSize);
    memcpy(compressedMipData.data(), indirectionLUT.data(), indirectionLUTBytes);
    memcpy(compressedMipData.data() + indirectionLUTBytes, SDFBlocksData.data(), SDFBlocksData.size());

    outMip.streamOffset = meshSDF.compressedMips.size();
    size_t maxSz = zstd_compress_bound(mipDataSize);
    meshSDF.compressedMips.append_default(maxSz);
    int sz = zstd_compress(meshSDF.compressedMips.data() + outMip.streamOffset, maxSz, compressedMipData.data(), mipDataSize, 0);
    G_ASSERT(sz > 0);

    meshSDF.compressedMips.resize(outMip.streamOffset + sz);
    outMip.compressedSize = sz;
    totalBytes += outMip.compressedSize;
    uncompressedBytes += mipDataSize;

    outMip.indirectionDim = outMip.encode_dim(indirectionDim.x, indirectionDim.y, indirectionDim.z);
    const float maxEncodedDist = mostlyTwoSided ? -fabsf(decodeSDFMulAdd.y) : fabsf(decodeSDFMulAdd.y);
    outMip.maxEncodedDistFP16 = float_to_half_unsafe(maxEncodedDist);
    outMip.sdfBlocksCount = validBlocks.size();

    // Account for the border voxels we added
    const Point3 indirMin = Point3(SDF_VOLUME_BORDER, SDF_VOLUME_BORDER, SDF_VOLUME_BORDER) / SDF_INTERNAL_BLOCK_SIZE;
    const Point3 indirSize = point3(indirectionDim) - 2.f * indirMin;

    const Point3 volumeExtent = meshBounds.width() * 0.5f * invMaxExt;

    // [-volumeExtent, volumeExtent] -> [to indirection lut coordinates]
    outMip.toIndirMul = div(indirSize, (2 * volumeExtent));
    outMip.toIndirAdd = mul(volumeExtent, outMip.toIndirMul) + indirMin;
  }

  meshSDF.mipCountTwoSided = mipCount | ((mostlyTwoSided ? 1 : 0) << 7);
  meshSDF.localBounds = meshBounds;
  // printf("bricks %d/%d bytes %dkb (%dkb uncompressed)\n",
  //   bricks, total_bricks, totalBytes>>10, uncompressedBytes>>10);
}
