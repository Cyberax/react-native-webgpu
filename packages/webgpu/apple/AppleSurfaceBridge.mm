#import <Foundation/Foundation.h>
#include "AppleSurfaceBridge.h"

namespace dawn::native::metal {
void WaitForCommandsToBeScheduled(WGPUDevice device);
}

namespace rnwgpu {

static void runOnMainThreadSync(std::function<void()> fn) {
  if ([NSThread isMainThread]) {
    fn();
  } else {
    dispatch_sync(dispatch_get_main_queue(), ^{ fn(); });
  }
}

AppleSurfaceBridge::AppleSurfaceBridge(wgpu::Instance gpu, int width,
                                       int height)
    : _gpu(std::move(gpu)), _width(width), _height(height) {}

AppleSurfaceBridge::~AppleSurfaceBridge() { _surface = nullptr; }

void AppleSurfaceBridge::configure(wgpu::SurfaceConfiguration &newConfig) {
  std::unique_lock<std::shared_mutex> lock(_mutex);
  _config = newConfig;
  _config.width = _width;
  _config.height = _height;
  _config.presentMode = wgpu::PresentMode::Fifo;
  _config.alphaMode = newConfig.alphaMode;
  _configureSurface();
}

void AppleSurfaceBridge::unconfigure() {
  std::unique_lock<std::shared_mutex> lock(_mutex);
  if (_surface) {
    runOnMainThreadSync([this]() { _surface.Unconfigure(); });
  }
}

wgpu::Texture AppleSurfaceBridge::getCurrentTexture(int width, int height) {
  if (_config.width != width || _config.height != height) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _config.width = width;
    _config.height = height;
    _configureSurface();
  }
  // Get the texture directly from the surface — no offscreen copy needed
  if (!_surface) {
    return nullptr;
  }
  wgpu::SurfaceTexture surfTex;
  runOnMainThreadSync([this, &surfTex]() {
    _surface.GetCurrentTexture(&surfTex);
  });
  return surfTex.texture;
}

void AppleSurfaceBridge::present() {
  if (_config.device) {
    dawn::native::metal::WaitForCommandsToBeScheduled(_config.device.Get());
  }
  if (_surface) {
    runOnMainThreadSync([this]() { _surface.Present(); });
  }
}

void AppleSurfaceBridge::prepareToDisplay(wgpu::Surface surface) {
  std::unique_lock<std::shared_mutex> lock(_mutex);
  _surface = std::move(surface);
  _configureSurface();
}

void AppleSurfaceBridge::resize(int width, int height) {
  std::lock_guard<std::mutex> lock(_sizeMutex);
  _width = width;
  _height = height;
}

Size AppleSurfaceBridge::getSize() {
  std::lock_guard<std::mutex> lock(_sizeMutex);
  return {.width = _width, .height = _height};
}

wgpu::Device AppleSurfaceBridge::getDevice() {
  std::shared_lock<std::shared_mutex> lock(_mutex);
  return _config.device;
}

void AppleSurfaceBridge::_configureSurface() {
  if (_surface && _config.device != nullptr) {
    runOnMainThreadSync([this]() { _surface.Configure(&_config); });
  }
}

// Factory
std::shared_ptr<SurfaceBridge> createSurfaceBridge(wgpu::Instance gpu,
                                                    int width, int height) {
  return std::make_shared<AppleSurfaceBridge>(std::move(gpu), width, height);
}

} // namespace rnwgpu
