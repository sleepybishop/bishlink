/* inject.h */

#ifndef INJECT_H
#define INJECT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct inject_t inject_t;

/* create and destroy an input injector instance */
inject_t *inject_create(void);
void inject_destroy(inject_t *j);

/* inject keyboard events */
void inject_keyboard(inject_t *j, uint16_t key, bool down);

/* inject mouse movement. dx and dy are relative or absolute coordinate offsets */
void inject_mouse_move(inject_t *j, int32_t dx, int32_t dy, bool absolute);

/* inject mouse button events */
void inject_mouse_button(inject_t *j, uint8_t button, bool down);

/* inject gamepad axis and button state updates */
void inject_gamepad(inject_t *j, uint8_t pad_idx, uint16_t buttons, int16_t lx, int16_t ly, int16_t rx, int16_t ry, uint8_t lt, uint8_t rt);

#endif /* INJECT_H */
