#ifndef ARTEMISDATAPRODUCTS_SUBTOPOLOGY_DEFS_HPP
#define ARTEMISDATAPRODUCTS_SUBTOPOLOGY_DEFS_HPP

#include <Fw/Types/MallocAllocator.hpp>
#include <Os/FileSystem.hpp>
#include <Svc/BufferManager/BufferManager.hpp>

#include "Subtopologies/ArtemisDataProducts/ArtemisDataProductsConfig/ArtemisDataProductsSubtopologyConfig.hpp"
#include "Subtopologies/ArtemisDataProducts/ArtemisDataProductsConfig/FppConstantsAc.hpp"

namespace ArtemisDataProducts {

struct SubtopologyState {};

struct TopologyState {
    SubtopologyState artemisDataProducts;
};

}  // namespace ArtemisDataProducts

#endif
