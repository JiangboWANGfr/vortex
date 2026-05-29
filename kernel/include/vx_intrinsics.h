// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// The intrinsics implemented use RISC-V assembler pseudo-directives defined here:
// https://sourceware.org/binutils/docs/as/RISC_002dV_002dFormats.html

#ifndef __VX_INTRINSICS_H__
#define __VX_INTRINSICS_H__

#include <stddef.h>
#include <stdint.h>
#include <VX_types.h>

#if defined(__clang__)
#define __UNIFORM__   __attribute__((annotate("vortex.uniform")))
#else
#define __UNIFORM__
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RISCV_CUSTOM0   0x0B
#define RISCV_CUSTOM1   0x2B
#define RISCV_CUSTOM2   0x5B
#define RISCV_CUSTOM3   0x7B

#define csr_read(csr) ({                        \
	size_t __r;	               		            \
	__asm__ __volatile__ ("csrr %0, %1" : "=r" (__r) : "i" (csr)); \
	__r;							            \
})

#define csr_write(csr, val)	({                  \
	size_t __v = (size_t)(val);                 \
	if (__builtin_constant_p(val) && __v < 32)  \
        __asm__ __volatile__ ("csrwi %0, %1" :: "i" (csr), "i" (__v));  \
    else                                        \
        __asm__ __volatile__ ("csrw %0, %1"	:: "i" (csr), "r" (__v));  \
})

#define csr_swap(csr, val) ({                   \
    size_t __r;                                 \
	size_t __v = (size_t)(val);	                \
	if (__builtin_constant_p(val) && __v < 32)  \
        __asm__ __volatile__ ("csrrwi %0, %1, %2" : "=r" (__r) : "i" (csr), "i" (__v)); \
    else                                        \
        __asm__ __volatile__ ("csrrw %0, %1, %2" : "=r" (__r) : "i" (csr), "r" (__v)); \
	__r;						                \
})

#define csr_read_set(csr, val) ({               \
	size_t __r;                                 \
	size_t __v = (size_t)(val);	                \
    if (__builtin_constant_p(val) && __v < 32)  \
	    __asm__ __volatile__ ("csrrsi %0, %1, %2" : "=r" (__r) : "i" (csr), "i" (__v)); \
    else                                        \
        __asm__ __volatile__ ("csrrs %0, %1, %2" : "=r" (__r) : "i" (csr), "r" (__v)); \
	__r;							            \
})

#define csr_set(csr, val) ({                    \
	size_t __v = (size_t)(val);	                \
    if (__builtin_constant_p(val) && __v < 32)  \
	    __asm__ __volatile__ ("csrsi %0, %1" :: "i" (csr), "i" (__v));  \
    else                                        \
        __asm__ __volatile__ ("csrs %0, %1"	:: "i" (csr), "r" (__v));  \
})

#define csr_read_clear(csr, val) ({             \
	size_t __r;                                 \
	size_t __v = (size_t)(val);	                \
    if (__builtin_constant_p(val) && __v < 32)  \
	    __asm__ __volatile__ ("csrrci %0, %1, %2" : "=r" (__r) : "i" (csr), "i" (__v)); \
    else                                        \
        __asm__ __volatile__ ("csrrc %0, %1, %2" : "=r" (__r) : "i" (csr), "r" (__v)); \
	__r;							            \
})

#define csr_clear(csr, val)	({                  \
	size_t __v = (size_t)(val);                 \
	if (__builtin_constant_p(val) && __v < 32)  \
        __asm__ __volatile__ ("csrci %0, %1" :: "i" (csr), "i" (__v)); \
    else                                        \
        __asm__ __volatile__ ("csrc %0, %1"	:: "i" (csr), "r" (__v)); \
})

// Set thread mask
inline void vx_tmc(int thread_mask) {
    __asm__ volatile (".insn r %0, 0, 0, x0, %1, x0" :: "i"(RISCV_CUSTOM0), "r"(thread_mask));
}

// disable all threads in the current warp
inline void vx_tmc_zero() {
    __asm__ volatile (".insn r %0, 0, 0, x0, x0, x0" :: "i"(RISCV_CUSTOM0));
}

// switch execution to single thread0
inline void vx_tmc_one() {
    __asm__ volatile (
        "li a0, 1\n\t"  // Load immediate value 1 into a0 (x10) register
        ".insn r %0, 0, 0, x0, a0, x0" :: "i"(RISCV_CUSTOM0) : "a0"
    );
}

// Set thread predicate
inline void vx_pred(int condition, int thread_mask) {
    __asm__ volatile (".insn r %0, 5, 0, x0, %1, %2" :: "i"(RISCV_CUSTOM0), "r"(condition), "r"(thread_mask));
}

// Set thread not predicate
inline void vx_pred_n(int condition, int thread_mask) {
    __asm__ volatile (".insn r %0, 5, 0, x1, %1, %2" :: "i"(RISCV_CUSTOM0), "r"(condition), "r"(thread_mask));
}

// Spawn warps
typedef void (*vx_wspawn_pfn)();
inline void vx_wspawn(int num_warps, vx_wspawn_pfn func_ptr) {
    __asm__ volatile (".insn r %0, 1, 0, x0, %1, %2" :: "i"(RISCV_CUSTOM0), "r"(num_warps), "r"(func_ptr));
}

// Split on a predicate
inline int vx_split(int predicate) {
    int ret;
    __asm__ volatile (".insn r %1, 2, 0, %0, %2, x0" : "=r"(ret) : "i"(RISCV_CUSTOM0), "r"(predicate));
    return ret;
}

// Split on a not predicate
inline int vx_split_n(int predicate) {
    int ret;
    __asm__ volatile (".insn r %1, 2, 0, %0, %2, x1" : "=r"(ret) : "i"(RISCV_CUSTOM0), "r"(predicate));
    return ret;
}

// Join
inline void vx_join(int stack_ptr) {
    __asm__ volatile (".insn r %0, 3, 0, x0, %1, x0" :: "i"(RISCV_CUSTOM0), "r"(stack_ptr));
}

// Warp Barrier
inline void vx_barrier(int barried_id, int num_warps) {
    __asm__ volatile (".insn r %0, 4, 0, x0, %1, %2" :: "i"(RISCV_CUSTOM0), "r"(barried_id), "r"(num_warps));
}

// Return current thread identifier
inline __attribute__((const)) int vx_thread_id() {
    int ret;
    __asm__ volatile ("csrr %0, %1" : "=r"(ret) : "i"(VX_CSR_THREAD_ID));
    return ret;
}

// Return current warp identifier
inline __attribute__((const)) int vx_warp_id() {
    int ret;
    __asm__ volatile ("csrr %0, %1" : "=r"(ret) : "i"(VX_CSR_WARP_ID));
    return ret;
}

// Return current core identifier
inline __attribute__((const)) int vx_core_id() {
    int ret;
    __asm__ volatile ("csrr %0, %1" : "=r"(ret) : "i"(VX_CSR_CORE_ID));
    return ret;
}

// Return active threads mask
inline __attribute__((const)) int vx_active_threads() {
    int ret;
    __asm__ volatile ("csrr %0, %1" : "=r"(ret) : "i"(VX_CSR_ACTIVE_THREADS));
    return ret;
}

// Return active warps mask
inline __attribute__((const)) int vx_active_warps() {
    int ret;
    __asm__ volatile ("csrr %0, %1" : "=r"(ret) : "i"(VX_CSR_ACTIVE_WARPS));
    return ret;
}

// Return the number of threads per warp
inline __attribute__((const)) int vx_num_threads() {
    int ret;
    __asm__ volatile ("csrr %0, %1" : "=r"(ret) : "i"(VX_CSR_NUM_THREADS));
    return ret;
}

// Return the number of warps per core
inline __attribute__((const)) int vx_num_warps() {
    int ret;
    __asm__ volatile ("csrr %0, %1" : "=r"(ret) : "i"(VX_CSR_NUM_WARPS));
    return ret;
}

// Return the number of cores per cluster
inline __attribute__((const)) int vx_num_cores() {
    int ret;
    __asm__ volatile ("csrr %0, %1" : "=r"(ret) : "i"(VX_CSR_NUM_CORES));
    return ret;
}

// Return the hart identifier (thread id accross the processor)
inline __attribute__((const)) int vx_hart_id() {
    int ret;
    __asm__ volatile ("csrr %0, %1" : "=r"(ret) : "i"(VX_CSR_MHARTID));
    return ret;
}

inline void vx_fence() {
    __asm__ volatile ("fence iorw, iorw");
}

#ifdef XLEN_64
static inline void __intrin_keccak_write_lane(uint64_t value, uint32_t lane_idx) {
    __asm__ volatile (".insn r 0x0b, 0, 0x03, x0, %0, %1" :: "r"(value), "r"(lane_idx));
}

static inline void __intrin_keccak_xor_lane(uint64_t value, uint32_t lane_idx) {
    __asm__ volatile (".insn r 0x0b, 1, 0x03, x0, %0, %1" :: "r"(value), "r"(lane_idx));
}

static inline uint64_t __intrin_keccak_read_lane(uint32_t lane_idx) {
    uint64_t rd;
    __asm__ volatile (".insn r 0x0b, 2, 0x03, %0, %1, x0" : "=r"(rd) : "r"(lane_idx));
    return rd;
}

static inline void __intrin_keccak_f1600(void) {
    __asm__ volatile (".insn r 0x0b, 3, 0x03, x0, x0, x0");
}
#else
static inline void __intrin_keccak_write_lane_u32(uint32_t value, uint32_t lane_idx) {
    __asm__ volatile (".insn r 0x0b, 0, 0x03, x0, %0, %1" :: "r"(value), "r"(lane_idx));
}

static inline void __intrin_keccak_xor_lane_u32(uint32_t value, uint32_t lane_idx) {
    __asm__ volatile (".insn r 0x0b, 1, 0x03, x0, %0, %1" :: "r"(value), "r"(lane_idx));
}

static inline uint32_t __intrin_keccak_read_lane_u32(uint32_t lane_idx) {
    uint32_t rd;
    __asm__ volatile (".insn r 0x0b, 2, 0x03, %0, %1, x0" : "=r"(rd) : "r"(lane_idx));
    return rd;
}

static inline void __intrin_keccak_write_lane(uint64_t value, uint32_t lane_idx) {
    __intrin_keccak_write_lane_u32((uint32_t)value, lane_idx);
    __intrin_keccak_write_lane_u32((uint32_t)(value >> 32), lane_idx | 0x20U);
}

static inline void __intrin_keccak_xor_lane(uint64_t value, uint32_t lane_idx) {
    __intrin_keccak_xor_lane_u32((uint32_t)value, lane_idx);
    __intrin_keccak_xor_lane_u32((uint32_t)(value >> 32), lane_idx | 0x20U);
}

static inline uint64_t __intrin_keccak_read_lane(uint32_t lane_idx) {
    uint64_t lo = __intrin_keccak_read_lane_u32(lane_idx);
    uint64_t hi = __intrin_keccak_read_lane_u32(lane_idx | 0x20U);
    return lo | (hi << 32);
}

static inline void __intrin_keccak_f1600(void) {
    __asm__ volatile (".insn r 0x0b, 3, 0x03, x0, x0, x0");
}
#endif

// GHASH (GF(2^128) MAC) native intrinsics. EXT1 (0x0b), funct7=0x04.
//   funct3: 0=SETH (H[word]=rs1), 1=XOR (Y[word]^=rs1), 2=RD (rd=Y[word]), 3=MUL.
// A 128-bit value is addressed as 128/XLEN words; the word index is rs2 low bits
// (SETH/XOR) or rs1 low bits (RD).
#ifdef XLEN_64
static inline void __intrin_ghash_seth(uint64_t value, uint32_t word) {
    __asm__ volatile (".insn r 0x0b, 0, 0x04, x0, %0, %1" :: "r"(value), "r"(word));
}

static inline void __intrin_ghash_xor(uint64_t value, uint32_t word) {
    __asm__ volatile (".insn r 0x0b, 1, 0x04, x0, %0, %1" :: "r"(value), "r"(word));
}

static inline uint64_t __intrin_ghash_rd(uint32_t word) {
    uint64_t rd;
    __asm__ volatile (".insn r 0x0b, 2, 0x04, %0, %1, x0" : "=r"(rd) : "r"(word));
    return rd;
}

static inline void __intrin_ghash_mul(void) {
    __asm__ volatile (".insn r 0x0b, 3, 0x04, x0, x0, x0");
}
#else
static inline void __intrin_ghash_seth_u32(uint32_t value, uint32_t slice) {
    __asm__ volatile (".insn r 0x0b, 0, 0x04, x0, %0, %1" :: "r"(value), "r"(slice));
}

static inline void __intrin_ghash_xor_u32(uint32_t value, uint32_t slice) {
    __asm__ volatile (".insn r 0x0b, 1, 0x04, x0, %0, %1" :: "r"(value), "r"(slice));
}

static inline uint32_t __intrin_ghash_rd_u32(uint32_t slice) {
    uint32_t rd;
    __asm__ volatile (".insn r 0x0b, 2, 0x04, %0, %1, x0" : "=r"(rd) : "r"(slice));
    return rd;
}

// On RV32 a 64-bit half spans two 32-bit slices (word*2, word*2+1).
static inline void __intrin_ghash_seth(uint64_t value, uint32_t word) {
    __intrin_ghash_seth_u32((uint32_t)value, word * 2U);
    __intrin_ghash_seth_u32((uint32_t)(value >> 32), word * 2U + 1U);
}

static inline void __intrin_ghash_xor(uint64_t value, uint32_t word) {
    __intrin_ghash_xor_u32((uint32_t)value, word * 2U);
    __intrin_ghash_xor_u32((uint32_t)(value >> 32), word * 2U + 1U);
}

static inline uint64_t __intrin_ghash_rd(uint32_t word) {
    uint64_t lo = __intrin_ghash_rd_u32(word * 2U);
    uint64_t hi = __intrin_ghash_rd_u32(word * 2U + 1U);
    return lo | (hi << 32);
}

static inline void __intrin_ghash_mul(void) {
    __asm__ volatile (".insn r 0x0b, 3, 0x04, x0, x0, x0");
}
#endif

static inline uint32_t __intrin_sha256sig0(uint32_t rs1) {
    uint32_t rd;
    __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x102" : "=r"(rd) : "r"(rs1));
    return rd;
}

static inline uint32_t __intrin_sha256sig1(uint32_t rs1) {
    uint32_t rd;
    __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x103" : "=r"(rd) : "r"(rs1));
    return rd;
}

static inline uint32_t __intrin_sha256sum0(uint32_t rs1) {
    uint32_t rd;
    __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x100" : "=r"(rd) : "r"(rs1));
    return rd;
}

static inline uint32_t __intrin_sha256sum1(uint32_t rs1) {
    uint32_t rd;
    __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x101" : "=r"(rd) : "r"(rs1));
    return rd;
}

static inline uint32_t __intrin_aes32esi(uint32_t acc, uint32_t word, uint32_t byte_select) {
    switch (byte_select & 0x3) {
    case 0:
        __asm__ volatile (".insn r 0x33, 0, 0x19, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    case 1:
        __asm__ volatile (".insn r 0x33, 0, 0x39, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    case 2:
        __asm__ volatile (".insn r 0x33, 0, 0x59, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    default:
        __asm__ volatile (".insn r 0x33, 0, 0x79, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    }
    return acc;
}

static inline uint32_t __intrin_aes32esmi(uint32_t acc, uint32_t word, uint32_t byte_select) {
    switch (byte_select & 0x3) {
    case 0:
        __asm__ volatile (".insn r 0x33, 0, 0x1b, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    case 1:
        __asm__ volatile (".insn r 0x33, 0, 0x3b, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    case 2:
        __asm__ volatile (".insn r 0x33, 0, 0x5b, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    default:
        __asm__ volatile (".insn r 0x33, 0, 0x7b, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    }
    return acc;
}

static inline uint32_t __intrin_aes32dsi(uint32_t acc, uint32_t word, uint32_t byte_select) {
    switch (byte_select & 0x3) {
    case 0:
        __asm__ volatile (".insn r 0x33, 0, 0x1d, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    case 1:
        __asm__ volatile (".insn r 0x33, 0, 0x3d, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    case 2:
        __asm__ volatile (".insn r 0x33, 0, 0x5d, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    default:
        __asm__ volatile (".insn r 0x33, 0, 0x7d, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    }
    return acc;
}

static inline uint32_t __intrin_aes32dsmi(uint32_t acc, uint32_t word, uint32_t byte_select) {
    switch (byte_select & 0x3) {
    case 0:
        __asm__ volatile (".insn r 0x33, 0, 0x1f, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    case 1:
        __asm__ volatile (".insn r 0x33, 0, 0x3f, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    case 2:
        __asm__ volatile (".insn r 0x33, 0, 0x5f, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    default:
        __asm__ volatile (".insn r 0x33, 0, 0x7f, %0, %0, %1" : "+&r"(acc) : "r"(word));
        break;
    }
    return acc;
}

#ifdef XLEN_64
static inline uint64_t __intrin_aes64es(uint64_t rs1, uint64_t rs2) {
    uint64_t rd;
    __asm__ volatile (".insn r 0x33, 0, 0x19, %0, %1, %2" : "=r"(rd) : "r"(rs1), "r"(rs2));
    return rd;
}

static inline uint64_t __intrin_aes64esm(uint64_t rs1, uint64_t rs2) {
    uint64_t rd;
    __asm__ volatile (".insn r 0x33, 0, 0x1b, %0, %1, %2" : "=r"(rd) : "r"(rs1), "r"(rs2));
    return rd;
}

static inline uint64_t __intrin_aes64ds(uint64_t rs1, uint64_t rs2) {
    uint64_t rd;
    __asm__ volatile (".insn r 0x33, 0, 0x1d, %0, %1, %2" : "=r"(rd) : "r"(rs1), "r"(rs2));
    return rd;
}

static inline uint64_t __intrin_aes64dsm(uint64_t rs1, uint64_t rs2) {
    uint64_t rd;
    __asm__ volatile (".insn r 0x33, 0, 0x1f, %0, %1, %2" : "=r"(rd) : "r"(rs1), "r"(rs2));
    return rd;
}

static inline uint64_t __intrin_aes64im(uint64_t rs1) {
    uint64_t rd;
    __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x300" : "=r"(rd) : "r"(rs1));
    return rd;
}

static inline uint64_t __intrin_aes64ks1i(uint64_t rs1, uint32_t rnum) {
    uint64_t rd;
    switch (rnum & 0xf) {
    case 0x0: __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x310" : "=r"(rd) : "r"(rs1)); break;
    case 0x1: __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x311" : "=r"(rd) : "r"(rs1)); break;
    case 0x2: __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x312" : "=r"(rd) : "r"(rs1)); break;
    case 0x3: __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x313" : "=r"(rd) : "r"(rs1)); break;
    case 0x4: __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x314" : "=r"(rd) : "r"(rs1)); break;
    case 0x5: __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x315" : "=r"(rd) : "r"(rs1)); break;
    case 0x6: __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x316" : "=r"(rd) : "r"(rs1)); break;
    case 0x7: __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x317" : "=r"(rd) : "r"(rs1)); break;
    case 0x8: __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x318" : "=r"(rd) : "r"(rs1)); break;
    case 0x9: __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x319" : "=r"(rd) : "r"(rs1)); break;
    case 0xA: __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x31a" : "=r"(rd) : "r"(rs1)); break;
    default:  __asm__ volatile (".insn i 0x13, 1, %0, %1, 0x31f" : "=r"(rd) : "r"(rs1)); break;
    }
    return rd;
}

static inline uint64_t __intrin_aes64ks2(uint64_t rs1, uint64_t rs2) {
    uint64_t rd;
    __asm__ volatile (".insn r 0x33, 0, 0x3f, %0, %1, %2" : "=r"(rd) : "r"(rs1), "r"(rs2));
    return rd;
}

static inline uint64_t __intrin_pack_cols(uint32_t col0, uint32_t col1) {
    return ((uint64_t)col1 << 32) | col0;
}

static inline void __intrin_unpack_cols(uint64_t packed, uint32_t *col0, uint32_t *col1) {
    *col0 = (uint32_t)packed;
    *col1 = (uint32_t)(packed >> 32);
}
#endif

static inline uint32_t __intrin_aes_subword(uint32_t word) {
#ifdef XLEN_64
    uint64_t dup = __intrin_pack_cols(word, word);
    return (uint32_t)__intrin_aes64es(dup, dup);
#else
    uint32_t ret = 0;
    ret = __intrin_aes32esi(ret, word, 0);
    ret = __intrin_aes32esi(ret, word, 1);
    ret = __intrin_aes32esi(ret, word, 2);
    ret = __intrin_aes32esi(ret, word, 3);
    return ret;
#endif
}

static inline void __intrin_aes_inv_mixcols(uint32_t *newcols, uint32_t *oldcols) {
#ifdef XLEN_64
    uint64_t lo = __intrin_pack_cols(oldcols[0], oldcols[1]);
    uint64_t hi = __intrin_pack_cols(oldcols[2], oldcols[3]);
    __intrin_unpack_cols(__intrin_aes64im(lo), &newcols[0], &newcols[1]);
    __intrin_unpack_cols(__intrin_aes64im(hi), &newcols[2], &newcols[3]);
#else
    uint32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;

    s0 = __intrin_aes32esi(s0, oldcols[0], 0);
    s0 = __intrin_aes32esi(s0, oldcols[0], 1);
    s0 = __intrin_aes32esi(s0, oldcols[0], 2);
    s0 = __intrin_aes32esi(s0, oldcols[0], 3);

    s1 = __intrin_aes32esi(s1, oldcols[1], 0);
    s1 = __intrin_aes32esi(s1, oldcols[1], 1);
    s1 = __intrin_aes32esi(s1, oldcols[1], 2);
    s1 = __intrin_aes32esi(s1, oldcols[1], 3);

    s2 = __intrin_aes32esi(s2, oldcols[2], 0);
    s2 = __intrin_aes32esi(s2, oldcols[2], 1);
    s2 = __intrin_aes32esi(s2, oldcols[2], 2);
    s2 = __intrin_aes32esi(s2, oldcols[2], 3);

    s3 = __intrin_aes32esi(s3, oldcols[3], 0);
    s3 = __intrin_aes32esi(s3, oldcols[3], 1);
    s3 = __intrin_aes32esi(s3, oldcols[3], 2);
    s3 = __intrin_aes32esi(s3, oldcols[3], 3);

    newcols[0] = 0;
    newcols[0] = __intrin_aes32dsmi(newcols[0], s0, 0);
    newcols[0] = __intrin_aes32dsmi(newcols[0], s0, 1);
    newcols[0] = __intrin_aes32dsmi(newcols[0], s0, 2);
    newcols[0] = __intrin_aes32dsmi(newcols[0], s0, 3);

    newcols[1] = 0;
    newcols[1] = __intrin_aes32dsmi(newcols[1], s1, 0);
    newcols[1] = __intrin_aes32dsmi(newcols[1], s1, 1);
    newcols[1] = __intrin_aes32dsmi(newcols[1], s1, 2);
    newcols[1] = __intrin_aes32dsmi(newcols[1], s1, 3);

    newcols[2] = 0;
    newcols[2] = __intrin_aes32dsmi(newcols[2], s2, 0);
    newcols[2] = __intrin_aes32dsmi(newcols[2], s2, 1);
    newcols[2] = __intrin_aes32dsmi(newcols[2], s2, 2);
    newcols[2] = __intrin_aes32dsmi(newcols[2], s2, 3);

    newcols[3] = 0;
    newcols[3] = __intrin_aes32dsmi(newcols[3], s3, 0);
    newcols[3] = __intrin_aes32dsmi(newcols[3], s3, 1);
    newcols[3] = __intrin_aes32dsmi(newcols[3], s3, 2);
    newcols[3] = __intrin_aes32dsmi(newcols[3], s3, 3);
#endif
}

static inline void __intrin_aes_last_enc_round(uint32_t *newcols, const uint32_t *oldcols, const uint32_t *round_key) {
#ifdef XLEN_64
    uint64_t state_lo = __intrin_pack_cols(oldcols[0], oldcols[1]);
    uint64_t state_hi = __intrin_pack_cols(oldcols[2], oldcols[3]);
    uint64_t key_lo = __intrin_pack_cols(round_key[0], round_key[1]);
    uint64_t key_hi = __intrin_pack_cols(round_key[2], round_key[3]);
    __intrin_unpack_cols(__intrin_aes64es(state_lo, state_hi) ^ key_lo, &newcols[0], &newcols[1]);
    __intrin_unpack_cols(__intrin_aes64es(state_hi, state_lo) ^ key_hi, &newcols[2], &newcols[3]);
#else
    uint32_t o0 = oldcols[0], o1 = oldcols[1], o2 = oldcols[2], o3 = oldcols[3];

    newcols[0] = __intrin_aes32esi(round_key[0], o0, 0);
    newcols[0] = __intrin_aes32esi(newcols[0], o1, 1);
    newcols[0] = __intrin_aes32esi(newcols[0], o2, 2);
    newcols[0] = __intrin_aes32esi(newcols[0], o3, 3);

    newcols[1] = __intrin_aes32esi(round_key[1], o1, 0);
    newcols[1] = __intrin_aes32esi(newcols[1], o2, 1);
    newcols[1] = __intrin_aes32esi(newcols[1], o3, 2);
    newcols[1] = __intrin_aes32esi(newcols[1], o0, 3);

    newcols[2] = __intrin_aes32esi(round_key[2], o2, 0);
    newcols[2] = __intrin_aes32esi(newcols[2], o3, 1);
    newcols[2] = __intrin_aes32esi(newcols[2], o0, 2);
    newcols[2] = __intrin_aes32esi(newcols[2], o1, 3);

    newcols[3] = __intrin_aes32esi(round_key[3], o3, 0);
    newcols[3] = __intrin_aes32esi(newcols[3], o0, 1);
    newcols[3] = __intrin_aes32esi(newcols[3], o1, 2);
    newcols[3] = __intrin_aes32esi(newcols[3], o2, 3);
#endif
}

static inline void __intrin_aes_enc_round(uint32_t *newcols, const uint32_t *oldcols, const uint32_t *round_key) {
#ifdef XLEN_64
    uint64_t state_lo = __intrin_pack_cols(oldcols[0], oldcols[1]);
    uint64_t state_hi = __intrin_pack_cols(oldcols[2], oldcols[3]);
    uint64_t key_lo = __intrin_pack_cols(round_key[0], round_key[1]);
    uint64_t key_hi = __intrin_pack_cols(round_key[2], round_key[3]);
    __intrin_unpack_cols(__intrin_aes64esm(state_lo, state_hi) ^ key_lo, &newcols[0], &newcols[1]);
    __intrin_unpack_cols(__intrin_aes64esm(state_hi, state_lo) ^ key_hi, &newcols[2], &newcols[3]);
#else
    uint32_t o0 = oldcols[0], o1 = oldcols[1], o2 = oldcols[2], o3 = oldcols[3];

    newcols[0] = __intrin_aes32esmi(round_key[0], o0, 0);
    newcols[0] = __intrin_aes32esmi(newcols[0], o1, 1);
    newcols[0] = __intrin_aes32esmi(newcols[0], o2, 2);
    newcols[0] = __intrin_aes32esmi(newcols[0], o3, 3);

    newcols[1] = __intrin_aes32esmi(round_key[1], o1, 0);
    newcols[1] = __intrin_aes32esmi(newcols[1], o2, 1);
    newcols[1] = __intrin_aes32esmi(newcols[1], o3, 2);
    newcols[1] = __intrin_aes32esmi(newcols[1], o0, 3);

    newcols[2] = __intrin_aes32esmi(round_key[2], o2, 0);
    newcols[2] = __intrin_aes32esmi(newcols[2], o3, 1);
    newcols[2] = __intrin_aes32esmi(newcols[2], o0, 2);
    newcols[2] = __intrin_aes32esmi(newcols[2], o1, 3);

    newcols[3] = __intrin_aes32esmi(round_key[3], o3, 0);
    newcols[3] = __intrin_aes32esmi(newcols[3], o0, 1);
    newcols[3] = __intrin_aes32esmi(newcols[3], o1, 2);
    newcols[3] = __intrin_aes32esmi(newcols[3], o2, 3);
#endif
}

static inline void __intrin_aes_last_dec_round(uint32_t *newcols, const uint32_t *oldcols, const uint32_t *round_key) {
#ifdef XLEN_64
    uint64_t state_lo = __intrin_pack_cols(oldcols[0], oldcols[1]);
    uint64_t state_hi = __intrin_pack_cols(oldcols[2], oldcols[3]);
    uint64_t key_lo = __intrin_pack_cols(round_key[0], round_key[1]);
    uint64_t key_hi = __intrin_pack_cols(round_key[2], round_key[3]);
    __intrin_unpack_cols(__intrin_aes64ds(state_lo, state_hi) ^ key_lo, &newcols[0], &newcols[1]);
    __intrin_unpack_cols(__intrin_aes64ds(state_hi, state_lo) ^ key_hi, &newcols[2], &newcols[3]);
#else
    uint32_t o0 = oldcols[0], o1 = oldcols[1], o2 = oldcols[2], o3 = oldcols[3];

    newcols[0] = __intrin_aes32dsi(round_key[0], o0, 0);
    newcols[0] = __intrin_aes32dsi(newcols[0], o3, 1);
    newcols[0] = __intrin_aes32dsi(newcols[0], o2, 2);
    newcols[0] = __intrin_aes32dsi(newcols[0], o1, 3);

    newcols[1] = __intrin_aes32dsi(round_key[1], o1, 0);
    newcols[1] = __intrin_aes32dsi(newcols[1], o0, 1);
    newcols[1] = __intrin_aes32dsi(newcols[1], o3, 2);
    newcols[1] = __intrin_aes32dsi(newcols[1], o2, 3);

    newcols[2] = __intrin_aes32dsi(round_key[2], o2, 0);
    newcols[2] = __intrin_aes32dsi(newcols[2], o1, 1);
    newcols[2] = __intrin_aes32dsi(newcols[2], o0, 2);
    newcols[2] = __intrin_aes32dsi(newcols[2], o3, 3);

    newcols[3] = __intrin_aes32dsi(round_key[3], o3, 0);
    newcols[3] = __intrin_aes32dsi(newcols[3], o2, 1);
    newcols[3] = __intrin_aes32dsi(newcols[3], o1, 2);
    newcols[3] = __intrin_aes32dsi(newcols[3], o0, 3);
#endif
}

static inline void __intrin_aes_dec_round(uint32_t *newcols, const uint32_t *oldcols, const uint32_t *round_key) {
#ifdef XLEN_64
    uint64_t state_lo = __intrin_pack_cols(oldcols[0], oldcols[1]);
    uint64_t state_hi = __intrin_pack_cols(oldcols[2], oldcols[3]);
    uint64_t key_lo = __intrin_pack_cols(round_key[0], round_key[1]);
    uint64_t key_hi = __intrin_pack_cols(round_key[2], round_key[3]);
    __intrin_unpack_cols(__intrin_aes64dsm(state_lo, state_hi) ^ key_lo, &newcols[0], &newcols[1]);
    __intrin_unpack_cols(__intrin_aes64dsm(state_hi, state_lo) ^ key_hi, &newcols[2], &newcols[3]);
#else
    uint32_t o0 = oldcols[0], o1 = oldcols[1], o2 = oldcols[2], o3 = oldcols[3];

    newcols[0] = __intrin_aes32dsmi(round_key[0], o0, 0);
    newcols[0] = __intrin_aes32dsmi(newcols[0], o3, 1);
    newcols[0] = __intrin_aes32dsmi(newcols[0], o2, 2);
    newcols[0] = __intrin_aes32dsmi(newcols[0], o1, 3);

    newcols[1] = __intrin_aes32dsmi(round_key[1], o1, 0);
    newcols[1] = __intrin_aes32dsmi(newcols[1], o0, 1);
    newcols[1] = __intrin_aes32dsmi(newcols[1], o3, 2);
    newcols[1] = __intrin_aes32dsmi(newcols[1], o2, 3);

    newcols[2] = __intrin_aes32dsmi(round_key[2], o2, 0);
    newcols[2] = __intrin_aes32dsmi(newcols[2], o1, 1);
    newcols[2] = __intrin_aes32dsmi(newcols[2], o0, 2);
    newcols[2] = __intrin_aes32dsmi(newcols[2], o3, 3);

    newcols[3] = __intrin_aes32dsmi(round_key[3], o3, 0);
    newcols[3] = __intrin_aes32dsmi(newcols[3], o2, 1);
    newcols[3] = __intrin_aes32dsmi(newcols[3], o1, 2);
    newcols[3] = __intrin_aes32dsmi(newcols[3], o0, 3);
#endif
}

// Returns 1 if every active lane’s predicate is true, 0 otherwise.
inline __attribute__((const)) int vx_vote_all(int predicate) {
    int ret;
    __asm__ volatile (".insn r %1, 0, 1, %0, %2, x0" : "=r"(ret) : "i"(RISCV_CUSTOM0), "r"(predicate));
    return ret;
}

// Returns 1 if any active lane’s predicate is true, 0 if none are true.
inline __attribute__((const)) int vx_vote_any(int predicate) {
    int ret;
    __asm__ volatile (".insn r %1, 1, 1, %0, %2, x0" : "=r"(ret) : "i"(RISCV_CUSTOM0), "r"(predicate));
    return ret;
}

//  Returns 1 if the predicate is uniform across all active lanes.
inline __attribute__((const)) int vx_vote_uni(int predicate) {
    int ret;
    __asm__ volatile (".insn r %1, 2, 1, %0, %2, x0" : "=r"(ret) : "i"(RISCV_CUSTOM0), "r"(predicate));
    return ret;
}

// Returns a bitmask of the warp, with bit i set if lane i’s predicate is true.
inline __attribute__((const)) int vx_vote_ballot(int predicate) {
    int ret;
    __asm__ volatile (".insn r %1, 3, 1, %0, %2, x0" : "=r"(ret) : "i"(RISCV_CUSTOM0), "r"(predicate));
    return ret;
}

// Shift values up by b lanes within each sub-group; out-of-range lanes keep their own value.
inline __attribute__((const)) int vx_shfl_up(size_t value, int bval, int cval, int mask) {
    int ret;
    int bc = (mask << 12) | (cval << 6) | bval;
    __asm__ volatile (".insn r %1, 4, 1, %0, %2, %3" : "=r"(ret) : "i"(RISCV_CUSTOM0), "r"(value), "r"(bc));
    return ret;
}

// Shift values down by b lanes within each sub-group; out-of-range lanes keep their own value.
inline __attribute__((const)) int vx_shfl_down(size_t value, int bval, int cval, int mask) {
    int ret;
    int bc = (mask << 12) | (cval << 6) | bval;
    __asm__ volatile (".insn r %1, 5, 1, %0, %2, %3" : "=r"(ret) : "i"(RISCV_CUSTOM0), "r"(value), "r"(bc));
    return ret;
}

// “Butterfly” exchange using XOR with b as a bit‐mask: each lane swaps with lane ⊕ b.
inline __attribute__((const)) int vx_shfl_bfly(size_t value, int bval, int cval, int mask) {
    int ret;
    int bc = (mask << 12) | (cval << 6) | bval;
    __asm__ volatile (".insn r %1, 6, 1, %0, %2, %3" : "=r"(ret) : "i"(RISCV_CUSTOM0), "r"(value), "r"(bc));
    return ret;
}

// Gather from an explicit index: every lane reads the value from base + idx, where idx = b[i].
inline __attribute__((const)) int vx_shfl_idx(size_t value, int bval, int cval, int mask) {
    int ret;
    int bc = (mask << 12) | (cval << 6) | bval;
    __asm__ volatile (".insn r %1, 7, 1, %0, %2, %3" : "=r"(ret) : "i"(RISCV_CUSTOM0), "r"(value), "r"(bc));
    return ret;
}

#ifdef __cplusplus
}
#endif

#endif // __VX_INTRINSICS_H__
