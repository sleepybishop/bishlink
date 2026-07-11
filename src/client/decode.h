/* decode.h */

#ifndef DECODE_H
#define DECODE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct decode_t decode_t;

typedef struct {
	uint32_t width;
	uint32_t height;
} decode_config_t;

/* create and destroy a hardware decoder instance */
decode_t *decode_create(const decode_config_t *config);
void decode_destroy(decode_t *d);

/* submit encoded bitstream data (e.g. h264/h265 frame packet) for decoding.
   returns pointer to a decoded frame (usually NV12 format) if a complete frame is available */
const uint8_t *decode_frame(decode_t *d, const uint8_t *data, size_t size, uint32_t *width, uint32_t *height, uint32_t *format, size_t *out_size);

#endif /* DECODE_H */
