#pragma once
#include <TargetConditionals.h>
#include <memory>
#include "render_device.h"
namespace ar {
#if TARGET_OS_OSX
constexpr bool kMetalSharedStorageDefault = false;   // macOS: Managed + synchronizeResource for read-back
#else
constexpr bool kMetalSharedStorageDefault = true;    // visionOS: Managed storage does not exist
#endif
std::unique_ptr<IRenderDevice> create_metal_device(void *mtl_device, bool shared_storage = kMetalSharedStorageDefault);
void *metal_device_handle(IRenderDevice &dev);
}
