#include "nstv/deko3d_video_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavutil/hwcontext_nvtegra.h>
#include <libavutil/pixfmt.h>
}
#endif

#ifdef __SWITCH__
#include <deko3d.hpp>
#include <switch.h>
#endif

namespace nstv {

namespace {

constexpr uint32_t FramebufferCount = 2;
constexpr uint32_t ScreenWidth = 1280;
constexpr uint32_t ScreenHeight = 720;
constexpr uint32_t CodeMemSize = 128 * 1024;
constexpr uint32_t CmdMemSize = 64 * 1024;
constexpr uint32_t DescriptorMemSize = 4 * 1024;

uint32_t alignUp(uint32_t value, uint32_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

#ifdef __SWITCH__
bool loadShader(dk::Device device, dk::MemBlock codeMem, uint32_t &codeOffset, dk::Shader &shader, const char *path, std::string &error) {
  FILE *file = std::fopen(path, "rb");

  if (!file) {
    error = std::string("could not open shader: ") + path;
    return false;
  }

  std::fseek(file, 0, SEEK_END);
  const long sizeLong = std::ftell(file);
  std::rewind(file);

  if (sizeLong <= 0) {
    std::fclose(file);
    error = std::string("invalid shader size: ") + path;
    return false;
  }

  const uint32_t size = static_cast<uint32_t>(sizeLong);
  const uint32_t offset = alignUp(codeOffset, DK_SHADER_CODE_ALIGNMENT);
  const uint32_t nextOffset = alignUp(offset + size, DK_SHADER_CODE_ALIGNMENT);

  if (nextOffset > CodeMemSize) {
    std::fclose(file);
    error = "Deko3D code memory is too small for shaders.";
    return false;
  }

  uint8_t *dst = static_cast<uint8_t *>(codeMem.getCpuAddr()) + offset;
  const size_t read = std::fread(dst, 1, size, file);
  std::fclose(file);

  if (read != size) {
    error = std::string("could not read shader: ") + path;
    return false;
  }

  (void)device;
  dk::ShaderMaker{codeMem, offset}.initialize(shader);

  if (!shader.isValid()) {
    error = std::string("invalid shader: ") + path;
    return false;
  }

  codeOffset = nextOffset;
  return true;
}
#endif

} // namespace

#ifdef __SWITCH__
struct Deko3dVideoRenderer::SwitchState {
  dk::Device device;
  dk::Queue queue;
  dk::CmdBuf cmdBuf;

  dk::MemBlock framebufferMem;
  dk::MemBlock codeMem;
  dk::MemBlock cmdMem;
  dk::MemBlock descriptorMem;

  dk::Image framebuffers[FramebufferCount];
  dk::Swapchain swapchain;

  dk::Shader vertexShader;
  dk::Shader fragmentShader;

  uint32_t framebufferSize = 0;
  uint32_t codeOffset = 0;
};
#endif

Deko3dVideoRenderer::~Deko3dVideoRenderer() {
  shutdown();
}

bool Deko3dVideoRenderer::initialize() {
  if (initialized_) {
    return true;
  }

  error_.clear();

#if !defined(NSTV_ENABLE_DEKO3D_DIRECT_IMPORT)
  error_ =
    "Deko3D direct NVTEGRA import is disabled: libnx/deko3d does not expose "
    "a safe NvMap import path for FFmpeg decoder surfaces yet.";
  return false;
#endif

#ifndef __SWITCH__
  error_ = "Deko3D renderer is only available on Switch.";
  return false;
#else
  switchState_ = new SwitchState();
  switchState_->device = dk::DeviceMaker{}.create();

  if (!switchState_->device) {
    error_ = "dk::DeviceMaker failed.";
    shutdown();
    return false;
  }

  switchState_->queue = dk::QueueMaker{switchState_->device}
    .setFlags(DkQueueFlags_Graphics)
    .create();

  if (!switchState_->queue) {
    error_ = "dk::QueueMaker failed.";
    shutdown();
    return false;
  }

  dk::ImageLayout framebufferLayout;
  dk::ImageLayoutMaker{switchState_->device}
    .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
    .setFormat(DkImageFormat_RGBA8_Unorm)
    .setDimensions(ScreenWidth, ScreenHeight)
    .initialize(framebufferLayout);

  switchState_->framebufferSize = alignUp(
    static_cast<uint32_t>(framebufferLayout.getSize()),
    framebufferLayout.getAlignment()
  );

  switchState_->framebufferMem = dk::MemBlockMaker{
      switchState_->device,
      FramebufferCount * switchState_->framebufferSize
    }
    .setFlags(DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
    .create();

  if (!switchState_->framebufferMem) {
    error_ = "could not allocate Deko3D framebuffers.";
    shutdown();
    return false;
  }

  std::array<DkImage const *, FramebufferCount> swapchainImages{};

  for (uint32_t i = 0; i < FramebufferCount; ++i) {
    switchState_->framebuffers[i].initialize(
      framebufferLayout,
      switchState_->framebufferMem,
      i * switchState_->framebufferSize
    );
    swapchainImages[i] = &switchState_->framebuffers[i];
  }

  switchState_->swapchain = dk::SwapchainMaker{
      switchState_->device,
      nwindowGetDefault(),
      swapchainImages
    }
    .create();

  if (!switchState_->swapchain) {
    error_ = "dk::SwapchainMaker failed.";
    shutdown();
    return false;
  }

  switchState_->codeMem = dk::MemBlockMaker{switchState_->device, CodeMemSize}
    .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code)
    .create();
  switchState_->cmdMem = dk::MemBlockMaker{switchState_->device, CmdMemSize}
    .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
    .create();
  switchState_->descriptorMem = dk::MemBlockMaker{switchState_->device, DescriptorMemSize}
    .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
    .create();

  if (!switchState_->codeMem || !switchState_->cmdMem || !switchState_->descriptorMem) {
    error_ = "could not allocate Deko3D command/shader memory.";
    shutdown();
    return false;
  }

  if (!loadShader(
        switchState_->device,
        switchState_->codeMem,
        switchState_->codeOffset,
        switchState_->vertexShader,
        "romfs:/shaders/nv12_video_vsh.dksh",
        error_
      ) ||
      !loadShader(
        switchState_->device,
        switchState_->codeMem,
        switchState_->codeOffset,
        switchState_->fragmentShader,
        "romfs:/shaders/nv12_video_fsh.dksh",
        error_
      )) {
    shutdown();
    return false;
  }

  switchState_->cmdBuf = dk::CmdBufMaker{switchState_->device}.create();

  if (!switchState_->cmdBuf) {
    error_ = "dk::CmdBufMaker failed.";
    shutdown();
    return false;
  }

  switchState_->cmdBuf.addMemory(switchState_->cmdMem, 0, CmdMemSize);

  initialized_ = true;
  std::printf("[KBORE][DEKO3D] initialized NVTEGRA renderer\n");
  return true;
#endif
}

void Deko3dVideoRenderer::shutdown() {
#ifdef __SWITCH__
  if (switchState_) {
    if (switchState_->queue) {
      switchState_->queue.waitIdle();
    }

    if (switchState_->cmdBuf) switchState_->cmdBuf.destroy();
    if (switchState_->swapchain) switchState_->swapchain.destroy();
    if (switchState_->descriptorMem) switchState_->descriptorMem.destroy();
    if (switchState_->cmdMem) switchState_->cmdMem.destroy();
    if (switchState_->codeMem) switchState_->codeMem.destroy();
    if (switchState_->framebufferMem) switchState_->framebufferMem.destroy();
    if (switchState_->queue) switchState_->queue.destroy();
    if (switchState_->device) switchState_->device.destroy();

    delete switchState_;
    switchState_ = nullptr;
  }
#endif

  initialized_ = false;
}

#ifdef NSTV_USE_FFMPEG
bool Deko3dVideoRenderer::canRender(const AVFrame *frame) const {
#if !defined(NSTV_ENABLE_DEKO3D_DIRECT_IMPORT)
  (void)frame;
  return false;
#else
  if (!initialized_ || !frame || frame->format != AV_PIX_FMT_NVTEGRA) {
    return false;
  }

  if (!frame->buf[0] || !frame->data[0] || !frame->data[1]) {
    return false;
  }

  return frame->width > 0 && frame->height > 0 && frame->linesize[0] > 0;
#endif
}

bool Deko3dVideoRenderer::renderFrame(const AVFrame *frame) {
#if !defined(NSTV_ENABLE_DEKO3D_DIRECT_IMPORT)
  (void)frame;
  error_ =
    "Deko3D direct NVTEGRA import is disabled to avoid wrapping FFmpeg NvMap "
    "storage as a Deko3D MemBlock.";
  return false;
#else
#ifndef __SWITCH__
  (void)frame;
  error_ = "Deko3D renderer is only available on Switch.";
  return false;
#else
  if (!canRender(frame)) {
    error_ = "frame is not a renderable NVTEGRA frame.";
    return false;
  }

  AVNVTegraMap *map = av_nvtegra_frame_get_fbuf_map(frame);

  if (!map) {
    error_ = "NVTEGRA frame has no backing map.";
    return false;
  }

  void *mapAddr = av_nvtegra_map_get_addr(map);
  const uint32_t mapSize = av_nvtegra_map_get_size(map);

  if (!mapAddr || mapSize == 0) {
    error_ = "NVTEGRA frame map is not CPU-visible.";
    return false;
  }

  dk::MemBlock frameMem = dk::MemBlockMaker{switchState_->device, alignUp(mapSize, 0x1000)}
    .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
    .setStorage(mapAddr)
    .create();

  if (!frameMem) {
    error_ = "could not wrap NVTEGRA map as Deko3D memory.";
    return false;
  }

  const uint32_t yWidth = static_cast<uint32_t>(std::max(frame->linesize[0], frame->width));
  const uint32_t yHeight = static_cast<uint32_t>(alignUp(frame->height, 32));
  const uint32_t uvOffset = static_cast<uint32_t>(frame->data[1] - frame->data[0]);
  const uint32_t uvWidth = static_cast<uint32_t>(std::max(frame->linesize[1] / 2, frame->width / 2));
  const uint32_t uvHeight = static_cast<uint32_t>(alignUp((frame->height + 1) / 2, 16));

  dk::ImageLayout yLayout;
  dk::ImageLayout uvLayout;

  dk::ImageLayoutMaker{switchState_->device}
    .setFlags(DkImageFlags_CustomTileSize | DkImageFlags_UsageVideo)
    .setFormat(DkImageFormat_R8_Unorm)
    .setDimensions(yWidth, yHeight)
    .setTileSize(DkTileSize_TwoGobs)
    .initialize(yLayout);

  dk::ImageLayoutMaker{switchState_->device}
    .setFlags(DkImageFlags_CustomTileSize | DkImageFlags_UsageVideo)
    .setFormat(DkImageFormat_RG8_Unorm)
    .setDimensions(uvWidth, uvHeight)
    .setTileSize(DkTileSize_TwoGobs)
    .initialize(uvLayout);

  dk::Image yImage;
  dk::Image uvImage;
  yImage.initialize(yLayout, frameMem, 0);
  uvImage.initialize(uvLayout, frameMem, uvOffset);

  dk::ImageView yView{yImage};
  dk::ImageView uvView{uvImage};

  dk::ImageDescriptor yDescriptor;
  dk::ImageDescriptor uvDescriptor;
  yDescriptor.initialize(yView, false, false);
  uvDescriptor.initialize(uvView, false, false);

  dk::Sampler sampler;
  sampler.setFilter(DkFilter_Linear, DkFilter_Linear);
  sampler.setWrapMode(DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge);

  dk::SamplerDescriptor samplerDescriptor;
  samplerDescriptor.initialize(sampler);

  switchState_->cmdBuf.clear();
  switchState_->cmdBuf.addMemory(switchState_->cmdMem, 0, CmdMemSize);

  constexpr uint32_t imageDescriptorOffset = 0;
  constexpr uint32_t samplerDescriptorOffset = 2 * sizeof(DkImageDescriptor);

  switchState_->cmdBuf.pushData(
    switchState_->descriptorMem.getGpuAddr() + imageDescriptorOffset,
    &yDescriptor,
    sizeof(yDescriptor)
  );
  switchState_->cmdBuf.pushData(
    switchState_->descriptorMem.getGpuAddr() + imageDescriptorOffset + sizeof(DkImageDescriptor),
    &uvDescriptor,
    sizeof(uvDescriptor)
  );
  switchState_->cmdBuf.pushData(
    switchState_->descriptorMem.getGpuAddr() + samplerDescriptorOffset,
    &samplerDescriptor,
    sizeof(samplerDescriptor)
  );

  switchState_->cmdBuf.bindImageDescriptorSet(
    switchState_->descriptorMem.getGpuAddr() + imageDescriptorOffset,
    2
  );
  switchState_->cmdBuf.bindSamplerDescriptorSet(
    switchState_->descriptorMem.getGpuAddr() + samplerDescriptorOffset,
    1
  );

  const int slot = switchState_->queue.acquireImage(switchState_->swapchain);

  if (slot < 0 || slot >= static_cast<int>(FramebufferCount)) {
    frameMem.destroy();
    error_ = "Deko3D could not acquire a swapchain image.";
    return false;
  }

  dk::ImageView framebufferView{switchState_->framebuffers[slot]};
  DkImageView const *targets[] = {&framebufferView};
  dkCmdBufBindRenderTargets(switchState_->cmdBuf, targets, 1, nullptr);

  DkViewport viewport{0.0f, 0.0f, static_cast<float>(ScreenWidth), static_cast<float>(ScreenHeight), 0.0f, 1.0f};
  DkScissor scissor{0, 0, ScreenWidth, ScreenHeight};
  DkShader const *shaders[] = {&switchState_->vertexShader, &switchState_->fragmentShader};
  DkResHandle textureHandles[] = {
    dkMakeTextureHandle(0, 0),
    dkMakeTextureHandle(1, 0)
  };

  DkRasterizerState rasterizerState;
  DkColorState colorState;
  DkColorWriteState colorWriteState;
  dkRasterizerStateDefaults(&rasterizerState);
  dkColorStateDefaults(&colorState);
  dkColorWriteStateDefaults(&colorWriteState);

  switchState_->cmdBuf.setViewports(0, viewport);
  switchState_->cmdBuf.setScissors(0, scissor);
  dkCmdBufBindShaders(switchState_->cmdBuf, DkStageFlag_GraphicsMask, shaders, 2);
  switchState_->cmdBuf.bindRasterizerState(rasterizerState);
  switchState_->cmdBuf.bindColorState(colorState);
  switchState_->cmdBuf.bindColorWriteState(colorWriteState);
  dkCmdBufBindTextures(switchState_->cmdBuf, DkStage_Fragment, 0, textureHandles, 2);
  switchState_->cmdBuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);

  switchState_->queue.submitCommands(switchState_->cmdBuf.finishList());
  switchState_->queue.waitIdle();
  switchState_->queue.presentImage(switchState_->swapchain, slot);

  frameMem.destroy();
  error_.clear();
  return true;
#endif
#endif
}
#endif

#ifdef __SWITCH__
std::unique_ptr<INativeVideoRenderer> createDeko3dVideoRenderer() {
  return std::unique_ptr<INativeVideoRenderer>(new Deko3dVideoRenderer());
}
#endif

} // namespace nstv
