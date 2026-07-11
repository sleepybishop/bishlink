/* main.c (host) */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "audio.h"
#include "capture.h"
#include "data_uds.h"
#include "inject.h"
#include "input_event.h"
#include "transport.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  transport_t *transport;
  inject_t *inject;
  capture_t *capture;
  audio_t *audio;
  data_uds_t *data_pipe;
  uint32_t data_object_id;
} host_ctx_t;

static void on_transport_event(void *user_data,
                               const transport_event_t *event) {
  host_ctx_t *ctx = user_data;
  if (!ctx)
    return;

  switch (event->type) {
  case TRANSPORT_EVENT_CONNECTED:
    printf("client connected\n");
    break;
  case TRANSPORT_EVENT_DISCONNECTED:
    printf("client disconnected\n");
    break;
  case TRANSPORT_EVENT_SUBSCRIBE:
    printf("client subscribed to track type: %d, name: %s\n",
           event->track_id.type, event->track_id.name);
    break;
  case TRANSPORT_EVENT_UNSUBSCRIBE:
    printf("client unsubscribed from track type: %d, name: %s\n",
           event->track_id.type, event->track_id.name);
    break;
  case TRANSPORT_EVENT_KEYFRAME_REQUEST:
    printf("received keyframe request for track type: %d, name: %s\n",
           event->track_id.type, event->track_id.name);
    if (event->track_id.type == MOQ_TRACK_VIDEO && ctx->capture) {
      capture_request_keyframe(ctx->capture);
    }
    break;
  case TRANSPORT_EVENT_AUTH:
    printf("received authentication token request\n");
    {
      bool success = false;
      if (event->auth.token_len == strlen("secret_token_123") &&
          memcmp(event->auth.token, "secret_token_123",
                 event->auth.token_len) == 0) {
        success = true;
        printf("client authentication successful\n");
      } else {
        printf("client authentication failed\n");
      }
      transport_respond_auth(ctx->transport, event->conn, success);
    }
    break;
  case TRANSPORT_EVENT_OBJECT:
    if (event->track_id.type == MOQ_TRACK_INPUT && ctx->inject) {
      if (event->object.size == sizeof(input_event_t)) {
        const input_event_t *input_evt =
            (const input_event_t *)event->object.data;
        if (input_evt->type == INPUT_TYPE_KEYBOARD) {
          inject_keyboard(ctx->inject, input_evt->keyboard.key,
                          input_evt->keyboard.pressed);
        } else if (input_evt->type == INPUT_TYPE_MOUSE_MOVE) {
          inject_mouse_move(ctx->inject, input_evt->mouse_move.dx,
                            input_evt->mouse_move.dy,
                            input_evt->mouse_move.absolute);
        } else if (input_evt->type == INPUT_TYPE_MOUSE_BUTTON) {
          inject_mouse_button(ctx->inject, input_evt->mouse_button.button,
                              input_evt->mouse_button.pressed);
        } else if (input_evt->type == INPUT_TYPE_GAMEPAD) {
          inject_gamepad(ctx->inject, input_evt->gamepad.pad_idx,
                         input_evt->gamepad.buttons, input_evt->gamepad.lx,
                         input_evt->gamepad.ly, input_evt->gamepad.rx,
                         input_evt->gamepad.ry, input_evt->gamepad.lt,
                         input_evt->gamepad.rt);
        }
      }
    } else if (event->track_id.type == MOQ_TRACK_DATA && ctx->data_pipe) {
      data_uds_send(ctx->data_pipe, &event->track_id, event->object.data,
                    event->object.size, event->object.priority);
    }
    break;
  case TRANSPORT_EVENT_AUTH_COMPLETE:
    /* only relevant for clients */
    break;
  case TRANSPORT_EVENT_OBJECT_LOST:
    printf("host: received object lost for track type %d\n",
           event->track_id.type);
    break;
  }
}

static void on_data_packet(void *user_data, const moq_track_id_t *track_id,
                           const uint8_t *buf, size_t size, uint8_t priority) {
  host_ctx_t *ctx = user_data;
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

static void on_audio_capture(void *user_data, const int16_t *pcm_data,
                             size_t samples) {
  host_ctx_t *ctx = user_data;
  if (!ctx || !ctx->transport)
    return;

  moq_object_t obj = {.track_id = {.type = MOQ_TRACK_AUDIO,
                                   .flags = MOQ_TRACK_FLAG_FEC_ENABLED},
                      .group_id = 0,
                      .object_id = 0,
                      .data = (const uint8_t *)pcm_data,
                      .size = samples * sizeof(int16_t),
                      .is_keyframe = false};
  transport_publish(ctx->transport, &obj);
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  (void)argc;
  (void)argv;
  printf("starting bishlink host...\n");

  host_ctx_t ctx = {0};

  /* initialize transport config */
  transport_config_t config = {0};
  config.port = 8888;
  config.cert_file = "t/assets/server.crt"; /* using the test certificate */
  config.key_file = "t/assets/server.key";
  config.callback = on_transport_event;
  config.user_data = &ctx;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
      if (config.num_bind_hosts < TRANSPORT_MAX_PATHS) {
        config.bind_hosts[config.num_bind_hosts++] = argv[++i];
      }
    }
  }
  if (config.num_bind_hosts == 0) {
    /* default to any */
    config.bind_hosts[config.num_bind_hosts++] = "0.0.0.0";
  }

  ctx.transport = transport_create(&config);
  if (!ctx.transport) {
    fprintf(stderr, "failed to initialize transport\n");
    return 1;
  }

  capture_config_t cap_cfg = {.width = 1920, .height = 1080, .fps = 60};
  ctx.capture = capture_create(&cap_cfg);
  ctx.audio = audio_create(on_audio_capture, &ctx);
  ctx.inject = inject_create();
  ctx.data_pipe = data_uds_create("bishlink-data", on_data_packet, &ctx);

  printf("host is running. press ctrl+c to exit.\n");
  bool running = true;
  uint64_t object_id = 0;

  while (running) {
    /* drive transport events */
    transport_tick(ctx.transport);

    /* feedback latest network capacity to video capture helper */
    uint64_t est_bw = transport_get_estimated_bandwidth(ctx.transport);
    if (est_bw > 0) {
      capture_set_bitrate(ctx.capture, (uint32_t)(est_bw * 8));
    }

    /* capture frame */
    uint32_t w, h, format;
    size_t size;
    const uint8_t *frame = capture_frame(ctx.capture, &w, &h, &format, &size);
    /* publish video frame using media over quic concepts */
    if (frame && size > 0) {
      moq_object_t obj = {.track_id = {.type = MOQ_TRACK_VIDEO,
                                       .flags = MOQ_TRACK_FLAG_FEC_ENABLED},
                          .group_id = 0, /* simple gop index */
                          .object_id = object_id++,
                          .data = frame,
                          .size = size,
                          .is_keyframe = (object_id % 60 == 0)};
      transport_publish(ctx.transport, &obj);

      /* publish mock subtitle once every 300 frames (~5 seconds) */
      if (object_id % 300 == 0) {
        const char *sub_text = "Subtitle: [BishLink VPN Tunnel Active]";
        moq_object_t sub_obj = {.track_id = {.type = MOQ_TRACK_TEXT,
                                             .flags = MOQ_TRACK_FLAG_RELIABLE,
                                             .name = "sub_en"},
                                .group_id = 0,
                                .object_id = object_id / 300,
                                .data = (const uint8_t *)sub_text,
                                .size = strlen(sub_text),
                                .is_keyframe = true};
        transport_publish(ctx.transport, &sub_obj);
      }
    }
    /* sleep to simulate frame timing (60fps ~16.6ms) */
    usleep(16666);
  }

  inject_destroy(ctx.inject);
  audio_destroy(ctx.audio);
  capture_destroy(ctx.capture);
  data_uds_destroy(ctx.data_pipe);
  transport_destroy(ctx.transport);

  return 0;
}
