// SPDX-License-Identifier: Apache-2.0
//
// Host driver for the warp-affine GHASH benchmark. Generates num_tasks
// independent streams of blocks_per_task 16-byte blocks under a shared H,
// computes the expected GHASH tags with the software reference (always
// software on the host), runs the kernel, and verifies every tag. The
// framework's "PERF: ... cycles=N ..." line carries the timing; this driver
// adds a "GHASH_BENCH:" line with the workload dimensions.

#include <iostream>
#include <unistd.h>
#include <vector>
#include <cstring>
#include <vortex.h>
#include "common.h"
#include "../ghash_smoke/ghash_ref.h"

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
uint32_t num_tasks = 32;        // independent GHASH streams (warps x cores)
uint32_t blocks_per_task = 64;  // 16-byte blocks per stream

vx_device_h device = nullptr;
vx_buffer_h data_buffer = nullptr;
vx_buffer_h h_buffer = nullptr;
vx_buffer_h tag_buffer = nullptr;
vx_buffer_h status_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

// H = AES-128(key=0, plaintext=0) — the canonical reference hash subkey.
const uint8_t kH[GHASH_BLOCK_BYTES] = {
  0x66, 0xe9, 0x4b, 0xd4, 0xef, 0x8a, 0x2c, 0x3b,
  0x88, 0x4c, 0xfa, 0x59, 0xca, 0x34, 0x2b, 0x2e,
};

void show_usage() {
  std::cout << "Vortex GHASH benchmark (warp-affine)." << std::endl;
  std::cout << "Usage: [-k kernel] [-t num_tasks] [-n blocks_per_task] [-h]" << std::endl;
}

void parse_args(int argc, char** argv) {
  int c;
  while ((c = getopt(argc, argv, "k:t:n:h")) != -1) {
    switch (c) {
    case 'k': kernel_file = optarg; break;
    case 't': num_tasks = (uint32_t)std::strtoul(optarg, nullptr, 0); break;
    case 'n': blocks_per_task = (uint32_t)std::strtoul(optarg, nullptr, 0); break;
    case 'h': show_usage(); exit(0);
    default:  show_usage(); exit(-1);
    }
  }
}

void cleanup() {
  if (device) {
    if (data_buffer)   vx_mem_free(data_buffer);
    if (h_buffer)      vx_mem_free(h_buffer);
    if (tag_buffer)    vx_mem_free(tag_buffer);
    if (status_buffer) vx_mem_free(status_buffer);
    if (krnl_buffer)   vx_mem_free(krnl_buffer);
    if (args_buffer)   vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

// Deterministic synthetic stream byte: distinct per (task, offset).
uint8_t gen_byte(uint32_t task, uint32_t off) {
  return (uint8_t)(task * 131u + off * 17u + 0x5Au);
}

} // namespace

int main(int argc, char** argv) {
  parse_args(argc, argv);
  if (num_tasks == 0 || blocks_per_task == 0) {
    std::cerr << "num_tasks and blocks_per_task must be > 0" << std::endl;
    return -1;
  }

  const uint64_t stream_bytes = (uint64_t)blocks_per_task * GHASH_BLOCK_BYTES;
  const uint64_t data_bytes = (uint64_t)num_tasks * stream_bytes;
  const uint64_t tag_bytes = (uint64_t)num_tasks * GHASH_BLOCK_BYTES;
  const uint64_t total_blocks = (uint64_t)num_tasks * blocks_per_task;

  std::cout << "GHASH_BENCH: num_tasks=" << num_tasks
            << ", blocks_per_task=" << blocks_per_task
            << ", total_blocks=" << total_blocks
            << ", total_bytes=" << data_bytes << std::endl;

  // Build input data and software-reference expected tags.
  std::vector<uint8_t> data(data_bytes);
  for (uint32_t t = 0; t < num_tasks; ++t) {
    for (uint64_t off = 0; off < stream_bytes; ++off) {
      data[t * stream_bytes + off] = gen_byte(t, (uint32_t)off);
    }
  }
  std::vector<uint8_t> expected(tag_bytes);
  for (uint32_t t = 0; t < num_tasks; ++t) {
    ghash_oneshot(kH, data.data() + t * stream_bytes, stream_bytes,
                  expected.data() + t * GHASH_BLOCK_BYTES);
  }

  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  std::cout << "allocate device memory" << std::endl;
  RT_CHECK(vx_mem_alloc(device, data_bytes, VX_MEM_READ_WRITE, &data_buffer));
  RT_CHECK(vx_mem_address(data_buffer, &kernel_arg.data_addr));
  RT_CHECK(vx_mem_alloc(device, GHASH_BLOCK_BYTES, VX_MEM_READ_WRITE, &h_buffer));
  RT_CHECK(vx_mem_address(h_buffer, &kernel_arg.h_addr));
  RT_CHECK(vx_mem_alloc(device, tag_bytes, VX_MEM_READ_WRITE, &tag_buffer));
  RT_CHECK(vx_mem_address(tag_buffer, &kernel_arg.tag_addr));
  RT_CHECK(vx_mem_alloc(device, sizeof(ghash_bench_status_t), VX_MEM_READ_WRITE, &status_buffer));
  RT_CHECK(vx_mem_address(status_buffer, &kernel_arg.status_addr));
  kernel_arg.num_tasks = num_tasks;
  kernel_arg.blocks_per_task = blocks_per_task;

  ghash_bench_status_t status = {};
  std::vector<uint8_t> tags(tag_bytes, 0);

  std::cout << "upload kernel binary" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));
  std::cout << "upload kernel argument" << std::endl;
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

  std::cout << "upload buffers" << std::endl;
  RT_CHECK(vx_copy_to_dev(data_buffer, data.data(), 0, data_bytes));
  RT_CHECK(vx_copy_to_dev(h_buffer, kH, 0, GHASH_BLOCK_BYTES));
  RT_CHECK(vx_copy_to_dev(tag_buffer, tags.data(), 0, tag_bytes));
  RT_CHECK(vx_copy_to_dev(status_buffer, &status, 0, sizeof(status)));

  std::cout << "start device" << std::endl;
  RT_CHECK(vx_start(device, krnl_buffer, args_buffer));
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

  std::cout << "download results" << std::endl;
  RT_CHECK(vx_copy_from_dev(&status, status_buffer, 0, sizeof(status)));
  RT_CHECK(vx_copy_from_dev(tags.data(), tag_buffer, 0, tag_bytes));

  cleanup();

  uint32_t mismatches = 0;
  for (uint32_t t = 0; t < num_tasks; ++t) {
    if (std::memcmp(tags.data() + t * GHASH_BLOCK_BYTES,
                    expected.data() + t * GHASH_BLOCK_BYTES,
                    GHASH_BLOCK_BYTES) != 0) {
      ++mismatches;
    }
  }

  if (status.errors != 0 || mismatches != 0) {
    std::cout << "GHASH bench FAILED: status.errors=" << status.errors
              << ", tag_mismatches=" << mismatches
              << ", completed_tasks=" << status.completed_tasks << std::endl;
    return (int)(status.errors + mismatches);
  }

  std::cout << "GHASH bench PASSED (" << status.completed_tasks
            << " streams, " << status.total_blocks << " blocks)" << std::endl;
  return 0;
}
