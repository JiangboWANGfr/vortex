#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <unistd.h>
#include <vector>
#include <vortex.h>
#include "../keccak_common/keccak_vectors.h"
#include "common.h"

#if defined(KECCAK_BENCH_DISPATCH_WARP) && defined(KECCAK_BENCH_DISPATCH_LANE)
#error "KECCAK_BENCH dispatch mode is over-specified"
#elif !defined(KECCAK_BENCH_DISPATCH_WARP) && !defined(KECCAK_BENCH_DISPATCH_LANE)
#define KECCAK_BENCH_DISPATCH_WARP
#endif

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
uint32_t dataset_cases = 256;
std::string dataset_kind = "byte-long";
std::string rsp_path;

vx_device_h device = nullptr;
vx_buffer_h msg_buffer = nullptr;
vx_buffer_h bit_len_buffer = nullptr;
vx_buffer_h digest_buffer = nullptr;
vx_buffer_h status_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;

static void show_usage() {
  std::cout << "Vortex Keccak benchmark." << std::endl;
  std::cout << "Usage: [-k kernel] [-n messages_per_task] [-c dataset_cases] [-d dataset_kind] [-r rsp_path]" << std::endl;
}

static void parse_args(int argc, char** argv) {
  int c;
  while ((c = getopt(argc, argv, "k:n:c:d:r:h")) != -1) {
    switch (c) {
    case 'k':
      kernel_file = optarg;
      break;
    case 'n':
      messages_per_task = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 0));
      break;
    case 'c':
      dataset_cases = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 0));
      break;
    case 'd':
      dataset_kind = optarg;
      break;
    case 'r':
      rsp_path = optarg;
      break;
    case 'h':
      show_usage();
      exit(0);
    default:
      show_usage();
      exit(-1);
    }
  }

  if (messages_per_task == 0 || dataset_cases == 0) {
    show_usage();
    exit(-1);
  }
}

static void cleanup() {
  if (device) {
    if (msg_buffer) {
      vx_mem_free(msg_buffer);
    }
    if (bit_len_buffer) {
      vx_mem_free(bit_len_buffer);
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

static bool digest_matches(const uint8_t* got, const std::array<uint8_t, keccak_test::kKeccakDigestBytes>& expected) {
  for (size_t i = 0; i < expected.size(); ++i) {
    if (got[i] != expected[i]) {
      return false;
    }
  }
  return true;
}

static uint64_t ceil_div_u64(uint64_t value, uint64_t divisor) {
  return (value + divisor - 1) / divisor;
}

static uint64_t round_up_u64(uint64_t value, uint64_t alignment) {
  return ceil_div_u64(value, alignment) * alignment;
}

static const char* dispatch_mode_name() {
#ifdef KECCAK_BENCH_DISPATCH_WARP
  return "warp";
#else
  return "lane";
#endif
}

static uint64_t compute_num_tasks(uint64_t num_cores,
                                  uint64_t num_warps,
                                  uint64_t num_threads,
                                  uint64_t dataset_size,
                                  uint32_t messages_per_task) {
  if (num_cores == 0) {
    return 0;
  }
#ifdef KECCAK_BENCH_DISPATCH_WARP
  (void)num_threads;
  uint64_t warp_slots = num_cores * num_warps;
  uint64_t dataset_warps = ceil_div_u64(dataset_size, messages_per_task);
  return round_up_u64(std::max<uint64_t>(warp_slots, dataset_warps), num_cores);
#else
  uint64_t lane_slots = num_cores * num_warps * num_threads;
  uint64_t lane_tasks = std::max<uint64_t>(lane_slots, ceil_div_u64(dataset_size, messages_per_task));
  return round_up_u64(lane_tasks, num_cores);
#endif
}

static const keccak_test::KeccakVector& select_vector(const std::vector<keccak_test::KeccakVector>& dataset,
                                                      uint64_t task_id,
                                                      uint32_t messages_per_task,
                                                      uint32_t slot_idx) {
  return dataset[(task_id * messages_per_task + slot_idx) % dataset.size()];
}

int main(int argc, char** argv) {
  parse_args(argc, argv);

  try {
    std::string effective_rsp = rsp_path.empty() ? keccak_test::default_rsp_for_kind(dataset_kind) : rsp_path;
    auto dataset = keccak_test::load_rsp_vectors(effective_rsp, dataset_cases);
    if (dataset.empty()) {
      std::cerr << "no Keccak vectors loaded from " << effective_rsp << std::endl;
      return -1;
    }

    std::cout << "open device connection" << std::endl;
    RT_CHECK(vx_dev_open(&device));

    uint64_t num_cores = 0;
    uint64_t num_warps = 0;
    uint64_t num_threads = 0;
    RT_CHECK(vx_dev_caps(device, VX_CAPS_NUM_CORES, &num_cores));
    RT_CHECK(vx_dev_caps(device, VX_CAPS_NUM_WARPS, &num_warps));
    RT_CHECK(vx_dev_caps(device, VX_CAPS_NUM_THREADS, &num_threads));

    uint64_t num_tasks64 = compute_num_tasks(num_cores, num_warps, num_threads, dataset.size(), messages_per_task);
    if (num_tasks64 == 0 || num_tasks64 > std::numeric_limits<uint32_t>::max() ||
        num_threads == 0 || num_threads > std::numeric_limits<uint32_t>::max()) {
      std::cerr << "invalid device task count: " << num_tasks64 << std::endl;
      cleanup();
      return -1;
    }

    uint32_t msg_stride = 1;
    for (const auto& vec : dataset) {
      msg_stride = std::max(msg_stride, keccak_test::bytes_for_bits(vec.bit_len));
    }

    uint64_t total_messages = num_tasks64 * messages_per_task;
    uint64_t msg_bytes = total_messages * msg_stride;
    uint64_t bit_len_bytes = total_messages * sizeof(uint64_t);
    uint64_t digest_bytes = total_messages * keccak_test::kKeccakDigestBytes;

    std::cout << "KECCAK_BENCH: dataset=" << dataset_kind
              << ", dataset_cases=" << dataset.size()
              << ", dispatch_mode=" << dispatch_mode_name()
              << ", messages_per_task=" << messages_per_task
              << ", msg_stride=" << msg_stride
              << ", tasks=" << num_tasks64
              << ", total_messages=" << total_messages << std::endl;

    kernel_arg_t kernel_arg = {};
    kernel_arg.num_tasks = static_cast<uint32_t>(num_tasks64);
    kernel_arg.messages_per_task = messages_per_task;
    kernel_arg.msg_stride = msg_stride;

    std::cout << "allocate device memory" << std::endl;
    RT_CHECK(vx_mem_alloc(device, msg_bytes, VX_MEM_READ_WRITE, &msg_buffer));
    RT_CHECK(vx_mem_address(msg_buffer, &kernel_arg.msg_addr));
    RT_CHECK(vx_mem_alloc(device, bit_len_bytes, VX_MEM_READ_WRITE, &bit_len_buffer));
    RT_CHECK(vx_mem_address(bit_len_buffer, &kernel_arg.bit_len_addr));
    RT_CHECK(vx_mem_alloc(device, digest_bytes, VX_MEM_READ_WRITE, &digest_buffer));
    RT_CHECK(vx_mem_address(digest_buffer, &kernel_arg.digest_addr));
    RT_CHECK(vx_mem_alloc(device, sizeof(keccak_bench_status_t), VX_MEM_READ_WRITE, &status_buffer));
    RT_CHECK(vx_mem_address(status_buffer, &kernel_arg.status_addr));

    std::vector<uint8_t> msg_data(msg_bytes, 0);
    std::vector<uint64_t> bit_lens(total_messages, 0);
    std::vector<uint8_t> expected(digest_bytes, 0);
    uint64_t total_bits = 0;

    for (uint64_t task_id = 0; task_id < num_tasks64; ++task_id) {
      for (uint32_t i = 0; i < messages_per_task; ++i) {
        uint64_t msg_idx = task_id * messages_per_task + i;
        const auto& vec = select_vector(dataset, task_id, messages_per_task, i);
        std::memcpy(msg_data.data() + (msg_idx * msg_stride), vec.msg.data(), keccak_test::bytes_for_bits(vec.bit_len));
        bit_lens[msg_idx] = vec.bit_len;
        std::memcpy(expected.data() + (msg_idx * keccak_test::kKeccakDigestBytes), vec.digest.data(), vec.digest.size());
        total_bits += vec.bit_len;
      }
    }

    std::cout << "KECCAK_BENCH: total_bits=" << total_bits << std::endl;

    std::vector<uint8_t> digest(digest_bytes, 0);
    keccak_bench_status_t status = {};

    std::cout << "upload message buffer" << std::endl;
    RT_CHECK(vx_copy_to_dev(msg_buffer, msg_data.data(), 0, msg_data.size()));
    std::cout << "upload bit lengths" << std::endl;
    RT_CHECK(vx_copy_to_dev(bit_len_buffer, bit_lens.data(), 0, bit_len_bytes));
    std::cout << "clear digest buffer" << std::endl;
    RT_CHECK(vx_copy_to_dev(digest_buffer, digest.data(), 0, digest.size()));
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

    uint32_t errors = status.errors;
    for (uint64_t task_id = 0; task_id < num_tasks64; ++task_id) {
      for (uint32_t i = 0; i < messages_per_task; ++i) {
        uint64_t msg_idx = task_id * messages_per_task + i;
        if (digest_matches(digest.data() + (msg_idx * keccak_test::kKeccakDigestBytes),
                           select_vector(dataset, task_id, messages_per_task, i).digest)) {
          continue;
        }
        ++errors;
        std::cout << "Keccak bench mismatch message=" << msg_idx << std::endl;
        break;
      }
      if (errors != 0) {
        break;
      }
    }

    std::cout << "KECCAK_BENCH: digest_checksum=0x" << std::hex
              << keccak_test::digest_checksum(digest) << std::dec << std::endl;

    std::cout << "cleanup" << std::endl;
    cleanup();

    if (errors != 0) {
      std::cout << "KECCAK_BENCH FAILED: errors=" << errors << std::endl;
      return errors;
    }

    std::cout << "KECCAK_BENCH PASSED!" << std::endl;
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Keccak bench setup failed: " << ex.what() << std::endl;
    cleanup();
    return -1;
  }
}
