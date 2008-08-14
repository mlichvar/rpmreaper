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
#include <rpmcli.h>
#include <rpmdb.h>
#include <rpmds.h>
#include <rpmts.h>

#include "rpm.h"

static int get_string(Header h, rpmTag tag, char **s) {
	int_32 n, t;
	int r;

	r = headerGetEntry(h, tag, &t, (void *)s, &n);
	if (r && t == RPM_STRING_TYPE)
		return n;
	*s = NULL;
	return 0;
}

static void free_string(Header h, char **s) {
	headerFreeTag(h, *s, RPM_STRING_TYPE);
	*s = NULL;
}

static int get_strings(Header h, rpmTag tag, char ***s) {
	int_32 n, t;
	int r;

	r = headerGetEntry(h, tag, &t, (void *)s, &n);
	if (r && t == RPM_STRING_ARRAY_TYPE)
		return n;
	*s = NULL;
	return 0;
}

static void free_strings(Header h, char ***s) {
	headerFreeTag(h, *s, RPM_STRING_ARRAY_TYPE);
	*s = NULL;
}

static int get_int(Header h, rpmTag tag, int_32 **i) {
	int_32 n, t;
	int r;

	r = headerGetEntry(h, tag, &t, (void *)i, &n);
	if (r && t == RPM_INT32_TYPE)
		return n;
	*i = NULL;
	return 0;
}

static void free_int(Header h, int_32 **i) {
	headerFreeTag(h, *i, RPM_INT32_TYPE);
	*i = NULL;
}

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
	char *argv[] = {""};

	rd->context = rpmcliInit(1, argv, NULL);
	rd->ts = rpmtsCreate();
	rpmtsSetRootDir(rd->ts, ((struct rpmrepodata *)repo->data)->root);
	rpmtsSetVSFlags(rd->ts, _RPMVSF_NOSIGNATURES | _RPMVSF_NODIGESTS);

	iter = rpmtsInitIterator(rd->ts, RPMTAG_NAME, NULL, 0);
	for (pid = firstpid; (header = rpmdbNextIterator(iter)) != NULL; pid++) {
		int i, ds1, ds2, ds3;
		int_32 *epoch, *size, *requireflags, zero = 0;
		char *release, *name, *version, *arch;
		char **requires, **requireversions;

		get_string(header, RPMTAG_NAME, &name);
		get_string(header, RPMTAG_VERSION, &version);
		get_string(header, RPMTAG_RELEASE, &release);
		get_string(header, RPMTAG_ARCH, &arch);
		if (!get_int(header, RPMTAG_SIZE, &size))
			size = &zero;
		if (!get_int(header, RPMTAG_EPOCH, &epoch))
			epoch = &zero;
		pkgs_set(p, pid, repo->repo, name, *epoch, version, release, arch,
				0, (*size + 1023) / 1024);
		free_string(header, &name);
		free_string(header, &version);
		free_string(header, &release);
		free_string(header, &arch);
		if (size != &zero)
			free_int(header, &size);
		if (size != &zero)
			free_int(header, &size);

		ds1 = get_strings(header, RPMTAG_REQUIRENAME, &requires);
		ds2 = get_int(header, RPMTAG_REQUIREFLAGS, &requireflags);
		ds3 = get_strings(header, RPMTAG_REQUIREVERSION, &requireversions);
		for (i = 0; i < ds1; i++) {
			if (i < ds2 && !(requireflags[i] & (RPMSENSE_RPMLIB & ~RPMSENSE_PREREQ)))
				pkgs_add_req(p, pid, i < ds1 ? requires[i] : 0, i < ds2 ? requireflags[i] & (RPMSENSE_LESS | RPMSENSE_GREATER | RPMSENSE_EQUAL) : 0, i < ds3 ? requireversions[i] : 0);
		}

		free_strings(header, &requires);
		free_int(header, &requireflags);
		free_strings(header, &requireversions);

		ds1 = get_strings(header, RPMTAG_PROVIDENAME, &requires);
		ds2 = get_int(header, RPMTAG_PROVIDEFLAGS, &requireflags);
		ds3 = get_strings(header, RPMTAG_PROVIDEVERSION, &requireversions);

		for (i = 0; i < ds1; i++) {
			pkgs_add_prov(p, pid, i < ds1 ? requires[i] : 0, i < ds2 ? requireflags[i] & (RPMSENSE_LESS | RPMSENSE_GREATER | RPMSENSE_EQUAL) : 0, i < ds3 ? requireversions[i] : 0);
		}

		free_strings(header, &requires);
		free_int(header, &requireflags);
		free_strings(header, &requireversions);

	}
	iter = rpmdbFreeIterator(iter);

	return 0;
}

static int rpm_read_fileprovs(const struct repo *repo, struct pkgs *p, uint firstpid,
		const struct strings *files, const struct strings *basenames) {
	struct rpmrepodata *rd = repo->data;
	rpmts ts = rd->ts;
	rpmdbMatchIterator iter;
	Header header;
	char buf[1000];
	uint pid, s;

	iter = rpmtsInitIterator(ts, RPMTAG_NAME, NULL, 0);

	for (pid = firstpid; (header = rpmdbNextIterator(iter)) != NULL; pid++) {
		int i, ds1, ds2 = 0, ds3 = 0;
		char **dirs = NULL, **bases;
		int *dirindexes;

		ds1 = get_strings(header, RPMTAG_BASENAMES, &bases);
		for (i = 0; i < ds1; i++) {
			if (strings_get_id(basenames, bases[i]) == -1)
				continue;

			if (dirs == NULL) {
				ds2 = get_strings(header, RPMTAG_DIRNAMES, &dirs);
				ds3 = get_int(header, RPMTAG_DIRINDEXES, &dirindexes);
				assert(dirs != NULL);
				assert(dirindexes != NULL);
			}
			assert(i < ds3 && dirindexes[i] < ds2);
			snprintf(buf, sizeof (buf), "%s%s", dirs[dirindexes[i]], bases[i]);

			if ((s = strings_get_id(files, buf)) == -1)
				continue;
			
			pkgs_add_fileprov(p, pid, buf);
		}

		free_strings(header, &bases);
		if (dirs != NULL) {
			free_strings(header, &dirs);
			free_int(header, &dirindexes);
		}
	}

	iter = rpmdbFreeIterator(iter);

	rd->ts = rpmtsFree(ts);
	rd->context = rpmcliFini(rd->context);

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
	char cmd[1000];
	int i, j, len, r;
	const char *const strs[] = { "(rpm -qi -r ", (const char *)1, " ", 0,
		";echo;echo Files:;rpm -ql -r ", (const char *)1, " ", 0, ") | less" };

	j = 0;
	len = sizeof (cmd);
	for (i = 0; i < sizeof (strs) / sizeof (char *); i++) {
		if (strs[i] == 0)
			r = rpmcname(cmd + j, len, p, pid);
		else if (strs[i] == (const char *)1)
			r = snprintf(cmd + j, len, ((struct rpmrepodata *)repo->data)->root);
		else 
			r = snprintf(cmd + j, len, strs[i]);
		if (r < 0 || r >= len)
			return 1;
		j += r;
		len -= r;
	}
	return system(cmd);
}

static int rpm_remove_pkgs(const struct repo *repo, const struct pkgs *p, int force) {
	uint i;
	int len, j = 0, r;
	char *cmd;

	len = 64;
	cmd = malloc(len);

	r = snprintf(cmd, len, force ? "rpm -e --nodeps -r %s " : "rpm -e -r %s ",
			((struct rpmrepodata *)repo->data)->root);
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
	free(r->data);
	r->data = NULL;
}

void rpm_fillrepo(struct repo *r, const char *root) {
	r->repo_read = rpm_read;
	r->repo_read_fileprovs = rpm_read_fileprovs;
	r->repo_pkg_info = rpm_pkg_info;
	r->repo_remove_pkgs = rpm_remove_pkgs;
	r->repo_clean = rpm_repo_clean;

	r->data = malloc(sizeof (struct rpmrepodata));
	((struct rpmrepodata *)r->data)->root = root;
}
