// Copyright © 2019-2023
// Licensed under the Apache License, Version 2.0.

#include <callbacks.h>

#include <cstddef>
#include <cstdio>

static_assert(offsetof(callbacks_t, dev_open) == 0,
              "callbacks ABI must begin with dev_open");
static_assert(offsetof(callbacks_t, dev_close)
              == offsetof(callbacks_t, dev_open)
               + sizeof(callbacks_t::dev_open),
              "callbacks ABI dev_close offset changed");
static_assert(offsetof(callbacks_t, cp_reg_write)
              == offsetof(callbacks_t, dev_close)
               + sizeof(callbacks_t::dev_close),
              "callbacks ABI cp_reg_write offset changed");
static_assert(offsetof(callbacks_t, cp_reg_read)
              == offsetof(callbacks_t, cp_reg_write)
               + sizeof(callbacks_t::cp_reg_write),
              "callbacks ABI cp_reg_read offset changed");
static_assert(offsetof(callbacks_t, host_mem_alloc)
              == offsetof(callbacks_t, cp_reg_read)
               + sizeof(callbacks_t::cp_reg_read),
              "callbacks ABI host_mem_alloc offset changed");
static_assert(offsetof(callbacks_t, host_mem_free)
              == offsetof(callbacks_t, host_mem_alloc)
               + sizeof(callbacks_t::host_mem_alloc),
              "callbacks ABI host_mem_free offset changed");
static_assert(sizeof(callbacks_t)
              == offsetof(callbacks_t, host_mem_free)
               + sizeof(callbacks_t::host_mem_free),
              "callbacks ABI must contain exactly six function pointers");

int main() {
  std::printf("Runtime callbacks ABI test passed\n");
  return 0;
}
