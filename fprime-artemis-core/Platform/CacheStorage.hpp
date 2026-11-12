#pragma once
#include <cstddef>
#include <cstdint>
namespace Artemis {
constexpr std::size_t PRODUCT_CACHE_BYTES = 196608;
extern std::uint8_t* const productCacheStorage;
}
