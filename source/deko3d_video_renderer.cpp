#include "nstv/deko3d_video_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef NSTV_USE_FFMPEG
extern "C" {
#include <libavutil/frame.h>
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
constexpr uint32_t FrameInFlightCount = 3;
constexpr uint32_t ScreenWidth = 1280;
constexpr uint32_t ScreenHeight = 720;
constexpr uint32_t CodeMemSize = 128 * 1024;
constexpr uint32_t CmdMemSize = 64 * 1024;
constexpr uint32_t DescriptorMemSize = 4 * 1024;
constexpr uint32_t UniformMemSize = 4 * 1024;
constexpr uint32_t PageSize = 0x1000;
constexpr uint32_t OverlayHeight = 104;
constexpr uint32_t OverlayWidth = ScreenWidth;
constexpr uint32_t OverlayPitch = OverlayWidth * 4;
constexpr uint32_t OverlayMemSize = OverlayPitch * OverlayHeight;

uint32_t alignUp(uint32_t value, uint32_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

#ifdef __SWITCH__
struct ColorCoefficients {
  float kr = 0.299f;
  float kb = 0.114f;
};

ColorCoefficients colorCoefficientsForFrame(const AVFrame *frame) {
  if (!frame) {
    return {};
  }

  switch (frame->colorspace) {
    case AVCOL_SPC_BT709:
      return {0.2126f, 0.0722f};
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
      return {0.2627f, 0.0593f};
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_SMPTE240M:
      return {0.299f, 0.114f};
    default:
      return frame->width >= 1280 || frame->height > 576
        ? ColorCoefficients{0.2126f, 0.0722f}
        : ColorCoefficients{0.299f, 0.114f};
  }
}

void setYuvCoefficients(float *r, float *g, float *b, const AVFrame *frame) {
  const ColorCoefficients coeff = colorCoefficientsForFrame(frame);
  const float kg = 1.0f - coeff.kr - coeff.kb;
  const bool fullRange = frame && frame->color_range == AVCOL_RANGE_JPEG;
  const float yScale = fullRange ? 1.0f : 255.0f / 219.0f;
  const float cScale = fullRange ? 1.0f : 255.0f / 224.0f;
  const float yOffset = fullRange ? 0.0f : 16.0f / 255.0f;
  const float uOffset = 0.5f;
  const float vOffset = 0.5f;
  const float rV = cScale * (2.0f - 2.0f * coeff.kr);
  const float bU = cScale * (2.0f - 2.0f * coeff.kb);
  const float gU = cScale * (coeff.kb * (2.0f - 2.0f * coeff.kb) / kg);
  const float gV = cScale * (coeff.kr * (2.0f - 2.0f * coeff.kr) / kg);

  r[0] = yScale;
  r[1] = 0.0f;
  r[2] = rV;
  r[3] = -yScale * yOffset - rV * vOffset;

  g[0] = yScale;
  g[1] = -gU;
  g[2] = -gV;
  g[3] = -yScale * yOffset + gU * uOffset + gV * vOffset;

  b[0] = yScale;
  b[1] = bU;
  b[2] = 0.0f;
  b[3] = -yScale * yOffset - bU * uOffset;
}

std::array<uint8_t, 7> glyph(char ch) {
  if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');

  switch (ch) {
    case 'A': return {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
    case 'B': return {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E};
    case 'C': return {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F};
    case 'D': return {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
    case 'E': return {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
    case 'F': return {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
    case 'G': return {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F};
    case 'H': return {0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
    case 'I': return {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F};
    case 'J': return {0x01,0x01,0x01,0x01,0x11,0x11,0x0E};
    case 'K': return {0x11,0x12,0x14,0x18,0x14,0x12,0x11};
    case 'L': return {0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
    case 'M': return {0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
    case 'N': return {0x11,0x19,0x15,0x13,0x11,0x11,0x11};
    case 'O': return {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
    case 'P': return {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
    case 'Q': return {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D};
    case 'R': return {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
    case 'S': return {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
    case 'T': return {0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
    case 'U': return {0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
    case 'V': return {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04};
    case 'W': return {0x11,0x11,0x11,0x15,0x15,0x1B,0x11};
    case 'X': return {0x11,0x0A,0x04,0x04,0x04,0x0A,0x11};
    case 'Y': return {0x11,0x0A,0x04,0x04,0x04,0x04,0x04};
    case 'Z': return {0x1F,0x02,0x04,0x08,0x10,0x10,0x1F};
    case '0': return {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};
    case '1': return {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};
    case '2': return {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};
    case '3': return {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E};
    case '4': return {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};
    case '5': return {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E};
    case '6': return {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E};
    case '7': return {0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
    case '8': return {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};
    case '9': return {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E};
    case ':': return {0x00,0x04,0x04,0x00,0x04,0x04,0x00};
    case '.': return {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C};
    case ',': return {0x00,0x00,0x00,0x00,0x04,0x04,0x08};
    case '-': return {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
    case '/': return {0x01,0x02,0x02,0x04,0x08,0x08,0x10};
    case '|': return {0x04,0x04,0x04,0x04,0x04,0x04,0x04};
    case '+': return {0x00,0x04,0x04,0x1F,0x04,0x04,0x00};
    case '(': return {0x02,0x04,0x08,0x08,0x08,0x04,0x02};
    case ')': return {0x08,0x04,0x02,0x02,0x02,0x04,0x08};
    case ' ': return {0x00,0x00,0x00,0x00,0x00,0x00,0x00};
    default: return {0x1F,0x11,0x05,0x02,0x05,0x11,0x1F};
  }
}

void putPixel(uint8_t *rgba, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  if (!rgba || x < 0 || y < 0 || x >= static_cast<int>(OverlayWidth) || y >= static_cast<int>(OverlayHeight)) {
    return;
  }

  uint8_t *pixel = rgba + y * OverlayPitch + x * 4;
  pixel[0] = r;
  pixel[1] = g;
  pixel[2] = b;
  pixel[3] = a;
}

void fillRect(uint8_t *rgba, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) {
      putPixel(rgba, xx, yy, r, g, b, a);
    }
  }
}

std::string fitText(const std::string &text, std::size_t maxChars) {
  std::string out;
  out.reserve(std::min(text.size(), maxChars));

  for (unsigned char ch : text) {
    if (ch >= 0x20 && ch < 0x7f) {
      out.push_back(static_cast<char>(ch));
    }
  }

  if (out.size() <= maxChars) {
    return out;
  }

  if (maxChars <= 3) {
    return out.substr(0, maxChars);
  }

  return out.substr(0, maxChars - 3) + "...";
}

void drawText(uint8_t *rgba, const std::string &text, int x, int y, int scale, uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool bold) {
  int cursor = x;

  for (char ch : text) {
    const auto bits = glyph(ch);

    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (!(bits[static_cast<std::size_t>(row)] & (1 << (4 - col)))) {
          continue;
        }

        fillRect(rgba, cursor + col * scale, y + row * scale, scale + (bold ? 1 : 0), scale, r, g, b, a);
      }
    }

    cursor += 6 * scale;
  }
}

void buildOverlayBitmap(
  uint8_t *rgba,
  const std::string &title,
  const std::string &subtitle,
  const std::string &status,
  const std::string &controls
) {
  if (!rgba) {
    return;
  }

  std::memset(rgba, 0, OverlayMemSize);
  fillRect(rgba, 0, 0, OverlayWidth, OverlayHeight, 0, 0, 0, 166);
  fillRect(rgba, 24, 16, 76, 58, 28, 38, 67, 230);
  fillRect(rgba, 29, 21, 66, 48, 8, 13, 26, 210);

  std::string initials;
  for (char ch : fitText(title, 24)) {
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
      initials.push_back(ch);
      if (initials.size() >= 2) {
        break;
      }
    }
  }

  if (initials.empty()) {
    initials = "TV";
  }

  drawText(rgba, initials, 42, 37, 3, 165, 190, 230, 255, true);
  drawText(rgba, fitText(title, 58), 122, 18, 2, 248, 250, 252, 255, true);
  drawText(rgba, fitText(subtitle, 98), 122, 46, 1, 150, 163, 190, 255, false);
  drawText(rgba, fitText(status, 24), 122, 72, 1, 57, 220, 35, 255, true);

  const std::string fittedControls = fitText(controls, 34);
  const int controlsWidth = static_cast<int>(fittedControls.size()) * 6 * 1;
  drawText(
    rgba,
    fittedControls,
    static_cast<int>(OverlayWidth) - 28 - controlsWidth,
    54,
    1,
    248,
    250,
    252,
    255,
    true
  );
}

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
  struct VideoParams {
    float yScaleX = 1.0f;
    float yScaleY = 1.0f;
    float uvScaleX = 1.0f;
    float uvScaleY = 1.0f;
    float coeffR[4] = {1.16438356f, 0.0f, 1.59602678f, -0.8707854f};
    float coeffG[4] = {1.16438356f, -0.39176229f, -0.81296764f, 0.529593f};
    float coeffB[4] = {1.16438356f, 2.01723214f, 0.0f, -1.08139f};
  };

  struct ExternalFrameMem {
    void *addr = nullptr;
    uint32_t size = 0;
    dk::MemBlock mem;
  };

  struct FrameSlot {
    dk::Fence fence;
    bool submitted = false;
    AVFrame *retainedFrame = nullptr;
  };

  dk::Device device;
  dk::Queue queue;
  dk::CmdBuf cmdBuf;

  dk::MemBlock framebufferMem;
  dk::MemBlock codeMem;
  dk::MemBlock cmdMem;
  dk::MemBlock descriptorMem;
  dk::MemBlock uniformMem;
  dk::MemBlock overlayMem;

  dk::Image framebuffers[FramebufferCount];
  dk::Image overlayImage;
  dk::Swapchain swapchain;

  dk::Shader vertexShader;
  dk::Shader fragmentShader;
  dk::Shader overlayVertexShader;
  dk::Shader overlayFragmentShader;

  uint32_t framebufferSize = 0;
  uint32_t codeOffset = 0;
  VideoParams videoParams;
  dk::SamplerDescriptor samplerDescriptor;
  dk::RasterizerState rasterizerState;
  dk::ColorState videoColorState;
  dk::ColorWriteState colorWriteState;
  dk::BlendState overlayBlendState;
  dk::ColorState overlayColorState;
  bool overlayReady = false;
  uint32_t nextFrameSlot = 0;

  std::vector<ExternalFrameMem> externalFrameMems;
  std::array<FrameSlot, FrameInFlightCount> frameSlots;

  void waitForFrameSlot(FrameSlot &slot) {
    if (!slot.submitted) {
      return;
    }

    slot.fence.wait();
    slot.submitted = false;

    if (slot.retainedFrame) {
      av_frame_free(&slot.retainedFrame);
    }
  }

  void waitForInFlightFrames() {
    for (FrameSlot &slot : frameSlots) {
      waitForFrameSlot(slot);
    }
  }

  dk::MemBlock frameMemFor(void *addr, uint32_t size) {
    for (const ExternalFrameMem &item : externalFrameMems) {
      if (item.addr == addr && item.size == size) {
        return item.mem;
      }
    }

    dk::MemBlock mem = dk::MemBlockMaker{device, alignUp(size, PageSize)}
      .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
      .setStorage(addr)
      .create();

    if (mem) {
      externalFrameMems.push_back({addr, size, mem});
    }

    return mem;
  }

  bool initializeOverlay(std::string &error) {
    if (overlayReady) {
      return true;
    }

    overlayMem = dk::MemBlockMaker{device, OverlayMemSize}
      .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
      .create();

    if (!overlayMem) {
      error = "could not allocate Deko3D overlay memory.";
      return false;
    }

    dk::ImageLayout overlayLayout;
    dk::ImageLayoutMaker{device}
      .setFlags(DkImageFlags_PitchLinear)
      .setFormat(DkImageFormat_RGBA8_Unorm)
      .setDimensions(OverlayWidth, OverlayHeight)
      .setPitchStride(OverlayPitch)
      .initialize(overlayLayout);

    overlayImage.initialize(overlayLayout, overlayMem, 0);
    overlayReady = true;
    return true;
  }
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
  switchState_->cmdMem = dk::MemBlockMaker{switchState_->device, CmdMemSize * FrameInFlightCount}
    .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
    .create();
  switchState_->descriptorMem = dk::MemBlockMaker{switchState_->device, DescriptorMemSize * FrameInFlightCount}
    .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
    .create();
  switchState_->uniformMem = dk::MemBlockMaker{switchState_->device, UniformMemSize * FrameInFlightCount}
    .setFlags(DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
    .create();

  if (!switchState_->codeMem || !switchState_->cmdMem || !switchState_->descriptorMem || !switchState_->uniformMem) {
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
      ) ||
      !loadShader(
        switchState_->device,
        switchState_->codeMem,
        switchState_->codeOffset,
        switchState_->overlayVertexShader,
        "romfs:/shaders/overlay_vsh.dksh",
        error_
      ) ||
      !loadShader(
        switchState_->device,
        switchState_->codeMem,
        switchState_->codeOffset,
        switchState_->overlayFragmentShader,
        "romfs:/shaders/overlay_fsh.dksh",
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

  dk::Sampler sampler;
  sampler.setFilter(DkFilter_Linear, DkFilter_Linear);
  sampler.setWrapMode(DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge);
  switchState_->samplerDescriptor.initialize(sampler);
  switchState_->overlayColorState.setBlendEnable(0, true);

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

    switchState_->waitForInFlightFrames();

    for (auto &item : switchState_->externalFrameMems) {
      if (item.mem) {
        item.mem.destroy();
      }
    }
    switchState_->externalFrameMems.clear();

    if (switchState_->cmdBuf) switchState_->cmdBuf.destroy();
    if (switchState_->swapchain) switchState_->swapchain.destroy();
    if (switchState_->overlayMem) switchState_->overlayMem.destroy();
    if (switchState_->uniformMem) switchState_->uniformMem.destroy();
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

void Deko3dVideoRenderer::setOverlayInfo(
  const std::string &title,
  const std::string &subtitle,
  const std::string &status,
  const std::string &controls
) {
  if (
    overlayTitle_ == title &&
    overlaySubtitle_ == subtitle &&
    overlayStatus_ == status &&
    overlayControls_ == controls
  ) {
    return;
  }

  overlayTitle_ = title;
  overlaySubtitle_ = subtitle;
  overlayStatus_ = status;
  overlayControls_ = controls;
  overlayDirty_ = true;
}

#ifdef NSTV_USE_FFMPEG
bool Deko3dVideoRenderer::canRender(const AVFrame *frame) const {
  if (!initialized_ || !frame || frame->format != AV_PIX_FMT_NVTEGRA) {
    return false;
  }

  if (!frame->buf[0] || !frame->data[0] || !frame->data[1]) {
    return false;
  }

  return frame->width > 0 && frame->height > 0 && frame->linesize[0] > 0;
}

bool Deko3dVideoRenderer::renderFrame(const AVFrame *frame) {
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

  const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(mapAddr);
  const uintptr_t yAddr = reinterpret_cast<uintptr_t>(frame->data[0]);
  const uintptr_t uvAddr = reinterpret_cast<uintptr_t>(frame->data[1]);

  if (
    yAddr < baseAddr ||
    uvAddr < baseAddr ||
    yAddr >= baseAddr + mapSize ||
    uvAddr >= baseAddr + mapSize ||
    uvAddr <= yAddr
  ) {
    error_ = "NVTEGRA frame planes are outside the backing map.";
    return false;
  }

  dk::MemBlock frameMem = switchState_->frameMemFor(mapAddr, mapSize);

  if (!frameMem) {
    error_ = "could not wrap NVTEGRA map as Deko3D memory.";
    return false;
  }

  const uint32_t yWidth = static_cast<uint32_t>(std::max(frame->linesize[0], frame->width));
  const uint32_t yHeight = static_cast<uint32_t>(alignUp(frame->height, 32));
  const uint32_t yOffset = static_cast<uint32_t>(yAddr - baseAddr);
  const uint32_t uvOffset = static_cast<uint32_t>(uvAddr - baseAddr);
  const uint32_t uvWidth = static_cast<uint32_t>(std::max(frame->linesize[1] / 2, frame->width / 2));
  const uint32_t uvHeight = static_cast<uint32_t>(alignUp((frame->height + 1) / 2, 16));
  const bool pitchLinear = map->is_linear;
  const uint32_t visibleYWidth = static_cast<uint32_t>(std::max(frame->width, 1));
  const uint32_t visibleYHeight = static_cast<uint32_t>(std::max(frame->height, 1));
  const uint32_t visibleUvWidth = static_cast<uint32_t>(std::max((frame->width + 1) / 2, 1));
  const uint32_t visibleUvHeight = static_cast<uint32_t>(std::max((frame->height + 1) / 2, 1));

  dk::ImageLayout yLayout;
  dk::ImageLayout uvLayout;

  auto yLayoutMaker = dk::ImageLayoutMaker{switchState_->device}
    .setFlags(
      (pitchLinear ? DkImageFlags_PitchLinear : DkImageFlags_CustomTileSize) |
      DkImageFlags_UsageVideo
    )
    .setFormat(DkImageFormat_R8_Unorm)
    .setDimensions(yWidth, yHeight);

  auto uvLayoutMaker = dk::ImageLayoutMaker{switchState_->device}
    .setFlags(
      (pitchLinear ? DkImageFlags_PitchLinear : DkImageFlags_CustomTileSize) |
      DkImageFlags_UsageVideo
    )
    .setFormat(DkImageFormat_RG8_Unorm)
    .setDimensions(uvWidth, uvHeight);

  if (pitchLinear) {
    yLayoutMaker.setPitchStride(static_cast<uint32_t>(frame->linesize[0]));
    uvLayoutMaker.setPitchStride(static_cast<uint32_t>(frame->linesize[1]));
  } else {
    yLayoutMaker.setTileSize(DkTileSize_TwoGobs);
    uvLayoutMaker.setTileSize(DkTileSize_TwoGobs);
  }

  yLayoutMaker.initialize(yLayout);
  uvLayoutMaker.initialize(uvLayout);

  dk::Image yImage;
  dk::Image uvImage;
  yImage.initialize(yLayout, frameMem, yOffset);
  uvImage.initialize(uvLayout, frameMem, uvOffset);

  dk::ImageView yView{yImage};
  dk::ImageView uvView{uvImage};

  dk::ImageDescriptor yDescriptor;
  dk::ImageDescriptor uvDescriptor;
  yDescriptor.initialize(yView, false, false);
  uvDescriptor.initialize(uvView, false, false);

  switchState_->videoParams.yScaleX = static_cast<float>(visibleYWidth) / static_cast<float>(yWidth);
  switchState_->videoParams.yScaleY = static_cast<float>(visibleYHeight) / static_cast<float>(yHeight);
  switchState_->videoParams.uvScaleX = static_cast<float>(visibleUvWidth) / static_cast<float>(uvWidth);
  switchState_->videoParams.uvScaleY = static_cast<float>(visibleUvHeight) / static_cast<float>(uvHeight);
  setYuvCoefficients(
    switchState_->videoParams.coeffR,
    switchState_->videoParams.coeffG,
    switchState_->videoParams.coeffB,
    frame
  );

  SwitchState::FrameSlot &frameSlot = switchState_->frameSlots[switchState_->nextFrameSlot];
  switchState_->waitForFrameSlot(frameSlot);

  switchState_->cmdBuf.clear();
  switchState_->cmdBuf.addMemory(
    switchState_->cmdMem,
    switchState_->nextFrameSlot * CmdMemSize,
    CmdMemSize
  );

  constexpr uint32_t imageDescriptorOffset = 0;
  constexpr uint32_t overlayDescriptorOffset = 2 * sizeof(DkImageDescriptor);
  constexpr uint32_t samplerDescriptorOffset = 3 * sizeof(DkImageDescriptor);
  const DkGpuAddr descriptorBase =
    switchState_->descriptorMem.getGpuAddr() + switchState_->nextFrameSlot * DescriptorMemSize;
  const DkGpuAddr uniformBase =
    switchState_->uniformMem.getGpuAddr() + switchState_->nextFrameSlot * UniformMemSize;

  switchState_->cmdBuf.pushData(
    descriptorBase + imageDescriptorOffset,
    &yDescriptor,
    sizeof(yDescriptor)
  );
  switchState_->cmdBuf.pushData(
    descriptorBase + imageDescriptorOffset + sizeof(DkImageDescriptor),
    &uvDescriptor,
    sizeof(uvDescriptor)
  );
  switchState_->cmdBuf.pushData(
    descriptorBase + samplerDescriptorOffset,
    &switchState_->samplerDescriptor,
    sizeof(switchState_->samplerDescriptor)
  );

  switchState_->cmdBuf.bindImageDescriptorSet(
    descriptorBase + imageDescriptorOffset,
    3
  );
  switchState_->cmdBuf.bindSamplerDescriptorSet(
    descriptorBase + samplerDescriptorOffset,
    1
  );
  switchState_->cmdBuf.pushConstants(
    uniformBase,
    UniformMemSize,
    0,
    sizeof(switchState_->videoParams),
    &switchState_->videoParams
  );
  switchState_->cmdBuf.bindUniformBuffer(
    DkStage_Vertex,
    0,
    uniformBase,
    UniformMemSize
  );
  switchState_->cmdBuf.bindUniformBuffer(
    DkStage_Fragment,
    0,
    uniformBase,
    UniformMemSize
  );

  const int slot = switchState_->queue.acquireImage(switchState_->swapchain);

  if (slot < 0 || slot >= static_cast<int>(FramebufferCount)) {
    error_ = "Deko3D could not acquire a swapchain image.";
    return false;
  }

  dk::ImageView framebufferView{switchState_->framebuffers[slot]};

  DkViewport viewport{0.0f, 0.0f, static_cast<float>(ScreenWidth), static_cast<float>(ScreenHeight), 0.0f, 1.0f};
  DkScissor scissor{0, 0, ScreenWidth, ScreenHeight};
  std::array<DkShader const *, 2> shaders = {
    &switchState_->vertexShader,
    &switchState_->fragmentShader
  };
  std::array<DkResHandle, 2> textureHandles = {
    dkMakeTextureHandle(0, 0),
    dkMakeTextureHandle(1, 0)
  };

  switchState_->cmdBuf.bindRenderTargets(&framebufferView);
  switchState_->cmdBuf.setViewports(0, viewport);
  switchState_->cmdBuf.setScissors(0, scissor);
  switchState_->cmdBuf.clearColor(0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);
  switchState_->cmdBuf.bindShaders(DkStageFlag_GraphicsMask, shaders);
  switchState_->cmdBuf.bindRasterizerState(switchState_->rasterizerState);
  switchState_->cmdBuf.bindColorState(switchState_->videoColorState);
  switchState_->cmdBuf.bindColorWriteState(switchState_->colorWriteState);
  switchState_->cmdBuf.bindTextures(DkStage_Fragment, 0, textureHandles);
  switchState_->cmdBuf.draw(DkPrimitive_Triangles, 3, 1, 0, 0);

  if (overlayVisible_) {
    if (!switchState_->initializeOverlay(error_)) {
      return false;
    }

    if (overlayDirty_) {
      switchState_->waitForInFlightFrames();
      buildOverlayBitmap(
        static_cast<uint8_t *>(switchState_->overlayMem.getCpuAddr()),
        overlayTitle_,
        overlaySubtitle_,
        overlayStatus_,
        overlayControls_
      );
      switchState_->overlayMem.flushCpuCache(0, OverlayMemSize);
      overlayDirty_ = false;
    }

    dk::ImageView overlayView{switchState_->overlayImage};
    dk::ImageDescriptor overlayDescriptor;
    overlayDescriptor.initialize(overlayView, false, false);

    switchState_->cmdBuf.pushData(
      descriptorBase + overlayDescriptorOffset,
      &overlayDescriptor,
      sizeof(overlayDescriptor)
    );

    std::array<DkShader const *, 2> overlayShaders = {
      &switchState_->overlayVertexShader,
      &switchState_->overlayFragmentShader
    };
    std::array<DkResHandle, 1> overlayHandle = {
      dkMakeTextureHandle(2, 0)
    };
    DkViewport overlayViewport{
      0.0f,
      0.0f,
      static_cast<float>(ScreenWidth),
      static_cast<float>(ScreenHeight),
      0.0f,
      1.0f
    };
    DkScissor overlayScissor{
      0,
      0,
      ScreenWidth,
      ScreenHeight
    };

    switchState_->cmdBuf.setViewports(0, overlayViewport);
    switchState_->cmdBuf.setScissors(0, overlayScissor);
    switchState_->cmdBuf.bindShaders(DkStageFlag_GraphicsMask, overlayShaders);
    switchState_->cmdBuf.bindColorState(switchState_->overlayColorState);
    switchState_->cmdBuf.bindBlendStates(0, switchState_->overlayBlendState);
    switchState_->cmdBuf.bindTextures(DkStage_Fragment, 0, overlayHandle);
    switchState_->cmdBuf.draw(DkPrimitive_Triangles, 6, 1, 0, 0);
  }

  frameSlot.retainedFrame = av_frame_clone(frame);
  if (!frameSlot.retainedFrame) {
    error_ = "could not retain NVTEGRA frame for asynchronous Deko3D rendering.";
    return false;
  }

  switchState_->cmdBuf.signalFence(frameSlot.fence);
  switchState_->queue.submitCommands(switchState_->cmdBuf.finishList());
  switchState_->queue.presentImage(switchState_->swapchain, slot);
  frameSlot.submitted = true;
  switchState_->nextFrameSlot = (switchState_->nextFrameSlot + 1) % FrameInFlightCount;

  error_.clear();
  return true;
#endif
}
#endif

#ifdef __SWITCH__
std::unique_ptr<INativeVideoRenderer> createDeko3dVideoRenderer() {
  return std::unique_ptr<INativeVideoRenderer>(new Deko3dVideoRenderer());
}
#endif

} // namespace nstv
