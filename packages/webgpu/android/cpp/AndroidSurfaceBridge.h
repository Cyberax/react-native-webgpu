#pragma once

#include <android/native_window.h>

#include "rnwgpu/SurfaceBridge.h"

namespace rnwgpu {

class AndroidSurfaceBridge : public SurfaceBridge {
public:
  AndroidSurfaceBridge(wgpu::Instance gpu, int width, int height);
  ~AndroidSurfaceBridge() override;

  // JS thread
  void configure(wgpu::SurfaceConfiguration &config) override;
  void unconfigure() override;
  wgpu::Texture getCurrentTexture(int width, int height) override;
  void present() override;

  // Android UI thread
  void switchToOnscreen(ANativeWindow *nativeWindow, wgpu::Surface surface);
  ANativeWindow *switchToOffscreen();
  void resize(int width, int height);

  // Read-only
  Size getSize() override;
  wgpu::Device getDevice() override;

private:
  void _reconfigure();
  void _createOffscreenTexture();
  void _copyToSurfaceAndPresent();

  wgpu::Instance _gpu;
  wgpu::SurfaceConfiguration _config;
  wgpu::Surface _surface = nullptr;
  wgpu::Texture _texture = nullptr;
  ANativeWindow *_nativeWindow = nullptr;

  std::recursive_mutex _localMutex;
  std::mutex _sizeMutex;
  mutable std::mutex _mutex;
  int _width;
  int _height;
};

} // namespace rnwgpu
