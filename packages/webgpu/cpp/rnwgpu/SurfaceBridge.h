#pragma once

#include <memory>

#include "GPULockInfo.h"
#include "webgpu/webgpu_cpp.h"

namespace rnwgpu {

struct Size {
  int width;
  int height;
};

struct NativeInfo {
  void *nativeSurface;
  int width;
  int height;
};

/**
 * The abstract bridge between the OS-specific surface and GPUCanvasContext.
 * It is created initially without the UI-level object in the JS thread, and
 * registered in the global SurfaceRegistry.
 *
 * It's then looked up by its ID by the UI thread and connected to the proper
 * system-level surface.
 *
 * JS-thread methods (called under GPU lock via NativeObject):
 *   configure, unconfigure, getCurrentTexture, present
 *
 * UI-thread lifecycle methods are platform-specific and live in the
 * concrete subclasses (AndroidSurfaceBridge, AppleSurfaceBridge).
 */
class SurfaceBridge {
public:
  SurfaceBridge(std::shared_ptr<GPULockInfo> gpuLock) : _gpuLock(std::move(gpuLock)) {}
  virtual ~SurfaceBridge() = default;

  // Accessors
  virtual NativeInfo getNativeInfo() = 0;
  std::shared_ptr<GPULockInfo> getGPULock() const { return _gpuLock; }

  ////////////////////////////////////////////////////////////
  ///// The JS thread operations (called under GPU lock)  ////
  ////////////////////////////////////////////////////////////
  virtual void configure(wgpu::SurfaceConfiguration &config) = 0;
  // Returns the texture to render into. Handles resize if width/height
  // differ from the current configuration.
  // Can return nullptr if the UI object is not yet attached.
  virtual wgpu::Texture getCurrentTexture(int width, int height) = 0;
  virtual bool present() = 0;

protected:

  void copyTextureToSurfaceAndPresent(wgpu::Device device, wgpu::Texture texture,
    wgpu::Surface surface) {

    if (!device || !texture || !surface) {
      return;
    }

    wgpu::SurfaceTexture surfTex;
    surface.GetCurrentTexture(&surfTex);
    if (surfTex.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
        surfTex.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
      // TODO: error handling?...
      return;
    }

    wgpu::CommandEncoderDescriptor encDesc;
    auto encoder = device.CreateCommandEncoder(&encDesc);

    wgpu::TexelCopyTextureInfo src = {};
    src.texture = texture;
    wgpu::TexelCopyTextureInfo dst = {};
    dst.texture = surfTex.texture;
    wgpu::Extent3D size = {texture.GetWidth(), texture.GetHeight(), 1};

    encoder.CopyTextureToTexture(&src, &dst, &size);
    auto cmds = encoder.Finish();
    device.GetQueue().Submit(1, &cmds);
    surface.Present();
  }

  std::shared_ptr<GPULockInfo> _gpuLock;
};

// Platform-specific factory. Implemented in:
//   android/cpp/AndroidSurfaceBridge.cpp
//   apple/AppleSurfaceBridge.mm
std::shared_ptr<SurfaceBridge> createSurfaceBridge(GPUWithLock gpu);

} // namespace rnwgpu
