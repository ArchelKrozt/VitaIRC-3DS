#include <stdio.h>
#include "booru.h"

const char *booru_engine_name(int e)
{
	static const char *names[] = { "Safebooru", "Danbooru", "Sankaku" };
	return e >= 0 && e < BE_COUNT ? names[e] : "?";
}

const BPost *booru_fav_find(int engine, const char *id)
{
	(void)engine; (void)id;
	return NULL;
}

int booru_lookup(int engine, const char *id, BPost *out, char *err, int errlen)
{
	(void)engine; (void)id; (void)out;
	snprintf(err, errlen, "-");
	return -1;
}
