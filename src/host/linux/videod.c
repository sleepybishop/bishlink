/* videod.c */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MTY_COLOR_FORMAT_BGRA 1

static bool mock_mode = false;
static uint32_t target_w = 1280;
static uint32_t target_h = 720;
static uint32_t target_fps = 60;

static void setup_uds_addr(struct sockaddr_un *addr, socklen_t *len,
                           const char *name) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  snprintf(addr->sun_path, sizeof(addr->sun_path), "/tmp/%s.sock", name);
  *len = sizeof(addr->sun_family) + strlen(addr->sun_path);
}

static uint8_t *generate_mock_frame(uint32_t w, uint32_t h, size_t *out_size) {
  size_t size = w * h * 4;
  uint8_t *buf = malloc(size);
  if (!buf)
    return NULL;

  static uint8_t color_offset = 0;
  color_offset += 2;

  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      size_t idx = (y * w + x) * 4;
      buf[idx + 0] = (uint8_t)(x + color_offset);     /* B */
      buf[idx + 1] = (uint8_t)(y + color_offset);     /* G */
      buf[idx + 2] = (uint8_t)(x + y + color_offset); /* R */
      buf[idx + 3] = 255;                             /* A */
    }
  }

  *out_size = size;
  return buf;
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--mock") == 0 || strcmp(argv[i], "-m") == 0) {
      mock_mode = true;
    }
  }

  printf("initializing bishlink screen capture daemon...\n");

  Display *display = NULL;
  Window root = 0;
  XImage *shm_image = NULL;
  XShmSegmentInfo shminfo = {0};
  bool x11_shm_active = false;

  if (!mock_mode) {
    display = XOpenDisplay(NULL);
    if (!display) {
      fprintf(
          stderr,
          "warning: failed to open X display. falling back to mock capture.\n");
      mock_mode = true;
    }
  }

  if (!mock_mode && display) {
    root = DefaultRootWindow(display);
    XWindowAttributes attr;
    XGetWindowAttributes(display, root, &attr);
    target_w = attr.width;
    target_h = attr.height;

    if (XShmQueryExtension(display)) {
      shm_image = XShmCreateImage(
          display, DefaultVisual(display, DefaultScreen(display)), attr.depth,
          ZPixmap, NULL, &shminfo, target_w, target_h);
      if (shm_image) {
        shminfo.shmid =
            shmget(IPC_PRIVATE, shm_image->bytes_per_line * shm_image->height,
                   IPC_CREAT | 0777);
        if (shminfo.shmid != -1) {
          shminfo.shmaddr = shm_image->data = shmat(shminfo.shmid, 0, 0);
          shminfo.readOnly = False;
          if (shminfo.shmaddr != (char *)-1) {
            if (XShmAttach(display, &shminfo)) {
              x11_shm_active = true;
              printf("XShm capture initialized at %dx%d\n", target_w, target_h);
            }
          }
        }
      }
    }

    if (!x11_shm_active) {
      fprintf(stderr, "warning: XShm extension not available. falling back to "
                      "mock capture.\n");
      mock_mode = true;
      if (shm_image) {
        XDestroyImage(shm_image);
        shm_image = NULL;
      }
      XCloseDisplay(display);
      display = NULL;
    }
  }

  int server_fd = -1;

  while (1) {
    /* self-healing: connect / reconnect loop */
    if (server_fd == -1) {
      server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
      if (server_fd >= 0) {
        struct sockaddr_un addr;
        socklen_t addr_len;
        setup_uds_addr(&addr, &addr_len, "bishlink-video");
        if (connect(server_fd, (struct sockaddr *)&addr, addr_len) != 0) {
          close(server_fd);
          server_fd = -1;
          usleep(500 * 1000); /* wait before retry */
          continue;
        }
        printf("connected to bishlink video socket\n");
      }
    }

    /* capture frame */
    uint8_t *frame_data = NULL;
    size_t frame_size = 0;

    if (mock_mode) {
      frame_data = generate_mock_frame(target_w, target_h, &frame_size);
    } else {
      XShmGetImage(display, root, shm_image, 0, 0, AllPlanes);
      frame_data = (uint8_t *)shm_image->data;
      frame_size = shm_image->bytes_per_line * shm_image->height;
    }

    /* transmit frame over UDS using IVF framing */
    if (frame_data && frame_size > 0 && server_fd != -1) {
      uint8_t ivf[44];
      memcpy(ivf, "DKIF", 4);
      ivf[4] = 0;
      ivf[5] = 0;
      ivf[6] = 32;
      ivf[7] = 0;
      memcpy(ivf + 8, "BGRA", 4);
      ivf[12] = (uint8_t)(target_w & 0xFF);
      ivf[13] = (uint8_t)((target_w >> 8) & 0xFF);
      ivf[14] = (uint8_t)(target_h & 0xFF);
      ivf[15] = (uint8_t)((target_h >> 8) & 0xFF);
      ivf[16] = (uint8_t)(target_fps & 0xFF);
      ivf[17] = (uint8_t)((target_fps >> 8) & 0xFF);
      ivf[18] = 0;
      ivf[19] = 0;
      ivf[20] = 1;
      ivf[21] = 0;
      ivf[22] = 0;
      ivf[23] = 0;
      memset(ivf + 24, 0xFF, 4);
      memset(ivf + 28, 0, 4);

      ivf[32] = (uint8_t)(frame_size & 0xFF);
      ivf[33] = (uint8_t)((frame_size >> 8) & 0xFF);
      ivf[34] = (uint8_t)((frame_size >> 16) & 0xFF);
      ivf[35] = (uint8_t)((frame_size >> 24) & 0xFF);
      static uint64_t mock_pts = 0;
      mock_pts += 16666;
      memcpy(ivf + 36, &mock_pts, 8);

      ssize_t ret = write(server_fd, ivf, sizeof(ivf));
      if (ret < 0) {
        fprintf(stderr, "failed to write IVF header, disconnecting\n");
        close(server_fd);
        server_fd = -1;
      } else {
        size_t written = 0;
        bool write_err = false;
        while (written < frame_size) {
          ssize_t wret =
              write(server_fd, frame_data + written, frame_size - written);
          if (wret < 0) {
            if (errno == EINTR)
              continue;
            write_err = true;
            break;
          }
          written += wret;
        }
        if (write_err) {
          fprintf(stderr, "failed to write video payload, disconnecting\n");
          close(server_fd);
          server_fd = -1;
        }
      }
    }

    if (mock_mode && frame_data) {
      free(frame_data);
    }

    /* check for control messages from host */
    if (server_fd != -1) {
      uint8_t cmd = 0;
      ssize_t rret = recv(server_fd, &cmd, 1, MSG_DONTWAIT);
      if (rret > 0) {
        if (cmd == 0x01) {
          printf("videod: received keyframe request from host\n");
        } else if (cmd == 0x02) {
          uint8_t val_buf[4];
          size_t got = 0;
          while (got < 4) {
            ssize_t rr = recv(server_fd, val_buf + got, 4 - got, 0);
            if (rr <= 0) {
              break;
            }
            got += rr;
          }
          if (got == 4) {
            uint32_t br = ((uint32_t)val_buf[0] << 24) |
                          ((uint32_t)val_buf[1] << 16) |
                          ((uint32_t)val_buf[2] << 8) | (uint32_t)val_buf[3];
            printf("videod: received target bitrate from host: %u bps\n", br);
          }
        }
      } else if (rret == 0) {
        fprintf(stderr, "host disconnected\n");
        close(server_fd);
        server_fd = -1;
      }
    }

    /* sleep based on target fps */
    usleep(1000000 / target_fps);
  }

  if (x11_shm_active) {
    XShmDetach(display, &shminfo);
    XDestroyImage(shm_image);
    shmdt(shminfo.shmaddr);
    shmctl(shminfo.shmid, IPC_RMID, 0);
  }
  if (display) {
    XCloseDisplay(display);
  }

  return 0;
}
