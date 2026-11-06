#ifndef VITAIRC3DS_BOORU_H
#define VITAIRC3DS_BOORU_H

/* The 3DS has no image search yet: just what profiles (social.c) need. */

#include <stdint.h>
#include <stddef.h>

enum { BE_SAFEBOORU = 0, BE_DANBOORU, BE_SANKAKU, BE_COUNT };

typedef struct {
	int   engine;
	char  id[24];
	char  rating;
	int   w, h;
	char  ext[8];
	long  file_size;
	char  thumb_url[512];
	char  view_url[512];
	char  file_url[512];
	char  tags[160];
	char  artist[96];
} BPost;

const char  *booru_engine_name(int e);
/* A favorite by engine and id (NULL: the 3DS has no favorites yet) */
const BPost *booru_fav_find(int engine, const char *id);
/* One post by its number. Blocking: worker threads. */
int booru_lookup(int engine, const char *id, BPost *out, char *err, int errlen);

#endif
