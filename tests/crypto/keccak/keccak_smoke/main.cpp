#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
#include <vortex.h>
#include "../keccak_common/keccak_vectors.h"
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
std::string byte_short_rsp = keccak_test::kDefaultByteShortRsp;
std::string byte_long_rsp = keccak_test::kDefaultByteLongRsp;
std::string bit_short_rsp = keccak_test::kDefaultBitShortRsp;
std::string bit_long_rsp = keccak_test::kDefaultBitLongRsp;
uint32_t short_cases = 16;
uint32_t long_cases = 4;

vx_device_h device = nullptr;
vx_buffer_h msg_buffer = nullptr;
vx_buffer_h bit_len_buffer = nullptr;
vx_buffer_h digest_buffer = nullptr;
vx_buffer_h status_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;

static void show_usage() {
  std::cout << "Vortex Keccak smoke regression." << std::endl;
  std::cout << "Usage: [-k kernel] [-n short_cases] [-l long_cases]" << std::endl;
}

static void parse_args(int argc, char** argv) {
  int c;
  while ((c = getopt(argc, argv, "k:n:l:b:B:t:T:h")) != -1) {
    switch (c) {
    case 'k':
      kernel_file = optarg;
      break;
    case 'n':
      short_cases = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 0));
      break;
    case 'l':
      long_cases = static_cast<uint32_t>(std::strtoul(optarg, nullptr, 0));
      break;
    case 'b':
      byte_short_rsp = optarg;
      break;
    case 'B':
      byte_long_rsp = optarg;
      break;
    case 't':
      bit_short_rsp = optarg;
      break;
    case 'T':
      bit_long_rsp = optarg;
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

static void append_vectors(const std::string& path,
                           uint32_t limit,
                           std::vector<keccak_test::KeccakVector>* out) {
  if (limit == 0) {
    return;
  }
  auto vectors = keccak_test::load_rsp_vectors(path, limit);
  out->insert(out->end(), vectors.begin(), vectors.end());
}

static bool digest_matches(const uint8_t* got, const std::array<uint8_t, keccak_test::kKeccakDigestBytes>& expected) {
  for (size_t i = 0; i < expected.size(); ++i) {
    if (got[i] != expected[i]) {
      return false;
    }
  }
  return true;
}

static uint32_t digest_word(const uint8_t* digest, uint32_t offset) {
  return (uint32_t(digest[offset]) << 24)
       | (uint32_t(digest[offset + 1]) << 16)
       | (uint32_t(digest[offset + 2]) << 8)
       | uint32_t(digest[offset + 3]);
}

int main(int argc, char** argv) {
  parse_args(argc, argv);

  try {
    std::vector<keccak_test::KeccakVector> vectors;
    append_vectors(byte_short_rsp, short_cases, &vectors);
    append_vectors(bit_short_rsp, short_cases, &vectors);
    append_vectors(byte_long_rsp, long_cases, &vectors);
    append_vectors(bit_long_rsp, long_cases, &vectors);

    if (vectors.empty()) {
      std::cerr << "no Keccak vectors loaded" << std::endl;
      return -1;
    }

    uint32_t msg_stride = 1;
    for (const auto& vec : vectors) {
      msg_stride = std::max(msg_stride, keccak_test::bytes_for_bits(vec.bit_len));
    }

    std::cout << "open device connection" << std::endl;
    RT_CHECK(vx_dev_open(&device));

    uint64_t total_cases = vectors.size();
    uint64_t msg_bytes = total_cases * msg_stride;
    uint64_t bit_len_bytes = total_cases * sizeof(uint64_t);
    uint64_t digest_bytes = total_cases * keccak_test::kKeccakDigestBytes;

    kernel_arg_t kernel_arg = {};
    kernel_arg.num_tasks = 1;
    kernel_arg.cases_per_task = static_cast<uint32_t>(total_cases);
    kernel_arg.msg_stride = msg_stride;

    std::cout << "allocate device memory" << std::endl;
    RT_CHECK(vx_mem_alloc(device, msg_bytes, VX_MEM_READ_WRITE, &msg_buffer));
    RT_CHECK(vx_mem_address(msg_buffer, &kernel_arg.msg_addr));
    RT_CHECK(vx_mem_alloc(device, bit_len_bytes, VX_MEM_READ_WRITE, &bit_len_buffer));
    RT_CHECK(vx_mem_address(bit_len_buffer, &kernel_arg.bit_len_addr));
    RT_CHECK(vx_mem_alloc(device, digest_bytes, VX_MEM_READ_WRITE, &digest_buffer));
    RT_CHECK(vx_mem_address(digest_buffer, &kernel_arg.digest_addr));
    RT_CHECK(vx_mem_alloc(device, sizeof(keccak_smoke_status_t), VX_MEM_READ_WRITE, &status_buffer));
    RT_CHECK(vx_mem_address(status_buffer, &kernel_arg.status_addr));

    std::vector<uint8_t> msg_data(msg_bytes, 0);
    std::vector<uint64_t> bit_lens(total_cases, 0);
    std::vector<uint8_t> expected(digest_bytes, 0);

    for (size_t i = 0; i < vectors.size(); ++i) {
      const auto& vec = vectors[i];
      std::memcpy(msg_data.data() + (i * msg_stride), vec.msg.data(), keccak_test::bytes_for_bits(vec.bit_len));
      bit_lens[i] = vec.bit_len;
      std::memcpy(expected.data() + (i * keccak_test::kKeccakDigestBytes), vec.digest.data(), vec.digest.size());
    }

    std::vector<uint8_t> digest(digest_bytes, 0);
    keccak_smoke_status_t status = {};

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

    std::cout << "cleanup" << std::endl;
    cleanup();

    uint32_t errors = status.errors;
    for (size_t i = 0; i < vectors.size(); ++i) {
      const uint8_t* got = digest.data() + (i * keccak_test::kKeccakDigestBytes);
      if (!digest_matches(got, vectors[i].digest)) {
        std::cout << "Keccak smoke mismatch case=" << i
                  << " source=" << vectors[i].source
                  << " len_bits=" << vectors[i].bit_len
                  << " got=" << std::hex << digest_word(got, 0) << digest_word(got, 4)
                  << std::dec << std::endl;
        ++errors;
      }
    }

    if (errors != 0) {
      std::cout << "KECCAK_SMOKE FAILED: errors=" << errors << std::endl;
      return errors;
    }

    std::cout << "KECCAK_SMOKE PASSED! cases=" << total_cases
              << ", completed_cases=" << status.completed_cases << std::endl;
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Keccak smoke setup failed: " << ex.what() << std::endl;
    cleanup();
    return -1;
  }
}
