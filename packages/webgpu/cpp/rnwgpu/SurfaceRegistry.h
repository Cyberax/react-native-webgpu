#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

#include "GPULockInfo.h"
#include "webgpu/webgpu_cpp.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

#include "MainThreadDispatch.h"

namespace rnwgpu {

struct NativeInfo {
  void *nativeSurface;
  int width;
  int height;
};

struct Size {
  int width;
  int height;
};

class SurfaceInfo {
public:
  SurfaceInfo(wgpu::Instance gpu, int width, int height)
      : gpu(std::move(gpu)), width(width), height(height) {}

  ~SurfaceInfo() { surface = nullptr; }

  void setGPULock(std::shared_ptr<GPULockInfo> lock) { _gpuLock = std::move(lock); }
  std::shared_ptr<GPULockInfo> getGPULock() const { return _gpuLock; }

  void reconfigure(int newWidth, int newHeight) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    config.width = newWidth;
    config.height = newHeight;
    _configure();
  }

  void configure(wgpu::SurfaceConfiguration &newConfig) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    config = newConfig;
    config.width = width;
    config.height = height;
    config.presentMode = wgpu::PresentMode::Fifo;
    // Ensure CopySrc so we can snapshot the surface to a backup texture
    // in present(), and CopyDst so switchToOnscreen can blit back.
    config.usage = config.usage | wgpu::TextureUsage::CopySrc |
                   wgpu::TextureUsage::CopyDst;
    _configure();
  }

  void unconfigure() {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    if (surface) {
      surface.Unconfigure();
    } else {
      texture = nullptr;
    }
  }

  void *switchToOffscreen() {
    // Acquire GPU lock to serialize with JS thread Dawn calls
    std::unique_lock<std::mutex> gpuLock(_gpuLock ? _gpuLock->mutex : _localMutex);
    std::unique_lock<std::shared_mutex> lock(_mutex);
    // The offscreen texture already holds the last rendered frame
    // (JS always renders to it, present() copies it to the surface).
    // Just drop the surface — getCurrentTexture() will keep returning
    // the offscreen texture.
    surface = nullptr;
    return nativeSurface;
  }

  void switchToOnscreen(void *newNativeSurface, wgpu::Surface newSurface) {
    // Acquire GPU lock to serialize with JS thread Dawn calls
    std::unique_lock<std::mutex> gpuLock(_gpuLock ? _gpuLock->mutex : _localMutex);
    std::unique_lock<std::shared_mutex> lock(_mutex);
    nativeSurface = newNativeSurface;
    surface = std::move(newSurface);
    // Configure the surface for receiving copies from the offscreen texture
    if (config.device != nullptr) {
      _runOnMainThread([this]() {
        auto surfConfig = config;
        surfConfig.usage = surfConfig.usage | wgpu::TextureUsage::CopyDst;
        surface.Configure(&surfConfig);
      });
      // Immediately show the last rendered frame on the new surface
      if (texture != nullptr) {
        _copyToSurfaceAndPresent();
      }
    }
  }

  void resize(int newWidth, int newHeight) {
    std::lock_guard<std::mutex> lock(_sizeMutex);
    width = newWidth;
    height = newHeight;
  }

  void present() {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    if (surface && texture) {
      // Copy the offscreen texture to the surface and present
      _copyToSurfaceAndPresent();
    }
  }

  wgpu::Texture getCurrentTexture() {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    // Always return the offscreen texture. present() copies it to the surface.
    return texture;
  }

  NativeInfo getNativeInfo() {
    std::lock_guard<std::mutex> lock(_sizeMutex);
    return {.nativeSurface = nativeSurface, .width = width, .height = height};
  }

  Size getSize() {
    std::lock_guard<std::mutex> lock(_sizeMutex);
    return {.width = width, .height = height};
  }

  wgpu::SurfaceConfiguration getConfig() {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return config;
  }

  wgpu::Device getDevice() {
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return config.device;
  }

private:
  // On iOS, surface operations must happen on the main thread (CAMetalLayer).
  // On Android, this is a no-op passthrough.
  template <typename F>
  void _runOnMainThread(F &&fn) {
    runOnMainThreadSync(std::function<void()>(std::forward<F>(fn)));
  }

  void _configure() {
    // Always create an offscreen texture — JS renders to it.
    // present() copies to the surface if one is available.
    _createOffscreenTexture();
    if (surface) {
      _runOnMainThread([this]() {
        auto surfConfig = config;
        surfConfig.usage = surfConfig.usage | wgpu::TextureUsage::CopyDst;
        surface.Configure(&surfConfig);
      });
    }
  }

  void _createOffscreenTexture() {
    wgpu::TextureDescriptor textureDesc;
    textureDesc.format = config.format;
    textureDesc.size.width = config.width;
    textureDesc.size.height = config.height;
    textureDesc.usage = wgpu::TextureUsage::RenderAttachment |
                        wgpu::TextureUsage::CopySrc |
                        wgpu::TextureUsage::CopyDst |
                        wgpu::TextureUsage::TextureBinding;
    texture = config.device.CreateTexture(&textureDesc);
  }

  // Copy the offscreen texture to the surface and present it.
  void _copyToSurfaceAndPresent() {
    _runOnMainThread([this]() {
      wgpu::SurfaceTexture surfTex;
      surface.GetCurrentTexture(&surfTex);
      if (surfTex.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
          surfTex.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
        return;
      }

      auto device = config.device;
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
    });
  }

  std::shared_ptr<GPULockInfo> _gpuLock;
  std::mutex _localMutex; // fallback when _gpuLock is not set
  std::mutex _sizeMutex;  // protects width/height (UI thread writes, JS thread reads)
  mutable std::shared_mutex _mutex;
  void *nativeSurface = nullptr;
  wgpu::Surface surface = nullptr;
  wgpu::Texture texture = nullptr;
  wgpu::Instance gpu;
  wgpu::SurfaceConfiguration config;
  int width;
  int height;
};

class SurfaceRegistry {
public:
  static SurfaceRegistry &getInstance() {
    static SurfaceRegistry instance;
    return instance;
  }

  SurfaceRegistry(const SurfaceRegistry &) = delete;
  SurfaceRegistry &operator=(const SurfaceRegistry &) = delete;

  std::shared_ptr<SurfaceInfo> getSurfaceInfo(int id) {
    std::shared_lock<std::shared_mutex> lock(_mutex);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "SurfaceRegistry",
                        "getSurfaceInfo(%d) size=%zu", id, _registry.size());
#endif
    auto it = _registry.find(id);
    if (it != _registry.end()) {
      return it->second;
    }
    return nullptr;
  }

  void removeSurfaceInfo(int id) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _registry.erase(id);
  }

  std::shared_ptr<SurfaceInfo> addSurfaceInfo(int id, wgpu::Instance gpu,
                                              int width, int height) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    auto info = std::make_shared<SurfaceInfo>(gpu, width, height);
    _registry[id] = info;
    return info;
  }

  std::shared_ptr<SurfaceInfo>
  getSurfaceInfoOrCreate(int id, wgpu::Instance gpu, int width, int height) {
    std::unique_lock<std::shared_mutex> lock(_mutex);
    auto it = _registry.find(id);
    if (it != _registry.end()) {
      return it->second;
    }
    auto info = std::make_shared<SurfaceInfo>(gpu, width, height);
    _registry[id] = info;
    return info;
  }

private:
  SurfaceRegistry() = default;
  mutable std::shared_mutex _mutex;
  std::unordered_map<int, std::shared_ptr<SurfaceInfo>> _registry;
};

} // namespace rnwgpu
