#import "MetalView.h"
#import "webgpu/webgpu_cpp.h"

#include "AppleSurfaceBridge.h"

@implementation MetalView {
  BOOL _isConfigured;
}

#if !TARGET_OS_OSX
+ (Class)layerClass {
  return [CAMetalLayer class];
}
#else  // !TARGET_OS_OSX
- (instancetype)init {
  self = [super init];
  if (self) {
    self.wantsLayer = true;
    self.layer = [CAMetalLayer layer];
  }
  return self;
}
#endif // !TARGET_OS_OSX

- (void)configure {
  auto size = self.frame.size;
  std::shared_ptr<rnwgpu::RNWebGPUManager> manager = [WebGPUModule getManager];
  void *nativeSurface = (__bridge void *)self.layer;
  auto &registry = rnwgpu::SurfaceRegistry::getInstance();
  auto gpu = manager->_gpu;
  auto surface = manager->_platformContext->makeSurface(
      gpu, nativeSurface, size.width, size.height);
  auto bridge = std::static_pointer_cast<rnwgpu::AppleSurfaceBridge>(
      registry.getSurfaceInfoOrCreate([_contextId intValue], gpu, size.width,
                                      size.height));
  bridge->prepareToDisplay(surface);
}

- (void)update {
  auto size = self.frame.size;
  auto &registry = rnwgpu::SurfaceRegistry::getInstance();
  auto bridge = std::static_pointer_cast<rnwgpu::AppleSurfaceBridge>(
      registry.getSurfaceInfo([_contextId intValue]));
  if (bridge) {
    bridge->resize(size.width, size.height);
  }
}

- (void)dealloc {
  auto &registry = rnwgpu::SurfaceRegistry::getInstance();
  registry.removeSurfaceInfo([_contextId intValue]);
}

@end
