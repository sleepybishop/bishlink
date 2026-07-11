/* test_uds.c */

#include "capture.h"
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

static void setup_uds_addr(struct sockaddr_un *addr, socklen_t *len,
                           const char *name) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  snprintf(addr->sun_path, sizeof(addr->sun_path), "/tmp/%s.sock", name);
  *len = sizeof(addr->sun_family) + strlen(addr->sun_path);
}

int main(void) {
  printf("testing video capture uds listener...\n");

  /* 1. setup video capture listener */
  capture_config_t cap_cfg = {.width = 100, .height = 100, .fps = 60};
  capture_t *capture = capture_create(&cap_cfg);
  if (!capture) {
    fprintf(stderr, "failed to create capture instance\n");
    return 1;
  }

  /* 2. create client socket and feed a test frame */
  int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (client_fd < 0) {
    fprintf(stderr, "failed to create client socket\n");
    capture_destroy(capture);
    return 1;
  }

  struct sockaddr_un addr;
  socklen_t addr_len;
  setup_uds_addr(&addr, &addr_len, "bishlink-video");

  /* wait a moment for the listen socket to bind */
  usleep(10 * 1000);

  if (connect(client_fd, (struct sockaddr *)&addr, addr_len) != 0) {
    fprintf(stderr, "failed to connect client socket: %s\n", strerror(errno));
    close(client_fd);
    capture_destroy(capture);
    return 1;
  }

  /* send frame: IVF header (44 bytes) + payload (20 bytes) */
  uint8_t ivf[44];
  memcpy(ivf, "DKIF", 4);
  ivf[4] = 0;
  ivf[5] = 0; /* version */
  ivf[6] = 32;
  ivf[7] = 0;                 /* header size */
  memcpy(ivf + 8, "BGRA", 4); /* fourcc */
  uint32_t target_w = 1920;
  uint32_t target_h = 1080;
  uint32_t target_fps = 60;
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

  uint32_t frame_size = 20;
  ivf[32] = (uint8_t)(frame_size & 0xFF);
  ivf[33] = (uint8_t)((frame_size >> 8) & 0xFF);
  ivf[34] = (uint8_t)((frame_size >> 16) & 0xFF);
  ivf[35] = (uint8_t)((frame_size >> 24) & 0xFF);
  uint64_t mock_pts = 12345;
  memcpy(ivf + 36, &mock_pts, 8);

  const char *payload = "hello video capture!";
  write(client_fd, ivf, sizeof(ivf));
  write(client_fd, payload, 20);

  /* wait for capture to process */
  usleep(50 * 1000);

  uint32_t w = 0, h = 0, format = 0;
  size_t size = 0;
  const uint8_t *frame = capture_frame(capture, &w, &h, &format, &size);

  if (!frame) {
    fprintf(stderr, "failed to capture frame\n");
    close(client_fd);
    capture_destroy(capture);
    return 1;
  }

  if (w != 1920 || h != 1080 || format != 1 || size != 64 ||
      memcmp(frame + 44, payload, 20) != 0) {
    fprintf(stderr,
            "video payload verification failed: w=%d h=%d format=%d size=%zu\n",
            w, h, format, size);
    close(client_fd);
    capture_destroy(capture);
    return 1;
  }

  close(client_fd);
  capture_destroy(capture);
  printf("video capture uds test passed.\n");

  /* 3. setup mock input injection daemon listener */
  printf("testing input injection uds client...\n");
  int mock_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (mock_listen_fd < 0) {
    fprintf(stderr, "failed to create mock listen socket\n");
    return 1;
  }

  setup_uds_addr(&addr, &addr_len, "bishlink-input");
  unlink(addr.sun_path); /* clear any stale filesystem socket */

  if (bind(mock_listen_fd, (struct sockaddr *)&addr, addr_len) != 0) {
    fprintf(stderr, "failed to bind mock listen socket: %s\n", strerror(errno));
    close(mock_listen_fd);
    return 1;
  }

  if (listen(mock_listen_fd, 5) != 0) {
    close(mock_listen_fd);
    return 1;
  }

  /* 4. create inject client */
  inject_t *inject = inject_create();
  if (!inject) {
    fprintf(stderr, "failed to create inject client\n");
    close(mock_listen_fd);
    return 1;
  }

  /* accept connection on mock listener */
  int mock_conn_fd = accept(mock_listen_fd, NULL, NULL);
  if (mock_conn_fd < 0) {
    fprintf(stderr, "failed to accept mock inject connection\n");
    inject_destroy(inject);
    close(mock_listen_fd);
    return 1;
  }

  /* inject event */
  inject_keyboard(inject, 123, true);

  /* read back injected event */
  input_event_t evt;
  ssize_t rret = read(mock_conn_fd, &evt, sizeof(evt));
  if (rret != sizeof(evt)) {
    fprintf(stderr, "failed to read injected event: got %zd bytes\n", rret);
    close(mock_conn_fd);
    inject_destroy(inject);
    close(mock_listen_fd);
    return 1;
  }

  if (evt.type != INPUT_TYPE_KEYBOARD || evt.keyboard.key != 123 ||
      !evt.keyboard.pressed) {
    fprintf(stderr, "injected event verification failed\n");
    close(mock_conn_fd);
    inject_destroy(inject);
    close(mock_listen_fd);
    return 1;
  }

  close(mock_conn_fd);
  inject_destroy(inject);
  close(mock_listen_fd);

  printf("===UDS OK===\n");
  return 0;
}
