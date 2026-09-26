#include "ArtemisDataProductsSubtopologyConfig.hpp"

namespace ArtemisDataProducts {
namespace Allocation {
// This instance can be changed to use a different allocator in the ArtemisDataProducts Subtopology
Fw::MallocAllocator mallocatorInstance;
Fw::MemAllocator& memAllocator = mallocatorInstance;
}  // namespace Allocation
}  // namespace ArtemisDataProducts
