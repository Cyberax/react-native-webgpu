#pragma once

#include "rnwgpu/SurfaceBridge.h"

#include <mutex>
#include <shared_mutex>

namespace rnwgpu {

class AppleSurfaceBridge : public SurfaceBridge {
public:
  AppleSurfaceBridge(GPUWithLock gpu, int width, int height);
  ~AppleSurfaceBridge() override {};

  // JS thread
  void configure(wgpu::SurfaceConfiguration &config) override;
  wgpu::Texture getCurrentTexture(int width, int height) override;
  void present() override;

  // Called by the UI thread once from MetalView when it's ready.
  // The UI thread must hold the GPU device lock.
  void prepareToDisplay(void *nativeSurface, int width, int height,
    wgpu::Surface surface);

  // Called by the UI thread when its size is changing, needs its own lock
  // because it can race with the JS thread.
  void resize(int width, int height); // Protected by _sizeMutex
  Size getSize() override; // Protected by _sizeMutex
  NativeInfo getNativeInfo() override; // Protected by _sizeMutex

private:
  void _reconfigureSurface();

  wgpu::Instance _gpu;
  wgpu::SurfaceConfiguration _config;
  wgpu::Surface _surface = nullptr;

  // It's possible that the JS thread accesses the getCurrentTexture
  // before the UI thread attaches the native Metal layer. In this case
  // we render to an offscreen texture.
  wgpu::Texture _renderTargetTexture = nullptr;
  wgpu::Texture _presentedTexture = nullptr;

  void *_nativeSurface = nullptr;

  std::mutex _sizeMutex;
  std::mutex _mutex;
  int _width;
  int _height;
};

} // namespace rnwgpu
