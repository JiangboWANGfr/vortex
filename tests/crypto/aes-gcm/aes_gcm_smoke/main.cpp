// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <unistd.h>
#include <vortex.h>
#include "common.h"

#define RT_CHECK(_expr)                                         \
   do {                                                         \
     int _ret = _expr;                                          \
     if (0 == _ret)                                             \
       break;                                                   \
     printf("Error: '%s' returned %d!\n", #_expr, (int)_ret);   \
     cleanup();                                                 \
     exit(-1);                                                  \
   } while (false)

namespace {

const char* kernel_file = "kernel.vxbin";
const char* kKatNames[] = { "TC13_empty", "TC14_oneblock", "TC15_64B", "TC16_aad_partial" };

vx_device_h device = nullptr;
vx_buffer_h status_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

void show_usage() {
  std::cout << "Vortex AES-256-GCM smoke (NIST KAT)." << std::endl;
  std::cout << "Usage: [-k kernel] [-h]" << std::endl;
}

void parse_args(int argc, char** argv) {
  int c;
  while ((c = getopt(argc, argv, "k:h")) != -1) {
    switch (c) {
    case 'k': kernel_file = optarg; break;
    case 'h': show_usage(); exit(0);
    default:  show_usage(); exit(-1);
    }
  }
}

void cleanup() {
  if (device) {
    if (status_buffer) vx_mem_free(status_buffer);
    if (krnl_buffer)   vx_mem_free(krnl_buffer);
    if (args_buffer)   vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

} // namespace

int main(int argc, char** argv) {
  parse_args(argc, argv);

  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  std::cout << "allocate device memory" << std::endl;
  RT_CHECK(vx_mem_alloc(device, sizeof(gcm_smoke_status_t), VX_MEM_READ_WRITE, &status_buffer));
  RT_CHECK(vx_mem_address(status_buffer, &kernel_arg.status_addr));

  gcm_smoke_status_t status = {};

  std::cout << "upload kernel binary" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));
  std::cout << "upload kernel argument" << std::endl;
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));
  std::cout << "initialize status buffer" << std::endl;
  RT_CHECK(vx_copy_to_dev(status_buffer, &status, 0, sizeof(status)));

  std::cout << "start device" << std::endl;
  RT_CHECK(vx_start(device, krnl_buffer, args_buffer));
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

  std::cout << "download status buffer" << std::endl;
  RT_CHECK(vx_copy_from_dev(&status, status_buffer, 0, sizeof(status)));

  cleanup();

  if (status.errors != 0) {
    std::cout << "AES-256-GCM smoke FAILED: errors=" << std::dec << status.errors
              << "/" << status.num_cases << ", failed_mask=0x"
              << std::hex << status.failed_mask << std::dec << std::endl;
    for (uint32_t i = 0; i < status.num_cases && i < 4; ++i) {
      if (status.failed_mask & (1u << i))
        std::cout << "  - " << kKatNames[i] << std::endl;
    }
    return static_cast<int>(status.errors);
  }

  std::cout << "AES-256-GCM smoke PASSED (" << std::dec << status.num_cases
            << " NIST vectors)" << std::endl;
  return 0;
}
