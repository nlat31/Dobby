
#include "dobby/dobby_internal.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <vector>

#if !defined(__APPLE__)
#if defined(__ANDROID__) || defined(__linux__)
namespace {

struct PatchRegion {
  uintptr_t start;
  uintptr_t end;
  int protection;
};

bool FindPatchRegion(uintptr_t address, PatchRegion *region) {
  FILE *maps = fopen("/proc/self/maps", "r");
  if (!maps)
    return false;

  char line[1024];
  bool found = false;
  while (fgets(line, sizeof(line), maps)) {
    unsigned long start = 0;
    unsigned long end = 0;
    char permissions[5] = {};
    if (sscanf(line, "%lx-%lx %4s", &start, &end, permissions) != 3)
      continue;
    if (address < start || address >= end)
      continue;

    int protection = 0;
    if (permissions[0] == 'r')
      protection |= PROT_READ;
    if (permissions[1] == 'w')
      protection |= PROT_WRITE;
    if (permissions[2] == 'x')
      protection |= PROT_EXEC;
    *region = {static_cast<uintptr_t>(start), static_cast<uintptr_t>(end), protection};
    found = true;
    break;
  }
  fclose(maps);
  return found;
}

} // namespace
#endif

PUBLIC int DobbyCodePatch(void *address, uint8_t *buffer, uint32_t buffer_size) {
#if defined(__ANDROID__) || defined(__linux__)
  if (!address || !buffer || buffer_size == 0)
    return -1;

  uintptr_t patch_start = reinterpret_cast<uintptr_t>(address);
  uintptr_t patch_end = patch_start + buffer_size;
  if (patch_end < patch_start)
    return -1;

  // Changing one page out of a file-backed executable mapping creates a
  // persistent VMA boundary after that page is dirtied. Temporarily make each
  // complete containing VMA writable instead, preserving the original layout.
  std::vector<PatchRegion> regions;
  for (uintptr_t cursor = patch_start; cursor < patch_end;) {
    PatchRegion region{};
    if (!FindPatchRegion(cursor, &region) || region.end <= cursor) {
      ERROR_LOG("DobbyCodePatch: failed to find VMA for %p", reinterpret_cast<void *>(cursor));
      return -1;
    }
    regions.push_back(region);
    cursor = region.end;
  }

  size_t writable_count = 0;
  for (const auto &region : regions) {
    if (mprotect(reinterpret_cast<void *>(region.start), region.end - region.start,
                 region.protection | PROT_WRITE) != 0) {
      ERROR_LOG("DobbyCodePatch: failed to make VMA writable");
      for (size_t i = writable_count; i > 0; --i) {
        const auto &changed = regions[i - 1];
        mprotect(reinterpret_cast<void *>(changed.start), changed.end - changed.start,
                 changed.protection);
      }
      return -1;
    }
    ++writable_count;
  }

  memcpy(address, buffer, buffer_size);

  bool restored = true;
  for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
    if (mprotect(reinterpret_cast<void *>(it->start), it->end - it->start, it->protection) != 0) {
      ERROR_LOG("DobbyCodePatch: failed to restore VMA protection");
      restored = false;
    }
  }

  addr_t clear_start_ = (addr_t)address;
  ClearCache((void *)clear_start_, (void *)(clear_start_ + buffer_size));
  if (!restored)
    return -1;
#endif
  return 0;
}

#endif