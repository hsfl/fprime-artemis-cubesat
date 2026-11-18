#ifndef COMPONENTS_PRODUCT_ID_HPP
#define COMPONENTS_PRODUCT_ID_HPP

#include <Fw/Types/BasicTypes.hpp>

namespace Components {

// Reserve a persistent ID in the catalog's N2_STORAGE_ROOT before emitting a product.
bool reserveProductId(U32& productIdOut);

// Recognize catalog metadata, legacy data and acquisition-labelled data names.
// Orphaned data files reserve IDs too, even when their metadata is unavailable.
bool parseProductIdFilename(const char* name, U32& productIdOut);

}  // namespace Components

#endif
