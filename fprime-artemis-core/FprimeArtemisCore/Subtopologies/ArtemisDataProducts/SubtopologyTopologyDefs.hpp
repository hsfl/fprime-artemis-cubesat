#ifndef ARTEMISDATAPRODUCTSSUBTOPOLOGY_DEFS_HPP
#define ARTEMISDATAPRODUCTSSUBTOPOLOGY_DEFS_HPP

#include <Fw/Types/MallocAllocator.hpp>
#include <Os/FileSystem.hpp>
#include <Svc/BufferManager/BufferManager.hpp>
#include "ArtemisDataProductsConfig/ArtemisDataProductsSubtopologyConfig.hpp"
#include "FprimeArtemisCore/Subtopologies/ArtemisDataProducts/ArtemisDataProductsConfig/FppConstantsAc.hpp"

namespace ArtemisDataProducts {
// State for topology construction
struct SubtopologyState {
    // Empty - no external state needed for ArtemisDataProducts subtopology
};

struct TopologyState {
    SubtopologyState dataProducts;
};
}  // namespace ArtemisDataProducts

#endif
