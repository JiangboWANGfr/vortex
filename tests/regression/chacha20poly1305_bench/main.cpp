// SPDX-License-Identifier: Apache-2.0
//
// Host driver for the ChaCha20-Poly1305 AEAD throughput / overhead benchmark.
// Generates num_tasks buffers of bytes_per_task each under a shared key, runs the
// kernel, and folds the per-task tags into a single checksum. SOFTWARE and NATIVE
// must produce the same checksum (cross-check); UNPROTECTED folds the moved data.
// The framework's "PERF: ... cycles=N" line carries the timing; this driver adds
// "CHACHA_BENCH: ... checksum=0x...".

#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <vector>
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
uint32_t num_tasks = 32;
uint32_t bytes_per_task = 1024;

vx_device_h device = nullptr;
vx_buffer_h data_buffer = nullptr;
vx_buffer_h out_buffer = nullptr;
vx_buffer_h tag_buffer = nullptr;
vx_buffer_h key_buffer = nullptr;
vx_buffer_h status_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

void show_usage() {
  std::cout << "Vortex ChaCha20-Poly1305 AEAD throughput benchmark." << std::endl;
  std::cout << "Usage: [-k kernel] [-t num_tasks] [-n bytes_per_task] [-h]" << std::endl;
}

void parse_args(int argc, char** argv) {
  int c;
  while ((c = getopt(argc, argv, "k:t:n:h")) != -1) {
    switch (c) {
    case 'k': kernel_file = optarg; break;
    case 't': num_tasks = (uint32_t)std::strtoul(optarg, nullptr, 0); break;
    case 'n': bytes_per_task = (uint32_t)std::strtoul(optarg, nullptr, 0); break;
    case 'h': show_usage(); exit(0);
    default:  show_usage(); exit(-1);
    }
  }
}

void cleanup() {
  if (device) {
    if (data_buffer)   vx_mem_free(data_buffer);
    if (out_buffer)    vx_mem_free(out_buffer);
    if (tag_buffer)    vx_mem_free(tag_buffer);
    if (key_buffer)    vx_mem_free(key_buffer);
    if (status_buffer) vx_mem_free(status_buffer);
    if (krnl_buffer)   vx_mem_free(krnl_buffer);
    if (args_buffer)   vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

} // namespace

int main(int argc, char** argv) {
  parse_args(argc, argv);
  if (num_tasks == 0 || bytes_per_task == 0 || (bytes_per_task % 16) != 0) {
    std::cerr << "num_tasks>0 and bytes_per_task>0 multiple of 16 required" << std::endl;
    return -1;
  }

  const uint64_t data_bytes = (uint64_t)num_tasks * bytes_per_task;
  const uint64_t tag_bytes = (uint64_t)num_tasks * 16;
  const uint64_t total_bytes = data_bytes;

  std::cout << "CHACHA_BENCH: num_tasks=" << num_tasks
            << ", bytes_per_task=" << bytes_per_task
            << ", total_bytes=" << total_bytes << std::endl;

  std::vector<uint8_t> data(data_bytes);
  uint8_t key[32];
  for (int i = 0; i < 32; ++i) key[i] = (uint8_t)(i * 7u + 1u);

  RT_CHECK(vx_dev_open(&device));

  for (uint64_t i = 0; i < data_bytes; ++i)
    data[i] = (uint8_t)(i * 131u + 0x5Au);

  RT_CHECK(vx_mem_alloc(device, data_bytes, VX_MEM_READ_WRITE, &data_buffer));
  RT_CHECK(vx_mem_address(data_buffer, &kernel_arg.data_addr));
  RT_CHECK(vx_mem_alloc(device, data_bytes, VX_MEM_READ_WRITE, &out_buffer));
  RT_CHECK(vx_mem_address(out_buffer, &kernel_arg.out_addr));
  RT_CHECK(vx_mem_alloc(device, tag_bytes, VX_MEM_READ_WRITE, &tag_buffer));
  RT_CHECK(vx_mem_address(tag_buffer, &kernel_arg.tag_addr));
  RT_CHECK(vx_mem_alloc(device, 32, VX_MEM_READ_WRITE, &key_buffer));
  RT_CHECK(vx_mem_address(key_buffer, &kernel_arg.key_addr));
  RT_CHECK(vx_mem_alloc(device, sizeof(ccp_bench_status_t), VX_MEM_READ_WRITE, &status_buffer));
  RT_CHECK(vx_mem_address(status_buffer, &kernel_arg.status_addr));
  kernel_arg.num_tasks = num_tasks;
  kernel_arg.bytes_per_task = bytes_per_task;

  ccp_bench_status_t status = {};
  std::vector<uint8_t> tags(tag_bytes, 0);

  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));
  RT_CHECK(vx_copy_to_dev(data_buffer, data.data(), 0, data_bytes));
  RT_CHECK(vx_copy_to_dev(key_buffer, key, 0, 32));
  RT_CHECK(vx_copy_to_dev(tag_buffer, tags.data(), 0, tag_bytes));
  RT_CHECK(vx_copy_to_dev(status_buffer, &status, 0, sizeof(status)));

  RT_CHECK(vx_start(device, krnl_buffer, args_buffer));
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

  RT_CHECK(vx_copy_from_dev(&status, status_buffer, 0, sizeof(status)));
  RT_CHECK(vx_copy_from_dev(tags.data(), tag_buffer, 0, tag_bytes));
  cleanup();

  // Fold all per-task tags into one 128-bit checksum.
  uint8_t checksum[16] = {0};
  for (uint64_t t = 0; t < num_tasks; ++t)
    for (int i = 0; i < 16; ++i)
      checksum[i] ^= tags[t * 16 + i];

  std::cout << "CHACHA_BENCH: checksum=0x";
  for (int i = 0; i < 16; ++i)
    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)checksum[i];
  std::cout << std::dec << std::endl;

  if (status.errors != 0) {
    std::cout << "ChaCha20-Poly1305 bench FAILED: errors=" << status.errors << std::endl;
    return (int)status.errors;
  }
  std::cout << "ChaCha20-Poly1305 bench PASSED (" << status.completed_tasks << " streams, "
            << status.total_bytes << " bytes)" << std::endl;
  return 0;
}
