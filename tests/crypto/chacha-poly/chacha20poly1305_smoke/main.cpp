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

vx_device_h device = nullptr;
vx_buffer_h status_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

void show_usage() {
  std::cout << "Vortex ChaCha20-Poly1305 smoke (RFC 8439 KAT)." << std::endl;
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

  RT_CHECK(vx_dev_open(&device));
  RT_CHECK(vx_mem_alloc(device, sizeof(ccp_smoke_status_t), VX_MEM_READ_WRITE, &status_buffer));
  RT_CHECK(vx_mem_address(status_buffer, &kernel_arg.status_addr));

  ccp_smoke_status_t status = {};
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));
  RT_CHECK(vx_copy_to_dev(status_buffer, &status, 0, sizeof(status)));

  RT_CHECK(vx_start(device, krnl_buffer, args_buffer));
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));
  RT_CHECK(vx_copy_from_dev(&status, status_buffer, 0, sizeof(status)));
  cleanup();

  if (status.errors != 0) {
    std::cout << "ChaCha20-Poly1305 smoke FAILED: failed_mask=0x"
              << std::hex << status.failed_mask << std::dec << std::endl;
    if (status.failed_mask & CCP_FAIL_CHACHA)   std::cout << "  - chacha20 block (RFC 2.3.2)" << std::endl;
    if (status.failed_mask & CCP_FAIL_POLY1305) std::cout << "  - poly1305 (RFC 2.5.2)" << std::endl;
    if (status.failed_mask & CCP_FAIL_AEAD_CT)  std::cout << "  - aead ciphertext (RFC 2.8.2)" << std::endl;
    if (status.failed_mask & CCP_FAIL_AEAD_TAG) std::cout << "  - aead tag (RFC 2.8.2)" << std::endl;
    return (int)status.errors;
  }
  std::cout << "ChaCha20-Poly1305 smoke PASSED (RFC 8439 2.3.2 + 2.5.2 + 2.8.2)" << std::endl;
  return 0;
}
