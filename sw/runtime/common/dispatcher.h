// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0

#pragma once

// ============================================================================
// dispatcher.h — interface common/ uses to ask the dispatcher to load a
// backend. The implementation lives in sw/runtime/stub/vortex.cpp (the
// libvortex.so dispatcher target). common/ stays backend-agnostic — it
// must not know about dlopen or VORTEX_DRIVER.
// ============================================================================

#include "callbacks.h"
#include <vortex.h>   // vx_result_t

namespace vx {

// Load (or reuse) the backend library named by $VORTEX_DRIVER (default
// "simx") and return a pointer to its populated callbacks table on
// success. The table pointer is owned by the dispatcher and stays valid for
// the lifetime of the process. An optional platform-query symbol is returned
// separately so callbacks_t retains its original binary layout. Idempotent:
// subsequent calls hand back the same values without reloading.
vx_result_t dispatcher_get_callbacks(
    const callbacks_t** out,
    vx_dev_platform_query_t* out_platform_query);

} // namespace vx
