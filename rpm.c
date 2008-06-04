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
#include <libgen.h>
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

static void pass1(struct pkgs *p, rpmts ts) {
	rpmdbMatchIterator iter;
	Header header;
	uint pid;

	iter = rpmtsInitIterator(ts, RPMTAG_NAME, NULL, 0);
	for (pid = 0; (header = rpmdbNextIterator(iter)) != NULL; pid++) {
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
		pkgs_set(p, pid, name, *epoch, version, release, arch, (*size + 1023) / 1024);
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
	}
	iter = rpmdbFreeIterator(iter);
}

static void pass2(struct pkgs *p, rpmts ts) {
	rpmdbMatchIterator iter;
	Header header;
	struct strings basenames;
	uint s;
	char buf[1000];
	uint pid;

	buf[sizeof (buf) - 1] = '\0';

	strings_init(&basenames);

	/* find file requires */
	for (s = strings_get_first(&p->strings); s != -1; s = strings_get_next(&p->strings, s)) {
		const char *str;

		str = strings_get(&p->strings, s);
		if (str && str[0] == '/') {
			char *base;

			strncpy(buf, str, sizeof (buf) - 1);
			base = basename(buf);
			if (!strcmp(base, ".") || !strcmp(base, "/"))
				continue;
			strings_add(&basenames, base);
		}
	}

	iter = rpmtsInitIterator(ts, RPMTAG_NAME, NULL, 0);

	for (pid = 0; (header = rpmdbNextIterator(iter)) != NULL; pid++) {
		int i, ds1, ds2 = 0, ds3 = 0;
		char **dirs = NULL, **bases;
		int *dirindexes;
		int_32 *requireflags;
		char **requires, **requireversions;

		ds1 = get_strings(header, RPMTAG_PROVIDENAME, &requires);
		ds2 = get_int(header, RPMTAG_PROVIDEFLAGS, &requireflags);
		ds3 = get_strings(header, RPMTAG_PROVIDEVERSION, &requireversions);
		for (i = 0; i < ds1; i++) {
			pkgs_add_prov(p, pid, i < ds1 ? requires[i] : 0, i < ds2 ? requireflags[i] & (RPMSENSE_LESS | RPMSENSE_GREATER | RPMSENSE_EQUAL) : 0, i < ds3 ? requireversions[i] : 0);
		}

		free_strings(header, &requires);
		free_int(header, &requireflags);
		free_strings(header, &requireversions);

		ds1 = get_strings(header, RPMTAG_BASENAMES, &bases);
		for (i = 0; i < ds1; i++) {
			if (strings_get_id(&basenames, bases[i]) == -1)
				continue;

			if (dirs == NULL) {
				ds2 = get_strings(header, RPMTAG_DIRNAMES, &dirs);
				ds3 = get_int(header, RPMTAG_DIRINDEXES, &dirindexes);
				assert(dirs != NULL);
				assert(dirindexes != NULL);
			}
			assert(i < ds3 && dirindexes[i] < ds2);
			snprintf(buf, sizeof (buf), "%s%s", dirs[dirindexes[i]], bases[i]);

			if ((s = strings_get_id(&p->strings, buf)) == -1)
				continue;
			
			pkgs_add_prov(p, pid, strings_get(&p->strings, s), 0, NULL);
		}

		free_strings(header, &bases);
		if (dirs != NULL) {
			free_strings(header, &dirs);
			free_int(header, &dirindexes);
		}
	}

	iter = rpmdbFreeIterator(iter);
	strings_clean(&basenames);
}

int read_rpmdb(struct pkgs *p)
{
	poptContext context;
	rpmts ts;
	char *argv[] = {""};

	context = rpmcliInit(1, argv, NULL);
	ts = rpmtsCreate();
	rpmtsSetVSFlags(ts, _RPMVSF_NOSIGNATURES | _RPMVSF_NODIGESTS);

	/* read names, rels, vers, requires */
	pass1(p, ts);

	/* read provides */
	pass2(p, ts);

	ts = rpmtsFree(ts);
	context = rpmcliFini(context);
	return 1;
}

int rpminfo(const struct pkgs *p, uint pid) {
	char cmd[1000];
	const struct strings *s = &p->strings;
	const struct pkg *pkg = pkgs_get(p, pid);

	snprintf(cmd, sizeof (cmd), "rpm -qi '%s-%s-%s.%s' | less",
			strings_get(s, pkg->name),
			strings_get(s, pkg->ver),
			strings_get(s, pkg->rel),
			strings_get(s, pkg->arch));
	return system(cmd);
}

int rpmremove(const struct pkgs *p, int force) {
	uint i;
	int len, j, r;
	char *cmd;
	const struct strings *s = &p->strings;
	const struct pkg *pkg;
 
	for (i = len = 0; i < pkgs_get_size(p); i++) {
	       	pkg = pkgs_get(p, i);
		if (!(pkg->status & PKG_DELETE))
			continue;
		len += 6 + strlen(strings_get(s, pkg->name)) +
			strlen(strings_get(s, pkg->ver)) +
			strlen(strings_get(s, pkg->rel)) +
			strlen(strings_get(s, pkg->arch));
	}

	j = 0;
	len += 100;
	cmd = malloc(len);
	r = snprintf(cmd, len, force ? "rpm -e --nodeps" : "rpm -e");
	if (r < 0 || r >= len)
		return 1;
	j += r; len -= r;

	for (i = 0; i < pkgs_get_size(p); i++) {
	       	pkg = pkgs_get(p, i);
		if (!(pkg->status & PKG_DELETE))
			continue;
		r = snprintf(cmd + j, len, " %s-%s-%s.%s",
			strings_get(s, pkg->name),
			strings_get(s, pkg->ver),
			strings_get(s, pkg->rel),
			strings_get(s, pkg->arch));
		if (r < 0 || r >= len)
			return 1;
		j += r; len -= r;
	}

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
