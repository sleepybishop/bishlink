/* audio.h */

#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct audio_t audio_t;

/* callback invoked when a chunk of audio samples is captured */
typedef void (*audio_callback_t)(void *user_data, const int16_t *pcm_data, size_t samples);

/* create and destroy a loopback audio capture instance */
audio_t *audio_create(audio_callback_t callback, void *user_data);
void audio_destroy(audio_t *a);

#endif /* AUDIO_H */
