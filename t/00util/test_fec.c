/* test_fec.c */

#include "fec.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_test(fec_type_t type, const char *name) {
  size_t data_symbols = 4;
  size_t parity_symbols = 2;
  size_t symbol_size = 100;
  size_t total_symbols = data_symbols + parity_symbols;

  /* create data and parity buffers */
  uint8_t **data = malloc(data_symbols * sizeof(uint8_t *));
  uint8_t **parity = malloc(parity_symbols * sizeof(uint8_t *));
  uint8_t **blocks = malloc(total_symbols * sizeof(uint8_t *));
  uint8_t **orig_data = malloc(data_symbols * sizeof(uint8_t *));

  for (size_t i = 0; i < data_symbols; i++) {
    data[i] = malloc(symbol_size);
    orig_data[i] = malloc(symbol_size);
    /* populate with deterministic test patterns */
    for (size_t j = 0; j < symbol_size; j++) {
      data[i][j] = (uint8_t)(i * 17 + j * 3);
    }
    memcpy(orig_data[i], data[i], symbol_size);
    blocks[i] = malloc(symbol_size);
    memcpy(blocks[i], data[i], symbol_size);
  }

  for (size_t i = 0; i < parity_symbols; i++) {
    parity[i] = malloc(symbol_size);
    blocks[data_symbols + i] = malloc(symbol_size);
  }

  /* create fec context and encode */
  fec_t *f = fec_create_ex(type, data_symbols, parity_symbols, symbol_size);
  if (!f) {
    fprintf(stderr, "failed to create fec context for %s\n", name);
    return 1;
  }

  fec_encode(f, (const uint8_t *const *)data, parity);

  /* copy parity symbols into blocks array */
  for (size_t i = 0; i < parity_symbols; i++) {
    memcpy(blocks[data_symbols + i], parity[i], symbol_size);
  }

  /* simulate packet loss: erase block 1 (data) and block 4 (parity) */
  bool *missing = calloc(total_symbols, sizeof(bool));
  missing[1] = true;
  missing[data_symbols] = true; /* first parity symbol */

  memset(blocks[1], 0, symbol_size);
  memset(blocks[data_symbols], 0, symbol_size);

  /* decode to recover missing blocks */
  if (!fec_decode(f, blocks, missing)) {
    fprintf(stderr, "fec decoding failed for %s\n", name);
    fec_destroy(f);
    free(missing);
    return 1;
  }

  /* verify recovered data */
  if (memcmp(blocks[1], orig_data[1], symbol_size) != 0) {
    fprintf(
        stderr,
        "fec validation failed for %s: block 1 data not recovered correctly\n",
        name);
    fec_destroy(f);
    free(missing);
    return 1;
  }

  printf("%s validation OK\n", name);

  /* cleanup */
  fec_destroy(f);
  free(missing);
  for (size_t i = 0; i < data_symbols; i++) {
    free(data[i]);
    free(orig_data[i]);
    free(blocks[i]);
  }
  for (size_t i = 0; i < parity_symbols; i++) {
    free(parity[i]);
    free(blocks[data_symbols + i]);
  }
  free(data);
  free(parity);
  free(blocks);
  free(orig_data);

  return 0;
}

int main(void) {
  printf("running FEC test suite...\n");
  if (run_test(FEC_REED_SOLOMON, "Reed-Solomon") != 0)
    return 1;
  if (run_test(FEC_RAPTORQ, "RaptorQ") != 0)
    return 1;
  printf("===FEC OK===\n");
  return 0;
}
