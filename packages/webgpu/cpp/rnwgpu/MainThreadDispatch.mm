#import <Foundation/Foundation.h>
#include "MainThreadDispatch.h"

namespace rnwgpu {

void runOnMainThreadSync(std::function<void()> fn) {
  if ([NSThread isMainThread]) {
    fn();
  } else {
    dispatch_sync(dispatch_get_main_queue(), ^{
      fn();
    });
  }
}

} // namespace rnwgpu
