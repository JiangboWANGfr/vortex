// SPDX-License-Identifier: Apache-2.0
#include "common.h"
#include <iostream>
#include <unistd.h>
#include <vector>
#include <vortex.h>

#define RT_CHECK(_expr)                                      \
  do {                                                       \
    int _ret = _expr;                                        \
    if (0 == _ret)                                           \
      break;                                                 \
    printf("Error: '%s' returned %d!\n", #_expr, (int)_ret); \
    cleanup();                                               \
    exit(-1);                                                \
  } while (false)

namespace {

const char *kernel_file = "kernel.vxbin";
uint32_t num_tasks = 64;

vx_device_h device = nullptr;
vx_buffer_h status_buffer = nullptr;
vx_buffer_h task_status_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

void show_usage() {
  std::cout << "Vortex GHASH SIMT smoke." << std::endl;
  std::cout << "Usage: [-k kernel] [-n num_tasks] [-h]" << std::endl;
}

void parse_args(int argc, char **argv) {
  int c;
  while ((c = getopt(argc, argv, "k:n:h")) != -1) {
    switch (c) {
    case 'k':
      kernel_file = optarg;
      break;
    case 'n':
      num_tasks = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 0));
      break;
    case 'h':
      show_usage();
      exit(0);
    default:
      show_usage();
      exit(-1);
    }
  }
}

void cleanup() {
  if (device) {
    if (status_buffer)
      vx_mem_free(status_buffer);
    if (task_status_buffer)
      vx_mem_free(task_status_buffer);
    if (krnl_buffer)
      vx_mem_free(krnl_buffer);
    if (args_buffer)
      vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

struct CheckLabel {
  uint32_t bit;
  const char *name;
};
const CheckLabel kLabels[] = {
    {GHASH_SIMT_FAIL_EMPTY, "empty"},
    {GHASH_SIMT_FAIL_ZERO_BLOCK, "zero_block"},
    {GHASH_SIMT_FAIL_ZERO_H, "zero_H"},
    {GHASH_SIMT_FAIL_IDENTITY_1BLOCK, "identity_1block"},
    {GHASH_SIMT_FAIL_IDENTITY_2BLOCK, "identity_2block"},
    {GHASH_SIMT_FAIL_DETERMINISM, "determinism"},
    {GHASH_SIMT_FAIL_STREAMING, "streaming"},
    {GHASH_SIMT_FAIL_LINEARITY, "linearity"},
};

} // namespace

int main(int argc, char **argv) {
  parse_args(argc, argv);
  if (num_tasks == 0) {
    std::cerr << "num_tasks must be > 0" << std::endl;
    return -1;
  }

  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  std::cout << "allocate device memory (num_tasks=" << num_tasks << ")" << std::endl;
  RT_CHECK(vx_mem_alloc(device, sizeof(ghash_simt_status_t),
                        VX_MEM_READ_WRITE, &status_buffer));
  RT_CHECK(vx_mem_address(status_buffer, &kernel_arg.status_addr));

  size_t task_status_bytes = static_cast<size_t>(num_tasks) * sizeof(task_status_t);
  RT_CHECK(vx_mem_alloc(device, task_status_bytes,
                        VX_MEM_READ_WRITE, &task_status_buffer));
  RT_CHECK(vx_mem_address(task_status_buffer, &kernel_arg.task_status_addr));
  kernel_arg.num_tasks = num_tasks;

  std::vector<task_status_t> task_status_init(num_tasks, 0xff);
  ghash_simt_status_t status = {};

  std::cout << "upload kernel binary" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));

  std::cout << "upload kernel argument" << std::endl;
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

  std::cout << "initialize buffers" << std::endl;
  RT_CHECK(vx_copy_to_dev(status_buffer, &status, 0, sizeof(status)));
  RT_CHECK(vx_copy_to_dev(task_status_buffer, task_status_init.data(),
                          0, task_status_bytes));

  std::cout << "start device" << std::endl;
  RT_CHECK(vx_start(device, krnl_buffer, args_buffer));
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

  std::cout << "download status" << std::endl;
  RT_CHECK(vx_copy_from_dev(&status, status_buffer, 0, sizeof(status)));

  cleanup();

  std::cout << "completed_tasks=" << status.completed_tasks
            << " errors=" << status.errors
            << " failed_mask=0x" << std::hex << status.failed_mask
            << std::dec << std::endl;

  if (status.errors != 0) {
    std::cout << "GHASH SIMT smoke FAILED:" << std::endl;
    for (const auto &lbl : kLabels) {
      if (status.failed_mask & lbl.bit) {
        std::cout << "  - " << lbl.name << std::endl;
      }
    }
    return static_cast<int>(status.errors);
  }

  std::cout << "GHASH SIMT smoke PASSED (" << status.completed_tasks
            << " tasks)" << std::endl;
  return 0;
}
