/* data_uds.c */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "data_uds.h"
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_UDS_CLIENTS 16

typedef struct {
  int fd;
  moq_track_id_t track_id;
  bool active;
} uds_client_t;

struct data_uds_t {
  pthread_t thread;
  int listen_fd;
  pthread_mutex_t send_mutex;
  data_uds_callback_t callback;
  void *user_data;
  char socket_name[128];
  volatile bool running;
  uds_client_t clients[MAX_UDS_CLIENTS];
};

/* setup UDS address */
static void setup_uds_addr(struct sockaddr_un *addr, socklen_t *len,
                           const char *name) {
  memset(addr, 0, sizeof(*addr));
  addr->sun_family = AF_UNIX;
  snprintf(addr->sun_path, sizeof(addr->sun_path), "/tmp/%s.sock", name);
  *len = sizeof(addr->sun_family) + strlen(addr->sun_path);
}

/* read exact number of bytes */
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

/* worker thread func for managing UDS multiplexing */
static void *data_thread_func(void *arg) {
  data_uds_t *d = arg;
  uint8_t *payload_buf = NULL;
  size_t payload_buf_capacity = 0;

  struct pollfd fds[MAX_UDS_CLIENTS + 1];

  while (d->running) {
    int nfds = 1;
    fds[0].fd = d->listen_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    pthread_mutex_lock(&d->send_mutex);
    int client_indices[MAX_UDS_CLIENTS];
    for (int i = 0; i < MAX_UDS_CLIENTS; i++) {
      if (d->clients[i].active) {
        fds[nfds].fd = d->clients[i].fd;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        client_indices[nfds - 1] = i;
        nfds++;
      }
    }
    pthread_mutex_unlock(&d->send_mutex);

    int ret = poll(fds, nfds, 100);
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (ret == 0) {
      continue;
    }

    /* check listener for new connections */
    if (fds[0].revents & POLLIN) {
      int client_fd = accept(d->listen_fd, NULL, NULL);
      if (client_fd >= 0) {
        uint8_t reg_hdr[2];
        if (read_exact(client_fd, reg_hdr, 2)) {
          uint8_t track_type = reg_hdr[0];
          uint8_t name_len = reg_hdr[1];
          char name[64];
          size_t copy_len = (name_len < 63) ? name_len : 63;
          if (read_exact(client_fd, name, name_len)) {
            name[copy_len] = '\0';
            pthread_mutex_lock(&d->send_mutex);
            int slot = -1;
            for (int i = 0; i < MAX_UDS_CLIENTS; i++) {
              if (!d->clients[i].active) {
                slot = i;
                break;
              }
            }
            if (slot != -1) {
              d->clients[slot].fd = client_fd;
              d->clients[slot].track_id.type = (moq_track_type_t)track_type;
              strcpy(d->clients[slot].track_id.name, name);
              d->clients[slot].active = true;
              fprintf(stderr, "generic data UDS helper connected\n");
            } else {
              close(client_fd);
            }
            pthread_mutex_unlock(&d->send_mutex);
          } else {
            close(client_fd);
          }
        } else {
          close(client_fd);
        }
      }
    }

    /* check client sockets for data */
    for (int i = 1; i < nfds; i++) {
      if (fds[i].revents & POLLIN) {
        int idx = client_indices[i - 1];
        pthread_mutex_lock(&d->send_mutex);
        int client_fd = d->clients[idx].active ? d->clients[idx].fd : -1;
        moq_track_id_t track_id = d->clients[idx].track_id;
        pthread_mutex_unlock(&d->send_mutex);

        if (client_fd == -1) {
          continue;
        }

        uint32_t payload_size = 0;
        uint8_t priority = 1;
        bool closed = false;

        if (!read_exact(client_fd, &payload_size, sizeof(payload_size))) {
          closed = true;
        } else if (!read_exact(client_fd, &priority, sizeof(priority))) {
          closed = true;
        } else {
          if (payload_size > 0) {
            if (payload_size > payload_buf_capacity) {
              payload_buf_capacity = payload_size;
              payload_buf = realloc(payload_buf, payload_size);
            }
            if (!read_exact(client_fd, payload_buf, payload_size)) {
              closed = true;
            }
          }
        }

        if (closed) {
          fprintf(stderr, "generic data UDS helper disconnected\n");
          pthread_mutex_lock(&d->send_mutex);
          if (d->clients[idx].active) {
            close(d->clients[idx].fd);
            d->clients[idx].active = false;
          }
          pthread_mutex_unlock(&d->send_mutex);
        } else {
          if (payload_size > 0) {
            d->callback(d->user_data, &track_id, payload_buf, payload_size,
                        priority);
          }
        }
      }
    }
  }

  if (payload_buf) {
    free(payload_buf);
  }
  return NULL;
}

/* create UDS data socket listener */
data_uds_t *data_uds_create(const char *socket_name,
                            data_uds_callback_t callback, void *user_data) {
  data_uds_t *d = calloc(1, sizeof(data_uds_t));
  if (!d)
    return NULL;

  d->callback = callback;
  d->user_data = user_data;
  d->running = true;
  strncpy(d->socket_name, socket_name, sizeof(d->socket_name) - 1);

  pthread_mutex_init(&d->send_mutex, NULL);

  d->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (d->listen_fd < 0) {
    pthread_mutex_destroy(&d->send_mutex);
    free(d);
    return NULL;
  }

  struct sockaddr_un addr;
  socklen_t addr_len;
  setup_uds_addr(&addr, &addr_len, socket_name);
  unlink(addr.sun_path);

  if (bind(d->listen_fd, (struct sockaddr *)&addr, addr_len) != 0) {
    close(d->listen_fd);
    pthread_mutex_destroy(&d->send_mutex);
    free(d);
    return NULL;
  }

  if (listen(d->listen_fd, 5) != 0) {
    close(d->listen_fd);
    pthread_mutex_destroy(&d->send_mutex);
    free(d);
    return NULL;
  }

  if (pthread_create(&d->thread, NULL, data_thread_func, d) != 0) {
    close(d->listen_fd);
    pthread_mutex_destroy(&d->send_mutex);
    free(d);
    return NULL;
  }

  fprintf(stderr, "generic data UDS listener initialized on /tmp/%s.sock\n",
          socket_name);
  return d;
}

/* destroy UDS data socket listener */
void data_uds_destroy(data_uds_t *d) {
  if (d) {
    d->running = false;
    pthread_mutex_lock(&d->send_mutex);
    for (int i = 0; i < MAX_UDS_CLIENTS; i++) {
      if (d->clients[i].active) {
        shutdown(d->clients[i].fd, SHUT_RDWR);
        close(d->clients[i].fd);
        d->clients[i].active = false;
      }
    }
    pthread_mutex_unlock(&d->send_mutex);

    shutdown(d->listen_fd, SHUT_RDWR);
    close(d->listen_fd);

    pthread_join(d->thread, NULL);
    pthread_mutex_destroy(&d->send_mutex);

    struct sockaddr_un addr;
    socklen_t addr_len;
    setup_uds_addr(&addr, &addr_len, d->socket_name);
    unlink(addr.sun_path);

    free(d);
  }
}

/* send length-prefixed packet to UDS client matching track_id */
bool data_uds_send(data_uds_t *d, const moq_track_id_t *track_id,
                   const uint8_t *buf, size_t size, uint8_t priority) {
  if (!d)
    return false;

  pthread_mutex_lock(&d->send_mutex);
  int target_fd = -1;
  for (int i = 0; i < MAX_UDS_CLIENTS; i++) {
    if (d->clients[i].active && d->clients[i].track_id.type == track_id->type &&
        strcmp(d->clients[i].track_id.name, track_id->name) == 0) {
      target_fd = d->clients[i].fd;
      break;
    }
  }

  if (target_fd == -1) {
    pthread_mutex_unlock(&d->send_mutex);
    return false;
  }

  uint32_t payload_size = (uint32_t)size;
  ssize_t ret = write(target_fd, &payload_size, sizeof(payload_size));
  if (ret < 0) {
    pthread_mutex_unlock(&d->send_mutex);
    return false;
  }

  ret = write(target_fd, &priority, sizeof(priority));
  if (ret < 0) {
    pthread_mutex_unlock(&d->send_mutex);
    return false;
  }

  size_t written = 0;
  while (written < size) {
    ssize_t wret = write(target_fd, buf + written, size - written);
    if (wret < 0) {
      if (errno == EINTR) {
        continue;
      }
      pthread_mutex_unlock(&d->send_mutex);
      return false;
    }
    written += wret;
  }

  pthread_mutex_unlock(&d->send_mutex);
  return true;
}
