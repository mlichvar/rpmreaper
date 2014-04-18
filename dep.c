/*
 * Copyright (C) 2008, 2009, 2014  Miroslav Lichvar <mlichvar@redhat.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>

#include <rpm/rpmlib.h>

#include "dep.h"

void deps_init(struct deps *deps, struct strings *strings) {
	array_init(&deps->names, 0);
	array_init(&deps->epochs, 0);
	array_init(&deps->vers, 0);
	array_init(&deps->rels, 0);
	array_init(&deps->flags, 0);
	hashtable_init(&deps->hashtable);
	deps->strings = strings;
}

void deps_clean(struct deps *deps) {
	array_clean(&deps->names);
	array_clean(&deps->epochs);
	array_clean(&deps->vers);
	array_clean(&deps->rels);
	array_clean(&deps->flags);
	hashtable_clean(&deps->hashtable);
}

static char *parse_epoch(char *s, uint *epoch) {
	char *c = strchr(s, ':');

	if (c != NULL) {
		*c = '\0';
		*epoch = atoi(s);
		return c + 1;
	}
	return s;
}

static char *parse_ver(char *s) {
	char *c = strchr(s, '-');

	if (c != NULL) {
		*c = '\0';
		return c + 1;
	}
	return NULL;
}

static uint dephash(uint name) {
	return 13 * name << 8 ^ name;
}

uint deps_add(struct deps *deps, const char *name, int flags, const char *vers) {
	uint epoch = 0;
	char *v = NULL, *r = NULL, buf[1000];
	
	if (vers && *vers) {
		buf[sizeof (buf) - 1] = '\0';
		strncpy(buf, vers, sizeof (buf) - 1);
		v = parse_epoch(buf, &epoch);
		r = parse_ver(v);
	}
	return deps_add_evr(deps, name, flags, epoch, v, r);
}
	
uint deps_add_evr(struct deps *deps, const char *name, int flags, uint epoch,
		const char *version, const char *release) {
	uint i, iter = 0, hash, size = array_get_size(&deps->names), ver, rel, nam;

	nam = strings_add(deps->strings, name);
	ver = strings_add(deps->strings, version != NULL ? version : "");
	rel = strings_add(deps->strings, release != NULL ? release : "");

	if (hashtable_resize(&deps->hashtable)) {
		for (i = 0; i < size; i++) {
			hash = dephash(array_get(&deps->names, i));
			hashtable_add(&deps->hashtable, i, hash);
		}
	}

	hash = dephash(nam);

	while ((i = hashtable_find(&deps->hashtable, hash, &iter)) != -1)
		if (array_get(&deps->names, i) == nam &&
				array_get(&deps->epochs, i) == epoch &&
				array_get(&deps->vers, i) == ver &&
				array_get(&deps->rels, i) == rel &&
				array_get(&deps->flags, i) == flags)
			break;
	if (i != -1)
		/* already stored */
		return i;

	i = size;
	array_set(&deps->names, i, nam);
	array_set(&deps->epochs, i, epoch);
	array_set(&deps->vers, i, ver);
	array_set(&deps->rels, i, rel);
	array_set(&deps->flags, i, flags);
	hashtable_add_dir(&deps->hashtable, i, hash, iter);

	return i;
}

uint deps_find(const struct deps *deps, uint dep, uint *iter) {
	uint i, hash = dephash(array_get(&deps->names, dep));

	while ((i = hashtable_find(&deps->hashtable, hash, iter)) != -1)
		if (dep == i || deps_match(deps, dep, i))
			break;
	return i;
}

int deps_match(const struct deps *deps, uint x, uint y) {
	uint f1, f2;
	const char *s1, *s2;
	int d;

	if (array_get(&deps->names, x) != array_get(&deps->names, y))
	       return 0;

	f1 = array_get(&deps->flags, x);
        f2 = array_get(&deps->flags, y);

	if (!f1 || !f2)
		return 1;

	d = array_get(&deps->epochs, x) - array_get(&deps->epochs, y);
	if (!d) {
		s1 = strings_get(deps->strings, array_get(&deps->vers, x)); 
		s2 = strings_get(deps->strings, array_get(&deps->vers, y)); 
		if (*s1 && *s2)
			d = rpmvercmp(s1, s2);
	}
	if (!d) {
		s1 = strings_get(deps->strings, array_get(&deps->rels, x)); 
		s2 = strings_get(deps->strings, array_get(&deps->rels, y)); 
		if (*s1 && *s2)
			d = rpmvercmp(s1, s2);
	}
	if ((!d && f1 & f2) ||
			(d > 0 && (f1 & RPMSENSE_LESS || f2 & RPMSENSE_GREATER)) ||
			(d < 0 && (f1 & RPMSENSE_GREATER || f2 & RPMSENSE_LESS)))
		return 1;
	return 0;
}

int deps_print(const struct deps *deps, uint dep, char *str, size_t size) {
	const char *n, *v, *r;
	uint f;
	int s, l;

	n = strings_get(deps->strings, array_get(&deps->names, dep));
	v = strings_get(deps->strings, array_get(&deps->vers, dep));
	r = strings_get(deps->strings, array_get(&deps->rels, dep));
	f = array_get(&deps->flags, dep);

	s = snprintf(str, size, "%s", n);
	if (s < 0 || s >= size)
		return s;
	l = s;

	if (!f || !v || !v[0])
		return l;

	s = snprintf(str + l, size - l, " %s%s%s %s", f & RPMSENSE_LESS ? "<" : "",
		f & RPMSENSE_GREATER ? ">" : "", f & RPMSENSE_EQUAL ? "=" : "", v);
	if (s < 0 || s >= size)
		return l + s;
	l += s;

	if (!r || !r[0])
		return l;

	return l + snprintf(str + l, size - l, ".%s", r);
}
