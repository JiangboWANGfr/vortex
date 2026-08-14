// Copyright © 2019-2023
// Licensed under the Apache License, Version 2.0.

#include "driver.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/ioctl.h>
#include <new>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

constexpr const char* kDevicePath = "/dev/intel_fpga_pcie_drv";
constexpr uint32_t kMaxDmaSize = 1024 * 1024;
constexpr uint64_t kDmaEndpointBias = 0x10000;

struct intel_fpga_pcie_arg {
  uint64_t ep_addr;
  void* user_addr;
  uint32_t size;
  bool is_read;
} __attribute__((packed));

static_assert(sizeof(void*) == 8,
              "Intel PCIe ioctl ABI requires 64-bit userspace");
static_assert(sizeof(intel_fpga_pcie_arg) == 21,
              "unexpected Intel PCIe DMA argument layout");

#define INTEL_FPGA_PCIE_IOCTL_MAGIC 0x70
#define INTEL_FPGA_PCIE_IOCTL_CHR_SEL_DEV \
  _IOW(INTEL_FPGA_PCIE_IOCTL_MAGIC, 0, unsigned int)
#define INTEL_FPGA_PCIE_IOCTL_CHR_GET_DEV \
  _IOR(INTEL_FPGA_PCIE_IOCTL_MAGIC, 1, unsigned int*)
#define INTEL_FPGA_PCIE_IOCTL_CHR_SEL_BAR \
  _IOW(INTEL_FPGA_PCIE_IOCTL_MAGIC, 2, unsigned int)
#define INTEL_FPGA_PCIE_IOCTL_SET_KMEM_SIZE \
  _IOW(INTEL_FPGA_PCIE_IOCTL_MAGIC, 7, unsigned int)
#define INTEL_FPGA_PCIE_IOCTL_DMA_QUEUE \
  _IOWR(INTEL_FPGA_PCIE_IOCTL_MAGIC, 8, struct intel_fpga_pcie_arg*)
#define INTEL_FPGA_PCIE_IOCTL_DMA_SEND \
  _IOW(INTEL_FPGA_PCIE_IOCTL_MAGIC, 9, unsigned int)

struct device_state {
  int fd;
  void* kmem;
  uint32_t kmem_size;
};

thread_local char g_last_error[512];

void set_error(const char* message) {
  std::snprintf(g_last_error, sizeof(g_last_error), "%s", message);
}

void set_errno_error(const char* action) {
  std::snprintf(g_last_error, sizeof(g_last_error), "%s: %s", action,
                std::strerror(errno));
}

device_state* to_state(pcie_handle_t handle) {
  return static_cast<device_state*>(handle);
}

bool dma_transfer(device_state* state, pcie_local_address_t address,
                  void* data, uint32_t size, bool is_read) {
  if (state == nullptr || data == nullptr) {
    set_error("invalid DMA argument");
    return false;
  }
  if (size < sizeof(uint32_t) || (size % sizeof(uint32_t)) != 0
   || size > state->kmem_size || size > kMaxDmaSize) {
    set_error("DMA size must be 4-byte aligned and no larger than 1 MiB");
    return false;
  }
  if (address < kDmaEndpointBias) {
    set_error("DMA endpoint address is below the Gen3x16 address bias");
    return false;
  }

  if (!is_read) {
    std::memcpy(state->kmem, data, size);
  }

  intel_fpga_pcie_arg arg{};
  arg.ep_addr = address - kDmaEndpointBias;
  arg.user_addr = nullptr;
  arg.size = size;
  arg.is_read = is_read;
  if (ioctl(state->fd, INTEL_FPGA_PCIE_IOCTL_DMA_QUEUE, &arg) != 0) {
    set_errno_error("DMA_QUEUE ioctl");
    return false;
  }

  const unsigned int direction = is_read ? 0x1u : 0x2u;
  if (ioctl(state->fd, INTEL_FPGA_PCIE_IOCTL_DMA_SEND, direction) != 0) {
    set_errno_error("DMA_SEND ioctl");
    return false;
  }

  if (is_read) {
    std::memcpy(data, state->kmem, size);
  }
  return true;
}

} // namespace

pcie_handle_t drv_open(uint32_t bdf, pcie_bar_t bar, uint32_t kmem_size) {
  g_last_error[0] = '\0';
  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    set_error("failed to query system page size");
    return nullptr;
  }
  if (kmem_size == 0 || kmem_size > kMaxDmaSize
   || (kmem_size % static_cast<uint32_t>(page_size)) != 0) {
    set_error("DMA staging size must be page-aligned and no larger than 1 MiB");
    return nullptr;
  }

  const int fd = open(kDevicePath, O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    set_errno_error("open /dev/intel_fpga_pcie_drv");
    return nullptr;
  }

  unsigned int selected_bdf = bdf;
  if (bdf != 0) {
    if (ioctl(fd, INTEL_FPGA_PCIE_IOCTL_CHR_SEL_DEV, bdf) != 0) {
      set_errno_error("CHR_SEL_DEV ioctl");
      close(fd);
      return nullptr;
    }
  } else if (ioctl(fd, INTEL_FPGA_PCIE_IOCTL_CHR_GET_DEV,
                   &selected_bdf) != 0) {
    set_errno_error("CHR_GET_DEV ioctl");
    close(fd);
    return nullptr;
  }

  if (ioctl(fd, INTEL_FPGA_PCIE_IOCTL_CHR_SEL_BAR, bar) != 0) {
    set_errno_error("CHR_SEL_BAR ioctl");
    close(fd);
    return nullptr;
  }

  if (ioctl(fd, INTEL_FPGA_PCIE_IOCTL_SET_KMEM_SIZE, kmem_size) != 0) {
    set_errno_error("SET_KMEM_SIZE ioctl");
    close(fd);
    return nullptr;
  }

  void* kmem = mmap(nullptr, kmem_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd, 0);
  if (kmem == MAP_FAILED) {
    set_errno_error("mmap DMA staging memory");
    (void)ioctl(fd, INTEL_FPGA_PCIE_IOCTL_SET_KMEM_SIZE, 0u);
    close(fd);
    return nullptr;
  }

  auto* state = new (std::nothrow) device_state{fd, kmem, kmem_size};
  if (state == nullptr) {
    set_error("failed to allocate PCIe driver state");
    munmap(kmem, kmem_size);
    (void)ioctl(fd, INTEL_FPGA_PCIE_IOCTL_SET_KMEM_SIZE, 0u);
    close(fd);
    return nullptr;
  }
  return state;
}

void drv_close(pcie_handle_t handle) {
  auto* state = to_state(handle);
  if (state == nullptr) {
    return;
  }
  munmap(state->kmem, state->kmem_size);
  (void)ioctl(state->fd, INTEL_FPGA_PCIE_IOCTL_SET_KMEM_SIZE, 0u);
  close(state->fd);
  delete state;
}

bool drv_read32(pcie_handle_t handle, pcie_address_t address,
                uint32_t* value) {
  auto* state = to_state(handle);
  if (state == nullptr || value == nullptr) {
    set_error("invalid BAR read argument");
    return false;
  }
  const ssize_t result = pread(state->fd, value, sizeof(*value),
                               static_cast<off_t>(address));
  if (result != static_cast<ssize_t>(sizeof(*value))) {
    if (result < 0) {
      set_errno_error("BAR pread");
    } else {
      set_error("short BAR read");
    }
    return false;
  }
  return true;
}

bool drv_write32(pcie_handle_t handle, pcie_address_t address,
                 uint32_t value) {
  auto* state = to_state(handle);
  if (state == nullptr) {
    set_error("invalid BAR write argument");
    return false;
  }
  const ssize_t result = pwrite(state->fd, &value, sizeof(value),
                                static_cast<off_t>(address));
  if (result != static_cast<ssize_t>(sizeof(value))) {
    if (result < 0) {
      set_errno_error("BAR pwrite");
    } else {
      set_error("short BAR write");
    }
    return false;
  }
  return true;
}

bool drv_dma_read(pcie_handle_t handle, pcie_local_address_t address,
                  void* data, uint32_t size) {
  return dma_transfer(to_state(handle), address, data, size, true);
}

bool drv_dma_write(pcie_handle_t handle, pcie_local_address_t address,
                   const void* data, uint32_t size) {
  return dma_transfer(to_state(handle), address, const_cast<void*>(data),
                      size, false);
}

const char* drv_get_last_error() {
  return g_last_error;
}
