#ifndef TABLES_H
#define TABLES_H

#ifdef AES_TABLE
#include <stdint.h>

extern const uint32_t T0_fwd[256];
extern const uint32_t T0_inv[256];
#ifndef AES_MONOTABLE
extern const uint32_t T1_fwd[256];
extern const uint32_t T2_fwd[256];
extern const uint32_t T3_fwd[256];
extern const uint32_t T1_inv[256];
extern const uint32_t T2_inv[256];
extern const uint32_t T3_inv[256];
#endif
#endif

#endif
