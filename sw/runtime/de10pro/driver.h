// Copyright © 2019-2023
// Licensed under the Apache License, Version 2.0.

#pragma once

#include <cstdint>

using pcie_handle_t = void*;
using pcie_address_t = uint64_t;
using pcie_local_address_t = uint64_t;
using pcie_bar_t = int;

using pfn_PCIE_Open = pcie_handle_t (*)(uint16_t, uint16_t, uint16_t);
using pfn_PCIE_Close = void (*)(pcie_handle_t);
using pfn_PCIE_Read32 = bool (*)(pcie_handle_t, pcie_bar_t,
                                 pcie_address_t, uint32_t*);
using pfn_PCIE_Write32 = bool (*)(pcie_handle_t, pcie_bar_t,
                                  pcie_address_t, uint32_t);
using pfn_PCIE_DmaRead = bool (*)(pcie_handle_t, pcie_local_address_t,
                                  void*, uint32_t);
using pfn_PCIE_DmaWrite = bool (*)(pcie_handle_t, pcie_local_address_t,
                                   void*, uint32_t);

struct de10pro_drv_api_t {
  pfn_PCIE_Open PCIE_Open;
  pfn_PCIE_Close PCIE_Close;
  pfn_PCIE_Read32 PCIE_Read32;
  pfn_PCIE_Write32 PCIE_Write32;
  pfn_PCIE_DmaRead PCIE_DmaRead;
  pfn_PCIE_DmaWrite PCIE_DmaWrite;
  const char* (*get_last_error)();
};

int drv_init(de10pro_drv_api_t* drv_funcs);

void drv_close();
