#ifndef COMPONENTS_PRODUCT_ID_HPP
#define COMPONENTS_PRODUCT_ID_HPP

#include <Fw/Types/BasicTypes.hpp>

namespace Components {

// Reserve a persistent ID in the catalog's N2_STORAGE_ROOT before emitting a product.
bool reserveProductId(U32& productIdOut);

}  // namespace Components

#endif
