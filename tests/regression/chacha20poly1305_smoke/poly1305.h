// SPDX-License-Identifier: Apache-2.0
//
// Poly1305 one-time MAC (RFC 8439 sec 2.5). Software reference using the
// well-known radix-2^26, 5-limb formulation (poly1305-donna-32): the
// accumulator a = (a + block) * r mod (2^130 - 5), a Horner chain in the prime
// field 2^130-5 -- the structural analogue of GHASH's GF(2^128) Horner chain,
// which makes it the natural second MAC for the algorithm-agnostic study.
//
// With POLY1305_NATIVE defined, the per-block field multiply runs on the
// hardware Poly1305 PE (VX_crypto_poly1305.sv): SETR loads the clamped key, each
// full 16-byte block is processed by a BLOCK op, and RD reads back the 5
// accumulator limbs. The padded final partial block and the final reduce (+s)
// stay in software -- exactly the limb form the PE produces, so the result is
// bit-identical to the pure-software path.
//
// -----------------------------------------------------------------------------
// 算法速览 (读代码前先理解这几点):
//
// Poly1305 是一个"一次性消息认证码"(one-time MAC): 给定一条消息和一把"只能用
// 一次"的 32 字节密钥, 算出一个 16 字节的"标签"(tag)。接收方用同样的 key 和消息
// 重算 tag 并比对, 以验证消息没被篡改。AEAD 里它负责"认证", ChaCha20 负责"加密"。
//
// 数学本质: 把消息切成一个个 16 字节块 m1,m2,...,mn, 当成大整数, 计算多项式
//
//     tag = ( m1*r^n + m2*r^(n-1) + ... + mn*r )  mod (2^130 - 5)  + s  mod 2^128
//
// 其中 r、s 各是 key 的一半 (各 128 bit)。这个多项式用"霍纳法则"(Horner) 累加:
//
//     h <- 0
//     对每个块 m_i:  h <- (h + m_i) * r   (在素域 2^130-5 上)
//     tag <- (h + s) mod 2^128
//
// 这正是 GHASH 在 GF(2^128) 上做的同一种 "Horner 链", 只不过 GHASH 用的是二元
// 域多项式乘法, Poly1305 用的是普通整数乘法再模大素数 2^130-5 —— 所以它是 GHASH
// 在"素域"侧的对应物。
//
// 为什么要"5 个 26-bit limb"(肢) 而不是直接用 128/256-bit 大数?
//   普通 32/64-bit CPU 没有 130-bit 整数。把 130-bit 的数拆成 5 段、每段 26 bit
//   (5*26 = 130), 乘法就退化成 5x5=25 个 "26bit x 26bit -> 52bit" 的小乘法, 每个
//   结果都稳稳装进 64-bit, 求和也不溢出。这套定点表示就是著名的 "poly1305-donna"。
//
// 模 2^130-5 的化简技巧 (理解 r5[k]=5*r[k] 的来历):
//   因为 2^130 ≡ 5 (mod 2^130-5), 任何"进位到第 2^130 位以上"的部分, 都等价于把它
//   乘 5 再挪回低位。乘法里凡是 limb 下标 i+j >= 5 的交叉项 (落在 2^130 及以上),
//   都改成 "乘 5、下移 5 个 limb"。预先算好 r5[k] = 5*r[k] 就是为了在 schoolbook
//   乘法里直接吃掉这些回绕项 (见 poly1305_blocks 里 d0..d4 的写法)。
//
// ⚠ 命名提醒 (容易踩坑): 本文件里 keysetup/blocks 用到的 r5[5] 数组 == 预算的化简
//   系数 5*r, 它【不是】Poly1305 规范里 key 后半段、最后加到 tag 上的 s (pad)!
//   那个 pad 从不存成变量, 只在 poly1305_finalize 里直接从 key+16 读取。参考实现
//   poly1305-donna 把这个 5*r 数组叫 s(s1..s4), 正是歧义来源 —— 这里改名为 r5。
// -----------------------------------------------------------------------------

#ifndef POLY1305_H
#define POLY1305_H

#include <stdint.h>
#include <stddef.h>

#ifdef POLY1305_NATIVE
#include <vx_intrinsics.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 小端读: 4 个字节 -> 一个 32-bit 字 (Poly1305 的 key/消息都按小端解释)。
// 参数:
//   p [入] 指向至少 4 个字节的内存。
// 返回: p[0..3] 按小端拼成的 32-bit 值 (p[0] 为最低字节)。
static inline uint32_t poly_le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// 小端写: 一个 32-bit 字 -> 4 个字节 (poly_le32 的逆)。
// 参数:
//   p [出] 至少 4 字节的目标内存 (低字节写到 p[0])。
//   v [入] 要写入的 32-bit 值。
static inline void poly_st32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// 准备密钥: "clamp"(钳制) r, 切成 5 个 26-bit limb; 并预算 r5[k] = 5*r[k]。
//
// clamp 是 Poly1305 安全性的硬性要求: 把 r 的某些位强制清零 (那几个 0x...
// 掩码常量), 避免出现会削弱 MAC 的"弱密钥"。RFC 8439 sec 2.5 规定了这些掩码。
// 注意这里的掩码同时完成了两件事: (1) clamp, (2) 切 26-bit limb —— 例如
// r[1] 取 t0/t1 拼接后的中间 26 位, 但 clamp 又把其中两位清掉, 所以掩码不是
// 纯 0x3ffffff 而是 0x3ffff03。r5[0] 不用 (霍纳里没有它对应的回绕项)。
// 参数:
//   r   [出] 5 个 26-bit limb 形式的、clamp 之后的密钥 r。
//   r5  [出] 预算好的回绕化简系数 r5[k] = 5*r[k] (r5[0]=0, 未使用)。注意这【不是】
//            key 后半段的 pad s, 只是乘法化简用的预算值。
//   key [入] 16 字节的原始 r (尚未 clamp), 即 32 字节 AEAD 密钥的前半。
static inline void poly1305_keysetup(uint32_t r[5], uint32_t r5[5], const uint8_t key[16]) {
  uint32_t t0 = poly_le32(key + 0),  t1 = poly_le32(key + 4);
  uint32_t t2 = poly_le32(key + 8),  t3 = poly_le32(key + 12);
  r[0] =  t0                       & 0x3ffffff;
  r[1] = ((t0 >> 26) | (t1 << 6))  & 0x3ffff03;
  r[2] = ((t1 >> 20) | (t2 << 12)) & 0x3ffc0ff;
  r[3] = ((t2 >> 14) | (t3 << 18)) & 0x3f03fff;
  r[4] =  (t3 >> 8)                & 0x00fffff;
  r5[0] = 0;
  r5[1] = r[1] * 5; r5[2] = r[2] * 5; r5[3] = r[3] * 5; r5[4] = r[4] * 5;
}

// 霍纳链主体: 对 `len` 字节的消息, 每 16 字节做一次 h = (h + block) * r mod (2^130-5)。
// h0..h4 是 5 个 26-bit limb 形式的累加器。
// 参数:
//   h   [入/出] 5-limb 累加器; 进来是当前值, 出去是吸收完这些块后的新值。
//   r   [入] 5-limb 密钥 r (poly1305_keysetup 产出)。
//   r5  [入] 5-limb 回绕化简系数 r5[k]=5*r[k] (r5[1..4] 有效)。非 key 后半段的 pad。
//   m   [入] 本次要处理的消息字节指针。
//   len [入] 本次要处理的字节数; 末块可不足 16 字节, 内部按 Poly1305 规则补位。
static inline void poly1305_blocks(uint32_t h[5], const uint32_t r[5], const uint32_t r5[5],
                                   const uint8_t* m, size_t len) {
  // 把累加器/密钥的 limb 读进局部变量 (循环里反复用, 避免每次访存)。
  uint32_t h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3], h4 = h[4];
  uint32_t r0 = r[0], r1 = r[1], r2 = r[2], r3 = r[3], r4 = r[4];
  uint32_t r5_1 = r5[1], r5_2 = r5[2], r5_3 = r5[3], r5_4 = r5[4];  // r5_k = 5*r_k, 回绕项用

  while (len > 0) {
    // ---- 取一块 (最后一块可能不足 16 字节) 并按 Poly1305 规则补位 ----
    size_t want = (len < 16) ? len : 16;
    uint8_t block[16];
    for (int i = 0; i < 16; ++i) block[i] = 0;
    for (size_t i = 0; i < want; ++i) block[i] = m[i];
    // Poly1305 把每个块看成"在最高有效字节后再接一个 1 字节"的整数:
    //   满块 (16 字节) -> 这个 1 落在第 128 位 (下面 h4 里 +2^128 处理);
    //   不满块 (want<16) -> 1 落在第 `want` 字节处, 即此处 block[want]=1。
    if (want < 16) block[want] = 1;   // partial: high bit at byte `want`

    // 把 16 字节块解读为 4 个 32-bit 字, 再切成 5 个 26-bit limb 并"加进"累加器。
    // 注意位拼接: b0 的高位 + b1 的低位 拼出第二个 26-bit limb, 以此类推。
    uint32_t b0 = poly_le32(block),     b1 = poly_le32(block + 4);
    uint32_t b2 = poly_le32(block + 8), b3 = poly_le32(block + 12);

    // LAZY carry add,因为h_x是32bit 但是加法是26bits有 6 个 bit 就是缓冲余量
    h0 += b0 & 0x3ffffff;
    h1 += ((b0 >> 26) | (b1 << 6))  & 0x3ffffff;
    h2 += ((b1 >> 20) | (b2 << 12)) & 0x3ffffff;
    h3 += ((b2 >> 14) | (b3 << 18)) & 0x3ffffff;
    // 满块在第 128 位补 1 (即 +2^128, 落在第 4 个 limb 的第 24 位); 不满块那个 1
    // 已由上面的 block[want]=1 体现在 b* 里, 这里就不再加。
    h4 += (b3 >> 8) | (want == 16 ? (1u << 24) : 0u);  // full block: 2^128

    // ---- schoolbook 乘法 h = (h+block) * r, 同时就地做 2^130-5 化简 ----
    // d_k = sum_{i+j=k} h_i*r_j  +  5 * sum_{i+j=k+5} h_i*r_j
    // 第二个和就是"回绕项": i+j>=5 的交叉项落到 2^130 以上, 乘 5 挪回低位 ——
    // 代码里用 r5_j (=5*r_j) 直接表达。例如 d0: h0*r0 是正项, h1*r5_4=h1*(5*r4)、
    // h2*r5_3、h3*r5_2、h4*r5_1 都是 i+j=5 的回绕项。每个 d_k 最多 ~54 bit, 装进 64-bit。
    uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * r5_4 + (uint64_t)h2 * r5_3 + (uint64_t)h3 * r5_2 + (uint64_t)h4 * r5_1;
    uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * r5_4 + (uint64_t)h3 * r5_3 + (uint64_t)h4 * r5_2;
    uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * r5_4 + (uint64_t)h4 * r5_3;
    uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * r5_4;
    uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

    // ---- 进位链: 把每个 d_k 拆成"低 26 位(留作 h_k) + 高位(进位给下一肢)" ----
    // 最高肢 d4 的进位要回绕到 h0 (因 2^130 ≡ 5), 所以 h0 += c*5。
    uint32_t c;
    c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff; d1 += c;
    c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff; d2 += c;
    c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff; d3 += c;
    c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff; d4 += c;
    c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff; h0 += c * 5;
    // 上一步 h0 可能因 +c*5 又溢出 26 位, 再传一次进位给 h1。注意这里只做"一轮半"
    // 进位 —— donna 的设计: h1 这里 += c 后不再 mask, 所以循环结束时 h1 可能是
    // 27 bit (略超 2^26)。这点很关键: 硬件 PE 的 acc limb 因此要做成 27-bit 宽。
    c = h0 >> 26;             h0 = h0 & 0x3ffffff;           h1 += c;

    m += want;
    len -= want;
  }

  h[0] = h0; h[1] = h1; h[2] = h2; h[3] = h3; h[4] = h4;
}

// 收尾: 最终进位、模 2^130-5 完全约简(冻结)、打包成 128 bit、再加上 s -> 16 字节 tag。
// 参数:
//   mac [出] 16 字节 tag 的输出缓冲区。
//   h   [入/出] 5-limb 累加器; 函数内会就地完成进位、冻结与打包 (内容被改写)。
//   key [入] 32 字节密钥; 本函数只用其后 16 字节 s (key+16..key+31)。
static inline void poly1305_finalize(uint8_t mac[16], uint32_t h[5], const uint8_t key[32]) {
  uint32_t h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3], h4 = h[4];

  // ---- 完整进位一遍, 把累加器规整到每肢 < 2^26 (上面 blocks 只做了"一轮半") ----
  uint32_t c;
  c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
  c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
  c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
  c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;   // 顶肢进位回绕 *5
  c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

  // ---- "冻结": 此刻 h 在 [0, 2^130) 内, 可能 >= p=2^130-5, 需要条件减 p ----
  //   技巧: 计算 g = h + 5 - 2^130 (即 h - p), 用借位判断 h 是否 >= p:
  //   若 h >= p, 则 g >= 0 (g4 的第 26 位不会变成"负"), 选 g;
  //   若 h <  p, 则 g 在顶肢借位为负 (g4 最高位=1), 保留 h。全程无分支 (常数时间)。
  uint32_t g0 = h0 + 5;            c = g0 >> 26; g0 &= 0x3ffffff;
  uint32_t g1 = h1 + c;            c = g1 >> 26; g1 &= 0x3ffffff;
  uint32_t g2 = h2 + c;            c = g2 >> 26; g2 &= 0x3ffffff;
  uint32_t g3 = h3 + c;            c = g3 >> 26; g3 &= 0x3ffffff;
  uint32_t g4 = h4 + c - (1u << 26);   // 减掉 2^130 的那一位

  uint32_t mask = (g4 >> 31) - 1;  // 0 if g<0 (keep h), all-ones if g>=0 (use g)
  g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
  mask = ~mask;
  h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
  h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;

  // ---- 把 5 个 26-bit limb 重新拼回 4 个 32-bit 字 (隐含 mod 2^128, 丢掉最高位) ----
  h0 = (h0)        | (h1 << 26);
  h1 = (h1 >> 6)   | (h2 << 20);
  h2 = (h2 >> 12)  | (h3 << 14);
  h3 = (h3 >> 18)  | (h4 << 8);

  // ---- tag = (h + s) mod 2^128, s = key 的后 16 字节 ----
  // 64-bit 累加配合 (f >> 32) 把进位逐字向上传, 自然实现 mod 2^128 (顶字进位丢弃)。
  uint64_t f;
  f = (uint64_t)h0 + poly_le32(key + 16);             h0 = (uint32_t)f;
  f = (uint64_t)h1 + poly_le32(key + 20) + (f >> 32); h1 = (uint32_t)f;
  f = (uint64_t)h2 + poly_le32(key + 24) + (f >> 32); h2 = (uint32_t)f;
  f = (uint64_t)h3 + poly_le32(key + 28) + (f >> 32); h3 = (uint32_t)f;

  poly_st32(mac + 0, h0); poly_st32(mac + 4, h1);
  poly_st32(mac + 8, h2); poly_st32(mac + 12, h3);
}

// 顶层入口: mac[16] = Poly1305(m[0..len), key[32])。key 的布局是 r(16字节) || s(16字节)。
// 参数:
//   mac [出] 16 字节认证标签 (tag) 的输出缓冲区。
//   m   [入] 待认证的消息字节指针。
//   len [入] 消息长度 (字节); 可为任意值, 末块不足 16 字节由内部补位处理。
//   key [入] 32 字节一次性密钥, 布局 = r(前 16 字节) || s(后 16 字节)。
static inline void poly1305_mac(uint8_t mac[16], const uint8_t* m, size_t len,
                                const uint8_t key[32]) {
  uint32_t r[5], r5[5], h[5] = {0, 0, 0, 0, 0};
  poly1305_keysetup(r, r5, key);   // clamp r, 切 limb, 预算 r5=5*r

#ifdef POLY1305_NATIVE
  // ---- 硬件 PE 路径 (VX_crypto_poly1305.sv) ----
  // SETR: 把 clamp 后的 128-bit r 作为两个 64-bit 半送进 PE。PE 内部自己按 26-bit
  // 切 limb —— 这里软件先做"字级 clamp"(下面的 0x0fffffff/0x0ffffffc 掩码), 这些
  // 字级掩码恰好覆盖了 donna 的逐肢掩码, 所以 PE 的"平直 26-bit 切片"== donna limb。
  uint32_t t0 = poly_le32(key + 0), t1 = poly_le32(key + 4);
  uint32_t t2 = poly_le32(key + 8), t3 = poly_le32(key + 12);
  uint64_t r_lo = (uint64_t)(t0 & 0x0fffffff) | ((uint64_t)(t1 & 0x0ffffffc) << 32);
  uint64_t r_hi = (uint64_t)(t2 & 0x0ffffffc) | ((uint64_t)(t3 & 0x0ffffffc) << 32);
  __intrin_poly1305_setr(r_lo, r_hi);

  // BLOCK: 每个"满 16 字节块"交给 PE 做一次 (acc+block+2^128)*r。PE 内部用一个
  // 乘法器时分复用跑完 25 个 schoolbook 乘积 + 化简 (多周期)。尾部不满 16 字节的
  // 残块不在这里处理 (PE 只吃满块)。
  size_t full = len & ~(size_t)15;
  for (size_t off = 0; off < full; off += 16) {
    uint64_t blo = (uint64_t)poly_le32(m + off)     | ((uint64_t)poly_le32(m + off + 4)  << 32);
    uint64_t bhi = (uint64_t)poly_le32(m + off + 8) | ((uint64_t)poly_le32(m + off + 12) << 32);
    __intrin_poly1305_block(blo, bhi);
  }

  // RD: 读回 5 个累加器 limb; 然后用软件 poly1305_blocks 处理"补位后的残块"
  // (它会自动补那个分隔 1 字节)。软件残块路径产出的 limb 形式与 PE 完全一致,
  // 所以 NATIVE 与纯软件结果 bit-identical。
  for (int k = 0; k < 5; ++k) h[k] = (uint32_t)__intrin_poly1305_rd((uint32_t)k);
  poly1305_blocks(h, r, r5, m + full, len - full);
#else
  // 纯软件: 所有块 (含残块) 都走 poly1305_blocks 的霍纳链。
  poly1305_blocks(h, r, r5, m, len);
#endif

  poly1305_finalize(mac, h, key);   // 冻结 + 加 s -> tag
}

#ifdef __cplusplus
}
#endif

#endif // POLY1305_H
