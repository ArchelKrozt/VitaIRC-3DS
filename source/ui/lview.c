#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "lview.h"
#include "draw.h"
#include "input.h"
#include "util.h"

void lv_clear(LView *v)
{
	v->n = 0;
	if (!v->row_h)
		v->row_h = 30;
}

void lv_free(LView *v)
{
	free(v->it);
	memset(v, 0, sizeof(*v));
}

LItem *lv_add(LView *v, const char *label)
{
	if (v->n == v->cap) {
		int cap = v->cap ? v->cap * 2 : 32;
		LItem *n = realloc(v->it, cap * sizeof(LItem));
		if (!n)
			return &v->it[v->n - 1];
		v->it = n;
		v->cap = cap;
	}
	LItem *it = &v->it[v->n++];
	memset(it, 0, sizeof(*it));
	str_copy(it->label, label, sizeof(it->label));
	it->toggle = -1;
	return it;
}

LItem *lv_header(LView *v, const char *label)
{
	LItem *it = lv_add(v, label);
	it->header = 1;
	return it;
}

static float max_off(const LView *v, float h)
{
	float m = v->n * v->row_h - h;
	return m > 0 ? m : 0;
}

int lv_moving(const LView *v)
{
	return fabsf(v->vel) > 0.3f;
}

void lv_select(LView *v, int idx, float h)
{
	if (idx < 0 || idx >= v->n)
		return;
	v->sel = idx;
	float top = idx * v->row_h;
	if (top < v->off) v->off = top;
	if (top + v->row_h > v->off + h) v->off = top + v->row_h - h;
}

static int next_sel(LView *v, int from, int dir)
{
	for (int i = from + dir; i >= 0 && i < v->n; i += dir)
		if (!v->it[i].header && !v->it[i].dim)
			return i;
	return from;
}

int lv_input(LView *v, float x, float y, float w, float h, int *idx)
{
	int res = LV_NONE;
	/* D-pad */
	if (key_repeat(KEY_DOWN) || key_repeat(KEY_CPAD_DOWN)) {
		v->show_sel = 1;
		lv_select(v, next_sel(v, v->sel < 0 ? -1 : v->sel, 1), h);
	}
	if (key_repeat(KEY_UP) || key_repeat(KEY_CPAD_UP)) {
		v->show_sel = 1;
		lv_select(v, next_sel(v, v->sel < 0 ? v->n : v->sel, -1), h);
	}
	if ((in.down & KEY_A) && v->sel >= 0 && v->sel < v->n && !v->it[v->sel].header) {
		*idx = v->sel;
		return LV_ACTIVATE;
	}
	/* touch */
	int inside = in.sx >= x && in.sx < x + w && in.sy >= y && in.sy < y + h;
	if (in.touching && inside) {
		if (in.pressed)
			v->vel = 0;
		if (in.dragging) {
			v->off -= in.dy;
			v->vel = -in.dy;
		}
	} else if (!in.touching) {
		v->off += v->vel;
		v->vel *= 0.92f;
		if (fabsf(v->vel) < 0.3f) v->vel = 0;
	}
	float m = max_off(v, h);
	if (v->off < 0) { v->off = 0; v->vel = 0; }
	if (v->off > m) { v->off = m; v->vel = 0; }

	if ((in.tapped || in.long_press) && inside) {
		int i = (int)((in.ty - y + v->off) / v->row_h);
		if (i >= 0 && i < v->n && !v->it[i].header && !v->it[i].dim) {
			*idx = i;
			v->sel = i;
			v->show_sel = 0;
			if (in.long_press)
				res = LV_LONG;
			else if (v->it[i].close && in.tx > x + w - 34)
				res = LV_CLOSE;
			else
				res = LV_ACTIVATE;
		}
	}
	return res;
}

static void draw_switch(float x, float y, int on)
{
	rrect(x, y, 30, 16, 8, on ? C_ACCENT : C_LINE);
	circle(on ? x + 22 : x + 8, y + 8, 6, on ? C_BG : C_DIM);
}

void lv_draw(LView *v, float x, float y, float w, float h)
{
	int first = (int)(v->off / v->row_h);
	for (int i = first; i < v->n; i++) {
		float ry = y + i * v->row_h - v->off;
		if (ry >= y + h)
			break;
		LItem *it = &v->it[i];
		float s = g_fs_ui;
		float ty = ry + (v->row_h - line_h(s)) / 2;
		if (it->header) {
			rect(x, ry, w, v->row_h, C_BG);
			text_fit(x + 8, ry + v->row_h - line_h(0.44f) - 4, w - 16, 0.44f, C_ACCENT, it->label);
			if (it->value[0])
				text_right(x + w - 8, ry + v->row_h - line_h(0.42f) - 4, 0.42f, C_DIM, it->value);
			continue;
		}
		int sel = v->show_sel && i == v->sel;
		int pressed = in.touching && !in.dragging && in.ty >= ry && in.ty < ry + v->row_h &&
		              in.sy >= y && in.sy < y + h && in.sx >= x && in.sx < x + w;
		rect(x, ry, w, v->row_h, sel ? C_SEL : pressed ? C_PANEL2 : C_BG2);
		rect(x + 8, ry + v->row_h - 1, w - 16, 1, C_LINE);
		if (sel)
			rect(x, ry, 3, v->row_h, C_ACCENT);
		float lx = x + 10;
		if (it->icon[0]) {
			float is = v->row_h - 10;
			rrect(lx, ry + 5, is, is, 5, it->icon_color ? it->icon_color : C_PANEL2);
			text_center(lx, ry + 5 + (is - line_h(0.5f)) / 2, is, 0.5f, C_BG, it->icon);
			lx += is + 8;
		}
		float right = x + w - 8;
		if (it->close) {
			float cx = right - 12, cy = ry + v->row_h / 2;
			circle(cx, cy, 9, C_PANEL2);
			text_center(cx - 9, cy - line_h(0.42f) / 2, 18, 0.42f, C_DIM, "x");
			right -= 28;
		}
		if (it->toggle >= 0) {
			draw_switch(right - 30, ry + (v->row_h - 16) / 2, it->toggle);
			right -= 38;
		}
		if (it->badge) {
			char b[12];
			snprintf(b, sizeof(b), it->badge > 99 ? "99+" : "%d", it->badge);
			float bw = text_w(b, 0.42f) + 10;
			if (bw < 18) bw = 18;
			rrect(right - bw, ry + (v->row_h - 16) / 2, bw, 16, 8, it->hot ? C_BAD : C_PANEL2);
			text_center(right - bw, ry + (v->row_h - line_h(0.42f)) / 2, bw, 0.42f, it->hot ? C_TEXT : C_DIM, b);
			right -= bw + 6;
		}
		if (it->value[0]) {
			float vw = text_w(it->value, 0.46f);
			float maxv = (right - lx) * 0.55f;
			if (vw > maxv) vw = maxv;
			text_fit(right - vw, ry + (v->row_h - line_h(0.46f)) / 2, vw, 0.46f, C_DIM, it->value);
			right -= vw + 8;
		}
		uint32_t col = it->dim ? C_FAINT : it->color ? it->color : C_TEXT;
		text_fit(lx, ty, right - lx, s, col, it->label);
	}
	if (v->n * v->row_h > h) {
		float bh = h * h / (v->n * v->row_h);
		if (bh < 14) bh = 14;
		float pos = v->off / max_off(v, h);
		rrect(x + w - 4, y + pos * (h - bh), 3, bh, 1.5f, C_LINE);
	}
}
