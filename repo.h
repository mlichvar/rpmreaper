#ifndef _REPO_H_
#define _REPO_H_

#include "pkg.h"

struct repo {
	uint repo;
	void *data;
	int (*repo_read)(const struct repo *repo, struct pkgs *p, uint firstpid);
	int (*repo_read_fileprovs)(const struct repo *repo, struct pkgs *p, uint firstpid,
			const struct strings *files, const struct strings *basenames);
	int (*repo_pkg_info)(const struct pkgs *p, uint pid);
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
