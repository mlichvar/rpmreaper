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
#include <limits.h>
#include <rpmcli.h>
#include <rpmdb.h>
#include <rpmds.h>
#include <rpmts.h>

#include "rpm.h"

struct rpmrepodata {
	const char *root;
	poptContext context;
	rpmts ts;
};

static int rpm_read(const struct repo *repo, struct pkgs *p, uint firstpid) {
	struct rpmrepodata *rd = repo->data;
	rpmdbMatchIterator iter;
	Header header;
	uint pid;
	rpmtd td;

	td = rpmtdNew();
	rd->ts = rpmtsCreate();
	rpmtsSetRootDir(rd->ts, ((struct rpmrepodata *)repo->data)->root);
	rpmtsSetVSFlags(rd->ts, _RPMVSF_NOSIGNATURES | _RPMVSF_NODIGESTS);

	iter = rpmtsInitIterator(rd->ts, RPMDBI_PACKAGES, NULL, 0);
	for (pid = firstpid; (header = rpmdbNextIterator(iter)) != NULL; pid++) {
		int r;
		uint32_t *epoch, *size, zero = 0;
		rpmds requires;
		const char *req, *reqver;
		rpmsenseFlags reqflags;
		const char *release, *name, *version, *arch;

		name = headerGetString(header, RPMTAG_NAME);
		version = headerGetString(header, RPMTAG_VERSION);
		release = headerGetString(header, RPMTAG_RELEASE);
		arch = headerGetString(header, RPMTAG_ARCH);
		r = headerGet(header, RPMTAG_EPOCH, td, HEADERGET_DEFAULT);
		epoch = r == 1 ? rpmtdGetUint32(td) : &zero;
		r = headerGet(header, RPMTAG_SIZE, td, HEADERGET_DEFAULT);
		size = r == 1 ? rpmtdGetUint32(td) : &zero;
		pkgs_set(p, pid, repo->repo, name, *epoch, version, release, arch,
				0, (*size + 1023) / 1024);
		requires = rpmdsNew(header, RPMTAG_REQUIRENAME, 0);
		while (rpmdsNext(requires) != -1) {
			req = rpmdsN(requires);
			reqflags = rpmdsFlags(requires);
			reqver = rpmdsEVR(requires);
			if (!(reqflags & (RPMSENSE_RPMLIB & ~RPMSENSE_PREREQ))) {
				reqflags &= RPMSENSE_LESS | RPMSENSE_GREATER |
					RPMSENSE_EQUAL;
				pkgs_add_req(p, pid, req, reqflags, reqver);
			}
		}
		rpmdsFree(requires);
	}
	rpmtdFree(td);
	iter = rpmdbFreeIterator(iter);

	return 0;
}

static int rpm_read_provs(const struct repo *repo, struct pkgs *p, uint firstpid,
		const struct strings *files, const struct strings *basenames) {
	struct rpmrepodata *rd = repo->data;
	rpmts ts = rd->ts;
	rpmdbMatchIterator iter;
	Header header;
	char buf[1000];
	uint pid;
	rpmtd bases, dirs, dirindexes;

	bases = rpmtdNew();
	dirs = rpmtdNew();
	dirindexes = rpmtdNew();
	iter = rpmtsInitIterator(ts, RPMDBI_PACKAGES, NULL, 0);

	for (pid = firstpid; (header = rpmdbNextIterator(iter)) != NULL; pid++) {
		int r, dirsread = 0;
		rpmds provides;
		const char *prov, *provver;
		rpmsenseFlags provflags;

		provides = rpmdsNew(header, RPMTAG_PROVIDENAME, 0);
		while (rpmdsNext(provides) != -1) {
			prov = rpmdsN(provides);
			provflags = rpmdsFlags(provides);
			provver = rpmdsEVR(provides);
			provflags &= RPMSENSE_LESS | RPMSENSE_GREATER |
				RPMSENSE_EQUAL;
			pkgs_add_prov(p, pid, prov, provflags, provver);
		};
		rpmdsFree(provides);

		r = headerGet(header, RPMTAG_BASENAMES, bases, HEADERGET_DEFAULT);
		if (r != 1)
			continue;

		while (rpmtdNext(bases) != -1) {
			if (strings_get_id(basenames, rpmtdGetString(bases)) == -1)
				continue;
			if (!dirsread) {
				r = headerGet(header, RPMTAG_DIRNAMES, dirs,
						HEADERGET_DEFAULT);
				assert(r == 1);

				r = headerGet(header, RPMTAG_DIRINDEXES, dirindexes,
						HEADERGET_DEFAULT);
				assert(r == 1);
				dirsread = 1;
			}

			rpmtdSetIndex(dirindexes, rpmtdGetIndex(bases));
			rpmtdSetIndex(dirs, *rpmtdGetUint32(dirindexes));

			snprintf(buf, sizeof (buf), "%s%s", rpmtdGetString(dirs),
					rpmtdGetString(bases));
			if (strings_get_id(files, buf) == -1)
				continue;

			pkgs_add_fileprov(p, pid, buf);
		}
		rpmtdFreeData(bases);
		if (dirsread) {
			rpmtdFreeData(dirs);
			rpmtdFreeData(dirindexes);
		}
	}

	rpmtdFree(bases);
	rpmtdFree(dirs);
	rpmtdFree(dirindexes);

	iter = rpmdbFreeIterator(iter);

	rd->ts = rpmtsFree(ts);

	return 0;
}

int rpmcname(char *str, size_t size, const struct pkgs *p, uint pid) {
	const char *n, *v, *r, *a;
	const struct strings *s = &p->strings;
	const struct pkg *pkg = pkgs_get(p, pid);

	n = strings_get(s, pkg->name);
	v = strings_get(s, pkg->ver);
	r = strings_get(s, pkg->rel);
	a = strings_get(s, pkg->arch);

	if (a && a[0] != '\0')
		return snprintf(str, size, "%s-%s-%s.%s", n, v, r, a);
	else
		return snprintf(str, size, "%s-%s-%s", n, v, r);
}

static int rpm_pkg_info(const struct repo *repo, const struct pkgs *p, uint pid) {
	char cmd[RPMMAXCNAME * 2 + PATH_MAX * 2 + 100];
	int i, j, len, r;
	const char *pager;
	const char *const strs[] = { "(rpm -qi -r ", " ",
		";echo;echo Files:;rpm -ql -r ", ") | " };
	const signed char const idx[] = { 0, -2, 1, -1, 2, -2, 1, -1, 3, -3 };

	pager = getenv("PAGER");
	if (pager == NULL)
		pager = "less";

	j = 0;
	len = sizeof (cmd);
	for (i = 0; i < sizeof (idx) / sizeof (char); i++) {
		switch (idx[i]) {
			case -1:
				r = rpmcname(cmd + j, len, p, pid);
				break;
			case -2:
				r = snprintf(cmd + j, len, "%s", ((struct rpmrepodata *)repo->data)->root);
				break;
			case -3:
				r = snprintf(cmd + j, len, "%s", pager);
				break;
			default:
				if (idx[i] < 0 || idx[i] >= sizeof (strs) / sizeof (char *))
					return 1;
				r = snprintf(cmd + j, len, "%s", strs[(int)idx[i]]);
				break;
		}
		if (r < 0 || r >= len)
			return 1;
		j += r;
		len -= r;
	}
	return system(cmd);
}

static int rpm_remove_pkgs(const struct repo *repo, const struct pkgs *p, const char *options) {
	uint i;
	int len, j = 0, r;
	char *cmd;
	const char *root = ((struct rpmrepodata *)repo->data)->root;

	len = 64 + strlen(root);
	cmd = malloc(len);

	r = snprintf(cmd, len, "rpm -evh %s -r %s ", options, root);
	if (r < 0 || r >= len)
		return 1;
	j += r; len -= r;

	for (i = 0; i < pkgs_get_size(p); i++) {
		if (pkgs_get(p, i)->repo != repo->repo ||
					!(pkgs_get(p, i)->status & PKG_DELETE))
			continue;
		r = rpmcname(cmd + j, len, p, i);
		if (r < 0)
			return 1;
	       	if (r + 1 >= len) {
			cmd = realloc(cmd, (j + len) * 2);
			len += j + len;
			/* try again */
			i--;
			continue;
		}
		j += r + 1; len -= r + 1;
		cmd[j - 1] = ' ';
	}

	cmd[j - 1] = '\0';

	printf("Removing %d packages (%d KB).\n", p->delete_pkgs, p->delete_pkgs_kbytes);
	fflush(stdout);
	r = system(cmd);
	if (r) {
		printf("Command \'%s\' failed.\n", cmd);
		fflush(stdout);
	}
	free(cmd);
	return r;
}

static void rpm_repo_clean(struct repo *r) {
	rpmcliFini(((struct rpmrepodata *)r->data)->context);
	free(r->data);
	r->data = NULL;
}

void rpm_fillrepo(struct repo *r, const char *root) {
	char *argv[] = {""};

	r->repo_read = rpm_read;
	r->repo_read_provs = rpm_read_provs;
	r->repo_pkg_info = rpm_pkg_info;
	r->repo_remove_pkgs = rpm_remove_pkgs;
	r->repo_clean = rpm_repo_clean;

	r->data = malloc(sizeof (struct rpmrepodata));
	((struct rpmrepodata *)r->data)->root = root;
	((struct rpmrepodata *)r->data)->context = rpmcliInit(1, argv, NULL);
}
