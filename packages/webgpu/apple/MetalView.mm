#import "MetalView.h"
#import "webgpu/webgpu_cpp.h"

#include "AppleSurfaceBridge.h"
#include "SurfaceRegistry.h"

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

static void attemptToPrepareForDisplay(__weak MetalView *weakView, int contextId,
  rnwgpu::GPUWithLock gpuWithLock, wgpu::Surface surface) {

  // Check if the view is still alive
  MetalView *view = weakView;
  if (!view) {
    return; // View was deallocated while we were waiting
  }

  // Try to acquire the GPU lock without blocking. If the JS thread holds it
  // (e.g. during surface reconfiguration), we can't block the UI thread -
  // that would deadlock because reconfigure dispatches back to the UI thread.
  // Instead, post a retry to the main queue.
  if (!gpuWithLock.lock->mutex.try_lock()) {
    // Lock is held by JS thread - retry after yielding to the run loop.
    // Capture weakView to avoid preventing deallocation while queued.
    dispatch_async(dispatch_get_main_queue(), ^{
      attemptToPrepareForDisplay(weakView, contextId, gpuWithLock, surface);
    });
    return;
  }
  // Lock acquired, make sure we release it when done
  std::lock_guard<std::recursive_mutex> gpuLock(gpuWithLock.lock->mutex, std::adopt_lock);

  auto &registry = rnwgpu::SurfaceRegistry::getInstance();
  auto bridge = std::static_pointer_cast<rnwgpu::AppleSurfaceBridge>(
      registry.getSurfaceInfo(contextId));
  if (!bridge) {
    // The surface was removed before we could display it
    return;
  }

  auto size = view.frame.size;
  void *nativeSurface = (__bridge void *) view.layer;
  bridge->prepareToDisplay(nativeSurface, size.width, size.height, surface);
}

- (void)configure {
  std::shared_ptr<rnwgpu::RNWebGPUManager> manager = [WebGPUModule getManager];
  auto gpuWithLock = manager->_gpu;

  auto &registry = rnwgpu::SurfaceRegistry::getInstance();

  wgpu::SurfaceSourceMetalLayer metalSurfaceDesc;
  metalSurfaceDesc.layer = (__bridge void *)self.layer;
  wgpu::SurfaceDescriptor surfaceDescriptor;
  surfaceDescriptor.nextInChain = &metalSurfaceDesc;
  // This is safe to call without holding a GPU lock
  wgpu::Surface surface = gpuWithLock.gpu.CreateSurface(&surfaceDescriptor);

  // Get or create the bridge.
  auto size = self.frame.size;
  int ctxId = [_contextId intValue];

  // Enforce the creation of the bridge
  registry.getSurfaceInfoOrCreate(ctxId, gpuWithLock, size.width, size.height);
  // Try to attach the surface — will retry asynchronously if GPU lock is busy
  attemptToPrepareForDisplay(self, ctxId, gpuWithLock, surface);
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
