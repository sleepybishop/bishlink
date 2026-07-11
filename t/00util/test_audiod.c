/* test_audiod.c */

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
  printf("starting audio capture daemon test...\n");

  /* 1. setup socket server at /tmp/bishlink-audio.sock */
  int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("failed to create socket");
    return 1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/bishlink-audio.sock");
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

  /* 2. fork and start bishlink-audiod in mock mode */
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork failed");
    close(listen_fd);
    return 1;
  }

  if (pid == 0) {
    /* child: exec audiod in mock mode */
    char *args[] = {"./bishlink-audiod", "--mock", NULL};
    execv(args[0], args);
    perror("execv failed");
    exit(1);
  }

  /* parent: accept connection from audiod */
  printf("waiting for audiod client connection...\n");
  int conn_fd = accept(listen_fd, NULL, NULL);
  if (conn_fd < 0) {
    perror("accept failed");
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    close(listen_fd);
    return 1;
  }

  /* 3. read and verify first frame sample count */
  uint32_t sample_count = 0;
  ssize_t rret = read(conn_fd, &sample_count, sizeof(sample_count));
  if (rret != sizeof(sample_count)) {
    fprintf(stderr, "failed to read sample count: got %zd bytes\n", rret);
    close(conn_fd);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    close(listen_fd);
    return 1;
  }

  printf("received sample count: %u\n", sample_count);

  if (sample_count != 1764) {
    fprintf(stderr, "sample count verification failed: expected 1764, got %u\n",
            sample_count);
    close(conn_fd);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    close(listen_fd);
    return 1;
  }

  /* 4. read audio PCM bytes */
  size_t byte_size = sample_count * sizeof(int16_t);
  uint8_t *buf = malloc(byte_size);
  if (!buf) {
    perror("malloc failed");
    close(conn_fd);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    close(listen_fd);
    return 1;
  }

  size_t read_bytes = 0;
  while (read_bytes < byte_size) {
    ssize_t ret = read(conn_fd, buf + read_bytes, byte_size - read_bytes);
    if (ret <= 0) {
      if (ret < 0 && errno == EINTR)
        continue;
      fprintf(stderr, "failed to read audio payload\n");
      free(buf);
      close(conn_fd);
      kill(pid, SIGTERM);
      waitpid(pid, NULL, 0);
      close(listen_fd);
      return 1;
    }
    read_bytes += ret;
  }

  /* cleanup */
  free(buf);
  close(conn_fd);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);
  close(listen_fd);
  unlink(addr.sun_path);

  printf("===AUDIOD OK===\n");
  return 0;
}
