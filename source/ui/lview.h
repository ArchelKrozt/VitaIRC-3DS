#ifndef VITAIRC3DS_LVIEW_H
#define VITAIRC3DS_LVIEW_H

/* Touch + D-pad list used by every list on the bottom screen. */

#include <stdint.h>

typedef struct {
	char     label[96];
	char     value[96];     /* right side (setting value), dim */
	char     icon[8];       /* short text in a colored square ("#", "@"...), "" = none */
	uint32_t icon_color;
	uint32_t color;         /* label color, 0 = default */
	int      badge;         /* count bubble, 0 = none */
	int      hot;           /* badge in red (mentions) */
	int      header;        /* section title, not selectable */
	int      toggle;        /* -1 none, 0 off, 1 on (switch drawn) */
	int      close;         /* ✕ button on the right */
	int      dim;           /* greyed out */
	int      id, arg;       /* what it does */
	void    *ptr;
} LItem;

typedef struct {
	LItem *it;
	int    n, cap;
	int    sel;             /* highlighted with the D-pad (-1 none) */
	float  off;             /* scroll offset in pixels */
	float  vel;
	int    row_h;
	int    show_sel;        /* draw the highlight (after D-pad use) */
} LView;

enum { LV_NONE = 0, LV_ACTIVATE, LV_CLOSE, LV_LONG };

void   lv_clear(LView *v);
LItem *lv_add(LView *v, const char *label);
LItem *lv_header(LView *v, const char *label);
void   lv_free(LView *v);
/* Handles touch inside (x,y,w,h) and the D-pad. Returns LV_* and the item in *idx. */
int    lv_input(LView *v, float x, float y, float w, float h, int *idx);
void   lv_draw(LView *v, float x, float y, float w, float h);
void   lv_select(LView *v, int idx, float h);
int    lv_moving(const LView *v);

#endif
