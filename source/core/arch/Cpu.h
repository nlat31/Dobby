#pragma once
#include <cstdint>
#include "core/arch/CpuRegister.h"
#include "PlatformUnifiedInterface/ExecMemory/ClearCacheTool.h"
#if defined(TARGET_ARCH_ARM)
using arm_inst_t = uint32_t;
using thumb1_inst_t = uint16_t;
using thumb2_inst_t = uint32_t;
#endif
class CpuFeatures {
public:
  static void ClearCache(void *start, void *end) { ::ClearCache(start, end); }
  static void FlushICache(void *start, void *end);
};
