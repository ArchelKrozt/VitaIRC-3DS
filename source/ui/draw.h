#ifndef VITAIRC3DS_DRAW_H
#define VITAIRC3DS_DRAW_H

/* Drawing helpers on top of citro2d: the system font (with the console's
 * button glyphs), text measuring and wrapping, rounded boxes and colors. */

#include <citro2d.h>
#include <stdint.h>

/* Same byte order as C2D_Color32, but usable in constant initializers */
#define RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

#define TOP_W  400
#define BOT_W  320
#define SCR_H  240

/* Palette (dark theme, same family as VitaIRC on the Vita) */
#define C_BG        RGBA(0x12, 0x12, 0x1a, 0xff)
#define C_BG2       RGBA(0x1b, 0x1b, 0x27, 0xff)
#define C_PANEL     RGBA(0x22, 0x22, 0x31, 0xff)
#define C_PANEL2    RGBA(0x2c, 0x2c, 0x3e, 0xff)
#define C_LINE      RGBA(0x34, 0x34, 0x48, 0xff)
#define C_TEXT      RGBA(0xea, 0xea, 0xf2, 0xff)
#define C_DIM       RGBA(0x9a, 0x9a, 0xb0, 0xff)
#define C_FAINT     RGBA(0x66, 0x66, 0x7c, 0xff)
#define C_ACCENT    RGBA(0xf5, 0xd0, 0x4c, 0xff)   /* VitaIRC yellow */
#define C_ACCENT_D  RGBA(0x5a, 0x4c, 0x18, 0xff)
#define C_SEL       RGBA(0x3a, 0x3a, 0x56, 0xff)
#define C_OK        RGBA(0x5c, 0xd6, 0x7a, 0xff)
#define C_WARN      RGBA(0xff, 0xa8, 0x40, 0xff)
#define C_BAD       RGBA(0xff, 0x5a, 0x5a, 0xff)
#define C_MENTION   RGBA(0x4a, 0x24, 0x2c, 0xff)
#define C_KEY       RGBA(0x30, 0x30, 0x44, 0xff)
#define C_KEY2      RGBA(0x24, 0x24, 0x34, 0xff)
#define C_KEYDOWN   RGBA(0xf5, 0xd0, 0x4c, 0xff)
#define C_TRANS     RGBA(0x8c, 0xc8, 0xff, 0xff)
#define C_SHADOW    RGBA(0x00, 0x00, 0x00, 0x90)

/* Button glyphs of the system font */
#define G_A      "\xEE\x80\x80"
#define G_B      "\xEE\x80\x81"
#define G_X      "\xEE\x80\x82"
#define G_Y      "\xEE\x80\x83"
#define G_L      "\xEE\x80\x84"
#define G_R      "\xEE\x80\x85"
#define G_DPAD   "\xEE\x80\x86"
#define G_START  "START"
#define G_SELECT "SELECT"

void  draw_init(void);
void  draw_exit(void);
/* Before drawing a frame: empties the text buffer. */
void  draw_frame_start(void);

/* Text size steps (Settings → text size) */
extern float g_fs;          /* chat text scale */
extern float g_fs_ui;       /* menus and lists */
float line_h(float scale);

float text_w(const char *s, float scale);
float text_wn(const char *s, int bytes, float scale);
/* Draws UTF-8 text (top-left at x,y). Returns the width. */
float text(float x, float y, float scale, uint32_t color, const char *s);
float textn(float x, float y, float scale, uint32_t color, const char *s, int bytes);
/* Centered horizontally in [x, x+w) */
void  text_center(float x, float y, float w, float scale, uint32_t color, const char *s);
void  text_right(float xr, float y, float scale, uint32_t color, const char *s);
/* Cut with "…" to fit maxw */
void  text_fit(float x, float y, float maxw, float scale, uint32_t color, const char *s);
/* Byte offsets where wrapped lines start (first is 0). Returns the number of lines. */
int   text_wrap(const char *s, float first_w, float w, float scale, uint16_t *breaks, int max);

void  rect(float x, float y, float w, float h, uint32_t c);
void  rrect(float x, float y, float w, float h, float r, uint32_t c);
void  rrect_outline(float x, float y, float w, float h, float r, uint32_t c);
void  circle(float x, float y, float r, uint32_t c);
void  tri(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t c);

uint32_t nick_color(const char *nick);
uint32_t mirc_color(int idx);
uint32_t color_mix(uint32_t a, uint32_t b, float t);

#endif
