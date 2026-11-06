#ifndef VITAIRC3DS_KBD_H
#define VITAIRC3DS_KBD_H

/* On-screen keyboard for the bottom screen, and the text being edited. */

#define KBD_H    166          /* 4 rows */
#define EDIT_MAX 480

typedef struct {
	char buf[EDIT_MAX];
	int  len;
	int  cur;                 /* cursor (byte offset) */
	int  max;                 /* bytes allowed (0 = EDIT_MAX - 1) */
	int  password;            /* drawn as dots */
} Edit;

void edit_set(Edit *e, const char *s);
void edit_clear(Edit *e);
void edit_insert(Edit *e, const char *s);
void edit_backspace(Edit *e);
void edit_left(Edit *e);
void edit_right(Edit *e);
/* The word being typed before the cursor (for nick completion). */
int  edit_word_start(const Edit *e);
/* Replaces the word before the cursor */
void edit_replace_word(Edit *e, const char *with);
/* Draws the text field with its cursor at (x,y,w,h); placeholder when empty. */
void edit_draw(const Edit *e, float x, float y, float w, float h, const char *placeholder, int focused);

enum { KE_NONE = 0, KE_CHANGED, KE_ENTER, KE_CLOSE };

void kbd_reset(void);
/* Touch input for the keyboard at y0 (edits e). Returns KE_*. */
int  kbd_update(Edit *e, float y0);
void kbd_draw(float y0, const char *enter_label);
/* Key preview and accent popup: drawn after everything else */
void kbd_draw_overlay(float y0);
/* A key is being pressed (the UI keeps drawing every frame) */
int  kbd_busy(void);

#endif
