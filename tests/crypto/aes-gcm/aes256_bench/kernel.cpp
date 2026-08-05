#include <stdint.h>
#include <vx_spawn.h>
#include "common.h"

namespace {

struct TaskArgs {
  const uint8_t* input;
  uint8_t* output;
  const uint8_t* iv;
  const uint32_t* round_keys;
  const uint32_t* key;
  uint32_t nblocks_per_task;
  uint32_t op_type;
};

uint32_t g_round_keys[Nb * (Nr + 1)];

bool is_decrypt_op(uint32_t op_type) {
  return op_type == AES256_BENCH_ECB_DEC
      || op_type == AES256_BENCH_CBC_DEC
      || op_type == AES256_BENCH_KEY_DEC;
}

void aes_worker(const TaskArgs* __UNIFORM__ args) {
  uint32_t task_id = blockIdx.x;
  uint32_t start_block_idx = task_id * args->nblocks_per_task;
  uint32_t offset = start_block_idx * BLOCK_SIZE;

  switch (args->op_type) {
  case AES256_BENCH_ECB_ENC:
    aes256_ecb_enc(args->input + offset, args->round_keys,
                   args->output + offset, args->nblocks_per_task);
    break;
  case AES256_BENCH_ECB_DEC:
    aes256_ecb_dec(args->input + offset, args->round_keys,
                   args->output + offset, args->nblocks_per_task);
    break;
  case AES256_BENCH_CBC_DEC: {
    uintptr_t first_iv = (uintptr_t)args->iv;
    uintptr_t prev_ct = (uintptr_t)args->input + offset - BLOCK_SIZE;
    uintptr_t use_iv_mask = (uintptr_t)0 - (uintptr_t)(offset == 0);
    uintptr_t iv_addr = prev_ct ^ ((prev_ct ^ first_iv) & use_iv_mask);
    const uint8_t* iv = (const uint8_t*)iv_addr;
    aes256_cbc_dec(iv, args->input + offset, args->round_keys,
                   args->output + offset, args->nblocks_per_task);
    break;
  }
  case AES256_BENCH_CTR_ENC:
  case AES256_BENCH_CTR_DEC:
    aes256_ctr(args->iv, start_block_idx, args->input + offset, args->round_keys,
               args->output + offset, args->nblocks_per_task);
    break;
  default:
    break;
  }
}

void key_worker(const TaskArgs* __UNIFORM__ args) {
  uint32_t task_id = blockIdx.x;
  uint32_t round_keys[Nb * (Nr + 1)];
  uint32_t checksum = 0x9e3779b9u ^ task_id;
  int inv_mix_cols = (args->op_type == AES256_BENCH_KEY_DEC);

  for (uint32_t i = 0; i < args->nblocks_per_task; ++i) {
    aes256_key_exp(args->key, round_keys, inv_mix_cols);
    checksum ^= round_keys[0] ^ round_keys[Nb * Nr] ^ round_keys[Nb * (Nr + 1) - 1];
  }

  ((uint32_t*)args->output)[task_id] = checksum;
}

} // namespace

int main() {
  kernel_arg_t* __UNIFORM__ arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
  aes256_bench_status_t* status = (aes256_bench_status_t*)(uintptr_t)arg->status_addr;
  status->errors = 0;
  status->completed_tasks = 0;
  status->op_type = arg->op_type;
  status->nblocks_per_task = arg->nblocks_per_task;
  status->total_blocks = (uint64_t)arg->num_tasks * arg->nblocks_per_task;

  if (arg->op_type >= AES256_BENCH_OP_COUNT || arg->nblocks_per_task == 0 || arg->num_tasks == 0) {
    status->errors = 1;
    return 1;
  }

  TaskArgs task_args = {
    (const uint8_t*)(uintptr_t)arg->in_addr,
    (uint8_t*)(uintptr_t)arg->out_addr,
    arg->iv,
    g_round_keys,
    (const uint32_t*)arg->key,
    arg->nblocks_per_task,
    arg->op_type,
  };

  if (arg->op_type == AES256_BENCH_KEY_ENC || arg->op_type == AES256_BENCH_KEY_DEC) {
    vx_spawn_threads(1, &arg->num_tasks, nullptr, (vx_kernel_func_cb)key_worker, &task_args);
    status->completed_tasks = arg->num_tasks;
    return 0;
  }

  aes256_key_exp((const uint32_t*)arg->key, g_round_keys, is_decrypt_op(arg->op_type));

  if (arg->op_type == AES256_BENCH_CBC_ENC) {
    uint32_t total_blocks = arg->num_tasks * arg->nblocks_per_task;
    aes256_cbc_enc(arg->iv, task_args.input, g_round_keys, task_args.output, total_blocks);
    status->completed_tasks = 1;
    return 0;
  }

  vx_spawn_threads(1, &arg->num_tasks, nullptr, (vx_kernel_func_cb)aes_worker, &task_args);
  status->completed_tasks = arg->num_tasks;
  return 0;
}
