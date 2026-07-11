/* fec.c */

#include "fec.h"
#include "rs.h"
#include <nanorq_core.h>
#include <nanorq_ops.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct fec_t {
  fec_type_t type;
  reed_solomon *rs;
  size_t data_symbols;
  size_t parity_symbols;
  size_t symbol_size;
  uint8_t **shards;
  uint8_t *marks;
};

fec_t *fec_create_ex(fec_type_t type, size_t data_symbols,
                     size_t parity_symbols, size_t symbol_size) {
  fec_t *f = calloc(1, sizeof(fec_t));
  if (!f)
    return NULL;

  f->type = type;
  f->data_symbols = data_symbols;
  f->parity_symbols = parity_symbols;
  f->symbol_size = symbol_size;

  if (type == FEC_REED_SOLOMON) {
    static bool rs_initialized = false;
    if (!rs_initialized) {
      reed_solomon_init();
      rs_initialized = true;
    }
    f->rs = reed_solomon_new(data_symbols, parity_symbols);
    if (!f->rs) {
      free(f);
      return NULL;
    }
    size_t total_symbols = data_symbols + parity_symbols;
    f->shards = calloc(total_symbols, sizeof(uint8_t *));
    f->marks = calloc(total_symbols, sizeof(uint8_t));
    if (!f->shards || !f->marks) {
      fec_destroy(f);
      return NULL;
    }
  }

  return f;
}

fec_t *fec_create(size_t data_symbols, size_t parity_symbols,
                  size_t symbol_size) {
  return fec_create_ex(FEC_REED_SOLOMON, data_symbols, parity_symbols,
                       symbol_size);
}

void fec_destroy(fec_t *f) {
  if (f) {
    if (f->rs)
      reed_solomon_release(f->rs);
    free(f->shards);
    free(f->marks);
    free(f);
  }
}

void fec_encode(fec_t *f, const uint8_t *const *data_blocks,
                uint8_t *const *parity_blocks) {
  if (!f)
    return;

  if (f->type == FEC_REED_SOLOMON) {
    size_t total_symbols = f->data_symbols + f->parity_symbols;
    /* populate shards array */
    for (size_t i = 0; i < f->data_symbols; i++) {
      f->shards[i] = (uint8_t *)data_blocks[i];
    }
    for (size_t i = 0; i < f->parity_symbols; i++) {
      f->shards[f->data_symbols + i] = parity_blocks[i];
    }
    reed_solomon_encode(f->rs, f->shards, total_symbols, f->symbol_size);
  } else if (f->type == FEC_RAPTORQ) {
    /* dynamic RaptorQ core encoding */
    static uint8_t *src_data_buf = NULL;
    static size_t src_data_cap = 0;
    static uint8_t *repair_out_buf = NULL;
    static size_t repair_out_cap = 0;

    size_t len = f->data_symbols * f->symbol_size;
    if (len > src_data_cap) {
      src_data_buf = realloc(src_data_buf, len);
      src_data_cap = len;
    }
    uint8_t *src_data = src_data_buf;
    if (!src_data)
      return;
    for (size_t i = 0; i < f->data_symbols; i++) {
      memcpy(src_data + i * f->symbol_size, data_blocks[i], f->symbol_size);
    }
    size_t repair_len = f->parity_symbols * f->symbol_size;
    if (repair_len > repair_out_cap) {
      repair_out_buf = realloc(repair_out_buf, repair_len);
      repair_out_cap = repair_len;
    }
    uint8_t *repair_out = repair_out_buf;
    if (repair_out) {
      bool ok =
          nanorq_core_encode_simple(src_data, f->data_symbols, f->symbol_size,
                                    f->parity_symbols, repair_out);
      if (ok) {
        for (size_t i = 0; i < f->parity_symbols; i++) {
          memcpy(parity_blocks[i], repair_out + i * f->symbol_size,
                 f->symbol_size);
        }
      }
    }
  }
}

bool fec_decode(fec_t *f, uint8_t *const *blocks, const bool *missing_mask) {
  if (!f)
    return false;

  if (f->type == FEC_REED_SOLOMON) {
    size_t total_symbols = f->data_symbols + f->parity_symbols;
    /* populate shards and erasure marks */
    for (size_t i = 0; i < total_symbols; i++) {
      f->shards[i] = blocks[i];
      f->marks[i] = missing_mask[i] ? 1 : 0;
    }
    int res = reed_solomon_decode(f->rs, f->shards, f->marks, total_symbols,
                                  f->symbol_size);
    return res == 0;
  } else if (f->type == FEC_RAPTORQ) {
    /* dynamic RaptorQ core decoding */
    size_t drops = 0;
    uint32_t dropped_esi[256];
    for (size_t i = 0; i < f->data_symbols; i++) {
      if (missing_mask[i]) {
        dropped_esi[drops++] = i;
      }
    }

    size_t available_repairs = 0;
    uint32_t repair_esi[256];
    for (size_t i = 0; i < f->parity_symbols; i++) {
      if (!missing_mask[f->data_symbols + i]) {
        repair_esi[available_repairs++] = f->data_symbols + i;
      }
    }

    if (drops > available_repairs) {
      return false;
    }

    if (drops == 0) {
      return true;
    }

    nanorq_core dec;
    if (!nanorq_core_encoder_new(f->data_symbols, 0, &dec)) {
      return false;
    }

    struct nanorq_core_mem_reqs reqs;
    nanorq_core_get_memory_reqs(f->data_symbols, 0, f->symbol_size, &reqs);

    static uint8_t *prep_mem_buf = NULL;
    static size_t prep_mem_cap = 0;
    if (reqs.prepare_bytes > prep_mem_cap) {
      prep_mem_buf = realloc(prep_mem_buf, reqs.prepare_bytes);
      prep_mem_cap = reqs.prepare_bytes;
    }
    uint8_t *prep_mem = prep_mem_buf;

    if (!nanorq_core_prepare(&dec, prep_mem, reqs.prepare_bytes)) {
      return false;
    }

    for (size_t i = 0; i < drops; i++) {
      nanorq_core_replace_symbol(&dec, dropped_esi[i], repair_esi[i]);
    }
    nanorq_core_patch_matrix(&dec);

    static uint8_t *work_mem_buf = NULL;
    static size_t work_mem_cap = 0;
    if (reqs.work_bytes > work_mem_cap) {
      work_mem_buf = realloc(work_mem_buf, reqs.work_bytes);
      work_mem_cap = reqs.work_bytes;
    }
    uint8_t *work_mem = work_mem_buf;

    static sched_op *ops_buf = NULL;
    static size_t ops_cap = 0;
    if (reqs.schedule_bytes > ops_cap) {
      ops_buf = realloc(ops_buf, reqs.schedule_bytes);
      ops_cap = reqs.schedule_bytes;
    }

    schedule S_dec = {0};
    S_dec.ops.a = ops_buf;
    S_dec.ops.m = reqs.schedule_bytes / sizeof(sched_op);

    if (!work_mem || !S_dec.ops.a) {
      return false;
    }

    nanorq_core_set_op_callback(&dec, &S_dec, ops_push);

    if (!nanorq_core_precalculate(&dec, work_mem, reqs.work_bytes)) {
      return false;
    }

    static uint8_t *D_buf = NULL;
    static size_t D_cap = 0;
    if (reqs.matrix_bytes > D_cap) {
      D_buf = realloc(D_buf, reqs.matrix_bytes);
      D_cap = reqs.matrix_bytes;
    }
    uint8_t *D = D_buf;
    memset(D, 0, reqs.matrix_bytes);

    if (!D) {
      return false;
    }
    uint32_t SH = nanorq_core_get_pc_genc_offset(&dec);

    for (size_t i = 0; i < f->data_symbols; i++) {
      if (!missing_mask[i]) {
        memcpy(D + (SH + i) * f->symbol_size, blocks[i], f->symbol_size);
      }
    }
    for (size_t i = 0; i < drops; i++) {
      uint32_t src_esi = dropped_esi[i];
      uint32_t rep_esi = repair_esi[i];
      memcpy(D + (SH + src_esi) * f->symbol_size, blocks[rep_esi],
             f->symbol_size);
    }

    ops_run(&dec, D, f->symbol_size, &S_dec);

    for (size_t i = 0; i < drops; i++) {
      uint32_t src_esi = dropped_esi[i];
      ops_mix(&dec, D, f->symbol_size, src_esi, blocks[src_esi]);
    }

    return true;
  }

  return false;
}
