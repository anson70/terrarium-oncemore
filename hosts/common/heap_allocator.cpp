#include "heap_allocator.h"

#include <cstring>

#include "alloc_guard.h"

namespace tfx::desktop {

void* HeapAllocator::allocate(size_t bytes) {
    const size_t rounded = (bytes + 15) / 16 * 16;
    void*        p       = alignedAlloc(16, rounded ? rounded : 16);
    if (!p) return nullptr;
    std::memset(p, 0, rounded);
    blocks_.push_back(p);
    bytes_ += rounded;
    return p;
}

void HeapAllocator::releaseAll() {
    for (void* p : blocks_) alignedFree(p);
    blocks_.clear();
    bytes_ = 0;
}

} // namespace tfx::desktop
