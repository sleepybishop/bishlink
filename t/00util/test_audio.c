/* test_audio.c */

#define _USE_MATH_DEFINES

#include "transport.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
  bool connected;
  bool subscribed;
  uint8_t *buf;
  size_t capacity;
  size_t received;
  transport_t *transport;
} test_state_t;

static void on_server_event(void *user_data, const transport_event_t *event) {
  test_state_t *state = user_data;
  switch (event->type) {
  case TRANSPORT_EVENT_CONNECTED:
    state->connected = true;
    break;
  case TRANSPORT_EVENT_SUBSCRIBE:
    if (event->track_id.type == MOQ_TRACK_AUDIO) {
      state->subscribed = true;
    }
    break;
  case TRANSPORT_EVENT_AUTH:
    transport_respond_auth(state->transport, event->conn, true);
    break;
  default:
    break;
  }
}

static void on_client_event(void *user_data, const transport_event_t *event) {
  test_state_t *state = user_data;
  switch (event->type) {
  case TRANSPORT_EVENT_CONNECTED:
    transport_send_auth(state->transport, event->conn,
                        (const uint8_t *)"secret_token_123",
                        strlen("secret_token_123"));
    break;
  case TRANSPORT_EVENT_AUTH_COMPLETE:
    if (event->auth.success) {
      state->connected = true;
    }
    break;
  case TRANSPORT_EVENT_OBJECT:
    if (event->track_id.type == MOQ_TRACK_AUDIO) {
      const uint8_t *src = event->object.data;
      size_t src_size = event->object.size;

      if (state->capacity == 0) {
        /* read the 4-byte total size header first */
        if (src_size >= 4) {
          uint32_t total_size = ((uint32_t)src[0]) | ((uint32_t)src[1] << 8) |
                                ((uint32_t)src[2] << 16) |
                                ((uint32_t)src[3] << 24);
          state->capacity = total_size;
          state->buf = malloc(total_size);
          state->received = 0;
          src += 4;
          src_size -= 4;
        }
      }

      if (src_size > 0 && state->buf) {
        if (state->received + src_size <= state->capacity) {
          memcpy(state->buf + state->received, src, src_size);
          state->received += src_size;
        }
      }
    }
    break;
  default:
    break;
  }
}

static uint32_t adler32(const uint8_t *data, size_t len) {
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < len; i++) {
    a = (a + data[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

static uint8_t *generate_mock_wav(size_t *out_size) {
  uint32_t sample_rate = 44100;
  uint16_t num_channels = 2;
  uint16_t bits_per_sample = 16;
  uint32_t num_samples = 4410; /* 100ms of audio */
  uint32_t data_size = num_samples * num_channels * (bits_per_sample / 8);
  size_t wav_size = 44 + data_size;

  uint8_t *wav = malloc(wav_size);
  if (!wav)
    return NULL;

  /* 1. RIFF header */
  memcpy(wav, "RIFF", 4);
  uint32_t chunk_size = wav_size - 8;
  memcpy(wav + 4, &chunk_size, 4);
  memcpy(wav + 8, "WAVE", 4);

  /* 2. fmt subchunk */
  memcpy(wav + 12, "fmt ", 4);
  uint32_t subchunk1_size = 16;
  memcpy(wav + 16, &subchunk1_size, 4);
  uint16_t audio_format = 1; /* PCM */
  memcpy(wav + 20, &audio_format, 2);
  memcpy(wav + 22, &num_channels, 2);
  memcpy(wav + 24, &sample_rate, 4);
  uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
  memcpy(wav + 28, &byte_rate, 4);
  uint16_t block_align = num_channels * (bits_per_sample / 8);
  memcpy(wav + 32, &block_align, 2);
  memcpy(wav + 34, &bits_per_sample, 2);

  /* 3. data subchunk */
  memcpy(wav + 36, "data", 4);
  memcpy(wav + 40, &data_size, 4);

  /* 4. generate mock sine wave PCM data (440Hz tone) */
  int16_t *pcm = (int16_t *)(wav + 44);
  for (uint32_t i = 0; i < num_samples; i++) {
    int16_t sample =
        (int16_t)(32767.0 * sin(2.0 * M_PI * 440.0 * i / sample_rate));
    pcm[i * 2] = sample;     /* left channel */
    pcm[i * 2 + 1] = sample; /* right channel */
  }

  *out_size = wav_size;
  return wav;
}

int main(void) {
  test_state_t server_state = {0};
  test_state_t client_state = {0};

  /* create server config */
  transport_config_t server_cfg = {0};
  server_cfg.bind_hosts[0] = "127.0.0.1";
  server_cfg.num_bind_hosts = 1;
  server_cfg.port = 9998;
  server_cfg.cert_file = "t/assets/server.crt";
  server_cfg.key_file = "t/assets/server.key";
  server_cfg.callback = on_server_event;
  server_cfg.user_data = &server_state;

  /* create client config */
  transport_config_t client_cfg = {0};
  client_cfg.bind_hosts[0] = "127.0.0.1";
  client_cfg.num_bind_hosts = 1;
  client_cfg.remote_hosts[0] = "127.0.0.1";
  client_cfg.num_remote_hosts = 1;
  client_cfg.port = 9998;
  client_cfg.cert_file = NULL;
  client_cfg.key_file = NULL;
  client_cfg.callback = on_client_event;
  client_cfg.user_data = &client_state;

  printf("initializing loopback transports for audio test...\n");
  transport_t *server = transport_create(&server_cfg);
  if (!server) {
    fprintf(stderr, "failed to create server\n");
    return 1;
  }
  server_state.transport = server;

  transport_t *client = transport_create(&client_cfg);
  if (!client) {
    fprintf(stderr, "failed to create client\n");
    transport_destroy(server);
    return 1;
  }
  client_state.transport = client;

  /* wait for connection */
  printf("connecting loopback...\n");
  int retries = 100;
  while (retries-- > 0 &&
         (!server_state.connected || !client_state.connected)) {
    transport_tick(server);
    transport_tick(client);
    usleep(10 * 1000);
  }

  if (!server_state.connected || !client_state.connected) {
    fprintf(stderr, "handshake timeout\n");
    transport_destroy(client);
    transport_destroy(server);
    return 1;
  }

  moq_track_id_t t_audio = {.type = MOQ_TRACK_AUDIO,
                            .flags = MOQ_TRACK_FLAG_FEC_ENABLED};
  transport_subscribe(client, t_audio);

  /* wait for subscription event on server */
  retries = 100;
  while (retries-- > 0 && !server_state.subscribed) {
    transport_tick(server);
    transport_tick(client);
    usleep(10 * 1000);
  }

  if (!server_state.subscribed) {
    fprintf(stderr, "subscription timeout\n");
    transport_destroy(client);
    transport_destroy(server);
    return 1;
  }

  /* generate mock WAV buffer */
  size_t wav_size = 0;
  uint8_t *wav_buf = generate_mock_wav(&wav_size);
  if (!wav_buf || wav_size == 0) {
    fprintf(stderr, "failed to generate WAV buffer\n");
    transport_destroy(client);
    transport_destroy(server);
    return 1;
  }

  uint32_t original_chksum = adler32(wav_buf, wav_size);
  printf("generated WAV file (%zu bytes, Adler32: 0x%08X)\n", wav_size,
         original_chksum);

  /* send WAV file.
     first block includes the 4-byte size header prepended. */
  size_t offset = 0;
  size_t block_id = 0;
  bool is_first = true;

  while (offset < wav_size) {
    size_t chunk = wav_size - offset;
    if (chunk > 400) {
      chunk = 400; /* limit packet sizes */
    }

    uint8_t pkt[410];
    size_t pkt_size = 0;

    if (is_first) {
      /* prepend 4-byte total size */
      uint32_t net_len = wav_size;
      pkt[0] = (uint8_t)(net_len & 0xFF);
      pkt[1] = (uint8_t)((net_len >> 8) & 0xFF);
      pkt[2] = (uint8_t)((net_len >> 16) & 0xFF);
      pkt[3] = (uint8_t)((net_len >> 24) & 0xFF);
      memcpy(pkt + 4, wav_buf + offset, chunk);
      pkt_size = chunk + 4;
      is_first = false;
    } else {
      memcpy(pkt, wav_buf + offset, chunk);
      pkt_size = chunk;
    }

    moq_object_t obj = {.track_id = {.type = MOQ_TRACK_AUDIO,
                                     .flags = MOQ_TRACK_FLAG_FEC_ENABLED},
                        .group_id = 0,
                        .object_id = block_id++,
                        .data = pkt,
                        .size = pkt_size,
                        .is_keyframe = false};

    transport_publish(server, &obj);
    offset += chunk;

    /* tick both sides to process packet transfer */
    transport_tick(server);
    transport_tick(client);
    usleep(1000);
  }

  /* wait for client to receive all bytes */
  printf("streaming WAV audio chunks...\n");
  retries = 200;
  while (retries-- > 0 && client_state.received < wav_size) {
    transport_tick(server);
    transport_tick(client);
    usleep(10 * 1000);
  }

  printf("received %zu/%zu bytes on client\n", client_state.received, wav_size);

  if (client_state.received != wav_size) {
    fprintf(stderr, "incomplete transmission\n");
    free(wav_buf);
    if (client_state.buf)
      free(client_state.buf);
    transport_destroy(client);
    transport_destroy(server);
    return 1;
  }

  uint32_t received_chksum = adler32(client_state.buf, client_state.received);
  printf("received WAV Adler32: 0x%08X\n", received_chksum);

  if (received_chksum != original_chksum) {
    fprintf(stderr, "checksum mismatch!\n");
    free(wav_buf);
    free(client_state.buf);
    transport_destroy(client);
    transport_destroy(server);
    return 1;
  }

  free(wav_buf);
  free(client_state.buf);
  transport_destroy(client);
  transport_destroy(server);

  printf("===AUDIO OK===\n");
  return 0;
}
