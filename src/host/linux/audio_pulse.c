/* audio_pulse.c */

#include "audio.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct audio_t {
  pthread_t thread;
  int listen_fd;
  int client_fd;
  audio_callback_t callback;
  void *user_data;
  volatile bool running;
};

static void setup_uds_addr(struct sockaddr_un *addr, socklen_t *len,
                           const char *name) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  snprintf(addr->sun_path, sizeof(addr->sun_path), "/tmp/%s.sock", name);
  *len = sizeof(addr->sun_family) + strlen(addr->sun_path);
}

static bool read_exact(int fd, void *buf, size_t size) {
  size_t read_bytes = 0;
  while (read_bytes < size) {
    ssize_t ret = read(fd, (uint8_t *)buf + read_bytes, size - read_bytes);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (ret == 0) {
      return false;
    }
    read_bytes += ret;
  }
  return true;
}

static void *audio_thread_func(void *arg) {
  audio_t *a = arg;
  int16_t *pcm_buf = NULL;
  size_t pcm_buf_capacity = 0;

  while (a->running) {
    a->client_fd = accept(a->listen_fd, NULL, NULL);
    if (a->client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }

    fprintf(stderr, "audio capture helper connected\n");

    while (a->running) {
      uint32_t samples = 0;
      if (!read_exact(a->client_fd, &samples, sizeof(samples))) {
        break;
      }

      if (samples == 0) {
        continue;
      }

      size_t byte_size = samples * sizeof(int16_t);
      if (samples > pcm_buf_capacity) {
        pcm_buf_capacity = samples;
        pcm_buf = realloc(pcm_buf, byte_size);
      }

      if (!read_exact(a->client_fd, pcm_buf, byte_size)) {
        break;
      }

      a->callback(a->user_data, pcm_buf, samples);
    }

    fprintf(stderr, "audio capture helper disconnected\n");
    close(a->client_fd);
    a->client_fd = -1;
  }

  if (pcm_buf) {
    free(pcm_buf);
  }
  return NULL;
}

audio_t *audio_create(audio_callback_t callback, void *user_data) {
  audio_t *a = calloc(1, sizeof(audio_t));
  if (!a)
    return NULL;

  a->callback = callback;
  a->user_data = user_data;
  a->running = true;
  a->client_fd = -1;

  a->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (a->listen_fd < 0) {
    free(a);
    return NULL;
  }

  struct sockaddr_un addr;
  socklen_t addr_len;
  setup_uds_addr(&addr, &addr_len, "bishlink-audio");
  unlink(addr.sun_path);

  if (bind(a->listen_fd, (struct sockaddr *)&addr, addr_len) != 0) {
    close(a->listen_fd);
    free(a);
    return NULL;
  }

  if (listen(a->listen_fd, 5) != 0) {
    close(a->listen_fd);
    free(a);
    return NULL;
  }

  if (pthread_create(&a->thread, NULL, audio_thread_func, a) != 0) {
    close(a->listen_fd);
    free(a);
    return NULL;
  }

  fprintf(stderr, "audio capture uds listener initialized\n");
  return a;
}

void audio_destroy(audio_t *a) {
  if (a) {
    a->running = false;
    if (a->client_fd != -1) {
      shutdown(a->client_fd, SHUT_RDWR);
      close(a->client_fd);
    }
    if (a->listen_fd != -1) {
      shutdown(a->listen_fd, SHUT_RDWR);
      close(a->listen_fd);
    }
    pthread_join(a->thread, NULL);
    free(a);
  }
}
