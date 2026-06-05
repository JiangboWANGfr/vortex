// SPDX-License-Identifier: Apache-2.0
//
// ChaCha20-Poly1305 AEAD (RFC 8439 sec 2.8). Composes the ChaCha20 stream
// cipher and the Poly1305 one-time MAC -- the ARX + 2^130-5 counterpart to
// AES-256-GCM (SPN + GF(2^128)) for the algorithm-agnostic study. Header-only.
//
// The MAC input AAD||pad16||C||pad16||len is always a multiple of 16 bytes, so
// Poly1305 here only sees full blocks. It is built contiguously in a bounded
// local buffer (CHACHA_AEAD_MAX_PT cap), which suits the smoke / small chunks.
//
// -----------------------------------------------------------------------------
// 算法速览 (读代码前先理解这几点):
//
// AEAD = "Authenticated Encryption with Associated Data" (带关联数据的认证加密)。
// 它一次同时给你两样保证:
//   - 机密性 (confidentiality): 明文被加密成密文 —— 由 ChaCha20 负责;
//   - 完整性/真实性 (integrity/authenticity): 密文 + AAD 没被篡改 —— 由 Poly1305
//     算出的 16 字节 tag 负责。
// "关联数据"(AAD) 是只认证、不加密的部分 (比如包头), 接收方据此既能读到包头明文,
// 又能确认它没被改。
//
// 这个文件就是把前两个文件 (chacha20.h + poly1305.h) 按 RFC 8439 sec 2.8 的规则
// 拼起来。它是 AES-256-GCM 的"对立面": GCM = AES(SPN 分组密码) + GHASH(GF(2^128)),
// 这里 = ChaCha20(ARX 流密码) + Poly1305(素域 2^130-5), 用于论文的"算法无关性"论证。
//
// 三个关键设计点 (RFC 8439 sec 2.8):
//   1. Poly1305 的一次性密钥 = 用 counter=0 跑一个 ChaCha20 块, 取它的前 32 字节。
//      "一次性"很重要: 每条消息因 nonce 不同, 这把 MAC 密钥都不一样。
//   2. 真正加密明文时, ChaCha20 的块计数器从 1 开始 (0 已被密钥占用)。
//   3. 被认证的数据要拼成: AAD || pad16 || 密文C || pad16 || le64(aad_len) || le64(ct_len)
//      其中 pad16 是"补零到 16 字节对齐", 末尾再附上两个 64-bit 小端长度。补齐 + 附长度
//      能防止"把 AAD 的尾字节挪到密文头部"这类拼接歧义攻击。
// -----------------------------------------------------------------------------

#ifndef CHACHA20POLY1305_H
#define CHACHA20POLY1305_H

#include <stdint.h>
#include <stddef.h>
#include "chacha20.h"
#include "poly1305.h"

// MAC 缓冲区是定长本地数组, 这两个上限决定它的大小。够 smoke / 小块用即可;
// 真实流式实现会改成增量喂给 Poly1305、不缓存整段 。
#ifndef CHACHA_AEAD_MAX_PT
#define CHACHA_AEAD_MAX_PT 1024
#endif
#ifndef CHACHA_AEAD_MAX_AAD
#define CHACHA_AEAD_MAX_AAD 64
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 小端写一个 64-bit 值 (用于把 aad_len / ct_len 附到 MAC 数据末尾)。
static inline void cc20p_st64(uint8_t* p, uint64_t v) {
  for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i));
}

// AEAD 加密。key=32B, nonce=12B, tag=16B 输出。ct 缓冲区须能容纳 pt_len 字节。
static inline void chacha20poly1305_encrypt(const uint8_t key[32],
                                            const uint8_t nonce[12],
                                            const uint8_t* aad, size_t aad_len,
                                            const uint8_t* pt, size_t pt_len,
                                            uint8_t* ct, uint8_t tag[16]) {
  // 用 counter=0 跑一个 ChaCha20 块, 它的前 32 字节就是 Poly1305 的
  // 一次性密钥 (block0 的后 32 字节按 RFC 丢弃不用)。
  uint8_t block0[64];
  chacha20_block(key, 0, nonce, block0);

  // 真正加密明文 -> 密文, ChaCha20 块计数器从 1 开始 (0 已留给密钥)。
  chacha20_xor(key, 1, nonce, pt, pt_len, ct);

  // 拼出被认证的数据
  // mac_data = AAD || pad16 || C || pad16 || le64(aad_len) || le64(ct_len)
  // pad16 = 把 AAD / 密文各自补零到 16 字节边界 (若已对齐则补 0 字节)。
  size_t aad_pad = (16 - (aad_len & 15)) & 15;   // AAD 需补的零字节数
  size_t ct_pad  = (16 - (pt_len & 15)) & 15;    // 密文需补的零字节数
  // 定长本地缓冲: AAD区 + 补齐 + 密文区 + 补齐 + 两个 8 字节长度。整段长度恒为
  // 16 的倍数, 所以下面喂给 Poly1305 时只会出现满块 (没有残块)。
  uint8_t mac_data[CHACHA_AEAD_MAX_AAD + 16 + CHACHA_AEAD_MAX_PT + 16 + 16];
  size_t n = 0;
  for (size_t i = 0; i < aad_len; ++i) mac_data[n++] = aad[i];   // AAD
  for (size_t i = 0; i < aad_pad; ++i) mac_data[n++] = 0;        // AAD 补零
  for (size_t i = 0; i < pt_len; ++i) mac_data[n++] = ct[i];     // 密文 C
  for (size_t i = 0; i < ct_pad; ++i) mac_data[n++] = 0;         // 密文补零
  cc20p_st64(mac_data + n, (uint64_t)aad_len); n += 8;           // le64(aad_len)
  cc20p_st64(mac_data + n, (uint64_t)pt_len);  n += 8;           // le64(ct_len)

  // 用一次性密钥 block0 对拼好的 mac_data 算 Poly1305 -> 16 字节认证 tag。
  poly1305_mac(tag, mac_data, n, block0);
}

#ifdef __cplusplus
}
#endif

#endif // CHACHA20POLY1305_H
