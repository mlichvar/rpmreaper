/*
 * Copyright (C) 2008  Miroslav Lichvar <mlichvar@redhat.com>
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

#ifndef _MISC_H_
#define _MISC_H_

#include <assert.h>
#include <sys/types.h>

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))

#define ASGETPTR(s, a, i) ((const struct s *)array_get_ptr(a, i))
#define ASGETWPTR(s, a, i) ((struct s *)array_get_wptr(a, i))

/* array of integers or fixed sized objects */
struct array {
	uint *array;
	uint size;
	uint alloced;
	unsigned char width;
	unsigned char fixed;
};

void array_init(struct array *a, unsigned char width);
void array_clean(struct array *a);
void array_zero(struct array *a, uint start, uint size);
void array_set(struct array *a, uint index, uint value);
uint array_get(const struct array *a, uint index);
void array_inc(struct array *a, uint index, int inc);
void *array_get_wptr(struct array *a, uint index);
const void *array_get_ptr(const struct array *a, uint index);
void array_set_size(struct array *a, uint size);
uint array_get_size(const struct array *a);
void array_clone(struct array *dest, const struct array *source);
void array_move(struct array *a, uint dest, uint source, uint size);
uint array_bsearch(const struct array *a, uint start, uint size, uint value);

struct hashtable {
	struct array table;
	uint load;
};

void hashtable_init(struct hashtable *h);
void hashtable_clean(struct hashtable *h);
void hashtable_add_dir(struct hashtable *h, uint value, uint hash, uint iter);
void hashtable_add(struct hashtable *h, uint value, uint hash);
int hashtable_resize(struct hashtable *h);
uint hashtable_find(const struct hashtable *h, uint hash, uint *iter);

/* set of strings */
struct strings {
	char *strings;
	uint used;
	uint alloced;
	struct hashtable hashtable;
};

void strings_init(struct strings *strs);
void strings_clean(struct strings *strs);

uint strings_add(struct strings *strs, const char *s);
uint strings_get_id(const struct strings *strs, const char *s);
uint strings_get_first(const struct strings *strs);
uint strings_get_next(const struct strings *strs, uint i);
const char *strings_get(const struct strings *strs, uint i);

/* array of arrays of sets of integers */
struct sets {
	struct array ints;
	struct array sets_first;
	struct array sets_size;
	struct array hashtable;
	struct array subsets;
};

void sets_init(struct sets *sets);
void sets_clean(struct sets *sets);
uint sets_add(struct sets *sets, uint set, uint subset, uint value);

void sets_set_size(struct sets *sets, uint size);
uint sets_get_size(const struct sets *sets);
uint sets_get_set_size(const struct sets *sets, uint set);
uint sets_get_subsets(const struct sets *sets, uint set);
uint sets_get_subset_size(const struct sets *sets, uint set, uint subset);
uint sets_get(const struct sets *sets, uint set, uint subset, uint index);
int sets_subset_has(const struct sets *sets, uint set, uint subset, uint value);
int sets_has(const struct sets *sets, uint set, uint value);

void sets_hash(struct sets *sets);
uint sets_find(const struct sets *sets, uint value, uint *iter);
void sets_unhash(struct sets *sets);

void sets_merge(struct sets *dest, const struct sets *source);
void sets_clone(struct sets *dest, const struct sets *source);

int sets_subsetcmp(const struct sets *sets1, uint set1, uint subset1,
		const struct sets *sets2, uint set2, uint subset2);
#endif
