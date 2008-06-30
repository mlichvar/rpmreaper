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

#include <stdlib.h>
#include <string.h>

#include "pkg.h"

void pkgs_init(struct pkgs *p) {
	memset(p, 0, sizeof (struct pkgs));
	strings_init(&p->strings);
	array_init(&p->pkgs, sizeof (struct pkg));
	deps_init(&p->deps, &p->strings);
	sets_init(&p->requires);
	sets_init(&p->provides);
	sets_init(&p->required);
	sets_init(&p->required_by);
}

void pkgs_clean(struct pkgs *p) {
	strings_clean(&p->strings);
	array_clean(&p->pkgs);
	deps_clean(&p->deps);
	sets_clean(&p->requires);
	sets_clean(&p->provides);
	sets_clean(&p->required);
	sets_clean(&p->required_by);
}

void pkgs_set(struct pkgs *pkgs, uint pid, const char *name, int epoch,
		const char *version, const char *release, const char *arch, uint kbytes) {
	struct pkg *p;

	p = pkgs_getw(pkgs, pid);

	/* p->epoch = epoch */
	p->name = strings_add(&pkgs->strings, name);
	p->ver = strings_add(&pkgs->strings, version);
	p->rel = strings_add(&pkgs->strings, release);
	p->arch = strings_add(&pkgs->strings, arch == NULL ? "" : arch);
	p->size = kbytes;
	p->status = PKG_INSTALLED;
	pkgs->pkgs_kbytes += kbytes;
}

uint pkgs_get_size(const struct pkgs *pkgs) {
	return array_get_size(&pkgs->pkgs);
}

inline const struct pkg *pkgs_get(const struct pkgs *pkgs, uint pid) {
	return ASGETPTR(pkg, &pkgs->pkgs, pid);
}

struct pkg *pkgs_getw(struct pkgs *pkgs, uint pid) {
	return ASGETWPTR(pkg, &pkgs->pkgs, pid);
}

void pkgs_add_req(struct pkgs *p, uint pid, const char *req, int flags, const char *ver) {
	sets_add(&p->requires, pid, 0, deps_add(&p->deps, req, flags, ver));
}

void pkgs_add_prov(struct pkgs *p, uint pid, const char *prov, int flags, const char *ver) {
	sets_add(&p->provides, pid, 0, deps_add(&p->deps, prov, flags, ver));
}

uint pkgs_get_req_size(const struct pkgs *p, uint pid) {
	return sets_get_subset_size(&p->requires, pid, 0);
}

uint pkgs_get_prov_size(const struct pkgs *p, uint pid) {
	return sets_get_subset_size(&p->provides, pid, 0);
}

uint pkgs_get_req(const struct pkgs *p, uint pid, uint req) {
	return sets_get(&p->requires, pid, 0, req);
}

uint pkgs_get_prov(const struct pkgs *p, uint pid, uint prov) {
	return sets_get(&p->provides, pid, 0, prov);
}

static int pkg_req_pkg(struct pkgs *p, uint pid, uint what) {
	uint i, j, n, subs;

	subs = sets_get_subsets(&p->required, pid);

	for (i = 1; i < subs; i++) {
		if (!sets_subset_has(&p->required, pid, i, what))
			continue;
		n = sets_get_subset_size(&p->required, pid, i);
		for (j = 0; j < n; j++) {
			uint req = sets_get(&p->required, pid, i, j);
			if (req != what && !(pkgs_get(p, req)->status & PKG_DELETE))
				break;
		}
		if (j == n)
			return 1;
	}
	return 0;
}

static int leaf_pkg(struct pkgs *p, uint pid) {
	uint i, n;
	uint partleaf;
	
	n = sets_get_subset_size(&p->required_by, pid, 0);

	for (i = 0; i < n; i++)
		if (!(pkgs_get(p, sets_get(&p->required_by, pid, 0, i))->status & PKG_DELETE))
			return 0;

	if (sets_get_subsets(&p->required_by, pid) <= 1)
		return PKG_LEAF;

	n = sets_get_subset_size(&p->required_by, pid, 1);

	partleaf = 0;
	for (i = 0; i < n; i++) {
		uint r = sets_get(&p->required_by, pid, 1, i);
		if (!(pkgs_get(p, r)->status & PKG_DELETE)) {
		       	if (pkg_req_pkg(p, r, pid))
				return 0;
			partleaf = 1;
		}
	}
	return partleaf ? PKG_PARTLEAF : PKG_LEAF;
}

static int broken_pkg(struct pkgs *p, uint pid) {
	uint i, j, n, subs;

	n = sets_get_subset_size(&p->required, pid, 0);
	for (i = 0; i < n; i++)
		if (pkgs_get(p, sets_get(&p->required, pid, 0, i))->status & PKG_DELETE)
			return 1;
	
	subs = sets_get_subsets(&p->required, pid);
	for (i = 1; i < subs; i++) {
		n = sets_get_subset_size(&p->required, pid, i);
		for (j = 0; j < n; j++)
			if (!(pkgs_get(p, sets_get(&p->required, pid, i, j))->status & PKG_DELETE))
				break;
		if (j == n)
			return 1;
	}

	return 0;
}

static void fill_required(struct pkgs *p, uint pid) {
	uint i, j, s, c, req, reqs, pr, prov, requires;
	struct sets set;

	requires = pkgs_get_req_size(p, pid);

	sets_init(&set);
	/* force allocating subsets */
	sets_add(&set, 0, requires, 0);

	for (i = 0; i < requires; i++) {
		uint iter1 = 0;

		req = pkgs_get_req(p, pid, i);

		while ((pr = deps_find(&p->deps, req, &iter1)) != -1) {
			uint iter2 = 0;

			while ((prov = sets_find(&p->provides, pr, &iter2)) != -1) {
				sets_add(&set, 0, i, prov);
				if (prov == pid)
					break;
			}
		}
	}

	for (i = reqs = 0; i < requires; i++) {
		if (sets_subset_has(&set, 0, i, pid))
			/* ignore self dependency */
			continue;
		s = sets_get_subset_size(&set, 0, i);
		if (s == 1) {
			sets_add(&p->required, pid, 0, sets_get(&set, 0, i, 0));
			reqs++;
		} else if (!s)
			pkgs_getw(p, pid)->status |= PKG_BROKEN;
	}

	for (i = 0, c = 1; i < requires; i++) {
		if (sets_subset_has(&set, 0, i, pid))
			continue;
		s = sets_get_subset_size(&set, 0, i);
		if (s > 1) {
			for (j = 0; j < s; j++)
				if (reqs && sets_subset_has(&p->required, pid, 0, sets_get(&set, 0, i, j)))
					/* contains already required package */
					goto continue2;
			for (j = 1; j < c; j++)
				if (!sets_subsetcmp(&p->required, pid, j, &set, 0, i))
					/* duplicate */
					goto continue2;
			for (j = 0; j < s; j++)
				sets_add(&p->required, pid, c, sets_get(&set, 0, i, j));
			c++;
		}
continue2:
		;
	}
	sets_clean(&set);
}

static void fill_required_by(struct pkgs *p, uint pid) {
	uint i, iter = 0;

	while ((i = sets_find(&p->required, pid, &iter)) != -1)
		if (sets_subset_has(&p->required, i, 0, pid))
			sets_add(&p->required_by, pid, 0, i);
		else
			sets_add(&p->required_by, pid, 1, i);
}

void pkgs_match_deps(struct pkgs *p) {
	uint i, n;

	n = array_get_size(&p->pkgs);

	sets_set_size(&p->requires, n);
	sets_set_size(&p->provides, n);

	sets_hash(&p->provides);

	for (i = 0; i < n; i++)
		fill_required(p, i);
	sets_set_size(&p->required, n);

	deps_clean(&p->deps);
	sets_hash(&p->required);

	for (i = 0; i < n; i++)
		fill_required_by(p, i);
	sets_set_size(&p->required_by, n);

	sets_unhash(&p->required);
	sets_clean(&p->requires);
	sets_clean(&p->provides);

	for (i = 0; i < n; i++)
		pkgs_getw(p, i)->status |= leaf_pkg(p, i);
}

static void verify_partleaves(struct pkgs *p, uint pid, uint what, int removed) {
	uint i, j, n, subs;
	struct pkg *pkg;

	subs = sets_get_subsets(&p->required, pid);

	for (i = 1; i < subs; i++) {
		if (!sets_subset_has(&p->required, pid, i, what))
			continue;
		n = sets_get_subset_size(&p->required, pid, i);
		for (j = 0; j < n; j++) {
			uint r = sets_get(&p->required, pid, i, j);

			pkg = pkgs_getw(p, r);
			if (pkg->status & PKG_DELETE)
			       continue;
			if (removed && pkg->status & PKG_PARTLEAF && !leaf_pkg(p, r))
				pkg->status &= ~PKG_PARTLEAF;
			if (!removed && !(pkg->status & (PKG_LEAF | PKG_PARTLEAF)) && leaf_pkg(p, r))
				pkg->status |= PKG_PARTLEAF;
		}
	}
}

int pkgs_delete(struct pkgs *p, uint pid, int force) {
	uint i, j, n, r, subs;
	struct pkg *pkg, *pkg1;

	pkg = pkgs_getw(p, pid);
	if (pkg->status & PKG_DELETE)
		return 0;

	if (!force && !(pkg->status & (PKG_LEAF | PKG_PARTLEAF)))
		return 0;

	pkg->status |= PKG_DELETE;
	p->delete_pkgs++;
	p->delete_pkgs_kbytes += pkg->size;
	pkg->status &= ~PKG_TOBEBROKEN;

	/* check if there are new leaves */
	subs = sets_get_subsets(&p->required, pid);
	for (i = 0; i < subs; i++) {
		n = sets_get_subset_size(&p->required, pid, i);
		for (j = 0; j < n; j++) {
			r = sets_get(&p->required, pid, i, j); 
			pkg1 = pkgs_getw(p, r);
			pkg1->status &= ~(PKG_LEAF | PKG_PARTLEAF);
			pkg1->status |= leaf_pkg(p, r);
		}
	}

	subs = sets_get_subsets(&p->required_by, pid);
	if (pkg->status & PKG_PARTLEAF || !(pkg->status & PKG_LEAF)) {
		/* check if there are new broken packages or lost leaves */
		for (i = 0; i < subs; i++) {
			n = sets_get_subset_size(&p->required_by, pid, i);
			for (j = 0; j < n; j++) {
				r = sets_get(&p->required_by, pid, i, j); 
				pkg1 = pkgs_getw(p, r);

				if (pkg1->status & PKG_DELETE)
				       continue;
				if (broken_pkg(p, r))
					pkg1->status |= PKG_TOBEBROKEN;
				if (i && pkg->status & PKG_PARTLEAF)
					verify_partleaves(p, r, pid, 1);
			}
		}
	}

	return 1;
}

int pkgs_undelete(struct pkgs *p, uint pid, int force) {
	uint i, j, n, r, subs;
	struct pkg *pkg;

	pkg = pkgs_getw(p, pid);
	if (!(pkg->status & PKG_DELETE))
		return 0;

	pkg->status &= ~PKG_DELETE;

	if (broken_pkg(p, pid)) {
		if (!force) {
			pkg->status |= PKG_DELETE;
			return 0;
		}
		pkg->status |= PKG_TOBEBROKEN;
	}

	p->delete_pkgs--;
	p->delete_pkgs_kbytes -= pkg->size;

	/* check if we lost some leaves */
	subs = sets_get_subsets(&p->required, pid);
	for (i = 0; i < subs; i++) {
		n = sets_get_subset_size(&p->required, pid, i);
		for (j = 0; j < n; j++) {
			r = sets_get(&p->required, pid, i, j); 

			pkg = pkgs_getw(p, r);
			pkg->status &= ~(PKG_LEAF | PKG_PARTLEAF);
			if (i)
				pkg->status |= leaf_pkg(p, r);
		}
	}

	/* check if we unbroke some packages or made new leaves */
	subs = sets_get_subsets(&p->required_by, pid);
	for (i = 0; i < subs; i++) {
		n = sets_get_subset_size(&p->required_by, pid, i);
		for (j = 0; j < n; j++) {
			r = sets_get(&p->required_by, pid, i, j); 
			pkg = pkgs_getw(p, r);

			if (pkg->status & PKG_DELETE)
				continue;
			if (pkg->status & PKG_TOBEBROKEN && !broken_pkg(p, r))
				pkg->status &= ~PKG_TOBEBROKEN;
			if (i)
				verify_partleaves(p, r, pid, 0);
		}
	}

	return 1;
}
