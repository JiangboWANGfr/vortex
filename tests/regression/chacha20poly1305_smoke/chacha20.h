// SPDX-License-Identifier: Apache-2.0
//
// ChaCha20 stream cipher (RFC 8439 sec 2.3-2.4). Software reference for the
// algorithm-agnostic study: ChaCha20 is an ARX cipher (add-rotate-xor on
// 32-bit words), structurally the opposite of AES's S-box/GF SPN. Header-only,
// usable from the Vortex kernel and host.
//
// -----------------------------------------------------------------------------
// 算法速览 :
//
// ChaCha20 是一个"流密码": 它本身不直接加密数据, 而是用 key/nonce/counter
// 生成一段伪随机的"密钥流"(keystream), 再把密钥流和明文逐字节异或得到密文。
// 解密就是把密文再和同一段密钥流异或一次 (XOR 自反)。
//
// 它的核心是一个 16 个 32-bit 字 (共 512 bit = 64 字节) 的"状态矩阵", 按 4x4
// 排列:
//        +-----+-----+-----+-----+
//        | s0  | s1  | s2  | s3  |   <- 4 个固定常量 "expand 32-byte k"
//        +-----+-----+-----+-----+
//        | s4  | s5  | s6  | s7  |   <- 256-bit 密钥的前半 (8 个字里的 4 个)
//        +-----+-----+-----+-----+
//        | s8  | s9  | s10 | s11 |   <- 256-bit 密钥的后半
//        +-----+-----+-----+-----+
//        | s12 | s13 | s14 | s15 |   <- s12=块计数器, s13..s15=96-bit nonce
//        +-----+-----+-----+-----+
//
// "ARX" = Add / Rotate / Xor, 这三种 32-bit 运算就是它全部的非线性来源 ——
// 没有 AES 那样的查表 S-box, 没有 GF(2^128) 乘法。把这三种运算按固定模式搅
// 20 轮, 再把结果加回初始状态 (feedforward), 就得到 64 字节密钥流。
// -----------------------------------------------------------------------------

#ifndef CHACHA20_H
#define CHACHA20_H

#include <stddef.h>
#include <stdint.h>

#ifdef CHACHA20_NATIVE
#include <vx_intrinsics.h> // 仅 NATIVE(硬件 PE)路径需要 __intrin_chacha_*
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 32-bit 循环左移 (rotate-left): ARX 里的 'R'。基础 RISC-V 没有 rotate 指令,
// 编译器会把它展开成 (sll | srl) 两条移位 + 一条 or, 共 3 条标量指令。
// (Zbb/Zbkb 扩展提供单条 rol/ror;)
static inline uint32_t cc20_rotl32(uint32_t x, int n) {
  return (x << n) | (x >> (32 - n));
}

// 小端 (little-endian) 读: 把内存里 4 个字节拼成一个 32-bit 字。RFC 8439
// 规定 key/nonce/keystream 都按小端解释, 所以 p[0] 是最低字节。
static inline uint32_t cc20_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// 小端写: 把一个 32-bit 字拆成 4 个字节存回内存 (cc20_le32 的逆操作)。
static inline void cc20_st32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

// ChaCha 的"四分之一轮" (quarter-round), 整个算法的原子操作。它一次搅动 4 个
// 状态字 a,b,c,d, 全部用 ARX 完成。固定的旋转量 16/12/8/7 是 ChaCha 的设计常数
// (区别于前身 Salsa20)。展开来看是 4 组 "加-异或-循环移位":
//   a += b; d ^= a; d <<<= 16   <- a 吸收 b, d 与新 a 混合后旋转 16 位
//   c += d; b ^= c; b <<<= 12
//   a += b; d ^= a; d <<<=  8
//   c += d; b ^= c; b <<<=  7
// 每一步都让某个字依赖另一个字的最新值, 4 步过后 a,b,c,d 充分扩散 (diffusion)。
// 注意: 这是宏 (in-place 修改传入的左值), 不是函数, 所以能直接改 x[] 数组元素。
#define CC20_QR(a, b, c, d) \
  a += b;                   \
  d ^= a;                   \
  d = cc20_rotl32(d, 16);   \
  c += d;                   \
  b ^= c;                   \
  b = cc20_rotl32(b, 12);   \
  a += b;                   \
  d ^= a;                   \
  d = cc20_rotl32(d, 8);    \
  c += d;                   \
  b ^= c;                   \
  b = cc20_rotl32(b, 7);

// 生成 1 个 64 字节 ChaCha20 密钥流块, 对应给定的 32-bit 块计数器 counter。
// 这是 RFC 8439 sec 2.3 的 "ChaCha20 block function"。
static inline void chacha20_block(const uint8_t key[32], uint32_t counter,
                                  const uint8_t nonce[12], uint8_t out[64]) {
  // ---- 第 1 步: 装配 16 字的初始状态矩阵 ----
  uint32_t st[16];
  // 行 0: 4 个固定常量。这 4 个魔数其实就是 ASCII 字符串 "expand 32-byte k"
  // 按小端拆成的 4 个字 (域分隔常量, 让不同 key 长度的输出互不碰撞)。
  st[0] = 0x61707865;
  st[1] = 0x3320646e;
  st[2] = 0x79622d32;
  st[3] = 0x6b206574;
  // 行 1-2: 256-bit 密钥 = 8 个小端字, 填入 st[4..11]。
  for (int i = 0; i < 8; ++i)
    st[4 + i] = cc20_le32(key + 4 * i);
  // 行 3: st[12] = 块计数器 (每块 +1), st[13..15] = 96-bit nonce。
  st[12] = counter;
  for (int i = 0; i < 3; ++i)
    st[13 + i] = cc20_le32(nonce + 4 * i);

#ifdef CHACHA20_NATIVE
  // ---- 硬件 PE 路径 (VX_crypto_chacha.sv) ----
  // 软件只负责装好 16 个字并处理字节序; 20 轮置换 + feedforward 全部丢给硬件:
  //   WR    x16: 把 st[0..15] 写进 PE 的每-lane 状态寄存器
  //   BLOCK     : PE 内部跑完 80 个 quarter-round + 把初始态加回 (多周期)
  //   RD    x16: 读回 PE 算好的 64 字节密钥流字
  // PE 返回的是已经做完 feedforward 的密钥流字, 字节序(小端打包)仍留在软件。
  for (int i = 0; i < 16; ++i)
    __intrin_chacha_wr((uint32_t)i, st[i]);
  __intrin_chacha_block();
  for (int i = 0; i < 16; ++i)
    cc20_st32(out + 4 * i, __intrin_chacha_rd((uint32_t)i));
#else
  // ---- 纯软件路径 ----
  // 在 x[] 上做置换, 保留 st[] 不动 —— 最后要把原始状态加回去 (feedforward)。
  uint32_t x[16];
  for (int i = 0; i < 16; ++i)
    x[i] = st[i];
  // 20 轮 = 10 次循环, 每次循环做 1 个 "double round" (2 轮: 列轮 + 对角轮)。
  for (int i = 0; i < 10; ++i) {
    // 列轮 (column round): 4 个 QR 各搅动状态矩阵的一"列" (4 个竖直字)。
    CC20_QR(x[0], x[4], x[8], x[12])
    CC20_QR(x[1], x[5], x[9], x[13])
    CC20_QR(x[2], x[6], x[10], x[14])
    CC20_QR(x[3], x[7], x[11], x[15])
    // 对角轮 (diagonal round): 4 个 QR 各搅动一条"对角线"。这一步让信息跨列
    // 扩散 —— 列轮 + 对角轮交替, 才能让任一输入位影响到所有输出位。
    CC20_QR(x[0], x[5], x[10], x[15])
    CC20_QR(x[1], x[6], x[11], x[12])
    CC20_QR(x[2], x[7], x[8], x[13])
    CC20_QR(x[3], x[4], x[9], x[14])
  }
  // ---- feedforward: 把置换结果与"初始状态"逐字相加 (mod 2^32) ----
  // 这一步至关重要: 没有它, 整个置换是可逆的, 给了密钥流就能反推出 key/counter。
  // 加回初始态破坏了可逆性。结果按小端写出, 即为 64 字节密钥流。
  for (int i = 0; i < 16; ++i)
    cc20_st32(out + 4 * i, x[i] + st[i]);
#endif
}

// 用密钥流加/解密 len 字节, 块计数器从 counter0 开始递增。
// 加密和解密是同一个函数 (XOR 自反): out = in ^ keystream。
static inline void chacha20_xor(const uint8_t key[32], uint32_t counter0,
                                const uint8_t nonce[12], const uint8_t *in,
                                size_t len, uint8_t *out) {
  uint8_t ks[64];          // 一次容纳 1 块 (64 字节) 密钥流
  size_t off = 0;          // 已处理的字节数
  uint32_t ctr = counter0; // 当前块计数器
  while (off < len) {
    chacha20_block(key, ctr, nonce, ks);            // 生成本块密钥流
    size_t n = (len - off < 64) ? (len - off) : 64; // 末块可能不足 64 字节
    for (size_t i = 0; i < n; ++i)
      out[off + i] = in[off + i] ^ ks[i];
    off += n;
    ++ctr; // 下一块用下一个计数器值 (块之间相互独立)
  }
}

#ifdef __cplusplus
}
#endif

#endif // CHACHA20_H
