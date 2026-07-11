/* capture_x11.c */

#include "capture.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct capture_t {
  int listen_fd;
  int client_fd;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint8_t *buffer;
  size_t buffer_size;
  bool has_frame;
};

static void setup_uds_addr(struct sockaddr_un *addr, socklen_t *len,
                           const char *name) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  snprintf(addr->sun_path, sizeof(addr->sun_path), "/tmp/%s.sock", name);
  *len = sizeof(addr->sun_family) + strlen(addr->sun_path);
}

static void set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags != -1) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

static bool read_exact(int fd, void *buf, size_t size) {
  size_t read_bytes = 0;
  while (read_bytes < size) {
    ssize_t ret = read(fd, (uint8_t *)buf + read_bytes, size - read_bytes);
    if (ret < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        usleep(100);
        continue;
      }
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

capture_t *capture_create(const capture_config_t *config) {
  capture_t *c = calloc(1, sizeof(capture_t));
  if (!c)
    return NULL;

  c->width = config->width;
  c->height = config->height;
  c->client_fd = -1;

  c->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (c->listen_fd < 0) {
    free(c);
    return NULL;
  }

  set_nonblock(c->listen_fd);

  struct sockaddr_un addr;
  socklen_t addr_len;
  setup_uds_addr(&addr, &addr_len, "bishlink-video");
  unlink(addr.sun_path);

  if (bind(c->listen_fd, (struct sockaddr *)&addr, addr_len) != 0) {
    close(c->listen_fd);
    free(c);
    return NULL;
  }

  if (listen(c->listen_fd, 5) != 0) {
    close(c->listen_fd);
    free(c);
    return NULL;
  }

  fprintf(stderr, "video capture uds listener initialized\n");
  return c;
}

void capture_destroy(capture_t *c) {
  if (c) {
    if (c->client_fd != -1) {
      close(c->client_fd);
    }
    if (c->listen_fd != -1) {
      close(c->listen_fd);
    }
    if (c->buffer) {
      free(c->buffer);
    }
    free(c);
  }
}

const uint8_t *capture_frame(capture_t *c, uint32_t *width, uint32_t *height,
                             uint32_t *format, size_t *size) {
  if (!c)
    return NULL;

  /* try to accept a new client connection if disconnected */
  if (c->client_fd == -1) {
    c->client_fd = accept(c->listen_fd, NULL, NULL);
    if (c->client_fd != -1) {
      set_nonblock(c->client_fd);
      fprintf(stderr, "video capture helper connected\n");
    }
  }

  /* read all fully available frames to get the latest one */
  if (c->client_fd != -1) {
    while (1) {
      uint8_t ivf[44];
      ssize_t ret =
          recv(c->client_fd, ivf, sizeof(ivf), MSG_PEEK | MSG_DONTWAIT);
      if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        }
        if (errno == EINTR) {
          continue;
        }
        fprintf(stderr, "video capture client disconnected unexpectedly\n");
        close(c->client_fd);
        c->client_fd = -1;
        break;
      }
      if (ret == 0) {
        fprintf(stderr, "video capture client disconnected\n");
        close(c->client_fd);
        c->client_fd = -1;
        break;
      }
      if ((size_t)ret < sizeof(ivf)) {
        break;
      }

      /* read the header */
      if (!read_exact(c->client_fd, ivf, sizeof(ivf))) {
        fprintf(stderr, "failed to read video header\n");
        close(c->client_fd);
        c->client_fd = -1;
        break;
      }

      if (memcmp(ivf, "DKIF", 4) != 0) {
        fprintf(stderr, "invalid IVF signature\n");
        close(c->client_fd);
        c->client_fd = -1;
        break;
      }

      uint32_t w = ivf[12] | (ivf[13] << 8);
      uint32_t h = ivf[14] | (ivf[15] << 8);
      uint32_t frame_size =
          ivf[32] | (ivf[33] << 8) | (ivf[34] << 16) | (ivf[35] << 24);
      uint32_t fmt = 1; /* BGRA format */

      c->width = w;
      c->height = h;
      c->format = fmt;
      c->buffer = realloc(c->buffer, 44 + frame_size);
      if (!c->buffer) {
        fprintf(stderr, "failed to allocate video buffer\n");
        close(c->client_fd);
        c->client_fd = -1;
        break;
      }

      memcpy(c->buffer, ivf, 44);

      /* read the body */
      if (!read_exact(c->client_fd, c->buffer + 44, frame_size)) {
        fprintf(stderr, "failed to read video body\n");
        close(c->client_fd);
        c->client_fd = -1;
        break;
      }

      c->has_frame = true;
      c->buffer_size = 44 + frame_size;
    }
  }

  if (c->has_frame) {
    *width = c->width;
    *height = c->height;
    *format = c->format;
    *size = c->buffer_size;
    return c->buffer;
  }

  return NULL;
}

void capture_request_keyframe(capture_t *c) {
  if (c && c->client_fd != -1) {
    uint8_t cmd = 0x01; /* UDS_CMD_KEYFRAME_REQUEST */
    if (write(c->client_fd, &cmd, 1) < 0) {
    }
  }
}

void capture_set_bitrate(capture_t *c, uint32_t bitrate_bps) {
  if (c && c->client_fd != -1) {
    uint8_t buf[5];
    buf[0] = 0x02; /* UDS_CMD_BITRATE_LIMIT */
    buf[1] = (uint8_t)((bitrate_bps >> 24) & 0xFF);
    buf[2] = (uint8_t)((bitrate_bps >> 16) & 0xFF);
    buf[3] = (uint8_t)((bitrate_bps >> 8) & 0xFF);
    buf[4] = (uint8_t)(bitrate_bps & 0xFF);
    if (write(c->client_fd, buf, 5) < 0) {
    }
  }
}
