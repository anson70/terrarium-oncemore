#pragma once
// The desktop stand-in for the pedal's SDRAM allocator: zeroed, 16-byte
// aligned blocks, all released when the allocator is destroyed.

#include <cstddef>
#include <vector>

#include "tfx/effect.h"

namespace tfx::desktop {

class HeapAllocator final : public Allocator {
public:
    HeapAllocator() = default;
    HeapAllocator(const HeapAllocator&)            = delete;
    HeapAllocator& operator=(const HeapAllocator&) = delete;
    ~HeapAllocator() override { releaseAll(); }

    void*  allocate(size_t bytes) override;
    void   releaseAll();
    size_t bytesInUse() const { return bytes_; }

private:
    std::vector<void*> blocks_;
    size_t             bytes_ = 0;
};

} // namespace tfx::desktop
