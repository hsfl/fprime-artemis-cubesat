#include "CacheStorage.hpp"
// Keep one full product outside main OCRAM2. Metadata validity is always reset
// by the owning cache constructor; NOLOAD bytes are never trusted at boot.
extern "C" {
#ifdef ARTEMIS_TWIN
alignas(32) std::uint8_t artemis_product_cache[Artemis::PRODUCT_CACHE_BYTES];
#else
alignas(32) std::uint8_t artemis_product_cache[Artemis::PRODUCT_CACHE_BYTES]
    __attribute__((section("OCRAM"), used));
#endif
}
namespace Artemis {
std::uint8_t* const productCacheStorage = artemis_product_cache;
}
