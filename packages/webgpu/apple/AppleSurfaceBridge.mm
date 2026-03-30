#import <Foundation/Foundation.h>
#include "AppleSurfaceBridge.h"

namespace dawn::native::metal {
void WaitForCommandsToBeScheduled(WGPUDevice device);
}

namespace rnwgpu {

static void runOnUiThreadSync(std::function<void()> fn) {
  if ([NSThread isMainThread]) {
    fn();
  } else {
    dispatch_sync(dispatch_get_main_queue(), ^{ fn(); });
  }
}

AppleSurfaceBridge::AppleSurfaceBridge(GPUWithLock gpu, int width, int height)
    : _gpu(std::move(gpu.gpu)), SurfaceBridge(gpu.lock), _width(width), _height(height) {}

void AppleSurfaceBridge::configure(wgpu::SurfaceConfiguration &newConfig) {
  auto size = getSize(); // Get the size while we're not locked
  std::lock_guard<std::mutex> lock(_mutex);
  _config = newConfig;
  _config.width = size.width;
  _config.height = size.height;
  _config.presentMode = wgpu::PresentMode::Fifo;
  _config.alphaMode = newConfig.alphaMode;
  _reconfigureSurface();
}

wgpu::Texture AppleSurfaceBridge::getCurrentTexture(int width, int height) {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_config.width != width || _config.height != height || !_surfaceConfigured) {
    _config.width = width;
    _config.height = height;
    _reconfigureSurface();
  }

  if (_surface && _surfaceConfigured) {
    wgpu::SurfaceTexture surfTex;
    // It's safe to get the texture without the UI thread roundtrip, only reconfiguration
    // needs to be delegated to the UI thread.
    _surface.GetCurrentTexture(&surfTex);
    return surfTex.texture;
  } else {
    // This can happen if the getCurrentTexture() runs before the UI thread
    // calls prepareToDisplay().
    wgpu::TextureDescriptor textureDesc;
    textureDesc.format = _config.format;
    textureDesc.size.width = _config.width;
    textureDesc.size.height = _config.height;
    textureDesc.usage = wgpu::TextureUsage::RenderAttachment |
                        wgpu::TextureUsage::CopySrc |
                        wgpu::TextureUsage::TextureBinding;
    _renderTargetTexture = _config.device.CreateTexture(&textureDesc);
    return _renderTargetTexture;
  }
}

void AppleSurfaceBridge::present() {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_config.device) {
    dawn::native::metal::WaitForCommandsToBeScheduled(_config.device.Get());
  }
  if (_surface) {
    if (_renderTargetTexture) {
      // The UI thread was late to the party...
      copyTextureToSurfaceAndPresent(_config.device, _renderTargetTexture, _surface);
      _renderTargetTexture = nullptr;
      _presentedTexture = nullptr;
      return;
    }
    // No need for the UI thread roundtrip for the Present call, only reconfiguration
    // needs to be delegated to the UI thread.
    _surface.Present();
  } else if (_renderTargetTexture) {
    _presentedTexture = _renderTargetTexture;
    _renderTargetTexture = nullptr;
  }
}

void AppleSurfaceBridge::prepareToDisplay(void *nativeSurface, int width, int height,
                                          wgpu::Surface surface) {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_surface) {
    return;
  }
  _nativeSurface = nativeSurface; // For nativeInfo only
  _surface = std::move(surface);

  if (_presentedTexture || _renderTargetTexture) {
    // We'll need to use the surface to copy from the texture to it.
    // Now or later in .present().
    _config.usage = _config.usage | wgpu::TextureUsage::CopyDst;
  }
  _config.width = width;
  _config.height = height;
  if (!_config.device) {
     return;
  }
  _surface.Configure(&_config); // We're in the UI thread, it's safe.
  _surfaceConfigured = true;
  if (_presentedTexture) {
    // We already presented something! So copy it to the surface.
    copyTextureToSurfaceAndPresent(_config.device, _presentedTexture, _surface);
    _presentedTexture = nullptr;
  }
}

void AppleSurfaceBridge::resize(int width, int height) {
  std::lock_guard<std::mutex> lock(_sizeMutex); // Size mutex, to prevent deadlocks
  _width = width;
  _height = height;
}

Size AppleSurfaceBridge::getSize() {
  std::lock_guard<std::mutex> lock(_sizeMutex);
  return {.width = _width, .height = _height};
}

NativeInfo AppleSurfaceBridge::getNativeInfo() {
  std::lock_guard<std::mutex> lock(_sizeMutex);
  return {.nativeSurface = _nativeSurface, .width = _width, .height = _height};
}

void AppleSurfaceBridge::_reconfigureSurface() {
  if (_surface && _config.device != nullptr) {
    // Thread safety: this will only be called with the GPU device lock
    // held from the JS thread. The UI thread is engineered to never
    // block on the GPU lock.
    runOnUiThreadSync([this]() {
      _surfaceConfigured = true;
      _surface.Configure(&_config);
    });
  }
}

// Factory
std::shared_ptr<SurfaceBridge> createSurfaceBridge(GPUWithLock gpu, int width, int height) {
  return std::make_shared<AppleSurfaceBridge>(std::move(gpu), width, height);
}

} // namespace rnwgpu
