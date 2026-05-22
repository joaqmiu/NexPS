#pragma once

// Aggregator that exposes the v1.x C backend (net / converter / ia_manager /
// config) to the new C++ UI. The .c sources stay untouched — only the
// declarations get wrapped here so they link without name mangling.

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "net.h"
#include "converter.h"
#include "ia_manager.h"

#ifdef __cplusplus
}
#endif
