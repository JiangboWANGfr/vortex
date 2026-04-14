#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
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
uint32_t messages_per_task = 1;
uint32_t msg_bytes = 128;

vx_device_h device = nullptr;
vx_buffer_h msg_buffer = nullptr;
vx_buffer_h digest_buffer = nullptr;
vx_buffer_h status_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;

static uint32_t padded_size_bytes(uint32_t n_bytes) {
  uint32_t mod = n_bytes & 63U;
  uint32_t pad = (mod < 56U) ? (56U - mod) : (120U - mod);
  return n_bytes + pad + 8U;
}

static void show_usage() {
  std::cout << "Vortex SHA256 benchmark." << std::endl;
  std::cout << "Usage: [-k kernel] [-n messages_per_task] [-m msg_bytes] [-h]" << std::endl;
}

static void parse_args(int argc, char** argv) {
  int c;
  while ((c = getopt(argc, argv, "k:n:m:h")) != -1) {
    switch (c) {
    case 'k':
      kernel_file = optarg;
      break;
    case 'n':
      messages_per_task = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 0));
      break;
    case 'm':
      msg_bytes = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 0));
      break;
    case 'h':
      show_usage();
      exit(0);
    default:
      show_usage();
      exit(-1);
    }
  }

  if (messages_per_task == 0 || msg_bytes == 0) {
    show_usage();
    exit(-1);
  }
}

static void cleanup() {
  if (device) {
    if (msg_buffer) {
      vx_mem_free(msg_buffer);
    }
    if (digest_buffer) {
      vx_mem_free(digest_buffer);
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

static uint32_t digest_checksum(const std::vector<uint8_t>& digest) {
  uint32_t checksum = 0x6a09e667u;
  for (uint8_t byte : digest) {
    checksum = (checksum << 5) ^ (checksum >> 2) ^ byte;
  }
  return checksum;
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

  uint32_t padded_msg_bytes = padded_size_bytes(msg_bytes);
  uint64_t total_messages = num_tasks64 * messages_per_task;
  uint64_t total_bytes = total_messages * msg_bytes;
  uint64_t msgbuf_bytes = total_messages * padded_msg_bytes;
  uint64_t digest_bytes = total_messages * SHA256_DIGEST_BYTES;

  if (msgbuf_bytes > std::numeric_limits<uint32_t>::max() || digest_bytes > std::numeric_limits<uint32_t>::max()) {
    std::cerr << "benchmark buffer too large: msgbuf=" << msgbuf_bytes
              << ", digest=" << digest_bytes << " bytes" << std::endl;
    cleanup();
    return -1;
  }

  std::cout << "SHA256_BENCH: messages_per_task=" << messages_per_task
            << ", msg_bytes=" << msg_bytes
            << ", padded_msg_bytes=" << padded_msg_bytes
            << ", tasks=" << num_tasks64
            << ", total_messages=" << total_messages
            << ", total_bytes=" << total_bytes << std::endl;

  kernel_arg_t kernel_arg = {};
  kernel_arg.num_tasks = static_cast<uint32_t>(num_tasks64);
  kernel_arg.messages_per_task = messages_per_task;
  kernel_arg.msg_bytes = msg_bytes;
  kernel_arg.padded_msg_bytes = padded_msg_bytes;

  std::cout << "allocate device memory" << std::endl;
  RT_CHECK(vx_mem_alloc(device, msgbuf_bytes, VX_MEM_READ_WRITE, &msg_buffer));
  RT_CHECK(vx_mem_address(msg_buffer, &kernel_arg.msg_addr));
  RT_CHECK(vx_mem_alloc(device, digest_bytes, VX_MEM_READ_WRITE, &digest_buffer));
  RT_CHECK(vx_mem_address(digest_buffer, &kernel_arg.digest_addr));
  RT_CHECK(vx_mem_alloc(device, sizeof(sha256_bench_status_t), VX_MEM_READ_WRITE, &status_buffer));
  RT_CHECK(vx_mem_address(status_buffer, &kernel_arg.status_addr));

  std::cout << "dev_msg=0x" << std::hex << kernel_arg.msg_addr << std::endl;
  std::cout << "dev_digest=0x" << kernel_arg.digest_addr << std::endl;
  std::cout << "dev_status=0x" << kernel_arg.status_addr << std::dec << std::endl;

  std::vector<uint8_t> input(msgbuf_bytes, 0);
  for (uint64_t msg_idx = 0; msg_idx < total_messages; ++msg_idx) {
    uint8_t* msg = input.data() + (msg_idx * padded_msg_bytes);
    for (uint32_t j = 0; j < msg_bytes; ++j) {
      msg[j] = myrand();
    }
  }

  std::cout << "upload message buffer" << std::endl;
  RT_CHECK(vx_copy_to_dev(msg_buffer, input.data(), 0, input.size()));

  std::vector<uint8_t> digest(digest_bytes, 0);
  std::cout << "clear digest buffer" << std::endl;
  RT_CHECK(vx_copy_to_dev(digest_buffer, digest.data(), 0, digest.size()));

  sha256_bench_status_t status = {};
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

  std::cout << "download digest buffer" << std::endl;
  RT_CHECK(vx_copy_from_dev(digest.data(), digest_buffer, 0, digest.size()));

  std::cout << "SHA256_BENCH: digest_checksum=0x" << std::hex
            << digest_checksum(digest) << std::dec << std::endl;

  std::cout << "cleanup" << std::endl;
  cleanup();

  if (status.errors != 0) {
    std::cout << "SHA256_BENCH FAILED: errors=" << status.errors << std::endl;
    return status.errors;
  }

  std::cout << "SHA256_BENCH PASSED!" << std::endl;
  return 0;
}
