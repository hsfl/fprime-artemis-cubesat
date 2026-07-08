#include "ArtemisDataProductsSubtopologyConfig.hpp"

namespace ArtemisDataProducts {
namespace Allocation {
Fw::MallocAllocator mallocatorInstance;
Fw::MemAllocator& memAllocator = mallocatorInstance;
}  // namespace Allocation
}  // namespace ArtemisDataProducts
