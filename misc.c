/*
 * Copyright (C) 2008, 2009  Miroslav Lichvar <mlichvar@redhat.com>
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
#include <assert.h>

#include "misc.h"

#if 0
#define ARRAYS_FIXED
#endif

void array_init(struct array *a, unsigned char width) {
	memset(a, 0, sizeof (struct array));
	a->width = width;
	if (width)
		a->fixed = 1;
#ifdef ARRAYS_FIXED
	if (!width)
		a->width = sizeof (uint);
#endif
}

void array_clean(struct array *a) {
	free(a->array);
	memset(a, 0, sizeof (struct array));
}

static inline void array_zero_no_check(struct array *a, uint start, uint size) {
	memset((char *)a->array + start * a->width, 0, size * a->width);
}

void array_zero(struct array *a, uint start, uint size) {
	assert(start + size <= a->size);
	array_zero_no_check(a, start, size);
}

static inline unsigned char value_width(uint v) {
#ifdef ARRAYS_FIXED
	return sizeof (uint);
#else
	if (v >= 1 << sizeof (short) * 8)
		return sizeof (uint);
	if (v >= 1 << 8)
		return sizeof (short);
	if (v)
		return 1;
	return 0;
#endif
}

static void array_write(uint *a, uint i, unsigned char w, uint v) {
#ifdef ARRAYS_FIXED
	a[i] = v;
#else
	switch (w) {
		case sizeof (uint): a[i] = v; break;
		case sizeof (short): ((unsigned short *)a)[i] = v; break;
		case sizeof (char): ((unsigned char *)a)[i] = v; break;
	}
#endif
}

static inline uint array_read(const uint *a, uint i, unsigned char w) {
#ifdef ARRAYS_FIXED
	return a[i];
#else
	switch (w) {
		case sizeof (uint): return a[i];
		case sizeof (short): return ((unsigned short *)a)[i];
		case sizeof (char): return ((unsigned char *)a)[i];
	}
	return 0;
#endif
}

static void array_resize(struct array *a, unsigned char width, uint size) {
	uint i, alloc = a->alloced;

	while (alloc < size)
		alloc = !alloc ? 16 : alloc * 2;

	a->array = realloc(a->array, MAX(width, a->width) * alloc);

	if (width > a->width) {
		for (i = a->size; i > 0; i--)
			array_write(a->array, i - 1, width, array_read(a->array, i - 1, a->width));
		a->width = width;
		array_zero_no_check(a, a->size, alloc - a->size);
	} else
		array_zero_no_check(a, a->alloced, alloc - a->alloced);

	a->alloced = alloc;
}

void array_set(struct array *a, uint index, uint value) {
	unsigned char width;

	width = value_width(value);

	if (index >= a->alloced
#ifndef ARRAYS_FIXED
			|| width > a->width
#endif
			)
		array_resize(a, width, index + 1);

	if (a->size <= index)
		a->size = index + 1;

	array_write(a->array, index, a->width, value);
}

inline uint array_get(const struct array *a, uint index) {
	assert(index < a->size);
	return array_read(a->array, index, a->width);
}

void array_inc(struct array *a, uint index, int inc) {
	array_set(a, index, array_get(a, index) + inc);
}

void *array_get_wptr(struct array *a, uint index) {
	assert(a->fixed);

	if (index >= a->alloced)
		array_resize(a, a->width, index + 1);

	if (a->size <= index)
		a->size = index + 1;

	return (void *)((char *)a->array + index * a->width);
}

inline const void *array_get_ptr(const struct array *a, uint index) {
	assert(index < a->size);
	return (char *)a->array + index * a->width;
}

void array_set_size(struct array *a, uint size) {
	if (size > a->alloced) {
		a->array = realloc(a->array, a->width * size);
		array_zero_no_check(a, a->alloced, size - a->alloced);
		a->alloced = size;
	} else if (size < a->size)
		array_zero_no_check(a, size, a->size - size);

	a->size = size;
}

inline uint array_get_size(const struct array *a) {
	return a->size;
}

void array_clone(struct array *dest, const struct array *source) {
	dest->width = source->width;
	dest->fixed = source->fixed;
	array_set_size(dest, source->size);
	memcpy(dest->array, source->array, source->width * source->size);
}

void array_move(struct array *a, uint dest, uint source, uint size) {
	assert(source + size <= a->size);

	if (dest + size > a->alloced)
		array_resize(a, a->width, dest + size);

	if (dest + size > a->size)
		a->size = dest + size;

	memmove((char *)a->array + dest * a->width, (char *)a->array + source * a->width, size * a->width);
}

uint array_bsearch(const struct array *a, uint start, uint size, uint value) {
	uint left, right;

	left = start;
	right = start + size;

	while (left < right) {
		uint center, v;
		
		center = (left + right) / 2;
		v = array_get(a, center);

		if (v == value)
			return center;
		else if (v > value)
			right = center;
		else
			left = center + 1;
	}

	return left;
}

void hashtable_init(struct hashtable *h) {
	memset(h, 0, sizeof (struct hashtable));
	array_init(&h->table, 0);
	array_set_size(&h->table, 1);
}

void hashtable_clean(struct hashtable *h) {
	array_clean(&h->table);
	memset(h, 0, sizeof (struct hashtable));
}

static int hashtable_need_resize(const struct hashtable *h) {
	return h->load * 2 + 1 >= array_get_size(&h->table);
}

static inline uint getslot(uint hash, uint i, uint size) {
	return (hash + (i + i * i) / 2) % size;
}

void hashtable_add_dir(struct hashtable *h, uint value, uint hash, uint iter) {
	uint size = array_get_size(&h->table);
	uint slot = getslot(hash, iter - 1, size);

	assert(slot < array_get_size(&h->table));
	assert(!hashtable_need_resize(h));
	assert(!array_get(&h->table, slot));
	h->load++;
	array_set(&h->table, slot, value + 1);
}

void hashtable_add(struct hashtable *h, uint value, uint hash) {
	uint size = array_get_size(&h->table);
	uint slot, v, m;

	assert(!hashtable_need_resize(h));

	for (m = 0; (v = array_get(&h->table, (slot = getslot(hash, m, size)))); m++)
	       if (v - 1 == value)
		       /* already stored */
		       return;
	
	h->load++;
	array_set(&h->table, slot, value + 1);
}

int hashtable_resize(struct hashtable *h) {
	uint size = array_get_size(&h->table);

	if (!hashtable_need_resize(h))
		return 0;

	array_zero(&h->table, 0, size);

	while (h->load * 2 + 1 >= size)
		size = size ? size * 2 : 16;

	array_set_size(&h->table, size);
	assert(!hashtable_need_resize(h));
	h->load = 0;
	return 1;
}

uint hashtable_find(const struct hashtable *h, uint hash, uint *iter) {
	uint size = array_get_size(&h->table);
	uint slot = getslot(hash, (*iter)++, size);

	return array_get(&h->table, slot) - 1;
}

static uint compute_stringhash(const char *s) {
	uint r;

	for (r = 0; *s; s++)
		r = r * 27 + *s;

	return r;
}

static void rebuild_hashtable(struct strings *h) {
	uint i, hash;
	
	for (i = strings_get_first(h); i != -1; i = strings_get_next(h, i)) {
		hash = compute_stringhash(h->strings + i);
		hashtable_add(&h->hashtable, i, hash);
	}
}

static void resize_strings(struct strings *h, uint minsize) {
	while (minsize > h->alloced)
		if (h->alloced < 16)
			h->alloced = 16;
		else
			h->alloced *= 2;
	h->strings = realloc(h->strings, h->alloced);
}

void strings_init(struct strings *strs) {
	memset(strs, 0, sizeof (struct strings));
	hashtable_init(&strs->hashtable);
}

void strings_clean(struct strings *strs) {
	free(strs->strings);
	hashtable_clean(&strs->hashtable);
	memset(strs, 0, sizeof (struct strings));
}

uint strings_add(struct strings *strs, const char *s) {
	uint hash = compute_stringhash(s);
	uint i, iter = 0;
	size_t len;
	
	if (hashtable_resize(&strs->hashtable))
		rebuild_hashtable(strs);

	while ((i = hashtable_find(&strs->hashtable, hash, &iter)) != -1)
	       if (!strcmp(s, strs->strings + i))
		       break;

	if (i != -1)
		/* string already stored */
		return i;

	len = strlen(s);
	if (strs->alloced - strs->used <= len)
	       resize_strings(strs, strs->used + len + 1);

	i = strs->used;
	hashtable_add_dir(&strs->hashtable, i, hash, iter);
	strs->used += len + 1;
	strcpy(strs->strings + i, s);
	return i;
}

uint strings_get_id(const struct strings *strs, const char *s) {
	uint hash = compute_stringhash(s);
	uint i, iter = 0;
	
	while ((i = hashtable_find(&strs->hashtable, hash, &iter)) != -1)
		if (!strcmp(s, strings_get(strs, i)))
			break;
	return i;
}

uint strings_get_first(const struct strings *strs) {
	if (strs->used)
		return 0;
	return -1;
}

uint strings_get_next(const struct strings *strs, uint i) {
	size_t len;

	assert(strs->strings != NULL);
	assert(i < strs->used);

	len = strlen(strs->strings + i);

	if (i + len + 1 >= strs->used)
		return -1;
	return i + len + 1;
}

const char *strings_get(const struct strings *strs, uint i) {
	assert(strs->strings != NULL);
	assert(i < strs->used);

	return strs->strings + i;
}

void sets_init(struct sets *sets) {
	memset(sets, 0, sizeof (struct sets));
	array_init(&sets->ints, 0);
	array_init(&sets->sets_first, 0);
	array_init(&sets->sets_size, 0);
	array_init(&sets->hashtable, 0);
	array_init(&sets->subsets, 0);
}

void sets_clean(struct sets *sets) {
	array_clean(&sets->ints);
	array_clean(&sets->sets_first);
	array_clean(&sets->sets_size);
	array_clean(&sets->hashtable);
	array_clean(&sets->subsets);
	memset(sets, 0, sizeof (struct sets));
}

static uint subset_get_first(const struct sets *sets, uint set, uint subset) {
	uint subsets = array_get(&sets->subsets, set);
	uint set_first = array_get(&sets->sets_first, set);

	assert(subset <= subsets);
	if (subset == 0)
		return subsets;
	return array_get(&sets->ints, set_first + subset - 1);
}

static uint subset_get_last(const struct sets *sets, uint set, uint subset) {
	uint subsets = array_get(&sets->subsets, set);
	uint set_first = array_get(&sets->sets_first, set);

	assert(subset <= subsets);
	if (subset == subsets)
		return array_get(&sets->sets_size, set);
	return array_get(&sets->ints, set_first + subset);
}

uint sets_add(struct sets *sets, uint set, uint subset, uint value) {
	uint i, size, first, subsets, sub_first, sub_size;

	/* don't add when hashtable is created */
	assert(!array_get_size(&sets->hashtable));

	/* adding only to the last set or creating new one is supported */
	assert(set + 1 >= array_get_size(&sets->sets_first));

	if (array_get_size(&sets->sets_size) > set) {
		first = array_get(&sets->sets_first, set);
		size = array_get(&sets->sets_size, set);
		subsets = array_get(&sets->subsets, set);
	} else {
		/* create new set */
		size = subsets = 0;
		first = array_get_size(&sets->ints);
		array_set(&sets->sets_first, set, first);
		array_set(&sets->sets_size, set, size);
		array_set(&sets->subsets, set, subsets);
	}

	if (subsets || subset) {
		if (subset > subsets) {
			/* create new subset(s) */
			array_move(&sets->ints, first + subset, first + subsets, size - subsets);
			size += subset - subsets;
			assert(array_get_size(&sets->ints) == first + size);
			for (i = 0; i < subsets; i++)
				array_inc(&sets->ints, first + i, subset - subsets);
			sub_first = size;
			for (; subsets < subset; subsets++)
				array_set(&sets->ints, first + subsets, sub_first);
			array_set(&sets->subsets, set, subsets);
			sub_size = 0;
		} else {
			sub_first = subset_get_first(sets, set, subset);
			sub_size = subset_get_last(sets, set, subset) - sub_first;
		}
	} else {
		sub_first = 0;
		sub_size = size;
	}

	sub_first += first;

	/* find the place in sorted array */
	i = array_bsearch(&sets->ints, sub_first, sub_size, value);
	
	if (i < sub_first + sub_size && array_get(&sets->ints, i) == value)
		/* value already stored in set */
		return i - sub_first;

	/* move the rest */
	array_move(&sets->ints, i + 1, i, first + size - i);
	size++;

	array_set(&sets->ints, i, value);
	array_set(&sets->sets_size, set, size);

	if (subsets) {
		uint j;
		for (j = subset; j < subsets; j++)
			array_inc(&sets->ints, first + j, 1);
	}

	assert(array_get_size(&sets->ints) == first + size);
	assert(array_get_size(&sets->sets_size) == array_get_size(&sets->sets_first));
	assert(array_get(&sets->sets_size, set) > 0);
	assert(sets_get(sets, set, subset, i - sub_first) == value);

	return i - sub_first;
}

void sets_set_size(struct sets *sets, uint size) {
	assert(!array_get_size(&sets->hashtable));

	array_set_size(&sets->sets_first, size);
	array_set_size(&sets->sets_size, size);
	array_set_size(&sets->subsets, size);
}

uint sets_get_size(const struct sets *sets) {
	return array_get_size(&sets->sets_first);
}

uint sets_get_set_size(const struct sets *sets, uint set) {
	return array_get(&sets->sets_size, set) - array_get(&sets->subsets, set);
}

uint sets_get_subsets(const struct sets *sets, uint set) {
	return array_get(&sets->subsets, set) + 1;
}

uint sets_get_subset_size(const struct sets *sets, uint set, uint subset) {
	uint subsets = array_get(&sets->subsets, set);

	if (subsets)
		return subset_get_last(sets, set, subset) - subset_get_first(sets, set, subset);
	return array_get(&sets->sets_size, set);
}

uint sets_get(const struct sets *sets, uint set, uint subset, uint index) {
	uint set_first = array_get(&sets->sets_first, set), sub_first = 0;
	uint subsets = array_get(&sets->subsets, set);

	if (subsets) {
		assert(index < sets_get_subset_size(sets, set, subset));
		sub_first = subset_get_first(sets, set, subset);
	}
	assert(index + sub_first < array_get(&sets->sets_size, set));
	return array_get(&sets->ints, set_first + index + sub_first);
}

int sets_subset_has(const struct sets *sets, uint set, uint subset, uint value) {
	uint i;
	uint first = array_get(&sets->sets_first, set);
	uint sub_first = subset_get_first(sets, set, subset);
	uint sub_last = subset_get_last(sets, set, subset);

	i = array_bsearch(&sets->ints, first + sub_first, sub_last - sub_first, value);
	if (i < first + sub_first + sub_last - sub_first && array_get(&sets->ints, i) == value)
		return 1;
	return 0;
}

int sets_has(const struct sets *sets, uint set, uint value) {
	uint i, j;
	uint first = array_get(&sets->sets_first, set);
	uint size = array_get(&sets->sets_size, set);
	uint subsets = array_get(&sets->subsets, set);

	if (subsets) {
		for (j = 0; j <= subsets; j++)
			if (sets_subset_has(sets, set, j, value))
				return 1;
		return 0;
	}
	
	i = array_bsearch(&sets->ints, first, size, value);
	return i < first + size && array_get(&sets->ints, i) == value;
}

static uint inthash(uint i) {
	return 13 * i << 8 ^ i;
}

void sets_hash(struct sets *sets) {
	struct array t1, t2;
	uint hash, i, j, index, last, slot, oslot, value, s;

	for (s = 16; s < array_get_size(&sets->ints) * 2; s *= 2)
		;

	array_set_size(&sets->hashtable, s);
	array_init(&t1, 0);
	array_init(&t2, 0);
	array_set_size(&t1, s);
	array_set_size(&t2, s);

	for (i = 0; i < array_get_size(&sets->sets_first); i++) {
		index = array_get(&sets->sets_first, i) + array_get(&sets->subsets, i);
		last = index + array_get(&sets->sets_size, i) - array_get(&sets->subsets, i);
		for ( ;	index < last; index++) {
			value = array_get(&sets->ints, index);
			hash = inthash(value);
			j = 0;
			oslot = slot = getslot(hash, j, s);
			if (array_get(&t2, slot) == value) {
				/* shortcut */
				j = array_get(&t1, slot);
				slot = getslot(hash, j, s);
			}
			while (array_get(&sets->hashtable, slot)) {
				j++;
				slot = getslot(hash, j, s);
			}	

			array_set(&sets->hashtable, slot, i + 1);
			array_set(&t1, oslot, j + 1);
			array_set(&t2, oslot, value);
		}
	}
	array_clean(&t2);
	array_clean(&t1);
}

uint sets_find(const struct sets *sets, uint value, uint *iter) {
	uint i, s, h;

	s = array_get_size(&sets->hashtable);
	h = inthash(value);

	while ((i = array_get(&sets->hashtable, getslot(h, (*iter)++, s))) &&
			!sets_has(sets, i - 1, value))
		;
	return i - 1;
}

void sets_unhash(struct sets *sets) {
	array_clean(&sets->hashtable);
}

void sets_merge(struct sets *dest, const struct sets *source) {
	uint sets1, sets2, sets, s, i, j, k;
	struct sets tmp;

	assert(!array_get_size(&dest->hashtable));
 
	sets1 = array_get_size(&dest->sets_first);
	sets2 = array_get_size(&source->sets_first);
	sets = MAX(sets1, sets2);

	sets_clone(&tmp, dest);
	sets_clean(dest);
	sets_init(dest);

	for (i = 0; i < sets; i++) {
		uint subs, subs1, subs2;

	        subs1 = i < sets1 ? array_get(&tmp.subsets, i) : 0;
		subs2 = i < sets2 ? array_get(&source->subsets, i) : 0;
		subs = MAX(subs1, subs2);

		for (j = 0; j <= subs; j++) {
			if (i < sets1 && j <= subs1) {
				s = sets_get_subset_size(&tmp, i, j);
				for (k = 0; k < s; k++)
					sets_add(dest, i, j, sets_get(&tmp, i, 0, k));
			}
			if (i < sets2 && j <= subs2) {
				s = sets_get_subset_size(source, i, j);
				for (k = 0; k < s; k++)
					sets_add(dest, i, j, sets_get(source, i, 0, k));
			}
		}
	}
	sets_clean(&tmp);
}

void sets_clone(struct sets *dest, const struct sets *source) {
	sets_init(dest);
	array_clone(&dest->ints, &source->ints);
	array_clone(&dest->sets_first, &source->sets_first);
	array_clone(&dest->sets_size, &source->sets_size);
	array_clone(&dest->subsets, &source->subsets);
}

int sets_subsetcmp(const struct sets *sets1, uint set1, uint subset1,
		const struct sets *sets2, uint set2, uint subset2) {
	uint i, s, first1, first2, sub_first1, sub_first2;

	sub_first1 = subset_get_first(sets1, set1, subset1);
	sub_first2 = subset_get_first(sets2, set2, subset2);

	s = subset_get_last(sets1, set1, subset1) - sub_first1;

	if (subset_get_last(sets2, set2, subset2) - sub_first2 != s)
		return 1;

	first1 = array_get(&sets1->sets_first, set1);
	first2 = array_get(&sets2->sets_first, set2);

	for (i = 0; i < s; i++)
		if (array_get(&sets1->ints, first1 + sub_first1 + i) !=
				array_get(&sets2->ints, first2 + sub_first2 + i))
			return 1;

	return 0;
}
