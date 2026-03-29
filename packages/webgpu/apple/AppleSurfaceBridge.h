#pragma once

#include "rnwgpu/SurfaceBridge.h"

#include <mutex>
#include <shared_mutex>

namespace rnwgpu {

class AppleSurfaceBridge : public SurfaceBridge {
public:
  AppleSurfaceBridge(wgpu::Instance gpu, int width, int height);
  ~AppleSurfaceBridge() override;

  // JS thread
  void configure(wgpu::SurfaceConfiguration &config) override;
  void unconfigure() override;
  wgpu::Texture getCurrentTexture(int width, int height) override;
  void present() override;

  // Apple UI thread — called once from MetalView when the layer is ready
  void prepareToDisplay(wgpu::Surface surface);
  void resize(int width, int height);

  // Read-only
  Size getSize() override;
  wgpu::Device getDevice() override;

private:
  void _configureSurface();

  wgpu::Instance _gpu;
  wgpu::SurfaceConfiguration _config;
  wgpu::Surface _surface = nullptr;

  std::mutex _sizeMutex;
  mutable std::shared_mutex _mutex;
  int _width;
  int _height;
};

} // namespace rnwgpu
