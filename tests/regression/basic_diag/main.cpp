#include <chrono>
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <vortex.h>
#include "common.h"

#define NONCE 0xdeadbeef

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
int test = -1;
uint32_t count = 0;

vx_device_h device = nullptr;
vx_buffer_h src0_buffer = nullptr;
vx_buffer_h src1_buffer = nullptr;
vx_buffer_h dst_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

static void show_usage() {
  std::cout << "Vortex basic diagnostic." << std::endl;
  std::cout << "Usage: [-t testno] [-k kernel] [-n words] [-h]" << std::endl;
}

static void parse_args(int argc, char** argv) {
  int c;
  while ((c = getopt(argc, argv, "n:t:k:h")) != -1) {
    switch (c) {
    case 'n':
      count = atoi(optarg);
      break;
    case 't':
      test = atoi(optarg);
      break;
    case 'k':
      kernel_file = optarg;
      break;
    case 'h':
      show_usage();
      exit(0);
      break;
    default:
      show_usage();
      exit(-1);
    }
  }
}

void cleanup() {
  if (device) {
    vx_mem_free(src0_buffer);
    vx_mem_free(src1_buffer);
    vx_mem_free(dst_buffer);
    vx_mem_free(krnl_buffer);
    vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

inline uint32_t shuffle(int i, uint32_t value) {
  return (value << i) | (value & ((1 << i) - 1));
}

static uint32_t expected_value(const std::vector<uint32_t>& src0,
                               const std::vector<uint32_t>& src1,
                               uint32_t index) {
#ifdef BASIC_DIAG_ADD
#ifdef BASIC_DIAG_ALIAS_SRC1
  return src0[index] + src0[index];
#else
  return src0[index] + src1[index];
#endif
#elif defined(BASIC_DIAG_COPY_SRC1)
#ifdef BASIC_DIAG_ALIAS_SRC1
  return src0[index];
#else
  return src1[index];
#endif
#else
  return src0[index];
#endif
}

int run_memcopy_test(const kernel_arg_t&) {
  uint32_t num_points = kernel_arg.count;
  uint32_t buf_size = num_points * sizeof(int32_t);

  std::vector<uint32_t> h_src0(num_points);
  std::vector<uint32_t> h_src1(num_points);
  std::vector<uint32_t> h_src0_readback(num_points);
  std::vector<uint32_t> h_src1_readback(num_points);

  for (uint32_t i = 0; i < num_points; ++i) {
    h_src0[i] = shuffle(i, NONCE);
    h_src1[i] = shuffle(i + 1, NONCE);
  }

  auto time_start = std::chrono::high_resolution_clock::now();

  std::cout << "write source buffer0 to local memory" << std::endl;
  auto t0 = std::chrono::high_resolution_clock::now();
  RT_CHECK(vx_copy_to_dev(src0_buffer, h_src0.data(), 0, buf_size));
#ifndef BASIC_DIAG_ALIAS_SRC1
  std::cout << "write source buffer1 to local memory" << std::endl;
  RT_CHECK(vx_copy_to_dev(src1_buffer, h_src1.data(), 0, buf_size));
#endif
  auto t1 = std::chrono::high_resolution_clock::now();

  std::cout << "read source buffer0 from local memory" << std::endl;
  auto t2 = std::chrono::high_resolution_clock::now();
  RT_CHECK(vx_copy_from_dev(h_src0_readback.data(), src0_buffer, 0, buf_size));
#ifndef BASIC_DIAG_ALIAS_SRC1
  std::cout << "read source buffer1 from local memory" << std::endl;
  RT_CHECK(vx_copy_from_dev(h_src1_readback.data(), src1_buffer, 0, buf_size));
#endif
  auto t3 = std::chrono::high_resolution_clock::now();

  int errors = 0;
  std::cout << "verify source buffer0" << std::endl;
  for (uint32_t i = 0; i < num_points; ++i) {
    auto ref = h_src0[i];
    auto cur = h_src0_readback[i];
    if (cur != ref) {
      printf("*** src0 error: [%d] expected=%d, actual=%d\n", i, ref, cur);
      ++errors;
    }
  }

  std::cout << "verify source buffer1" << std::endl;
  for (uint32_t i = 0; i < num_points; ++i) {
#ifdef BASIC_DIAG_ALIAS_SRC1
    auto ref = h_src0[i];
    auto cur = h_src0_readback[i];
#else
    auto ref = h_src1[i];
    auto cur = h_src1_readback[i];
#endif
    if (cur != ref) {
      printf("*** src1 error: [%d] expected=%d, actual=%d\n", i, ref, cur);
      ++errors;
    }
  }

  auto time_end = std::chrono::high_resolution_clock::now();

  double elapsed;
  elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  printf("upload time: %lg ms\n", elapsed);
  elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
  printf("download time: %lg ms\n", elapsed);
  elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(time_end - time_start).count();
  printf("Total elapsed time: %lg ms\n", elapsed);

  return errors;
}

int run_kernel_test(const kernel_arg_t&) {
  uint32_t num_points = kernel_arg.count;
  uint32_t buf_size = num_points * sizeof(int32_t);

  std::vector<uint32_t> h_src0(num_points);
  std::vector<uint32_t> h_src1(num_points);
  std::vector<uint32_t> h_dst(num_points);

  for (uint32_t i = 0; i < num_points; ++i) {
    h_src0[i] = shuffle(i, NONCE);
    h_src1[i] = shuffle(i + 1, NONCE);
  }

  std::cout << "Upload kernel binary" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));

  std::cout << "upload kernel argument" << std::endl;
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

  auto time_start = std::chrono::high_resolution_clock::now();

  auto t0 = std::chrono::high_resolution_clock::now();
  std::cout << "upload source buffer0" << std::endl;
  RT_CHECK(vx_copy_to_dev(src0_buffer, h_src0.data(), 0, buf_size));
#ifndef BASIC_DIAG_ALIAS_SRC1
  std::cout << "upload source buffer1" << std::endl;
  RT_CHECK(vx_copy_to_dev(src1_buffer, h_src1.data(), 0, buf_size));
#endif
  auto t1 = std::chrono::high_resolution_clock::now();

  std::cout << "start execution" << std::endl;
  auto t2 = std::chrono::high_resolution_clock::now();
  RT_CHECK(vx_start(device, krnl_buffer, args_buffer));
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));
  auto t3 = std::chrono::high_resolution_clock::now();

  std::cout << "read destination buffer from local memory" << std::endl;
  auto t4 = std::chrono::high_resolution_clock::now();
  RT_CHECK(vx_copy_from_dev(h_dst.data(), dst_buffer, 0, buf_size));
  auto t5 = std::chrono::high_resolution_clock::now();

  int errors = 0;
  std::cout << "verify result" << std::endl;
  for (uint32_t i = 0; i < num_points; ++i) {
    auto ref = expected_value(h_src0, h_src1, i);
    auto cur = h_dst[i];
    if (cur != ref) {
      printf("*** error: [%d] expected=%d, actual=%d\n", i, ref, cur);
      ++errors;
    }
  }

  auto time_end = std::chrono::high_resolution_clock::now();

  double elapsed;
  elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  printf("upload time: %lg ms\n", elapsed);
  elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
  printf("execute time: %lg ms\n", elapsed);
  elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t5 - t4).count();
  printf("download time: %lg ms\n", elapsed);
  elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(time_end - time_start).count();
  printf("Total elapsed time: %lg ms\n", elapsed);

  return errors;
}

int main(int argc, char* argv[]) {
  parse_args(argc, argv);

  if (count == 0) {
    count = 1;
  }

  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  uint64_t num_cores;
  RT_CHECK(vx_dev_caps(device, VX_CAPS_NUM_CORES, &num_cores));

  uint32_t num_points = count * num_cores;
  uint32_t buf_size = num_points * sizeof(int32_t);

  std::cout << "number of points: " << num_points << std::endl;
  std::cout << "buffer size: " << buf_size << " bytes" << std::endl;

  std::cout << "allocate device memory" << std::endl;
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_READ, &src0_buffer));
  RT_CHECK(vx_mem_address(src0_buffer, &kernel_arg.src0_addr));
#ifdef BASIC_DIAG_ALIAS_SRC1
  kernel_arg.src1_addr = kernel_arg.src0_addr;
#else
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_READ, &src1_buffer));
  RT_CHECK(vx_mem_address(src1_buffer, &kernel_arg.src1_addr));
#endif
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_WRITE, &dst_buffer));
  RT_CHECK(vx_mem_address(dst_buffer, &kernel_arg.dst_addr));

  kernel_arg.count = count;

  std::cout << "dev_src0=0x" << std::hex << kernel_arg.src0_addr << std::endl;
  std::cout << "dev_src1=0x" << std::hex << kernel_arg.src1_addr << std::endl;
  std::cout << "dev_dst=0x" << std::hex << kernel_arg.dst_addr << std::endl;

  int errors = 0;

  if (0 == test || -1 == test) {
    std::cout << "run memcopy test" << std::endl;
    errors = run_memcopy_test(kernel_arg);
  }

  if (1 == test || -1 == test) {
    std::cout << "run kernel test" << std::endl;
    errors = run_kernel_test(kernel_arg);
  }

  std::cout << "cleanup" << std::endl;
  cleanup();

  if (errors != 0) {
    std::cout << "Found " << std::dec << errors << " errors!" << std::endl;
    std::cout << "Test FAILED" << std::endl;
    return errors;
  }

  std::cout << "Test PASSED" << std::endl;
  return 0;
}
