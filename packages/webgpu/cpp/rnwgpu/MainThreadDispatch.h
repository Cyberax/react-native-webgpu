#pragma once

#include <functional>

namespace rnwgpu {

// Run a function on the main thread synchronously.
// If already on the main thread, runs inline.
// On Apple: dispatches to main queue. On Android: no-op passthrough.
#ifdef __ANDROID__
inline void runOnMainThreadSync(std::function<void()> fn) {
  fn();
}
#else
// Implemented in MainThreadDispatch.mm (Objective-C++ for NSThread/dispatch)
void runOnMainThreadSync(std::function<void()> fn);
#endif

} // namespace rnwgpu
