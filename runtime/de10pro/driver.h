// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <TERASIC_PCIE_AVMM.h>

typedef PCIE_HANDLE (*pfn_PCIE_Open)(uint16_t wVendorID, uint16_t wDeviceID, uint16_t wCardNum);
typedef void (*pfn_PCIE_Close)(PCIE_HANDLE hFPGA);
typedef bool (*pfn_PCIE_Read32)(PCIE_HANDLE hFPGA, PCIE_BAR PciBar, PCIE_ADDRESS PciAddress, uint32_t *pdwData);
typedef bool (*pfn_PCIE_Write32)(PCIE_HANDLE hFPGA, PCIE_BAR PciBar, PCIE_ADDRESS PciAddress, uint32_t dwData);
typedef bool (*pfn_PCIE_Read8)(PCIE_HANDLE hFPGA, PCIE_BAR PciBar, PCIE_ADDRESS PciAddress, uint8_t *pByte);
typedef bool (*pfn_PCIE_Write8)(PCIE_HANDLE hFPGA, PCIE_BAR PciBar, PCIE_ADDRESS PciAddress, uint8_t Byte);
typedef bool (*pfn_PCIE_DmaRead)(PCIE_HANDLE hFPGA, PCIE_LOCAL_ADDRESS LocalAddress, void *pBuffer, uint32_t dwBufSize);
typedef bool (*pfn_PCIE_DmaWrite)(PCIE_HANDLE hFPGA, PCIE_LOCAL_ADDRESS LocalAddress, void *pData, uint32_t dwDataSize);
typedef bool (*pfn_PCIE_ConfigRead32)(PCIE_HANDLE hFPGA, uint32_t Offset, uint32_t *pData32);

struct de10pro_drv_api_t {
  pfn_PCIE_Open PCIE_Open;
  pfn_PCIE_Close PCIE_Close;
  pfn_PCIE_Read32 PCIE_Read32;
  pfn_PCIE_Write32 PCIE_Write32;
  pfn_PCIE_Read8 PCIE_Read8;
  pfn_PCIE_Write8 PCIE_Write8;
  pfn_PCIE_DmaRead PCIE_DmaRead;
  pfn_PCIE_DmaWrite PCIE_DmaWrite;
  pfn_PCIE_ConfigRead32 PCIE_ConfigRead32;
  const char* (*get_last_error)();
};

int drv_init(de10pro_drv_api_t* drv_funcs);

void drv_close();
