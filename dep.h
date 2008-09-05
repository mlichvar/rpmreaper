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

#ifndef _DEP_H_
#define _DEP_H_

#include "misc.h"

struct deps {
	struct array names;
	struct array epochs;
	struct array vers;
	struct array rels;
	struct array flags;
	struct hashtable hashtable;
	struct strings *strings;
};

void deps_init(struct deps *deps, struct strings *strings);
void deps_clean(struct deps *deps);
uint deps_add(struct deps *deps, const char *name, int flags, const char *ver);
uint deps_add_evr(struct deps *deps, const char *name, int flags,
		uint epoch, const char *version, const char *release);
uint deps_find(const struct deps *deps, uint dep, uint *iter);
int deps_match(const struct deps *deps, uint x, uint y);

#endif
