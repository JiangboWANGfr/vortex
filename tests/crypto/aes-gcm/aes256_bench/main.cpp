#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <unistd.h>
#include <vector>
#include <vortex.h>
#include "common.h"

#define RT_CHECK(_expr)                                         \
  do {                                                          \
    int _ret = _expr;                                           \
    if (0 == _ret)                                              \
      break;                                                    \
    printf("Error: '%s' returned %d!\n", #_expr, (int)_ret);    \
    cleanup();                                                  \
    exit(-1);                                                   \
  } while (false)

const char* kernel_file = "kernel.vxbin";
uint32_t nblocks_per_task = 1;
uint32_t op_type = AES256_BENCH_CTR_ENC;

vx_device_h device = nullptr;
vx_buffer_h src_buffer = nullptr;
vx_buffer_h dst_buffer = nullptr;
vx_buffer_h status_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;

static bool is_key_op(uint32_t op) {
  return op == AES256_BENCH_KEY_ENC || op == AES256_BENCH_KEY_DEC;
}

static const char* op_name(uint32_t op) {
  switch (op) {
  case AES256_BENCH_ECB_ENC: return "ecb-enc";
  case AES256_BENCH_ECB_DEC: return "ecb-dec";
  case AES256_BENCH_CBC_ENC: return "cbc-enc";
  case AES256_BENCH_CBC_DEC: return "cbc-dec";
  case AES256_BENCH_CTR_ENC: return "ctr-enc";
  case AES256_BENCH_CTR_DEC: return "ctr-dec";
  case AES256_BENCH_KEY_ENC: return "key-enc";
  case AES256_BENCH_KEY_DEC: return "key-dec";
  default: return "unknown";
  }
}

static void show_usage() {
  std::cout << "Vortex AES256 benchmark." << std::endl;
  std::cout << "Usage: [-k kernel] [-n blocks_per_task] [-t op_type] [-h]" << std::endl;
  std::cout << "  op_type: 0 ecb-enc, 1 ecb-dec, 2 cbc-enc, 3 cbc-dec,"
            << " 4 ctr-enc, 5 ctr-dec, 6 key-enc, 7 key-dec" << std::endl;
}

static void parse_args(int argc, char** argv) {
  int c;
  while ((c = getopt(argc, argv, "k:n:t:h")) != -1) {
    switch (c) {
    case 'k':
      kernel_file = optarg;
      break;
    case 'n':
      nblocks_per_task = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 0));
      break;
    case 't':
      op_type = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 0));
      break;
    case 'h':
      show_usage();
      exit(0);
    default:
      show_usage();
      exit(-1);
    }
  }

  if (nblocks_per_task == 0 || op_type >= AES256_BENCH_OP_COUNT) {
    show_usage();
    exit(-1);
  }
}

static void cleanup() {
  if (device) {
    if (src_buffer) {
      vx_mem_free(src_buffer);
    }
    if (dst_buffer) {
      vx_mem_free(dst_buffer);
    }
    if (status_buffer) {
      vx_mem_free(status_buffer);
    }
    if (krnl_buffer) {
      vx_mem_free(krnl_buffer);
    }
    if (args_buffer) {
      vx_mem_free(args_buffer);
    }
    vx_dev_close(device);
    device = nullptr;
  }
}

static uint8_t myrand() {
  static uint32_t next = 1;
  next = next * 1103515245 + 12345;
  return static_cast<uint8_t>((next / 65536) & 0xff);
}

int main(int argc, char** argv) {
  parse_args(argc, argv);

  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  uint64_t num_cores = 0;
  uint64_t num_warps = 0;
  uint64_t num_threads = 0;
  RT_CHECK(vx_dev_caps(device, VX_CAPS_NUM_CORES, &num_cores));
  RT_CHECK(vx_dev_caps(device, VX_CAPS_NUM_WARPS, &num_warps));
  RT_CHECK(vx_dev_caps(device, VX_CAPS_NUM_THREADS, &num_threads));

  uint64_t num_tasks64 = num_cores * num_warps * num_threads;
  if (num_tasks64 == 0 || num_tasks64 > std::numeric_limits<uint32_t>::max()) {
    std::cerr << "invalid device task count: " << num_tasks64 << std::endl;
    cleanup();
    return -1;
  }

  uint64_t total_blocks = num_tasks64 * nblocks_per_task;
  uint64_t data_bytes = total_blocks * BLOCK_SIZE;
  uint64_t dst_bytes = is_key_op(op_type) ? (num_tasks64 * sizeof(uint32_t)) : data_bytes;

  if (!is_key_op(op_type) && data_bytes > std::numeric_limits<uint32_t>::max()) {
    std::cerr << "benchmark buffer too large: " << data_bytes << " bytes" << std::endl;
    cleanup();
    return -1;
  }

  std::cout << "AES256_BENCH: op=" << op_name(op_type) << "(" << op_type << ")"
            << ", blocks_per_task=" << nblocks_per_task
            << ", tasks=" << num_tasks64
            << ", total_blocks=" << total_blocks
            << ", total_bytes=" << data_bytes << std::endl;

  kernel_arg_t kernel_arg = {};
  kernel_arg.num_tasks = static_cast<uint32_t>(num_tasks64);
  kernel_arg.nblocks_per_task = nblocks_per_task;
  kernel_arg.op_type = op_type;

  for (uint32_t i = 0; i < KEY_SIZE; ++i) {
    kernel_arg.key[i] = myrand();
  }
  for (uint32_t i = 0; i < BLOCK_SIZE; ++i) {
    kernel_arg.iv[i] = myrand();
  }

  std::cout << "allocate device memory" << std::endl;
  if (!is_key_op(op_type)) {
    RT_CHECK(vx_mem_alloc(device, data_bytes, VX_MEM_READ, &src_buffer));
    RT_CHECK(vx_mem_address(src_buffer, &kernel_arg.in_addr));
  }
  RT_CHECK(vx_mem_alloc(device, std::max<uint64_t>(dst_bytes, 1), VX_MEM_READ_WRITE, &dst_buffer));
  RT_CHECK(vx_mem_address(dst_buffer, &kernel_arg.out_addr));
  RT_CHECK(vx_mem_alloc(device, sizeof(aes256_bench_status_t), VX_MEM_READ_WRITE, &status_buffer));
  RT_CHECK(vx_mem_address(status_buffer, &kernel_arg.status_addr));

  std::cout << "dev_in=0x" << std::hex << kernel_arg.in_addr << std::endl;
  std::cout << "dev_out=0x" << kernel_arg.out_addr << std::endl;
  std::cout << "dev_status=0x" << kernel_arg.status_addr << std::dec << std::endl;

  if (!is_key_op(op_type)) {
    std::vector<uint8_t> input(data_bytes);
    for (uint64_t i = 0; i < data_bytes; ++i) {
      input[i] = myrand();
    }
    std::cout << "upload input buffer" << std::endl;
    RT_CHECK(vx_copy_to_dev(src_buffer, input.data(), 0, data_bytes));
  }

  std::vector<uint8_t> output(std::max<uint64_t>(dst_bytes, 1), 0);
  std::cout << "clear output buffer" << std::endl;
  RT_CHECK(vx_copy_to_dev(dst_buffer, output.data(), 0, output.size()));

  aes256_bench_status_t status = {};
  std::cout << "initialize status buffer" << std::endl;
  RT_CHECK(vx_copy_to_dev(status_buffer, &status, 0, sizeof(status)));

  std::cout << "upload kernel binary" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));

  std::cout << "upload kernel argument" << std::endl;
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg), &args_buffer));

  std::cout << "start device" << std::endl;
  RT_CHECK(vx_start(device, krnl_buffer, args_buffer));

  std::cout << "wait for completion" << std::endl;
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

  std::cout << "download status buffer" << std::endl;
  RT_CHECK(vx_copy_from_dev(&status, status_buffer, 0, sizeof(status)));

  if (is_key_op(op_type)) {
    RT_CHECK(vx_copy_from_dev(output.data(), dst_buffer, 0, output.size()));
    uint32_t checksum = 0;
    for (uint64_t i = 0; i < output.size(); ++i) {
      checksum = (checksum << 5) ^ (checksum >> 2) ^ output[i];
    }
    std::cout << "AES256_BENCH: key_checksum=0x" << std::hex << checksum << std::dec << std::endl;
  }

  std::cout << "cleanup" << std::endl;
  cleanup();

  if (status.errors != 0) {
    std::cout << "AES256_BENCH FAILED: errors=" << status.errors << std::endl;
    return status.errors;
  }

  std::cout << "AES256_BENCH PASSED!" << std::endl;
  return 0;
}
