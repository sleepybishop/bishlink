/* test_videod.c */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
  printf("starting video capture daemon test...\n");

  /* 1. setup socket server at /tmp/bishlink-video.sock */
  int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("failed to create socket");
    return 1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/bishlink-video.sock");
  unlink(addr.sun_path);

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    perror("failed to bind socket");
    close(listen_fd);
    return 1;
  }

  if (listen(listen_fd, 5) != 0) {
    perror("failed to listen");
    close(listen_fd);
    return 1;
  }

  /* 2. fork and start bishlink-videod in mock mode */
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork failed");
    close(listen_fd);
    return 1;
  }

  if (pid == 0) {
    /* child: exec videod in mock mode */
    char *args[] = {"./bishlink-videod", "--mock", NULL};
    execv(args[0], args);
    perror("execv failed");
    exit(1);
  }

  /* parent: accept connection from videod */
  printf("waiting for videod client connection...\n");
  int conn_fd = accept(listen_fd, NULL, NULL);
  if (conn_fd < 0) {
    perror("accept failed");
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    close(listen_fd);
    return 1;
  }

  /* 3. read and verify first frame header */
  uint8_t ivf[44] = {0};
  ssize_t rret = read(conn_fd, ivf, sizeof(ivf));
  if (rret != sizeof(ivf)) {
    fprintf(stderr, "failed to read frame header: got %zd bytes\n", rret);
    close(conn_fd);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    close(listen_fd);
    return 1;
  }

  if (memcmp(ivf, "DKIF", 4) != 0) {
    fprintf(stderr, "invalid IVF signature\n");
    close(conn_fd);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    close(listen_fd);
    return 1;
  }

  uint32_t w = ivf[12] | (ivf[13] << 8);
  uint32_t h = ivf[14] | (ivf[15] << 8);
  uint32_t size = ivf[32] | (ivf[33] << 8) | (ivf[34] << 16) | (ivf[35] << 24);
  char fourcc[5] = {0};
  memcpy(fourcc, ivf + 8, 4);

  printf("received frame: w=%d h=%d size=%d fourcc=%s\n", w, h, size, fourcc);

  if (w != 1280 || h != 720 || size != 1280 * 720 * 4 ||
      memcmp(fourcc, "BGRA", 4) != 0) {
    fprintf(stderr, "frame header verification failed\n");
    close(conn_fd);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    close(listen_fd);
    return 1;
  }

  /* 4. read frame body bytes partially to check data presence */
  uint8_t buf[1024];
  ssize_t body_read = read(conn_fd, buf, sizeof(buf));
  if (body_read <= 0) {
    fprintf(stderr, "failed to read frame body data\n");
    close(conn_fd);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    close(listen_fd);
    return 1;
  }

  /* cleanup */
  close(conn_fd);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  close(listen_fd);
  unlink(addr.sun_path);

  printf("===VIDEOD OK===\n");
  return 0;
}
