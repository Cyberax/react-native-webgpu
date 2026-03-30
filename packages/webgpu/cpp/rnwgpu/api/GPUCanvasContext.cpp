#include "GPUCanvasContext.h"
#include "Convertors.h"
#include "RNWebGPUManager.h"
#include <memory>

namespace rnwgpu {

GPUCanvasContext::GPUCanvasContext(std::shared_ptr<GPU> gpu, int contextId, float pixelRatio,
  std::shared_ptr<jsi::Function> measureCallback,
  std::shared_ptr<facebook::react::CallInvoker> callInvoker)
    : NativeObject(CLASS_NAME), _gpu(std::move(gpu)), _contextId(contextId),
      _pixelRatio(pixelRatio), _measureCallback(std::move(measureCallback)),
      _callInvoker(std::move(callInvoker)) {

  _canvas = std::make_shared<Canvas>(nullptr, 0, 0);
  auto &registry = SurfaceRegistry::getInstance();
  _bridge = registry.getSurfaceInfoOrCreate(contextId, _gpu->get(), 0, 0);
}

void GPUCanvasContext::configure(
    std::shared_ptr<GPUCanvasConfiguration> configuration) {
  _updateCanvasSize();

  Convertor conv;
  wgpu::SurfaceConfiguration surfaceConfiguration;
  surfaceConfiguration.device = configuration->device->get();
  if (configuration->viewFormats.has_value()) {
    if (!conv(surfaceConfiguration.viewFormats,
              surfaceConfiguration.viewFormatCount,
              configuration->viewFormats.value())) {
      throw std::runtime_error("Error with SurfaceConfiguration");
    }
  }
  if (!conv(surfaceConfiguration.usage, configuration->usage) ||
      !conv(surfaceConfiguration.format, configuration->format)) {
    throw std::runtime_error("Error with SurfaceConfiguration");
  }

#ifdef __APPLE__
  surfaceConfiguration.alphaMode = configuration->alphaMode;
#endif
  surfaceConfiguration.presentMode = wgpu::PresentMode::Fifo;
  _bridge->configure(surfaceConfiguration);
}

void GPUCanvasContext::unconfigure() {}

void GPUCanvasContext::_updateCanvasSize() {
  auto *rt = getCreationRuntime();
  auto *mainRt = BaseRuntimeAwareCache::getMainJsRuntime();

  auto measure = [this, mainRt]() {
    if (!mainRt || !_measureCallback) return;
    auto result = _measureCallback->call(*mainRt);
    if (result.isObject()) {
      auto obj = result.getObject(*mainRt);
      auto w = static_cast<double>(
          obj.getProperty(*mainRt, "width").asNumber());
      auto h = static_cast<double>(
          obj.getProperty(*mainRt, "height").asNumber());
      if (w > 0 && h > 0) {
        _width = w;
        _height = h;
        // Canvas size is used by the clients only
        _canvas->setWidth(_width * _pixelRatio);
        _canvas->setHeight(_height * _pixelRatio);
        _canvas->setClientWidth(_width * _pixelRatio);
        _canvas->setClientHeight(_height * _pixelRatio);
      }
    }
  };

  if (rt == mainRt) {
    // Already on the main JS thread — call directly
    measure();
  } else if (_callInvoker) {
    // On a worklet/UI thread — marshal to the JS thread synchronously
    _callInvoker->invokeSync(std::move(measure));
  }
  // else: no invoker, use last known size (set by previous JS-thread call)
}

std::shared_ptr<Canvas> GPUCanvasContext::getCanvas() {
  _updateCanvasSize();
  return _canvas;
}

std::shared_ptr<GPUTexture> GPUCanvasContext::getCurrentTexture() {
  _updateCanvasSize();
  auto texture = _bridge->getCurrentTexture(
    ceil(_width * _pixelRatio), ceil(_height * _pixelRatio));
  if (!texture) {
    return nullptr;
  }
  auto result = std::make_shared<GPUTexture>(texture, "");
  result->setGPULock(getGPULock());
  _startedFrame = true;
  return result;
}

void GPUCanvasContext::present() {
  if (_startedFrame) {
    _bridge->present();
  }
  _startedFrame = false;
}

} // namespace rnwgpu
