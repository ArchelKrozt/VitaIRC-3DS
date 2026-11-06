#include <stdlib.h>
#include "input.h"
#include "util.h"

Input in;

static u64 rep_at[32];
static int long_fired;

void input_update(void)
{
	hidScanInput();
	in.down = hidKeysDown();
	in.held = hidKeysHeld();
	in.up = hidKeysUp();
	hidCircleRead(&in.cpad);
	hidCstickRead(&in.cstick);

	in.pressed = in.released = in.tapped = in.long_press = 0;
	in.dx = in.dy = 0;
	if (in.held & KEY_TOUCH) {
		touchPosition t;
		hidTouchRead(&t);
		if (!in.touching) {
			in.touching = 1;
			in.pressed = 1;
			in.sx = in.tx = t.px;
			in.sy = in.ty = t.py;
			in.dragging = 0;
			in.t_down = now_ms();
			long_fired = 0;
		} else {
			in.dx = t.px - in.tx;
			in.dy = t.py - in.ty;
			in.tx = t.px;
			in.ty = t.py;
			if (abs(in.tx - in.sx) > 6 || abs(in.ty - in.sy) > 6)
				in.dragging = 1;
			if (!in.dragging && !long_fired && now_ms() - in.t_down > 450) {
				in.long_press = 1;
				long_fired = 1;
			}
		}
	} else if (in.touching) {
		in.touching = 0;
		in.released = 1;
		in.tapped = !in.dragging && !long_fired;
	}
}

int key_repeat(u32 key)
{
	int bit = __builtin_ctz(key);
	u64 now = now_ms();
	if (in.down & key) {
		rep_at[bit] = now + 400;
		return 1;
	}
	if ((in.held & key) && now >= rep_at[bit]) {
		rep_at[bit] = now + 70;
		return 1;
	}
	return 0;
}

int touch_in(int x, int y, int w, int h)
{
	return in.touching && in.tx >= x && in.tx < x + w && in.ty >= y && in.ty < y + h;
}

int tap_in(int x, int y, int w, int h)
{
	return in.tapped && in.tx >= x && in.tx < x + w && in.ty >= y && in.ty < y + h;
}

int press_in(int x, int y, int w, int h)
{
	return in.pressed && in.sx >= x && in.sx < x + w && in.sy >= y && in.sy < y + h;
}
