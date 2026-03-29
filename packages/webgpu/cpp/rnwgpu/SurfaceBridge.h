#pragma once

#include <memory>

#include "GPULockInfo.h"
#include "webgpu/webgpu_cpp.h"

namespace rnwgpu {

struct Size {
  int width;
  int height;
};

/**
 * Abstract bridge between the OS-specific surface and GPUCanvasContext.
 *
 * JS-thread methods (called under GPU lock via NativeObject):
 *   configure, unconfigure, getCurrentTexture, present
 *
 * UI-thread lifecycle methods are platform-specific and live in the
 * concrete subclasses (AndroidSurfaceBridge, AppleSurfaceBridge).
 */
class SurfaceBridge {
public:
  virtual ~SurfaceBridge() = default;

  // ─── JS thread (called under GPU lock) ─────────────────────────

  virtual void configure(wgpu::SurfaceConfiguration &config) = 0;
  virtual void unconfigure() = 0;

  // Returns the texture to render into. Handles resize if width/height
  // differ from the current configuration.
  virtual wgpu::Texture getCurrentTexture(int width, int height) = 0;

  virtual void present() = 0;

  // ─── Read-only accessors ───────────────────────────────────────

  virtual Size getSize() = 0;
  virtual wgpu::Device getDevice() = 0;

  // ─── Lock management ──────────────────────────────────────────

  void setGPULock(std::shared_ptr<GPULockInfo> lock) {
    _gpuLock = std::move(lock);
  }
  std::shared_ptr<GPULockInfo> getGPULock() const { return _gpuLock; }

protected:
  std::shared_ptr<GPULockInfo> _gpuLock;
};

// Platform-specific factory. Implemented in:
//   android/cpp/AndroidSurfaceBridge.cpp
//   apple/AppleSurfaceBridge.mm
std::shared_ptr<SurfaceBridge> createSurfaceBridge(wgpu::Instance gpu,
                                                    int width, int height);

} // namespace rnwgpu
