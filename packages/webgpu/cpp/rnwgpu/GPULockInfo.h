#pragma once

#include <memory>
#include <mutex>

namespace rnwgpu {

/**
 * GPU-level lock that serializes all Dawn API calls.
 *
 * Dawn's wgpu::Device is not thread-safe. All calls that touch the device
 * (texture creation, command encoding, queue submit, surface operations)
 * must be serialized. This lock is shared by ALL objects derived from the
 * same GPU device.
 *
 * Created when the GPU instance is initialized, propagated to every
 * downstream object (adapter, device, textures, buffers, encoders, etc.)
 * via constructor arguments.
 *
 * The NativeObject base class acquires this lock automatically around
 * every JS→native method call. The UI thread (SurfaceInfo) also acquires
 * it before touching Dawn APIs.
 */
struct GPULockInfo {
  std::mutex mutex;
};

} // namespace rnwgpu
