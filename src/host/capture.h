/* capture.h */

#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct capture_t capture_t;

typedef struct {
	uint32_t width;
	uint32_t height;
	uint32_t fps;
} capture_config_t;

/* create and destroy a screen capture instance */
capture_t *capture_create(const capture_config_t *config);
void capture_destroy(capture_t *c);

/* acquire a new desktop frame. returns a pointer to raw image data (e.g. BGRA or NV12) */
const uint8_t *capture_frame(capture_t *c, uint32_t *width, uint32_t *height, uint32_t *format, size_t *size);

/* request the connected capture helper to force a keyframe */
void capture_request_keyframe(capture_t *c);

/* set video encoder target bitrate in bits per second */
void capture_set_bitrate(capture_t *c, uint32_t bitrate_bps);

#endif /* CAPTURE_H */
