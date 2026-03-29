#include "AndroidSurfaceBridge.h"

namespace rnwgpu {

AndroidSurfaceBridge::AndroidSurfaceBridge(wgpu::Instance gpu, int width,
                                           int height)
    : _gpu(std::move(gpu)), _width(width), _height(height) {}

AndroidSurfaceBridge::~AndroidSurfaceBridge() { _surface = nullptr; }

// ─── JS thread ───────────────────────────────────────────────────

void AndroidSurfaceBridge::configure(wgpu::SurfaceConfiguration &newConfig) {
  std::lock_guard<std::mutex> lock(_mutex);
  _config = newConfig;
  _config.width = _width;
  _config.height = _height;
  _config.presentMode = wgpu::PresentMode::Fifo;
  _config.usage =
      _config.usage | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;
  _reconfigure();
}

void AndroidSurfaceBridge::unconfigure() {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_surface) {
    _surface.Unconfigure();
  }
  _texture = nullptr;
}

wgpu::Texture AndroidSurfaceBridge::getCurrentTexture(int width, int height) {
  if (_config.width != width || _config.height != height) {
    std::lock_guard<std::mutex> lock(_mutex);
    _config.width = width;
    _config.height = height;
    _reconfigure();
  }
  return _texture;
}

void AndroidSurfaceBridge::present() {
  if (_surface && _texture) {
    _copyToSurfaceAndPresent();
  }
}

// ─── UI thread (Android-specific) ───────────────────────────────

void AndroidSurfaceBridge::switchToOnscreen(ANativeWindow *nativeWindow,
                                            wgpu::Surface surface) {
  std::lock_guard<std::recursive_mutex> gpuLock(
      _gpuLock ? _gpuLock->mutex : _localMutex);
  std::unique_lock<std::mutex> lock(_mutex);
  _nativeWindow = nativeWindow;
  _surface = std::move(surface);
  if (_config.device != nullptr) {
    auto surfConfig = _config;
    surfConfig.usage = surfConfig.usage | wgpu::TextureUsage::CopyDst;
    _surface.Configure(&surfConfig);
    if (_texture != nullptr) {
      _copyToSurfaceAndPresent();
    }
  }
}

ANativeWindow *AndroidSurfaceBridge::switchToOffscreen() {
  std::lock_guard<std::recursive_mutex> gpuLock(
      _gpuLock ? _gpuLock->mutex : _localMutex);
  std::unique_lock<std::mutex> lock(_mutex);
  _surface = nullptr;
  return _nativeWindow;
}

void AndroidSurfaceBridge::resize(int width, int height) {
  std::lock_guard<std::mutex> lock(_sizeMutex);
  _width = width;
  _height = height;
}

// ─── Read-only ───────────────────────────────────────────────────

Size AndroidSurfaceBridge::getSize() {
  std::lock_guard<std::mutex> lock(_sizeMutex);
  return {.width = _width, .height = _height};
}

wgpu::Device AndroidSurfaceBridge::getDevice() {
  std::lock_guard<std::mutex> lock(_mutex);
  return _config.device;
}

// ─── Private ─────────────────────────────────────────────────────

void AndroidSurfaceBridge::_reconfigure() {
  _createOffscreenTexture();
  if (_surface) {
    auto surfConfig = _config;
    surfConfig.usage = surfConfig.usage | wgpu::TextureUsage::CopyDst;
    _surface.Configure(&surfConfig);
  }
}

void AndroidSurfaceBridge::_createOffscreenTexture() {
  wgpu::TextureDescriptor desc;
  desc.format = _config.format;
  desc.size.width = _config.width;
  desc.size.height = _config.height;
  desc.usage = wgpu::TextureUsage::RenderAttachment |
               wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst |
               wgpu::TextureUsage::TextureBinding;
  _texture = _config.device.CreateTexture(&desc);
}

void AndroidSurfaceBridge::_copyToSurfaceAndPresent() {
  wgpu::SurfaceTexture surfTex;
  _surface.GetCurrentTexture(&surfTex);
  if (surfTex.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
      surfTex.status !=
          wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
    return;
  }

  auto device = _config.device;
  wgpu::CommandEncoderDescriptor encDesc;
  auto encoder = device.CreateCommandEncoder(&encDesc);

  wgpu::TexelCopyTextureInfo src = {};
  src.texture = _texture;
  wgpu::TexelCopyTextureInfo dst = {};
  dst.texture = surfTex.texture;
  wgpu::Extent3D size = {_texture.GetWidth(), _texture.GetHeight(), 1};

  encoder.CopyTextureToTexture(&src, &dst, &size);
  auto cmds = encoder.Finish();
  device.GetQueue().Submit(1, &cmds);
  _surface.Present();
}

// Factory
std::shared_ptr<SurfaceBridge> createSurfaceBridge(wgpu::Instance gpu,
                                                    int width, int height) {
  return std::make_shared<AndroidSurfaceBridge>(std::move(gpu), width, height);
}

} // namespace rnwgpu
