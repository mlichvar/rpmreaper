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

#ifndef _REPO_H_
#define _REPO_H_

#include "pkg.h"

struct repo {
	uint repo;
	void *data;
	int (*repo_read)(const struct repo *repo, struct pkgs *p, uint firstpid);
	int (*repo_read_fileprovs)(const struct repo *repo, struct pkgs *p, uint firstpid,
			const struct strings *files, const struct strings *basenames);
	int (*repo_pkg_info)(const struct repo *repo, const struct pkgs *p, uint pid);
	int (*repo_remove_pkgs)(const struct repo *repo, const struct pkgs *p, int force);
	void (*repo_clean)(struct repo *repo);
};

struct repos {
	struct array repos;
	struct pkgs pkgs;
};

void repos_init(struct repos *repos);
void repos_clean(struct repos *repos);
struct repo *repos_new(struct repos *repos);
const struct repo *repos_get(const struct repos *repos, uint repo);
struct repo *repos_getw(struct repos *repos, uint repo);
int repos_read(struct repos *repos);

int repos_pkg_info(const struct repos *repos, uint pid);
int repos_remove_pkgs(struct repos *repos, int force); 

#endif
