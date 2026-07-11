/* input_event.h */

#ifndef INPUT_EVENT_H
#define INPUT_EVENT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  INPUT_TYPE_KEYBOARD,
  INPUT_TYPE_MOUSE_MOVE,
  INPUT_TYPE_MOUSE_BUTTON,
  INPUT_TYPE_GAMEPAD
} input_type_t;

typedef struct __attribute__((packed)) {
  uint8_t type;
  union {
    struct {
      uint16_t key;
      bool pressed;
    } keyboard;
    struct {
      int32_t dx;
      int32_t dy;
      bool absolute;
    } mouse_move;
    struct {
      uint8_t button;
      bool pressed;
    } mouse_button;
    struct {
      uint8_t pad_idx;
      uint16_t buttons;
      int16_t lx;
      int16_t ly;
      int16_t rx;
      int16_t ry;
      uint8_t lt;
      uint8_t rt;
    } gamepad;
  };
} input_event_t;

#endif /* INPUT_EVENT_H */
