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

#include <common.h>

#include "driver.h"
#include "vortex_afu.h"

#ifdef SCOPE
#include "scope.h"
#endif

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <unistd.h>

using namespace vortex;

#define CMD_MEM_READ  AFU_IMAGE_CMD_MEM_READ
#define CMD_MEM_WRITE AFU_IMAGE_CMD_MEM_WRITE
#define CMD_RUN       AFU_IMAGE_CMD_RUN
#define CMD_DCR_WRITE AFU_IMAGE_CMD_DCR_WRITE

#define STATUS_STATE_BITS 8
#define CACHE_BLOCK_SHIFT 6

#define CHECK_PCIE(_expr, _cleanup)                                           \
  do {                                                                        \
    if (_expr)                                                                \
      break;                                                                  \
    printf("[VXDRV] Error: '%s' failed%s%s!\n", #_expr,                       \
           api_.get_last_error ? ": " : "",                                   \
           api_.get_last_error ? api_.get_last_error() : "");                 \
    _cleanup                                                                  \
  } while (false)

namespace {

uint64_t env_u64(const char* name, uint64_t default_value) {
  const char* value = getenv(name);
  if (value == nullptr || value[0] == '\0')
    return default_value;
  char* end = nullptr;
  auto parsed = strtoull(value, &end, 0);
  return (end == value) ? default_value : parsed;
}

uint32_t env_u32(const char* name, uint32_t default_value) {
  return static_cast<uint32_t>(env_u64(name, default_value));
}

const char* state_name(uint32_t state) {
  switch (state) {
  case 0: return "IDLE";
  case 1: return "INIT";
  case 2: return "RUN";
  default: return "?";
  }
}

} // namespace

class vx_device {
public:
  vx_device()
    : bar_(PCIE_BAR(VX_DE10PRO_DEFAULT_BAR))
    , mmio_base_(VX_DE10PRO_DEFAULT_MMIO_BASE)
    , staging_addr_(VX_DE10PRO_DEFAULT_STAGING)
    , staging_size_(VX_DE10PRO_DEFAULT_CHUNK_SIZE)
    , pcie_(nullptr)
    , dev_caps_(0)
    , isa_caps_(0)
    , global_mem_size_(0)
  {}

  ~vx_device() {
#ifdef SCOPE
    vx_scope_stop(this);
#endif
    if (pcie_ != nullptr) {
      api_.PCIE_Close(pcie_);
      pcie_ = nullptr;
    }
    drv_close();
  }

  int init() {
    memset(&api_, 0, sizeof(api_));
    if (drv_init(&api_) != 0)
      return -1;

    auto vendor_id = static_cast<uint16_t>(env_u32("TERASIC_PCIE_VENDOR_ID", DEFAULT_PCIE_VID));
    auto device_id = static_cast<uint16_t>(env_u32("TERASIC_PCIE_DEVICE_ID", DEFAULT_PCIE_DID));
    auto card_id   = static_cast<uint16_t>(env_u32("TERASIC_PCIE_CARD", 0));

    bar_ = PCIE_BAR(env_u32("DE10PRO_VX_BAR", VX_DE10PRO_DEFAULT_BAR));
    mmio_base_ = env_u64("DE10PRO_VX_MMIO_BASE", VX_DE10PRO_DEFAULT_MMIO_BASE);
    staging_addr_ = env_u64("DE10PRO_VX_STAGING_ADDR", VX_DE10PRO_DEFAULT_STAGING);
    staging_size_ = aligned_size(env_u64("DE10PRO_VX_STAGING_SIZE", VX_DE10PRO_DEFAULT_CHUNK_SIZE), CACHE_BLOCK_SIZE);

    pcie_ = api_.PCIE_Open(vendor_id, device_id, card_id);
    if (pcie_ == nullptr) {
      printf("[VXDRV] Error: PCIE_Open returned NULL!\n");
      drv_close();
      return -1;
    }

    CHECK_ERR(mmio_read64(AFU_IMAGE_MMIO_ISA_CAPS, &isa_caps_), {
      return err;
    });

    CHECK_ERR(mmio_read64(AFU_IMAGE_MMIO_DEV_CAPS, &dev_caps_), {
      return err;
    });

    uint64_t num_banks, bank_size;
    CHECK_ERR(this->get_caps(VX_CAPS_NUM_MEM_BANKS, &num_banks), {
      return err;
    });
    CHECK_ERR(this->get_caps(VX_CAPS_MEM_BANK_SIZE, &bank_size), {
      return err;
    });

    global_mem_size_ = num_banks * bank_size;
    if (global_mem_size_ <= ALLOC_BASE_ADDR)
      return -1;

    global_mem_ = std::make_unique<MemoryAllocator>(
      ALLOC_BASE_ADDR,
      global_mem_size_ - ALLOC_BASE_ADDR,
      RAM_PAGE_SIZE,
      CACHE_BLOCK_SIZE);

#ifdef SCOPE
    {
      scope_callback_t callback;
      callback.registerWrite = [](vx_device_h hdevice, uint64_t value) -> int {
        auto* device = reinterpret_cast<vx_device*>(hdevice);
        return device->mmio_write64(AFU_IMAGE_MMIO_SCOPE_WRITE, value);
      };
      callback.registerRead = [](vx_device_h hdevice, uint64_t* value) -> int {
        auto* device = reinterpret_cast<vx_device*>(hdevice);
        return device->mmio_read64(AFU_IMAGE_MMIO_SCOPE_READ, value);
      };

      CHECK_ERR(vx_scope_start(&callback, this, -1, -1), {
        return err;
      });
    }
#endif

    return 0;
  }

  int get_caps(uint32_t caps_id, uint64_t* value) {
    uint64_t _value;
    switch (caps_id) {
    case VX_CAPS_VERSION:
      _value = (dev_caps_ >> 0) & 0xff;
      break;
    case VX_CAPS_NUM_THREADS:
      _value = (dev_caps_ >> 8) & 0xff;
      break;
    case VX_CAPS_NUM_WARPS:
      _value = (dev_caps_ >> 16) & 0xff;
      break;
    case VX_CAPS_NUM_CORES:
      _value = (dev_caps_ >> 24) & 0xffff;
      break;
    case VX_CAPS_CACHE_LINE_SIZE:
      _value = CACHE_BLOCK_SIZE;
      break;
    case VX_CAPS_GLOBAL_MEM_SIZE:
      _value = global_mem_size_;
      break;
    case VX_CAPS_LOCAL_MEM_SIZE:
      _value = 1ull << ((dev_caps_ >> 40) & 0xff);
      break;
    case VX_CAPS_ISA_FLAGS:
      _value = isa_caps_;
      break;
    case VX_CAPS_NUM_MEM_BANKS:
      _value = 1ull << ((dev_caps_ >> 48) & 0x7);
      break;
    case VX_CAPS_MEM_BANK_SIZE:
      _value = 1ull << (20 + ((dev_caps_ >> 51) & 0x1f));
      break;
    default:
      fprintf(stderr, "[VXDRV] Error: invalid caps id: %u\n", caps_id);
      return -1;
    }

    *value = _value;
    return 0;
  }

  int mem_alloc(uint64_t size, int flags, uint64_t* dev_addr) {
    uint64_t addr;
    CHECK_ERR(global_mem_->allocate(size, &addr), {
      return err;
    });
    CHECK_ERR(this->mem_access(addr, size, flags), {
      global_mem_->release(addr);
      return err;
    });
    *dev_addr = addr;
    return 0;
  }

  int mem_reserve(uint64_t dev_addr, uint64_t size, int flags) {
    CHECK_ERR(global_mem_->reserve(dev_addr, size), {
      return err;
    });
    CHECK_ERR(this->mem_access(dev_addr, size, flags), {
      global_mem_->release(dev_addr);
      return err;
    });
    return 0;
  }

  int mem_free(uint64_t dev_addr) {
    return global_mem_->release(dev_addr);
  }

  int mem_access(uint64_t, uint64_t, int) {
    return 0;
  }

  int mem_info(uint64_t* mem_free, uint64_t* mem_used) const {
    if (mem_free)
      *mem_free = global_mem_->free();
    if (mem_used)
      *mem_used = global_mem_->allocated();
    return 0;
  }

  uint64_t to_local_addr(uint64_t dev_addr) const {
    return staging_addr_ + dev_addr;
  }

  int upload(uint64_t dev_addr, const void* host_ptr, uint64_t size) {
    if (!is_aligned(dev_addr, CACHE_BLOCK_SIZE))
      return -1;

    auto asize = aligned_size(size, CACHE_BLOCK_SIZE);
    if (dev_addr + asize > global_mem_size_)
      return -1;

    CHECK_ERR(this->ready_wait(VX_MAX_TIMEOUT), {
      return err;
    });

    std::vector<uint8_t> scratch;
    auto* src = reinterpret_cast<const uint8_t*>(host_ptr);

    for (uint64_t offset = 0; offset < size;) {
      auto chunk_bytes = std::min(staging_size_, size - offset);
      auto chunk_dma_bytes = aligned_size(chunk_bytes, CACHE_BLOCK_SIZE);

      const void* dma_src = src + offset;
      if (chunk_dma_bytes != chunk_bytes) {
        scratch.assign(chunk_dma_bytes, 0);
        std::memcpy(scratch.data(), src + offset, chunk_bytes);
        dma_src = scratch.data();
      }

      CHECK_PCIE(api_.PCIE_DmaWrite(pcie_, to_local_addr(dev_addr + offset), const_cast<void*>(dma_src),
                                    static_cast<uint32_t>(chunk_dma_bytes)), {
        return -1;
      });

      offset += chunk_bytes;
    }

    return 0;
  }

  int download(void* host_ptr, uint64_t dev_addr, uint64_t size) {
    if (!is_aligned(dev_addr, CACHE_BLOCK_SIZE))
      return -1;

    auto asize = aligned_size(size, CACHE_BLOCK_SIZE);
    if (dev_addr + asize > global_mem_size_)
      return -1;

    CHECK_ERR(this->ready_wait(VX_MAX_TIMEOUT), {
      return err;
    });

    std::vector<uint8_t> scratch;
    auto* dst = reinterpret_cast<uint8_t*>(host_ptr);

    for (uint64_t offset = 0; offset < size;) {
      auto chunk_bytes = std::min(staging_size_, size - offset);
      auto chunk_dma_bytes = aligned_size(chunk_bytes, CACHE_BLOCK_SIZE);

      if (chunk_dma_bytes != chunk_bytes) {
        scratch.resize(chunk_dma_bytes);
        CHECK_PCIE(api_.PCIE_DmaRead(pcie_, to_local_addr(dev_addr + offset), scratch.data(),
                                     static_cast<uint32_t>(chunk_dma_bytes)), {
          return -1;
        });
        std::memcpy(dst + offset, scratch.data(), chunk_bytes);
      } else {
        CHECK_PCIE(api_.PCIE_DmaRead(pcie_, to_local_addr(dev_addr + offset), dst + offset,
                                     static_cast<uint32_t>(chunk_dma_bytes)), {
          return -1;
        });
      }

      offset += chunk_bytes;
    }

    return 0;
  }

  int start(uint64_t krnl_addr, uint64_t args_addr) {
    CHECK_ERR(this->dcr_write(VX_DCR_BASE_STARTUP_ADDR0, krnl_addr & 0xffffffff), {
      return err;
    });
    CHECK_ERR(this->dcr_write(VX_DCR_BASE_STARTUP_ADDR1, krnl_addr >> 32), {
      return err;
    });
    CHECK_ERR(this->dcr_write(VX_DCR_BASE_STARTUP_ARG0, args_addr & 0xffffffff), {
      return err;
    });
    CHECK_ERR(this->dcr_write(VX_DCR_BASE_STARTUP_ARG1, args_addr >> 32), {
      return err;
    });
    CHECK_ERR(mmio_write32(AFU_IMAGE_MMIO_CMD_TYPE, CMD_RUN), {
      return err;
    });
    mpm_cache_.clear();
    return 0;
  }

  int ready_wait(uint64_t timeout) {
    timeout = env_u64("DE10PRO_VX_TIMEOUT_MS", timeout);
    auto verbose_status = env_u32("DE10PRO_VX_VERBOSE_STATUS", 0) != 0;
    std::unordered_map<uint32_t, std::stringstream> print_bufs;

    struct timespec sleep_time;
    sleep_time.tv_sec = 0;
    sleep_time.tv_nsec = 1000000;
    uint64_t sleep_time_ms = (sleep_time.tv_sec * 1000) + (sleep_time.tv_nsec / 1000000);
    uint32_t last_state = 0xffffffffu;
    uint64_t elapsed_ms = 0;

    for (;;) {
      uint64_t status;
      CHECK_ERR(mmio_read64(AFU_IMAGE_MMIO_STATUS, &status), {
        return err;
      });

      uint32_t cout_data = status >> STATUS_STATE_BITS;
      if (cout_data & 0x1) {
        do {
          char cout_char = (cout_data >> 1) & 0xff;
          uint32_t cout_tid = (cout_data >> 9) & 0xff;
          auto& ss_buf = print_bufs[cout_tid];
          ss_buf << cout_char;
          if (cout_char == '\n') {
            std::cout << std::dec << "#" << cout_tid << ": " << ss_buf.str() << std::flush;
            ss_buf.str("");
          }
          CHECK_ERR(mmio_read64(AFU_IMAGE_MMIO_STATUS, &status), {
            return err;
          });
          cout_data = status >> STATUS_STATE_BITS;
        } while (cout_data & 0x1);
      }

      uint32_t state = status & ((1u << STATUS_STATE_BITS) - 1);
      if (verbose_status && state != last_state) {
        std::cout << "[VXDRV] status state=" << state_name(state)
                  << " (" << state << "), status=0x"
                  << std::hex << status << std::dec << std::endl;
        last_state = state;
      }
      if (state == 0 || timeout == 0) {
        for (auto& buf : print_bufs) {
          auto str = buf.second.str();
          if (!str.empty()) {
            std::cout << "#" << buf.first << ": " << str << std::endl;
          }
        }
        if (state != 0) {
          fprintf(stdout, "[VXDRV] ready-wait timed out: state=%s(%u), elapsed=%llu ms, status=0x%llx\n",
                  state_name(state), state,
                  (unsigned long long)elapsed_ms,
                  (unsigned long long)status);
          return -1;
        }
        break;
      }

      nanosleep(&sleep_time, nullptr);
      timeout -= sleep_time_ms;
      elapsed_ms += sleep_time_ms;
    }

    return 0;
  }

  int dcr_write(uint32_t addr, uint32_t value) {
    CHECK_ERR(mmio_write64(AFU_IMAGE_MMIO_CMD_ARG0, addr), {
      return err;
    });
    CHECK_ERR(mmio_write64(AFU_IMAGE_MMIO_CMD_ARG1, value), {
      return err;
    });
    CHECK_ERR(mmio_write32(AFU_IMAGE_MMIO_CMD_TYPE, CMD_DCR_WRITE), {
      return err;
    });
    dcrs_.write(addr, value);
    return 0;
  }

  int dcr_read(uint32_t addr, uint32_t* value) const {
    return dcrs_.read(addr, value);
  }

  int mpm_query(uint32_t addr, uint32_t core_id, uint64_t* value) {
    uint32_t offset = addr - VX_CSR_MPM_BASE;
    if (offset > 31)
      return -1;
    if (mpm_cache_.count(core_id) == 0) {
      uint64_t mpm_mem_addr = IO_MPM_ADDR + core_id * 32 * sizeof(uint64_t);
      CHECK_ERR(this->download(mpm_cache_[core_id].data(), mpm_mem_addr, 32 * sizeof(uint64_t)), {
        return err;
      });
    }
    *value = mpm_cache_.at(core_id).at(offset);
    return 0;
  }

private:
  int mmio_read32(uint64_t reg, uint32_t* value) const {
    CHECK_PCIE(api_.PCIE_Read32(pcie_, bar_, mmio_base_ + reg, value), {
      return -1;
    });
    return 0;
  }

  int mmio_write32(uint64_t reg, uint32_t value) const {
    CHECK_PCIE(api_.PCIE_Write32(pcie_, bar_, mmio_base_ + reg, value), {
      return -1;
    });
    return 0;
  }

  int mmio_read64(uint64_t reg, uint64_t* value) const {
    uint32_t lo, hi;
    CHECK_ERR(mmio_read32(reg + 0, &lo), {
      return err;
    });
    CHECK_ERR(mmio_read32(reg + 4, &hi), {
      return err;
    });
    *value = (uint64_t(hi) << 32) | lo;
    return 0;
  }

  int mmio_write64(uint64_t reg, uint64_t value) const {
    CHECK_ERR(mmio_write32(reg + 0, uint32_t(value & 0xffffffff)), {
      return err;
    });
    CHECK_ERR(mmio_write32(reg + 4, uint32_t(value >> 32)), {
      return err;
    });
    return 0;
  }

  de10pro_drv_api_t api_;
  PCIE_BAR bar_;
  uint64_t mmio_base_;
  uint64_t staging_addr_;
  uint64_t staging_size_;
  PCIE_HANDLE pcie_;
  std::unique_ptr<MemoryAllocator> global_mem_;
  DeviceConfig dcrs_;
  uint64_t dev_caps_;
  uint64_t isa_caps_;
  uint64_t global_mem_size_;
  std::unordered_map<uint32_t, std::array<uint64_t, 32>> mpm_cache_;
};

#include <callbacks.inc>
