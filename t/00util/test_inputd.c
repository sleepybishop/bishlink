/* test_inputd.c */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "input_event.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
  printf("starting input injection daemon test...\n");

  /* 1. fork and start bishlink-inputd in mock mode, redirecting stdout */
  unlink("/tmp/inputd_out.log");
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork failed");
    return 1;
  }

  if (pid == 0) {
    /* child: redirect stdout to file and exec inputd */
    int log_fd =
        open("/tmp/inputd_out.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (log_fd < 0) {
      perror("child failed to open log file");
      exit(1);
    }
    dup2(log_fd, STDOUT_FILENO);
    close(log_fd);

    char *args[] = {"./bishlink-inputd", "--mock", NULL};
    execv(args[0], args);
    perror("execv failed");
    exit(1);
  }

  /* parent: wait for server to start */
  usleep(150 * 1000);

  /* 2. connect to socket as client */
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("failed to create socket");
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    return 1;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  snprintf(addr.sun_path, sizeof(addr.sun_path), "/tmp/bishlink-input.sock");

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    fprintf(stderr, "failed to connect to inputd: %s\n", strerror(errno));
    close(fd);
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);
    return 1;
  }

  /* 3. send a keyboard event (key 30 = KEY_A, pressed) */
  input_event_t key_evt = {.type = INPUT_TYPE_KEYBOARD,
                           .keyboard = {.key = 30, .pressed = true}};
  if (write(fd, &key_evt, sizeof(key_evt)) != sizeof(key_evt)) {
    fprintf(stderr, "failed to write keyboard event\n");
  }

  /* 4. send relative mouse move event (dx = 10, dy = -20) */
  input_event_t move_evt = {
      .type = INPUT_TYPE_MOUSE_MOVE,
      .mouse_move = {.dx = 10, .dy = -20, .absolute = false}};
  if (write(fd, &move_evt, sizeof(move_evt)) != sizeof(move_evt)) {
    fprintf(stderr, "failed to write mouse move event\n");
  }

  /* wait for daemon to process */
  usleep(100 * 1000);

  /* close connection and terminate daemon */
  close(fd);
  usleep(50 * 1000);
  kill(pid, SIGTERM);
  waitpid(pid, NULL, 0);

  /* 5. read log file and verify output contains the mapped events */
  FILE *log_file = fopen("/tmp/inputd_out.log", "r");
  if (!log_file) {
    perror("failed to open log file");
    return 1;
  }

  char line[256];
  bool got_key_a = false;
  bool got_rel_x = false;
  bool got_rel_y = false;
  bool got_syn = false;

  while (fgets(line, sizeof(line), log_file)) {
    if (strstr(line, "EVENT type=1 code=30 value=1")) {
      got_key_a = true;
    }
    if (strstr(line, "EVENT type=2 code=0 value=10")) {
      got_rel_x = true;
    }
    if (strstr(line, "EVENT type=2 code=1 value=-20")) {
      got_rel_y = true;
    }
    if (strstr(line, "EVENT type=0 code=0 value=0")) {
      got_syn = true;
    }
  }
  fclose(log_file);

  if (!got_key_a) {
    fprintf(stderr, "verification failed: missed KEY_A press event\n");
    return 1;
  }
  if (!got_rel_x || !got_rel_y) {
    fprintf(
        stderr,
        "verification failed: missed REL_X or REL_Y relative motion event\n");
    return 1;
  }
  if (!got_syn) {
    fprintf(stderr, "verification failed: missed EV_SYN SYN_REPORT event\n");
    return 1;
  }

  printf("===INPUTD OK===\n");
  return 0;
}
