#pragma once
// Counts heap allocations made while the guard is armed. Desktop hosts arm it
// around every process() call to catch effects that break the real-time rule
// "no allocation in process()" (see tfx/effect.h). Linking this replaces the
// global operator new/delete of the executable.

#include <cstddef>

namespace tfx::desktop {

void   allocGuardArm();    // affects the calling thread only
void   allocGuardDisarm();
size_t allocGuardCount();  // allocations seen while armed, on any thread, since the last reset
void   allocGuardReset();

// Portable aligned allocation (std::aligned_alloc does not exist on MSVC).
// Memory from alignedAlloc must be released with alignedFree.
void* alignedAlloc(size_t alignment, size_t bytes);
void  alignedFree(void* p);

} // namespace tfx::desktop
