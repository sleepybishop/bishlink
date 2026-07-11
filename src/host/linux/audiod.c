/* audiod.c */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static bool mock_mode = false;

static void setup_uds_addr(struct sockaddr_un *addr, socklen_t *len,
                           const char *name) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  snprintf(addr->sun_path, sizeof(addr->sun_path), "/tmp/%s.sock", name);
  *len = sizeof(addr->sun_family) + strlen(addr->sun_path);
}

static int16_t *generate_mock_audio(uint32_t samples) {
  int16_t *buf = malloc(samples * sizeof(int16_t));
  if (!buf)
    return NULL;

  static double phase = 0.0;
  double freq = 440.0;
  double rate = 44100.0;

  for (uint32_t i = 0; i < samples; i += 2) {
    int16_t val = (int16_t)(16384.0 * sin(phase));
    buf[i] = val;
    buf[i + 1] = val;
    phase += 2.0 * M_PI * freq / rate;
    if (phase >= 2.0 * M_PI) {
      phase -= 2.0 * M_PI;
    }
  }

  return buf;
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--mock") == 0 || strcmp(argv[i], "-m") == 0) {
      mock_mode = true;
    }
  }

  printf("initializing bishlink audio capture daemon...\n");

  pa_simple *pa = NULL;
  pa_sample_spec ss;
  ss.format = PA_SAMPLE_S16LE;
  ss.channels = 2;
  ss.rate = 44100;

  int pa_error = 0;

  if (!mock_mode) {
    pa = pa_simple_new(NULL, "bishlink-audiod", PA_STREAM_RECORD, NULL,
                       "record", &ss, NULL, NULL, &pa_error);
    if (!pa) {
      fprintf(stderr,
              "warning: failed to connect to PulseAudio (%s). falling back to "
              "mock capture.\n",
              pa_strerror(pa_error));
      mock_mode = true;
    }
  }

  int server_fd = -1;

  while (1) {
    /* self-healing connection setup */
    if (server_fd == -1) {
      server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
      if (server_fd >= 0) {
        struct sockaddr_un addr;
        socklen_t addr_len;
        setup_uds_addr(&addr, &addr_len, "bishlink-audio");
        if (connect(server_fd, (struct sockaddr *)&addr, addr_len) != 0) {
          close(server_fd);
          server_fd = -1;
          usleep(500 * 1000);
          continue;
        }
        printf("connected to bishlink audio socket\n");
      }
    }

    uint32_t sample_count = 0;
    int16_t *audio_data = NULL;
    bool audio_free_needed = false;

    if (mock_mode) {
      /* 20ms block = 882 stereo frames = 1764 samples */
      sample_count = 1764;
      audio_data = generate_mock_audio(sample_count);
      audio_free_needed = true;
      usleep(20 * 1000);
    } else {
      static int16_t pa_buf[2048];
      sample_count = sizeof(pa_buf) / sizeof(int16_t);
      if (pa_simple_read(pa, pa_buf, sizeof(pa_buf), &pa_error) < 0) {
        fprintf(stderr, "PulseAudio read error: %s\n", pa_strerror(pa_error));
        continue;
      }
      audio_data = pa_buf;
    }

    /* transmit sample block over UDS */
    if (audio_data && sample_count > 0 && server_fd != -1) {
      ssize_t ret = write(server_fd, &sample_count, sizeof(sample_count));
      if (ret < 0) {
        fprintf(stderr, "failed to write audio sample size, disconnecting\n");
        close(server_fd);
        server_fd = -1;
      } else {
        size_t byte_size = sample_count * sizeof(int16_t);
        size_t written = 0;
        bool write_err = false;

        while (written < byte_size) {
          ssize_t wret = write(server_fd, (uint8_t *)audio_data + written,
                               byte_size - written);
          if (wret < 0) {
            if (errno == EINTR)
              continue;
            write_err = true;
            break;
          }
          written += wret;
        }

        if (write_err) {
          fprintf(stderr,
                  "failed to write audio data payload, disconnecting\n");
          close(server_fd);
          server_fd = -1;
        }
      }
    }

    if (audio_free_needed && audio_data) {
      free(audio_data);
    }
  }

  if (pa) {
    pa_simple_free(pa);
  }

  return 0;
}
