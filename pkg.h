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

#ifndef _PKG_H_
#define _PKG_H_

#include "misc.h"
#include "dep.h"

#define PKG_INLOOP	(1<<1)
#define PKG_LEAF	(1<<2)
#define PKG_PARTLEAF	(1<<3)
#define PKG_DELETE	(1<<4)
#define PKG_BROKEN	(1<<5)
#define PKG_TOBEBROKEN	(1<<6)
#define PKG_DELETED	(1<<7)

#define PKG_ALLDEL (PKG_DELETED | PKG_DELETE)

struct pkg {
	uint name;
	uint ver;
	uint rel;
	uint arch;
	uint size;
	uint status;
	uint repo;
};

struct pkgs {
	struct strings strings;
	struct array pkgs;
	struct deps deps;

	struct sets requires;
	struct sets provides;
	struct sets fileprovides;
	//struct sets conflicts;
	struct sets required;
	struct sets required_by;
	struct sets sccs;

	uint delete_pkgs;
	uint break_pkgs;
	uint pkgs_kbytes;
	uint delete_pkgs_kbytes;
};

void pkgs_init(struct pkgs *p);
void pkgs_clean(struct pkgs *p);
void pkgs_set(struct pkgs *pkgs, uint pid, uint repo, const char *name, int epoch,
		const char *version, const char *release, const char *arch,
		uint status, uint kbytes);
uint pkgs_get_size(const struct pkgs *pkgs);
const struct pkg *pkgs_get(const struct pkgs *p, uint i);
struct pkg *pkgs_getw(struct pkgs *pkgs, uint pid);

void pkgs_add_req(struct pkgs *p, uint pid, const char *req, int flags,
		const char *ver);
void pkgs_add_prov(struct pkgs *p, uint pid, const char *prov, int flags,
		const char *ver);
void pkgs_add_req_evr(struct pkgs *p, uint pid, const char *req, int flags,
		uint epoch, const char *version, const char *release);
void pkgs_add_prov_evr(struct pkgs *p, uint pid, const char *prov, int flags,
		uint epoch, const char *version, const char *release);
void pkgs_add_fileprov(struct pkgs *p, uint pid, const char *file);
uint pkgs_get_req_size(const struct pkgs *p, uint pid);
uint pkgs_get_prov_size(const struct pkgs *p, uint pid);
uint pkgs_get_req(const struct pkgs *p, uint pid, uint req);
uint pkgs_get_prov(const struct pkgs *p, uint pid, uint prov);
uint pkgs_find_req(const struct pkgs *p, uint prov, uint *iter);
uint pkgs_find_prov(const struct pkgs *p, uint req, uint *iter);

void pkgs_match_deps(struct pkgs *p);

uint pkgs_get_scc(const struct pkgs *p, uint pid);
int pkgs_in_scc(const struct pkgs *p, uint scc, uint pid);

int pkgs_delete(struct pkgs *p, uint pid, int force);
int pkgs_undelete(struct pkgs *p, uint pid, int force);
int pkgs_delete_rec(struct pkgs *p, uint pid);
int pkgs_undelete_rec(struct pkgs *p, uint pid);

void pkgs_get_trans_reqs(const struct pkgs *p, uint pid, int reqby, struct sets *set);
void pkgs_get_matching_deps(const struct pkgs *p, uint pid, uint ppid, int prov, struct sets *set);
#endif
