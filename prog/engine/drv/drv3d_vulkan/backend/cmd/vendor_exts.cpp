// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include "vendor_exts.h"
#include "driver.h"
#include "backend/context.h"
#include "execution_sync.h"
#include "dlss.h"
#include "amdFsr.h"
#include "xess.h"
#include "driver_config.h"

using namespace drv3d_vulkan;

static VkImageLayout prepareImage(BEContext &ctx, Image *src, bool out = false)
{
  VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  if (out)
    layout = VK_IMAGE_LAYOUT_GENERAL;
  else if (src && Globals::cfg.bits.sampledDepthReadOnlyLayout && src->getUsage() & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
    layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  if (src)
  {
    ctx.verifyResident(src);
    Backend::sync.addImageAccess(LogicAddress::forImageOnExecStage(ExtendedShaderStage::CS, out ? RegisterType::U : RegisterType::T),
      src, layout, {0, 1, 0, 1});
  }
  return layout;
}

// Streamline takes the image, its create info and a view; the adapter unpacks this tuple.
static eastl::tuple<VkImage, VkImageCreateInfo, VkImageView> to_image_tuple(Image *src, bool out = false)
{
  if (!src)
    return eastl::make_tuple(VkImage(0), VkImageCreateInfo{}, VkImageView(0));

  ImageViewState ivs;
  ivs.setMipBase(0);
  ivs.setMipCount(1);
  ivs.setArrayBase(0);
  ivs.setArrayCount(1);
  ivs.isArray = 0;
  ivs.isCubemap = 0;
  ivs.isUAV = out ? 1 : 0;
  ivs.setFormat(src->getFormat());
  auto ici = src->getDescription().ici.toVk();
  ici.format = src->getDescription().format.asVkFormat();
  return eastl::make_tuple(src->getHandle(), ici, src->getImageView(ivs));
}

#if USE_STREAMLINE_FOR_DLSS
// Optional inputs must reach Streamline as a null pointer, not as a tuple holding a null image.
static void *ptr_or_null(eastl::tuple<VkImage, VkImageCreateInfo, VkImageView> &t) { return eastl::get<0>(t) ? &t : nullptr; }
#endif

TSPEC void BEContext::execCmd(const CmdExecuteFSR &cmd)
{
  beginCustomStage("executeFSR");

  auto toPair = [&](Image *src) -> eastl::pair<VkImage, VkImageCreateInfo> {
    if (src)
    {
      auto ici = src->getDescription().ici.toVk();
      ici.format = src->getDescription().format.asVkFormat();
      return eastl::make_pair(src->getHandle(), ici);
    }
    return eastl::make_pair(VkImage(0), VkImageCreateInfo{});
  };

  prepareImage(*this, cmd.params.colorTexture);
  prepareImage(*this, cmd.params.motionVectors);
  prepareImage(*this, cmd.params.exposureTexture);
  prepareImage(*this, cmd.params.reactiveTexture);
  prepareImage(*this, cmd.params.transparencyAndCompositionTexture);
#if USE_ARM_ASR
  // ARM ASR writes the upscaled image as a color attachment
  // Using prepareImage causes false positive "reading garbage" warning (ASR transitioned layout internally but it was too late)
  if (cmd.params.outputTexture)
  {
    verifyResident(cmd.params.outputTexture);
    Backend::sync.addImageAccess(LogicAddress::forAttachmentWithLayout(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL),
      cmd.params.outputTexture, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, {0, 1, 0, 1});
    verifyResident(cmd.params.depthTexture);
    Backend::sync.addImageAccess(LogicAddress::forImageOnExecStage(ExtendedShaderStage::CS, RegisterType::T), cmd.params.depthTexture,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, {0, 1, 0, 1});
  }
#else
  prepareImage(*this, cmd.params.outputTexture);
  prepareImage(*this, cmd.params.depthTexture);
#endif

  auto colorTexture = toPair(cmd.params.colorTexture);
  auto depthTexture = toPair(cmd.params.depthTexture);
  auto motionTexture = toPair(cmd.params.motionVectors);
  auto exposureTexture = toPair(cmd.params.exposureTexture);
  auto reactiveTexture = toPair(cmd.params.reactiveTexture);
  auto transparencyAndCompositionTexture = toPair(cmd.params.transparencyAndCompositionTexture);
  auto outputTexture = toPair(cmd.params.outputTexture);

  amd::FSR::UpscalingPlatformArgs args = cmd.params;
  args.colorTexture = &colorTexture;
  args.depthTexture = &depthTexture;
  args.motionVectors = &motionTexture;
  args.exposureTexture = &exposureTexture;
  args.outputTexture = &outputTexture;
  args.reactiveTexture = &reactiveTexture;
  args.transparencyAndCompositionTexture = &transparencyAndCompositionTexture;

  Backend::sync.completeNeeded();

  if (amd::FSRVulkan *fsr = amd::FSRVulkan::getExistingInstance())
    fsr->doApplyUpscaling(args, frameCore);

  // FSR modifies state in command buffer, so we must setup it back,
  // same way as if command buffer was interrupted
  onFrameCoreReset();
}

TSPEC void BEContext::execCmd(const CmdExecuteDLSS &cmd)
{
  beginCustomStage("executeDLSS");
  G_UNUSED(cmd);
#if !USE_STREAMLINE_FOR_DLSS
  prepareImage(*this, cmd.params.inColor);
  prepareImage(*this, cmd.params.inDepth);
  prepareImage(*this, cmd.params.inMotionVectors);
  prepareImage(*this, cmd.params.inExposure);
  prepareImage(*this, cmd.params.inAlbedo);
  prepareImage(*this, cmd.params.inSpecularAlbedo);
  prepareImage(*this, cmd.params.inNormalRoughness);
  prepareImage(*this, cmd.params.inHitDist);
  prepareImage(*this, cmd.params.outColor, true);

  auto tColor = to_image_tuple(cmd.params.inColor);
  auto tDepth = to_image_tuple(cmd.params.inDepth);
  auto tMotionVectors = to_image_tuple(cmd.params.inMotionVectors);
  auto tExposure = to_image_tuple(cmd.params.inExposure);
  auto tAlbedo = to_image_tuple(cmd.params.inAlbedo);
  auto tSpecularAlbedo = to_image_tuple(cmd.params.inSpecularAlbedo);
  auto tNormalRoughness = to_image_tuple(cmd.params.inNormalRoughness);
  auto tHitDist = to_image_tuple(cmd.params.inHitDist);
  auto tOutColor = to_image_tuple(cmd.params.outColor, true);

  auto args = nv::convertDlssParams(cmd.params, [](Image *src) -> void * { return src; });
  args.inColor = &tColor;
  args.inDepth = &tDepth;
  args.inMotionVectors = &tMotionVectors;
  args.inExposure = &tExposure;
  args.inAlbedo = &tAlbedo;
  args.inSpecularAlbedo = &tSpecularAlbedo;
  args.inNormalRoughness = &tNormalRoughness;
  args.inHitDist = &tHitDist;
  args.outColor = &tOutColor;

  Backend::sync.completeNeeded();

  Globals::dlss.evaluate((nv::DlssParams<void> &)args, frameCore);

  // DLSS modifies state in command buffer, so we must setup it back,
  // same way as if command buffer was interrupted
  onFrameCoreReset();
#endif
}

TSPEC void BEContext::execCmd(const CmdInitializeDLSS &cmd)
{
  beginCustomStage("initializeDLSS");
  G_UNUSED(cmd);
#if !USE_STREAMLINE_FOR_DLSS
  Globals::dlss.setOptionsBackend(Globals::VK::dev.get(), frameCore, nv::DLSS::Mode(cmd.mode), IPoint2(cmd.width, cmd.height),
    cmd.use_rr, cmd.use_legacy_model);
#endif
}

TSPEC void BEContext::execCmd(const CmdReleaseDLSS &cmd)
{
  beginCustomStage("releaseDLSS");
  G_UNUSED(cmd);
#if !USE_STREAMLINE_FOR_DLSS
  Globals::dlss.DeleteFeature();
#endif
}

TSPEC void BEContext::execCmd(const CmdReleaseStreamlineDLSS &cmd)
{
  beginCustomStage("releaseStreamlineDlss");
  G_UNUSED(cmd);
#if USE_STREAMLINE_FOR_DLSS
  auto &streamlineAdapter = Globals::VK::loader.streamlineAdapter;

  streamlineAdapter->releaseDlssFeature(0);
  if (cmd.stereo_render)
    streamlineAdapter->releaseDlssFeature(1);

  if (streamlineAdapter->isDlssGSupported() == nv::SupportState::Supported)
    streamlineAdapter->releaseDlssGFeature(0);

  if (streamlineAdapter->isDlssNRSupported() == nv::SupportState::Supported)
    streamlineAdapter->releaseDlssNRFeature(0);
#endif
}

TSPEC void BEContext::execCmd(const CmdInitializeStreamlineDLSS &cmd)
{
  beginCustomStage("initializeStreamlineDlss");
  G_UNUSED(cmd);
#if USE_STREAMLINE_FOR_DLSS
  if (Globals::VK::loader.streamlineAdapter->isDlssSupported() == nv::SupportState::Supported)
    Globals::VK::loader.streamlineAdapter->createDlssFeature(0, {cmd.width, cmd.height}, frameCore);

  if (Globals::VK::loader.streamlineAdapter->isDlssGSupported() == nv::SupportState::Supported)
    Globals::VK::loader.streamlineAdapter->createDlssGFeature(0, frameCore);

  if (Globals::VK::loader.streamlineAdapter->isDlssNRSupported() == nv::SupportState::Supported)
    Globals::VK::loader.streamlineAdapter->createDlssNRFeature(0, frameCore);
#endif
}

TSPEC void BEContext::execCmd(const CmdExecuteStreamlineDLSS &cmd)
{
  beginCustomStage("executeStreamlineDlss");
  G_UNUSED(cmd);
#if USE_STREAMLINE_FOR_DLSS
  const nv::DlssParams<Image> &params = cmd.dlssParams;

  const VkImageLayout inColorState = prepareImage(*this, params.inColor);
  const VkImageLayout inDepthState = prepareImage(*this, params.inDepth);
  const VkImageLayout inMotionVectorsState = prepareImage(*this, params.inMotionVectors);
  const VkImageLayout inExposureState = prepareImage(*this, params.inExposure);
  const VkImageLayout inAlbedoState = prepareImage(*this, params.inAlbedo);
  const VkImageLayout inSpecularAlbedoState = prepareImage(*this, params.inSpecularAlbedo);
  const VkImageLayout inNormalRoughnessState = prepareImage(*this, params.inNormalRoughness);
  const VkImageLayout inHitDistState = prepareImage(*this, params.inHitDist);
  const VkImageLayout inSsssGuideState = prepareImage(*this, params.inSsssGuide);
  const VkImageLayout inColorBeforeTransparencyState = prepareImage(*this, params.inColorBeforeTransparency);
  const VkImageLayout outColorState = prepareImage(*this, params.outColor, true);

  auto tColor = to_image_tuple(params.inColor);
  auto tDepth = to_image_tuple(params.inDepth);
  auto tMotionVectors = to_image_tuple(params.inMotionVectors);
  auto tExposure = to_image_tuple(params.inExposure);
  auto tAlbedo = to_image_tuple(params.inAlbedo);
  auto tSpecularAlbedo = to_image_tuple(params.inSpecularAlbedo);
  auto tNormalRoughness = to_image_tuple(params.inNormalRoughness);
  auto tHitDist = to_image_tuple(params.inHitDist);
  auto tSsssGuide = to_image_tuple(params.inSsssGuide);
  auto tColorBeforeTransparency = to_image_tuple(params.inColorBeforeTransparency);
  auto tOutColor = to_image_tuple(params.outColor, true);

  auto args = nv::convertDlssParams(params, [](Image *src) -> void * { return src; });
  args.inColor = &tColor;
  args.inColorState = inColorState;
  args.inDepth = &tDepth;
  args.inDepthState = inDepthState;
  args.inMotionVectors = &tMotionVectors;
  args.inMotionVectorsState = inMotionVectorsState;
  args.inExposure = ptr_or_null(tExposure);
  args.inExposureState = inExposureState;
  args.inAlbedo = &tAlbedo;
  args.inAlbedoState = inAlbedoState;
  args.inSpecularAlbedo = &tSpecularAlbedo;
  args.inSpecularAlbedoState = inSpecularAlbedoState;
  args.inNormalRoughness = &tNormalRoughness;
  args.inNormalRoughnessState = inNormalRoughnessState;
  args.inHitDist = &tHitDist;
  args.inHitDistState = inHitDistState;
  args.inSsssGuide = ptr_or_null(tSsssGuide);
  args.inSsssGuideState = inSsssGuideState;
  args.inColorBeforeTransparency = ptr_or_null(tColorBeforeTransparency);
  args.inColorBeforeTransparencyState = inColorBeforeTransparencyState;
  args.outColor = &tOutColor;
  args.outColorState = outColorState;

  Backend::sync.completeNeeded();

  Globals::VK::loader.streamlineAdapter->getDlssFeature(cmd.viewIndex)->evaluate(args, frameCore);

  // DLSS modifies state in command buffer, so we must setup it back,
  // same way as if command buffer was interrupted
  onFrameCoreReset();
#endif
}

TSPEC void BEContext::execCmd(const CmdExecuteStreamlineDLSSG &cmd)
{
  beginCustomStage("executeStreamlineDlssG");
  G_UNUSED(cmd);
#if USE_STREAMLINE_FOR_DLSS
  const nv::DlssGParams<Image> &params = cmd.dlssGParams;

  const VkImageLayout inHUDlessState = prepareImage(*this, params.inHUDless);
  const VkImageLayout inUIState = prepareImage(*this, params.inUI);
  const VkImageLayout inDepthState = prepareImage(*this, params.inDepth);
  const VkImageLayout inMotionVectorsState = prepareImage(*this, params.inMotionVectors);

  auto tHUDless = to_image_tuple(params.inHUDless);
  auto tUI = to_image_tuple(params.inUI);
  auto tDepth = to_image_tuple(params.inDepth);
  auto tMotionVectors = to_image_tuple(params.inMotionVectors);

  auto args = nv::convertDlssGParams(params, [](Image *src) -> void * { return src; });
  args.inHUDless = &tHUDless;
  args.inHUDlessState = inHUDlessState;
  args.inUI = &tUI;
  args.inUIState = inUIState;
  args.inDepth = &tDepth;
  args.inDepthState = inDepthState;
  args.inMotionVectors = &tMotionVectors;
  args.inMotionVectorsState = inMotionVectorsState;

  Backend::sync.completeNeeded();

  DLSSFrameGeneration *dlss =
    static_cast<DLSSFrameGeneration *>(Globals::VK::loader.streamlineAdapter->getDlssGFeature(cmd.viewIndex));
  dlss->evaluate(args, frameCore);

  // DLSS modifies state in command buffer, so we must setup it back,
  // same way as if command buffer was interrupted
  onFrameCoreReset();
#endif
}

TSPEC void BEContext::execCmd(const CmdSetDlssGEnabled &cmd)
{
  G_UNUSED(cmd);
#if USE_STREAMLINE_FOR_DLSS
  if (auto *dlss = static_cast<DLSSFrameGeneration *>(Globals::VK::loader.streamlineAdapter->getDlssGFeature(cmd.viewIndex)))
    dlss->setEnabled(cmd.framesToGenerate);
#endif
}

TSPEC void BEContext::execCmd(const CmdSetDlssOptions &cmd)
{
  G_UNUSED(cmd);
#if USE_STREAMLINE_FOR_DLSS
  if (auto *dlss = static_cast<DLSSWithSizeQuery *>(Globals::VK::loader.streamlineAdapter->getDlssFeature(cmd.viewIndex)))
    dlss->setOptions(cmd.options.mode, cmd.options.outputResolution, cmd.options.useRayReconstruction, cmd.options.useLegacyModel);
#endif
}

TSPEC void BEContext::execCmd(const CmdExecuteStreamlineDLSSNR &cmd)
{
  beginCustomStage("executeStreamlineDlssNR");
  G_UNUSED(cmd);
#if USE_STREAMLINE_FOR_DLSS
  const nv::DlssNRParams<Image> &params = cmd.dlssNRParams;

  const VkImageLayout inColorState = prepareImage(*this, params.inColor);
  const VkImageLayout inDepthState = prepareImage(*this, params.inDepth);
  const VkImageLayout inMotionVectorsState = prepareImage(*this, params.inMotionVectors);
  const VkImageLayout inControlMaskState = prepareImage(*this, params.inControlMask);
  const VkImageLayout outColorState = prepareImage(*this, params.outColor, true);

  auto tColor = to_image_tuple(params.inColor);
  auto tDepth = to_image_tuple(params.inDepth);
  auto tMotionVectors = to_image_tuple(params.inMotionVectors);
  auto tControlMask = to_image_tuple(params.inControlMask);
  auto tOutColor = to_image_tuple(params.outColor, true);

  auto args = nv::convertDlssNRParams(params, [](Image *src) -> void * { return src; });
  args.inColor = &tColor;
  args.inColorState = inColorState;
  args.inDepth = &tDepth;
  args.inDepthState = inDepthState;
  args.inMotionVectors = &tMotionVectors;
  args.inMotionVectorsState = inMotionVectorsState;
  args.inControlMask = ptr_or_null(tControlMask);
  args.inControlMaskState = inControlMaskState;
  args.outColor = &tOutColor;
  args.outColorState = outColorState;

  Backend::sync.completeNeeded();

  if (auto *nr = Globals::VK::loader.streamlineAdapter->getDlssNRFeature(cmd.viewIndex))
    nr->evaluate(args, frameCore);

  // DLSS modifies state in command buffer, so we must setup it back,
  // same way as if command buffer was interrupted
  onFrameCoreReset();
#endif
}

TSPEC void BEContext::execCmd(const CmdSetDlssNROptions &cmd)
{
  G_UNUSED(cmd);
#if USE_STREAMLINE_FOR_DLSS
  if (auto *nr = Globals::VK::loader.streamlineAdapter->getDlssNRFeature(cmd.viewIndex))
    nr->setOptions(cmd.options);
#endif
}

TSPEC void BEContext::execCmd(const CmdExecuteXESS &cmd)
{
  beginCustomStage("executeXESS");

  prepareImage(*this, cmd.inColor);
  prepareImage(*this, cmd.inDepth);
  prepareImage(*this, cmd.inMotionVectors);
  prepareImage(*this, cmd.outColor, true);
  Backend::sync.completeNeeded();

  Globals::xess.evaluate(cmd);

  // XESS modifies state in command buffer, so we must setup it back,
  // same way as if command buffer was interrupted
  onFrameCoreReset();
}