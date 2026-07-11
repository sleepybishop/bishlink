/* inputd.c */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "input_event.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static bool mock_mode = false;

/* translate MTY_Key enums to standard Linux keycodes */
static uint16_t map_key_to_linux(uint16_t mty_key) {
  if (mty_key < 256) {
    return mty_key;
  }

  switch (mty_key) {
  case 0x110:
    return KEY_PREVIOUSSONG;
  case 0x119:
    return KEY_NEXTSONG;
  case 0x11c:
    return KEY_KPENTER;
  case 0x11d:
    return KEY_RIGHTCTRL;
  case 0x120:
    return KEY_MUTE;
  case 0x122:
    return KEY_PLAYPAUSE;
  case 0x124:
    return KEY_STOPCD;
  case 0x12e:
    return KEY_VOLUMEDOWN;
  case 0x130:
    return KEY_VOLUMEUP;
  case 0x135:
    return KEY_KPSLASH;
  case 0x136:
    return KEY_LEFTSHIFT;
  case 0x137:
    return KEY_SYSRQ;
  case 0x138:
    return KEY_RIGHTALT;
  case 0x145:
    return KEY_NUMLOCK;
  case 0x147:
    return KEY_HOME;
  case 0x148:
    return KEY_UP;
  case 0x149:
    return KEY_PAGEUP;
  case 0x14b:
    return KEY_LEFT;
  case 0x14d:
    return KEY_RIGHT;
  case 0x14f:
    return KEY_END;
  case 0x150:
    return KEY_DOWN;
  case 0x151:
    return KEY_PAGEDOWN;
  case 0x152:
    return KEY_INSERT;
  case 0x153:
    return KEY_DELETE;
  case 0x15b:
    return KEY_LEFTMETA;
  case 0x15c:
    return KEY_RIGHTMETA;
  default:
    return 0;
  }
}

/* translate MTY_Button mouse clicks to Linux BTN codes */
static uint16_t map_button_to_linux(uint8_t mty_button) {
  switch (mty_button) {
  case 1:
    return BTN_LEFT;
  case 2:
    return BTN_RIGHT;
  case 3:
    return BTN_MIDDLE;
  case 4:
    return BTN_SIDE;
  case 5:
    return BTN_EXTRA;
  default:
    return 0;
  }
}

static int create_uinput_device(void) {
  if (mock_mode) {
    printf("mock mode enabled: skipping /dev/uinput creation\n");
    return 1000;
  }

  int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (fd < 0) {
    fd = open("/dev/uinput", O_WRONLY);
  }
  if (fd < 0) {
    perror("failed to open /dev/uinput");
    return -1;
  }

  /* register event types */
  ioctl(fd, UI_SET_EVBIT, EV_KEY);
  ioctl(fd, UI_SET_EVBIT, EV_REL);
  ioctl(fd, UI_SET_EVBIT, EV_SYN);

  /* register mouse buttons */
  ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
  ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
  ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);
  ioctl(fd, UI_SET_KEYBIT, BTN_SIDE);
  ioctl(fd, UI_SET_KEYBIT, BTN_EXTRA);

  /* register relative mouse axes */
  ioctl(fd, UI_SET_RELBIT, REL_X);
  ioctl(fd, UI_SET_RELBIT, REL_Y);

  /* register standard keyboard keys */
  for (int i = 1; i < 256; i++) {
    ioctl(fd, UI_SET_KEYBIT, i);
  }

  /* register extended keyboard keys */
  ioctl(fd, UI_SET_KEYBIT, KEY_RIGHTCTRL);
  ioctl(fd, UI_SET_KEYBIT, KEY_RIGHTALT);
  ioctl(fd, UI_SET_KEYBIT, KEY_UP);
  ioctl(fd, UI_SET_KEYBIT, KEY_DOWN);
  ioctl(fd, UI_SET_KEYBIT, KEY_LEFT);
  ioctl(fd, UI_SET_KEYBIT, KEY_RIGHT);
  ioctl(fd, UI_SET_KEYBIT, KEY_HOME);
  ioctl(fd, UI_SET_KEYBIT, KEY_END);
  ioctl(fd, UI_SET_KEYBIT, KEY_PAGEUP);
  ioctl(fd, UI_SET_KEYBIT, KEY_PAGEDOWN);
  ioctl(fd, UI_SET_KEYBIT, KEY_INSERT);
  ioctl(fd, UI_SET_KEYBIT, KEY_DELETE);
  ioctl(fd, UI_SET_KEYBIT, KEY_LEFTMETA);
  ioctl(fd, UI_SET_KEYBIT, KEY_RIGHTMETA);

  /* configure device setup */
  struct uinput_setup usetup;
  memset(&usetup, 0, sizeof(usetup));
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = 0x1234;
  usetup.id.product = 0x5678;
  strcpy(usetup.name, "bishlink virtual keyboard/mouse");

  if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0) {
    /* fallback for older kernels */
    struct uinput_user_dev udev;
    memset(&udev, 0, sizeof(udev));
    strcpy(udev.name, "bishlink virtual keyboard/mouse");
    udev.id.bustype = BUS_USB;
    udev.id.vendor = 0x1234;
    udev.id.product = 0x5678;
    if (write(fd, &udev, sizeof(udev)) < 0) {
      perror("failed to write fallback uinput config");
      close(fd);
      return -1;
    }
  }

  if (ioctl(fd, UI_DEV_CREATE) < 0) {
    perror("failed to create uinput device");
    close(fd);
    return -1;
  }

  return fd;
}

static void write_event(int fd, uint16_t type, uint16_t code, int32_t value) {
  if (mock_mode) {
    printf("EVENT type=%d code=%d value=%d\n", type, code, value);
    fflush(stdout);
    return;
  }

  struct input_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = type;
  ev.code = code;
  ev.value = value;
  if (write(fd, &ev, sizeof(ev)) < 0) {
    /* suppress errors to keep streaming */
  }
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--mock") == 0 || strcmp(argv[i], "-m") == 0) {
      mock_mode = true;
    }
  }

  printf("initializing bishlink virtual input injection daemon...\n");

  int uinput_fd = create_uinput_device();
  if (uinput_fd < 0) {
    fprintf(
        stderr,
        "failed to setup virtual uinput devices. make sure you run as root.\n");
    return 1;
  }

  int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("failed to create socket");
    close(uinput_fd);
    return 1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/bishlink-input.sock");
  unlink(addr.sun_path);

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    perror("failed to bind socket");
    close(listen_fd);
    close(uinput_fd);
    return 1;
  }

  if (listen(listen_fd, 5) != 0) {
    perror("failed to listen");
    close(listen_fd);
    close(uinput_fd);
    return 1;
  }

  printf("listening on /tmp/bishlink-input.sock...\n");

  while (1) {
    int client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      perror("accept failed");
      break;
    }

    printf("host connection accepted\n");

    while (1) {
      input_event_t evt;
      ssize_t ret = read(client_fd, &evt, sizeof(evt));
      if (ret < 0) {
        if (errno == EINTR) {
          continue;
        }
        break;
      }
      if (ret == 0) {
        break; /* connection closed */
      }
      if (ret != sizeof(evt)) {
        continue; /* incomplete packet */
      }

      if (evt.type == INPUT_TYPE_KEYBOARD) {
        uint16_t key = map_key_to_linux(evt.keyboard.key);
        if (key != 0) {
          write_event(uinput_fd, EV_KEY, key, evt.keyboard.pressed ? 1 : 0);
          write_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
        }
      } else if (evt.type == INPUT_TYPE_MOUSE_MOVE) {
        /* relative mouse movement */
        write_event(uinput_fd, EV_REL, REL_X, evt.mouse_move.dx);
        write_event(uinput_fd, EV_REL, REL_Y, evt.mouse_move.dy);
        write_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
      } else if (evt.type == INPUT_TYPE_MOUSE_BUTTON) {
        uint16_t btn = map_button_to_linux(evt.mouse_button.button);
        if (btn != 0) {
          write_event(uinput_fd, EV_KEY, btn, evt.mouse_button.pressed ? 1 : 0);
          write_event(uinput_fd, EV_SYN, SYN_REPORT, 0);
        }
      }
    }

    printf("host disconnected\n");
    close(client_fd);
  }

  close(listen_fd);
  ioctl(uinput_fd, UI_DEV_DESTROY);
  close(uinput_fd);

  return 0;
}
