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

#ifndef _PKG_H_
#define _PKG_H_

#include "misc.h"
#include "dep.h"

#define PKG_INSTALLED	(1<<0)
#define PKG_INLOOP	(1<<1)
#define PKG_LEAF	(1<<2)
#define PKG_PARTLEAF	(1<<3)
#define PKG_DELETE	(1<<4)
#define PKG_BROKEN	(1<<5)
#define PKG_TOBEBROKEN	(1<<6)

struct pkg {
	uint name;
	uint ver;
	uint rel;
	uint arch;
	uint size;
	uint status;
};

struct pkgs {
	struct strings strings;
	struct array pkgs;
	struct deps deps;

	struct sets requires;
	struct sets provides;
	//struct sets conflicts;
	struct sets required;
	struct sets required_by;
	struct sets sccs;

	int delete_pkgs;
	int pkgs_kbytes;
	int delete_pkgs_kbytes;
};

void pkgs_init(struct pkgs *p);
void pkgs_clean(struct pkgs *p);
void pkgs_set(struct pkgs *pkgs, uint pid, const char *name, int epoch,
		const char *version, const char *release, const char *arch,
		uint kbytes);
uint pkgs_get_size(const struct pkgs *pkgs);
const struct pkg *pkgs_get(const struct pkgs *p, uint i);
struct pkg *pkgs_getw(struct pkgs *pkgs, uint pid);

void pkgs_add_req(struct pkgs *p, uint pid, const char *req, int flags,
		const char *ver);
void pkgs_add_prov(struct pkgs *p, uint pid, const char *prov, int flags,
		const char *ver);
uint pkgs_get_req_size(const struct pkgs *p, uint pid);
uint pkgs_get_prov_size(const struct pkgs *p, uint pid);
uint pkgs_get_req(const struct pkgs *p, uint pid, uint req);
uint pkgs_get_prov(const struct pkgs *p, uint pid, uint prov);

void pkgs_match_deps(struct pkgs *p);

int pkgs_delete(struct pkgs *p, uint pid, int force);
int pkgs_undelete(struct pkgs *p, uint pid, int force);

#endif
