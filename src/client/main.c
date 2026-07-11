/* main.c (client) */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "data_uds.h"
#include "input_event.h"
#include "transport.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  transport_t *transport;
  transport_conn_t *conn;
  data_uds_t *data_pipe;
  bool running;
  uint32_t data_object_id;
  bool use_reliable;
} client_ctx_t;

static void on_transport_event(void *user_data,
                               const transport_event_t *event) {
  client_ctx_t *ctx = user_data;
  switch (event->type) {
  case TRANSPORT_EVENT_CONNECTED:
    printf("connected to host, sending authentication token...\n");
    ctx->conn = event->conn;
    transport_send_auth(ctx->transport, ctx->conn,
                        (const uint8_t *)"secret_token_123",
                        strlen("secret_token_123"));
    break;
  case TRANSPORT_EVENT_AUTH_COMPLETE:
    if (event->auth.success) {
      moq_track_id_t t_video = {.type = MOQ_TRACK_VIDEO,
                                .flags = MOQ_TRACK_FLAG_FEC_ENABLED};
      moq_track_id_t t_data = {.type = MOQ_TRACK_DATA,
                               .flags = ctx->use_reliable
                                            ? MOQ_TRACK_FLAG_RELIABLE
                                            : MOQ_TRACK_FLAG_FEC_ENABLED};
      moq_track_id_t t_gamepad = {
          .type = MOQ_TRACK_INPUT, .flags = 0, .name = "gamepad"};
      moq_track_id_t t_kbmouse = {.type = MOQ_TRACK_INPUT,
                                  .flags = MOQ_TRACK_FLAG_RELIABLE,
                                  .name = "kbmouse"};
      moq_track_id_t t_sub = {.type = MOQ_TRACK_TEXT,
                              .flags = MOQ_TRACK_FLAG_RELIABLE,
                              .name = "sub_en"};

      transport_subscribe(ctx->transport, t_video);
      transport_subscribe(ctx->transport, t_data);
      transport_subscribe(ctx->transport, t_gamepad);
      transport_subscribe(ctx->transport, t_kbmouse);
      transport_subscribe(ctx->transport, t_sub);
    } else {
      fprintf(stderr, "authentication failed, terminating connection\n");
      ctx->running = false;
    }
    break;
  case TRANSPORT_EVENT_DISCONNECTED:
    printf("disconnected from host\n");
    ctx->running = false;
    break;
  case TRANSPORT_EVENT_SUBSCRIBE:
  case TRANSPORT_EVENT_UNSUBSCRIBE:
    break;
  case TRANSPORT_EVENT_OBJECT:
    /* forward all objects directly to UDS pipe for external decoding/rendering
     */
    if (ctx->data_pipe) {
      data_uds_send(ctx->data_pipe, &event->track_id, event->object.data,
                    event->object.size, event->object.priority);
    }
    if (event->track_id.type == MOQ_TRACK_TEXT) {
      printf("[Subtitle %s]: %.*s\n", event->track_id.name,
             (int)event->object.size, (char *)event->object.data);
    }
    break;
  }
}

static void on_data_packet(void *user_data, const moq_track_id_t *track_id,
                           const uint8_t *buf, size_t size, uint8_t priority) {
  client_ctx_t *ctx = user_data;
  if (!ctx || !ctx->transport)
    return;

  moq_object_t obj = {.track_id = *track_id,
                      .group_id = 0,
                      .object_id = ctx->data_object_id++,
                      .data = buf,
                      .size = size,
                      .is_keyframe = false,
                      .priority = priority};
  transport_publish(ctx->transport, &obj);
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  printf("starting bishlink client...\n");

  client_ctx_t ctx = {.running = true};

  transport_config_t config = {0};
  config.port = 8888;
  config.callback = on_transport_event;
  config.user_data = &ctx;

  bool reliable_stream = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--reliable") == 0) {
      reliable_stream = true;
    } else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
      if (config.num_bind_hosts < TRANSPORT_MAX_PATHS) {
        config.bind_hosts[config.num_bind_hosts++] = argv[++i];
      }
    } else if (strcmp(argv[i], "--remote") == 0 && i + 1 < argc) {
      if (config.num_remote_hosts < TRANSPORT_MAX_PATHS) {
        config.remote_hosts[config.num_remote_hosts++] = argv[++i];
      }
    } else {
      if (config.num_remote_hosts == 0) {
        config.remote_hosts[config.num_remote_hosts++] = argv[i];
      }
    }
  }

  if (config.num_remote_hosts == 0) {
    config.remote_hosts[config.num_remote_hosts++] = "127.0.0.1";
  }
  if (config.num_bind_hosts == 0) {
    /* default bind address */
    config.bind_hosts[config.num_bind_hosts++] = "0.0.0.0";
  }
  ctx.use_reliable = reliable_stream;

  /* create transport client */
  ctx.transport = transport_create(&config);
  if (!ctx.transport) {
    fprintf(stderr, "failed to create transport client\n");
    return 1;
  }

  ctx.data_pipe = data_uds_create("bishlink-data-client", on_data_packet, &ctx);
  if (!ctx.data_pipe) {
    fprintf(stderr, "failed to create UDS data pipe\n");
    transport_destroy(ctx.transport);
    return 1;
  }

  printf("client is running. press ctrl+c to exit.\n");
  while (ctx.running) {
    transport_tick(ctx.transport);
    usleep(16666);
  }

  data_uds_destroy(ctx.data_pipe);
  transport_destroy(ctx.transport);

  return 0;
}
