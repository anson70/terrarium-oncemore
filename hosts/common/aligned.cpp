#include <cstdlib>

#include "alloc_guard.h"

#ifdef _WIN32
#include <malloc.h>
#endif

namespace tfx::desktop {

void* alignedAlloc(size_t alignment, size_t bytes) {
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if (bytes == 0) bytes = alignment;
#ifdef _WIN32
    return _aligned_malloc(bytes, alignment);
#else
    void* p = nullptr;
    return posix_memalign(&p, alignment, bytes) == 0 ? p : nullptr;
#endif
}

void alignedFree(void* p) {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}

} // namespace tfx::desktop
