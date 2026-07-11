/* inject_uinput.c */

#include "inject.h"
#include "input_event.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

struct inject_t {
  int fd;
};

static void setup_uds_addr(struct sockaddr_un *addr, socklen_t *len,
                           const char *name) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  snprintf(addr->sun_path, sizeof(addr->sun_path), "/tmp/%s.sock", name);
  *len = sizeof(addr->sun_family) + strlen(addr->sun_path);
}

static bool try_connect(inject_t *j) {
  if (j->fd != -1) {
    return true;
  }

  j->fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (j->fd < 0) {
    return false;
  }

  struct sockaddr_un addr;
  socklen_t addr_len;
  setup_uds_addr(&addr, &addr_len, "bishlink-input");

  if (connect(j->fd, (struct sockaddr *)&addr, addr_len) != 0) {
    close(j->fd);
    j->fd = -1;
    return false;
  }

  fprintf(stderr, "connected to input injection daemon\n");
  return true;
}

static void send_event(inject_t *j, const input_event_t *evt) {
  if (!try_connect(j)) {
    return; /* discard if injection daemon is not running */
  }

  size_t written = 0;
  size_t total = sizeof(input_event_t);
  const uint8_t *data = (const uint8_t *)evt;

  while (written < total) {
    ssize_t ret = write(j->fd, data + written, total - written);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      fprintf(stderr, "failed to send input event, disconnecting\n");
      close(j->fd);
      j->fd = -1;
      return;
    }
    written += ret;
  }
}

inject_t *inject_create(void) {
  inject_t *j = calloc(1, sizeof(inject_t));
  if (!j)
    return NULL;

  j->fd = -1;
  try_connect(j); /* try initial connection */

  return j;
}

void inject_destroy(inject_t *j) {
  if (j) {
    if (j->fd != -1) {
      close(j->fd);
    }
    free(j);
  }
}

void inject_keyboard(inject_t *j, uint16_t key, bool pressed) {
  input_event_t evt = {.type = INPUT_TYPE_KEYBOARD,
                       .keyboard = {.key = key, .pressed = pressed}};
  send_event(j, &evt);
}

void inject_mouse_move(inject_t *j, int32_t dx, int32_t dy, bool absolute) {
  input_event_t evt = {
      .type = INPUT_TYPE_MOUSE_MOVE,
      .mouse_move = {.dx = dx, .dy = dy, .absolute = absolute}};
  send_event(j, &evt);
}

void inject_mouse_button(inject_t *j, uint8_t button, bool pressed) {
  input_event_t evt = {.type = INPUT_TYPE_MOUSE_BUTTON,
                       .mouse_button = {.button = button, .pressed = pressed}};
  send_event(j, &evt);
}

void inject_gamepad(inject_t *j, uint8_t pad_idx, uint16_t buttons, int16_t lx,
                    int16_t ly, int16_t rx, int16_t ry, uint8_t lt,
                    uint8_t rt) {
  input_event_t evt = {.type = INPUT_TYPE_GAMEPAD,
                       .gamepad = {.pad_idx = pad_idx,
                                   .buttons = buttons,
                                   .lx = lx,
                                   .ly = ly,
                                   .rx = rx,
                                   .ry = ry,
                                   .lt = lt,
                                   .rt = rt}};
  send_event(j, &evt);
}
