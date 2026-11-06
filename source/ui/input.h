#ifndef VITAIRC3DS_INPUT_H
#define VITAIRC3DS_INPUT_H

#include <3ds.h>
#include <stdint.h>

/* Buttons and touch screen, read once per frame. */
typedef struct {
	u32  down, held, up;
	int  touching;          /* finger on the screen now */
	int  tx, ty;            /* where */
	int  pressed;           /* the finger landed this frame */
	int  released;          /* the finger left this frame */
	int  tapped;            /* released without dragging (a tap) at tx, ty */
	int  dragging;          /* moved more than a few pixels since landing */
	int  dx, dy;            /* movement since the last frame */
	int  sx, sy;            /* where it landed */
	int  long_press;        /* held still for 450 ms (fires once) */
	u64  t_down;
	circlePosition cpad, cstick;
} Input;

extern Input in;

void input_update(void);
/* Down this frame, then again every 70 ms after 400 ms held. */
int  key_repeat(u32 key);
int  touch_in(int x, int y, int w, int h);
int  tap_in(int x, int y, int w, int h);
int  press_in(int x, int y, int w, int h);

#endif
