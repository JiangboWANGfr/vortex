// Copyright © 2019-2023
// Licensed under the Apache License, Version 2.0.

#pragma once

#include <cstdint>

using pcie_handle_t = void*;
using pcie_address_t = uint64_t;
using pcie_local_address_t = uint64_t;
using pcie_bar_t = unsigned int;

// A zero kmem_size opens an MMIO-only handle without touching the driver's
// per-device DMA staging allocation.
pcie_handle_t drv_open(uint32_t bdf, pcie_bar_t bar, uint32_t kmem_size);

void drv_close(pcie_handle_t handle);

bool drv_read32(pcie_handle_t handle, pcie_address_t address,
                uint32_t* value);

bool drv_write32(pcie_handle_t handle, pcie_address_t address,
                 uint32_t value);

bool drv_dma_read(pcie_handle_t handle, pcie_local_address_t address,
                  void* data, uint32_t size);

bool drv_dma_write(pcie_handle_t handle, pcie_local_address_t address,
                   const void* data, uint32_t size);

const char* drv_get_last_error();
