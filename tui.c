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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <regex.h>
#include <ncurses.h>

#include "repo.h"
#include "rpm.h"

#define FLAG_REQ	(1<<0)
#define FLAG_REQOR	(1<<1)
#define FLAG_REQBY	(1<<2)
#define FLAG_EDGE	(1<<3)
#define FLAG_PLOOP	(1<<4)
#define FLAG_TRANS	(1<<5)

#define SORT_BY_NAME	0
#define SORT_BY_FLAGS	1
#define SORT_BY_SIZE	2

struct row {
	uint pid;
	short level;
	char flags;
};

struct pkglist {
	struct array rows;

	int first;
	int cursor;
	int lines;
	int context;
	int sortby;

	char *limit;
};

void sort_rows(struct pkglist *l, const struct pkgs *p, uint first, uint size, int s);

const struct row *get_row(const struct pkglist *l, uint r) {
	return ASGETPTR(row, &l->rows, r);
}

struct row *get_wrow(struct pkglist *l, uint r) {
	return ASGETWPTR(row, &l->rows, r);
}

uint get_used_pkgs(const struct pkglist *l) {
	return array_get_size(&l->rows);
}

void select_color_by_status(const struct pkg *pkg) {
	if (pkg->status & PKG_DELETED)
		attron(COLOR_PAIR(7));
	else if (pkg->status & PKG_DELETE)
		attron(COLOR_PAIR(4));
	else if (pkg->status & (PKG_BROKEN | PKG_TOBEBROKEN))
		attron(COLOR_PAIR(5));
	else if (pkg->status & (PKG_LEAF | PKG_PARTLEAF))
		attron(COLOR_PAIR(3));
	else
		attron(COLOR_PAIR(2));
}

void display_pkg_status(const struct pkg *pkg) {
	addch(pkg->status & PKG_DELETE ? 'D' : pkg->status & PKG_DELETED ? 'd' : ' ');
	addch(pkg->status & PKG_LEAF ? 'L' : pkg->status & PKG_PARTLEAF ? 'l' : ' ');
	addch(pkg->status & PKG_INLOOP ? 'o' : ' ');
	addch(pkg->status & PKG_BROKEN ? 'B' : pkg->status & PKG_TOBEBROKEN ? 'b' : ' ');
}

void display_size(uint size, int style) {
	const char units[] = { 'K', 'M', 'G', 'T' };
	float s = size;
	int i;

	for (i = 0; s >= 1000.0 && i + 1 < sizeof (units); i++)
		s /= 1024.0;
	printw(style ? (i ? "%6.1f" : "%6.f") : (i ? "%.1f " : "%.f "), s);
	addch(units[i]);
	if (!style)
		addch('B');
}

void display_help() {
	attron(COLOR_PAIR(1));
	move(0, 0);
	hline(' ', COLS);
	addnstr("q:Quit  d,D,E:Del  u,U,I:Undel  r,R:Req  b,B:ReqBy  i:Info  c,C:Commit  F1:Help", COLS);
}

void display_status(const struct pkgs *p, const struct pkglist *l) {
	const char * const sortnames[] = { "name", "flags", "size" };
	int lp, line = LINES - 2;

	attron(COLOR_PAIR(1));
	move(line, 0);
	hline('-', COLS);

	if (l != NULL && COLS > 25) {
		if (l->limit != NULL && l->limit[0] != '\0')
			mvprintw(line, COLS - 23, "(limit)");
		mvprintw(line , COLS - 15, "(%s)", sortnames[l->sortby]);

		lp = l->first + l->lines > get_used_pkgs(l) ? 100 :
			(l->first + l->lines) * 100 / get_used_pkgs(l);
		mvprintw(line, COLS - (lp < 100 ? lp < 10 ? 5 : 6 : 7), "(%d%%)", lp);
	}

	move(line, 1);
	printw("[ Pkgs: %d (", pkgs_get_size(p));
	display_size(p->pkgs_kbytes, 0);
	printw(")  Del: %d (", p->delete_pkgs);
	display_size(p->delete_pkgs_kbytes, 0);
	printw(")  Break: %d ]", p->break_pkgs);
}

void display_message(const char *m, int attr) {
	attron(attr);
	mvhline(LINES - 1, 0, ' ', COLS);
	if (m)
		mvaddstr(LINES - 1, 0, m);
	refresh();
	attroff(attr);
}

void display_info_message(const char *m) {
	display_message(m, COLOR_PAIR(6) | A_BOLD);
}

void display_error_message(const char *m) {
	display_message(m, COLOR_PAIR(5) | A_BOLD);
}

void display_question(const char *m) {
	display_message(m, COLOR_PAIR(2));
}

void display_pkg_info(const struct repos *r, uint pid) {
	endwin();
	repos_pkg_info(r, pid);
}

void display_pkgs(const struct pkglist *l, const struct pkgs *p) {
	int i, used = get_used_pkgs(l);
	const struct pkg *pkg;

	for (i = l->first; i - l->first < l->lines && i < used; i++) {
		pkg = pkgs_get(p, get_row(l, i)->pid);

		move(i - l->first + 1, 0);
		select_color_by_status(pkg);
		if (i == l->cursor)
			attron(A_REVERSE | A_BOLD);

		hline(' ', COLS);
		display_pkg_status(pkg);
		addch(' ');
		display_size(pkg->size, 1);

		move(i - l->first + 1, 13 + 4 * get_row(l, i)->level);
		printw("%-25s %s-%s.%s",
				strings_get(&p->strings, pkg->name),
				strings_get(&p->strings, pkg->ver),
				strings_get(&p->strings, pkg->rel),
				strings_get(&p->strings, pkg->arch));

		if (i == l->cursor)
			attroff(A_REVERSE | A_BOLD);
	}
}

void draw_deplines(const struct pkglist *l, const struct pkgs *p) {
	int i, dir, first = l->first, lines = l->lines, used = get_used_pkgs(l);

	for (i = first, dir = -1; dir <= 1; i += dir, dir += 2) {
		while (i > 0 && i + 1 < used &&	get_row(l, i)->level != 0)
		       	i += dir;

		for (; i < used && ((dir > 0 && i > first) || (dir < 0 && i - first < lines));
				i -= dir) {
			int edge, ploop = 0, trans = 0, j, level1, level2, reqor = 0, oldreqor = 0;

			if (!(get_row(l, i)->flags & (dir > 0 ? FLAG_REQBY : FLAG_REQ)))
				continue;

			level1 = get_row(l, i)->level;
			for (j = i - dir, edge = 0; !edge &&
					((dir > 0 && j >= first) ||
					 (dir < 0 && j - first < lines)) &&
					j >= 0 && j < used; j -= dir) {
				level2 = get_row(l, j)->level;
				if (level2 == level1 + 1) {
					edge = get_row(l, j)->flags & FLAG_EDGE;
					ploop = get_row(l, j)->flags & FLAG_PLOOP;
					trans = get_row(l, j)->flags & FLAG_TRANS;
					oldreqor = reqor;
					reqor = !!(get_row(l, j)->flags & FLAG_REQOR);
				}

				if ((dir > 0 && j >= first + lines) || (dir < 0 && j < first))
					continue;

				move(j - first + 1, 13 + 4 * level1);

				if (j == l->cursor) {
					attron(A_REVERSE | A_BOLD);
					select_color_by_status(pkgs_get(p, get_row(l, j)->pid));
				} else {
					attroff(A_REVERSE | A_BOLD);
					attron(COLOR_PAIR(4));
				}

				if (level2 == level1 + 1) {
					if (edge)
						addch(dir > 0 ? ACS_ULCORNER : ACS_LLCORNER);
					else 
						addch(reqor && dir < 0 ? ACS_VLINE : ACS_LTEE);
					if (reqor)
						addch(dir > 0 ? ' ' : oldreqor ? ACS_LTEE : ACS_ULCORNER);
					else
						addch(trans ? '*' : oldreqor ? ACS_BTEE : ACS_HLINE);
					addch(dir > 0 ? '<' : '>');
					if (ploop)
						addch('+');
				} else {
					addch(ACS_VLINE);
					if (reqor && dir < 0)
						addch(ACS_VLINE);
				}
			}
		}
	}

	attroff(A_REVERSE | A_BOLD);
}

void move_cursor(struct pkglist *l, int x) {
	int used = get_used_pkgs(l);

	if (used < 1)
		return;

	l->cursor += x;
	l->cursor = MIN(l->cursor, used - 1);
	l->cursor = MAX(l->cursor, 0);

	l->first = MIN(l->first, l->cursor - l->context);
	l->first = MAX(l->first, 0);
	l->first = MAX(l->first, l->cursor + l->context + 1 - l->lines);
}

void move_to_next_leaf(struct pkglist *l, const struct pkgs *p, int dir) {
	uint c, f, used = get_used_pkgs(l);

	for (c = (l->cursor + dir + used) % used; c != l->cursor; c = (c + dir + used) % used) {
		f = pkgs_get(p, get_row(l, c)->pid)->status;
		if (f &	(PKG_LEAF | PKG_PARTLEAF) && !(f & PKG_DELETE)) {
			l->cursor = c;
			return;
		}
	}
}

void move_to_pid(struct pkglist *l, uint pid) {
	uint i;

	for (i = 0; i < get_used_pkgs(l); i++)
		if (!get_row(l, i)->level && get_row(l, i)->pid == pid) {
			l->cursor = i;
			break;
		}
}

void scroll_pkglist(struct pkglist *l, int x) {
	int used = get_used_pkgs(l);

	if ((x > 0 && (l->first + x >= used || l->cursor + x <= l->context)) ||
			(x < 0 && l->first + x < 0)) {
		move_cursor(l, x);
		return;
	}

	l->first += x;
	l->cursor = MAX(l->cursor, l->first + l->context);
	l->cursor = MIN(l->cursor, used - 1);
	l->cursor = MIN(l->cursor, l->first + l->lines - l->context - 1);
	l->cursor = MAX(l->cursor, 0);
}

int find_parent(const struct pkglist *l, int x) {
	int level = get_row(l, x)->level;
	int used = get_used_pkgs(l);
	int i, dir, edges;

	if (!level)
		return x;
	for (dir = -1; dir <= 1; dir += 2)
		for (i = x, edges = 0; i >= 0 && i < used; i += dir)
			if (get_row(l, i)->level + 1 == level) {
				if (get_row(l, i)->flags & (dir == 1 ? FLAG_REQBY : FLAG_REQ))
					return i;
				break;
			} else if (get_row(l, i)->level == level && get_row(l, i)->flags & FLAG_EDGE)
				if (edges++ == 1)
					break;
	return x;
}

int find_outer_edge(const struct pkglist *l, int x, int flag) {
	int level = get_row(l, x)->level, used = get_used_pkgs(l);

	while ((get_row(l, x)->flags & FLAG_REQ && flag & FLAG_REQ) ||
			(get_row(l, x)->flags & FLAG_REQBY && flag & FLAG_REQBY)) {
		x += flag & FLAG_REQ ? 1 : -1;
		while (x < used && x >= 0 && !(get_row(l, x)->level == level + 1 && get_row(l, x)->flags & FLAG_EDGE))
			x += flag & FLAG_REQ ? 1 : -1;
		level++;
	}
	return x;
}

void move_rows(struct pkglist *l, int from, int where) {
	array_move(&l->rows, where, from, get_used_pkgs(l) - from);
	if (from > where)
		array_set_size(&l->rows, get_used_pkgs(l) - (from - where));
}

void toggle_req(struct pkglist *l, const struct pkgs *p, int reqby, int trans) {
	if (!reqby && get_row(l, l->cursor)->flags & FLAG_REQ) {
		move_rows(l, find_outer_edge(l, l->cursor, FLAG_REQ) + 1, l->cursor + 1);
		get_wrow(l, l->cursor)->flags &= ~FLAG_REQ;
	} else if (reqby && get_row(l, l->cursor)->flags & FLAG_REQBY) {
		int edge = find_outer_edge(l, l->cursor, FLAG_REQBY);

		move_rows(l, l->cursor, edge);
		l->cursor = edge;
		get_wrow(l, l->cursor)->flags &= ~FLAG_REQBY;
	} else {
		uint i, j, d, dep, deps, pid, pid2, s, scc;
		const struct sets *r;
		struct row *row = NULL;
		struct sets transreqs;

		pid = get_row(l, l->cursor)->pid;

		if (trans) {
			sets_init(&transreqs);
			sets_set_size(&transreqs, 1);
			pkgs_get_trans_reqs(p, pid, reqby, &transreqs);
			r = &transreqs;
			s = 0;
		} else {
			r = reqby ? &p->required_by : &p->required;
			s = pid;
		}

		deps = sets_get_set_size(r, s);
		if (!deps) {
			if (trans)
				sets_clean(&transreqs);
			return;
		}

		if (reqby) {
			move_rows(l, l->cursor, l->cursor + deps);
			l->cursor += deps;
		} else
			move_rows(l, l->cursor + 1, l->cursor + deps + 1);

		scc = pkgs_get_scc(p, pid);

		for (i = 0, dep = 1; i < sets_get_subsets(r, s); i++) {
			d = sets_get_subset_size(r, s, i);
			if (!d)
				continue;
			for (j = 0; j < d; j++, dep++) {
				row = get_wrow(l, l->cursor + (reqby ? -dep : dep));
				row->pid = pid2 = sets_get(r, s, i, j);
				row->level = get_row(l, l->cursor)->level + 1;
				row->flags = 0;
				if (i) 
					row->flags |= FLAG_REQOR;
				if (scc != -1 && pkgs_in_scc(p, scc, pid2))
					row->flags |= FLAG_PLOOP;
				if (trans)
					row->flags |= FLAG_TRANS;
			}
			sort_rows(l, p, l->cursor + (reqby ? -dep + 1 : dep - d), d, SORT_BY_NAME);
			if (i && !reqby)
				row->flags &= ~FLAG_REQOR;
		}
		row->flags |= FLAG_EDGE;
		get_wrow(l, l->cursor)->flags |= reqby ? FLAG_REQBY : FLAG_REQ;

		if (trans)
			sets_clean(&transreqs);
	}
}

static const struct pkgs *pkgs;
static int sortby;

static int compare_rows(const void *r1, const void *r2) {
	const struct pkg *p1, *p2;
	int r;

	p1 = pkgs_get(pkgs, ((const struct row *)r1)->pid);
	p2 = pkgs_get(pkgs, ((const struct row *)r2)->pid);
	switch (sortby) {
		case SORT_BY_FLAGS:
			if ((r = (p2->status ^ PKG_DELETED) - (p1->status ^ PKG_DELETED)))
				return r;
		case SORT_BY_SIZE:
			if ((r = p2->size - p1->size))
				return r;
		case SORT_BY_NAME:
			if ((r = strcmp(strings_get(&pkgs->strings, p1->name), strings_get(&pkgs->strings, p2->name))))
				return r;
	}
	return 0;
}

void sort_rows(struct pkglist *l, const struct pkgs *p, uint first, uint size, int s) {
	if (!size)
		return;
	pkgs = p;
	sortby = s;
	qsort(get_wrow(l, first), size, sizeof (struct row), compare_rows);
	pkgs = NULL;
	sortby = 0;
}

struct searchexpr {
	uint set;
	uint unset;
	regex_t reg;
};

int searchexpr_comp(struct searchexpr *expr, const char *s) {
	const char *rest = s;
	char c = *rest;

	if (rest == NULL)
		return -1;

	expr->set = 0;
	expr->unset = 0;

	if (c == '~' || c == '!') {
		const char *it = rest;
		int not = 0;
		int flag = 0;
		int letter = 0;

		while ((c = *it++)) {
			if (!not && !flag && c == '!')
				not = 1;
			else if (!flag && c == '~')
				flag = 1;
			else if (flag && (c == 'L' || c == 'l' || c == 'D' || c == 'd' ||
						c == 'B' || c == 'b' || c == 'o')) {
				uint *field = not ? &expr->unset : &expr->set;

				switch (c) {
					case 'L': *field |= PKG_LEAF; break;
					case 'l': *field |= PKG_PARTLEAF; break;
					case 'D': *field |= PKG_DELETE; break;
					case 'd': *field |= PKG_DELETED; break;
					case 'B': *field |= PKG_BROKEN; break;
					case 'b': *field |= PKG_TOBEBROKEN; break;
					case 'o': *field |= PKG_INLOOP; break;
				}
				letter = 1;
				rest = it;
			} else if (letter && c == ' ') {
				not = 0;
				flag = 0;
				letter = 0;
				rest = it;
			} else
				break;
		}
	}

	if (regcomp(&expr->reg, rest, REG_EXTENDED | REG_NOSUB))
		return -1;

	return 0;
}

void searchexpr_clean(struct searchexpr *expr) {
	regfree(&expr->reg);
}

int searchexpr_match(const struct pkgs *p, uint pid, const struct searchexpr *expr) {
	char cname[RPMMAXCNAME];
	const struct pkg *pkg = pkgs_get(p, pid);

	if ((expr->set && (pkg->status & expr->set) == 0) ||
			(pkg->status & expr->unset) != 0)
		return 0;

	rpmcname(cname, sizeof (cname), p, pid);
	return !regexec(&expr->reg, cname, 0, NULL, 0);
}

void fill_pkglist(struct pkglist *l, const struct pkgs *p) {
	uint i, j;
	struct searchexpr expr;

	array_set_size(&l->rows, 0);
	l->cursor = l->first = 0;

	if (l->limit != NULL && searchexpr_comp(&expr, l->limit))
		return;

	for (i = j = 0; i < pkgs_get_size(p); i++) {
		if (l->limit != NULL && !searchexpr_match(p, i, &expr))
			continue;

		get_wrow(l, j++)->pid = i;
	}

	if (l->limit != NULL)
		searchexpr_clean(&expr);

	sort_rows(l, p, 0, get_used_pkgs(l), l->sortby);
}

void stretch_pkglist(struct pkglist *l) {
	l->lines = MAX(LINES - 3, 0);
	l->context = MIN((l->lines - 1) / 2, 3);
}

void init_pkglist(struct pkglist *l, const struct pkgs *p, int sortby, char *limit) {
	memset(l, 0, sizeof (struct pkglist));
	array_init(&l->rows, sizeof (struct row));

	l->sortby = sortby;
	l->limit = limit;

	stretch_pkglist(l);
	fill_pkglist(l, p);
}

void clean_pkglist(struct pkglist *l) {
	free(l->limit);
	array_clean(&l->rows);
	memset(l, 0, sizeof (struct pkglist));
}

void sort_pkglist(struct pkglist *l, const struct pkgs *p) {
	uint cpid;
	int c;

	display_question("Sort by (f)lags/(n)ame/(s)ize?:");
	c = getch();
	switch (c) {
		case 'f':
			l->sortby = SORT_BY_FLAGS;
			break;
		case 'n':
			l->sortby = SORT_BY_NAME;
			break;
		case 's':
			l->sortby = SORT_BY_SIZE;
			break;
		default:
			return;
	}

	cpid = l->cursor < get_used_pkgs(l) ? get_row(l, l->cursor)->pid : -1;
	fill_pkglist(l, p);
	move_to_pid(l, cpid);
}

void limit_pkglist(struct pkglist *l, const struct pkgs *p, char *limit) {
	uint cpid;

	free(l->limit);
	l->limit = limit;

	cpid = l->cursor < get_used_pkgs(l) ? get_row(l, l->cursor)->pid : -1;
	fill_pkglist(l, p);
	move_to_pid(l, cpid);
}

void search_pkglist(struct pkglist *l, const struct pkgs *p, const char *searchre, int dir) {
	uint c, used = get_used_pkgs(l);
	struct searchexpr expr;

	if (searchre == NULL || searchexpr_comp(&expr, searchre))
		return;

	for (c = (l->cursor + dir + used) % used; c != l->cursor; c = (c + dir + used) % used) {
		if (searchexpr_match(p, get_row(l, c)->pid, &expr)) {
			l->cursor = c;
			break;
		}
	}

	searchexpr_clean(&expr);
}

void print_pkg(const struct pkgs *p, uint pid) {
	char cname[RPMMAXCNAME];

	rpmcname(cname, sizeof (cname), p, pid);
	printf("%s", cname);
}

void print_pkgs(const struct pkgs *p, const char *limit, int verbose, int oneline) {
	const struct pkg *pkg;
	struct searchexpr expr;
	uint i;

	if (limit != NULL && searchexpr_comp(&expr, limit))
		return;

	for (i = 0; i < pkgs_get_size(p); i++) {
		pkg = pkgs_get(p, i);
		if (limit != NULL && !searchexpr_match(p, i, &expr))
			continue;
		if (verbose)
			printf("%d ", i);
		print_pkg(p, i);
		if (!verbose) {
			printf(oneline ? " " : "\n");
			continue;
		}
		if (pkg->status & PKG_LEAF)
			printf(" LEAF");
		if (pkg->status & PKG_PARTLEAF)
			printf(" PARTLEAF");
		if (pkg->status & PKG_BROKEN)
			printf(" BROKEN");
		if (pkg->status & PKG_INLOOP)
			printf(" INLOOP:%d", pkgs_get_scc(p, i));
		if (pkg->status & PKG_DELETED)
			printf(" DELETED");
		printf("\n");
	}
	if (oneline)
		printf("\n");

	if (limit != NULL)
		searchexpr_clean(&expr);
}

void read_list(struct repos *r) {
	display_info_message("Reading packages...");
	repos_read(r);
	display_info_message(NULL);
}

int ask_remove_pkgs(const struct pkgs *p) {
	if (!p->delete_pkgs)
		return 0;
	display_question("Remove marked packages? (yes/[no]):");
	if (getch() != 'y')
		return 0;
	return 1;
}

struct selection {
	struct strings deleted;
};

void save_selection(struct selection *s, const struct pkgs *p) {
	uint i;
	char cname[RPMMAXCNAME];

	strings_init(&s->deleted);

	for (i = 0; i < pkgs_get_size(p); i++) {
		if (!(pkgs_get(p, i)->status & PKG_DELETE))
			continue;
		rpmcname(cname, sizeof (cname), p, i);
		strings_add(&s->deleted, cname);
	}
}

void load_selection(struct selection *s, struct pkgs *p) {
	uint i;
	char cname[RPMMAXCNAME];

	for (i = 0; i < pkgs_get_size(p); i++) {
		rpmcname(cname, sizeof (cname), p, i);
		if (strings_get_id(&s->deleted, cname) == -1)
			continue;
		pkgs_delete(p, i, 1);
	}
}

void clean_selection(struct selection *s) {
	strings_clean(&s->deleted);
}

void save_cursor(char *c, int size, const struct pkglist *l, const struct pkgs *p) {
	int i, j;

	c[0] = '\0';
	if (l->cursor >= get_used_pkgs(l))
		return;
	for (i = j = l->cursor; (j = find_parent(l, i)) != i; i = j)
		;

	rpmcname(c, size, p, get_row(l, i)->pid);
}

void load_cursor(const char *c, struct pkglist *l, const struct pkgs *p) {
	uint i;
	char cname[RPMMAXCNAME];

	for (i = 0; i < get_used_pkgs(l); i++) {
		rpmcname(cname, sizeof (cname), p, get_row(l, i)->pid);
		if (!strcmp(cname, c)) {
			l->cursor = i;
			return;
		}
	}
}

void reread_list(struct repos *r, struct pkglist *l) {
	struct pkgs *p = &r->pkgs;
	struct selection s;
	char cursor[RPMMAXCNAME];

	save_selection(&s, p);
	save_cursor(cursor, sizeof (cursor), l, p);

	read_list(r);

	load_selection(&s, p);
	clean_selection(&s);

	fill_pkglist(l, p);

	load_cursor(cursor, l, p);
}

void commit(struct repos *r, struct pkglist *l, int force) {
	struct pkgs *p = &r->pkgs;

	if (ask_remove_pkgs(p)) {
		endwin();
		if (repos_remove_pkgs(r, force)) {
			char buf[100], *d;

			printf("\nPress Enter to continue.");
			fflush(stdout);
			d = fgets(buf, sizeof (buf), stdin);
		}
		reread_list(r, l);
	}
}

char *readline(const char *prompt) {
	char *buf;
	int c, size = 16, used = 0, cur = 0, x, quit = 0, o = 0;

	buf = malloc(size);
	buf[used] = '\0';

	attron(COLOR_PAIR(2));
	mvprintw(LINES - 1, 0, prompt);
	curs_set(1);
	x = getcurx(stdscr);

	while (!quit) {
		move(LINES - 1, x);
		clrtoeol();
		o = x + cur >= COLS ? x + cur - COLS + 1 : 0;
		addnstr(buf + o, COLS - x);
		move(LINES - 1, x + cur - o);
		c = getch();
		switch (c) {
			case KEY_ENTER:
			case '\r':
			case '\n':
				quit = 1;
				break;
			case 'G' - 0x40:
				quit = 2;
				break;
			case KEY_HOME:
			case 'A' - 0x40:
				cur = 0;
				break;
			case KEY_END:
			case 'E' - 0x40:
				cur = used;
				break;
			case 'U' - 0x40:
				memmove(buf, buf + cur, used - cur + 1);
				used -= cur;
				cur = 0;
				break;
			case 'K' - 0x40:
				buf[cur] = '\0';
				used = cur;
				break;
			case KEY_LEFT:
				if (cur)
					cur--;
				break;
			case KEY_RIGHT:
				if (cur < used)
					cur++;
				break;
			case KEY_BACKSPACE:
			case 0x8:
			case 0x7f:
				if (!cur)
					break;
				cur--;
			case KEY_DC:
				if (cur >= used)
					break;
				memmove(buf + cur, buf + cur + 1, used - cur);
				used--;
				break;
			default:
				if (c < 0x20 || c > 0x7f)
					break;
				while (used + 2 > size)
					buf = realloc(buf, (size *= 2));
				memmove(buf + cur + 1, buf + cur, used - cur + 1);
				buf[cur++] = c;
				used++;
		}
	}

	curs_set(0);

	if (quit > 1) {
		free(buf);
		return NULL;
	}
	return buf;
}

void tui(struct repos *r, const char *limit) {
	struct pkglist l;
	struct pkgs *p = &r->pkgs;
	int c, quit, searchdir = 0;
	char *s, *searchre = NULL;

	initscr();
	if (has_colors()) {
		start_color();
		init_pair(1, COLOR_YELLOW, COLOR_BLUE);
		init_pair(2, COLOR_WHITE, COLOR_BLACK);
		init_pair(3, COLOR_YELLOW, COLOR_BLACK);
		init_pair(4, COLOR_MAGENTA, COLOR_BLACK);
		init_pair(5, COLOR_RED, COLOR_BLACK);
		init_pair(6, COLOR_CYAN, COLOR_BLACK);
		init_pair(7, COLOR_BLUE, COLOR_BLACK);
		bkgdset(COLOR_PAIR(2));
	}
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	nonl();
	curs_set(0);
	erase();

	display_help();
	display_status(p, NULL);
	read_list(r);

	init_pkglist(&l, p, SORT_BY_FLAGS, limit != NULL ? strdup(limit) : NULL);

	for (quit = 0; !quit; ) {
		move_cursor(&l, 0);

		erase();
		display_pkgs(&l, p);
		draw_deplines(&l, p);
		display_help();
		display_status(p, &l);

		if (!get_used_pkgs(&l))
			display_error_message("Nothing to select.");

		move(l.cursor - l.first + 1, 0);

		c = getch();

		switch (c) {
			case 'c':
			case 'C':
				commit(r, &l, c == 'c' ? 0 : 1);
				break;
			case 'l':
				if ((s = readline("Limit: ")) != NULL)
					limit_pkglist(&l, p, s);
				break;
			case 'o':
				sort_pkglist(&l, p);
				break;
			case 'q':
				quit = 1;
				break;
			case 'x':
				quit = 2;
				break;
			/* ^R */
			case 'R' - 0x40:
				reread_list(r, &l);
				break;
			/* ^L */
			case 'L' - 0x40:
				clear();
				break;
			case KEY_F(1):
				endwin();
				if (system("man rpmreaper"))
					;
				break;
			case KEY_RESIZE:
				stretch_pkglist(&l);
				break;
		}

		if (!get_used_pkgs(&l) || l.lines < 1)
			continue;

		switch (c) {
			case 'k':
			case KEY_UP:
				move_cursor(&l, -1);
				break;
			case 'j':
			case KEY_DOWN:
				move_cursor(&l, 1);
				break;
			case 'h':
			case KEY_LEFT:
				l.cursor = find_parent(&l, l.cursor);
				break;
			case KEY_PPAGE:
				scroll_pkglist(&l, -l.lines);
				break;
			case KEY_NPAGE:
				scroll_pkglist(&l, l.lines);
				break;
			case '<':
				scroll_pkglist(&l, -1);
				break;
			case '>':
				scroll_pkglist(&l, 1);
				break;
			case '[':
				scroll_pkglist(&l, -l.lines / 2);
				break;
			case ']':
				scroll_pkglist(&l, l.lines / 2);
				break;
			case KEY_HOME:
				l.cursor = 0;
				break;
			case KEY_END:
				l.cursor = get_used_pkgs(&l) - 1;
				break;
			case KEY_BTAB:
				move_to_next_leaf(&l, p, -1);
				break;
			case '\t':
				move_to_next_leaf(&l, p, 1);
				break;
			case 'r':
			case 'R':
				toggle_req(&l, p, 0, c == 'r' ? 0 : 1);
				break;
			case 'b':
			case 'B':
				toggle_req(&l, p, 1, c == 'b' ? 0 : 1);
				break;
			case 'd':
			case 'D':
				pkgs_delete(p, get_row(&l, l.cursor)->pid, c == 'd' ? 0 : 1);
				break;
			case 'u':
			case 'U':
				pkgs_undelete(p, get_row(&l, l.cursor)->pid, c == 'u' ? 0 : 1);
				break;
			case 'E':
				pkgs_delete_rec(p, get_row(&l, l.cursor)->pid);
				break;
			case 'I':
				pkgs_undelete_rec(p, get_row(&l, l.cursor)->pid);
				break;
			case 'i':
				display_pkg_info(r, get_row(&l, l.cursor)->pid);
				break;
			case '/':
			case '?':
				if ((s = readline("Search: ")) == NULL)
					break;
				free(searchre);
				searchre = s;
				searchdir = (c == '/') ? 1 : -1;
			case 'n':
				search_pkglist(&l, p, searchre, searchdir);
				break;
			case 'N':
				search_pkglist(&l, p, searchre, -searchdir);
				break;
		}
	}	
	if (quit < 2) {
		int remove = ask_remove_pkgs(p);
		endwin();
		if (!remove)
			print_pkgs(p, "~D", 0, 1);
		else
			repos_remove_pkgs(r, 0);
	} else
		endwin();
	clean_pkglist(&l);
	free(searchre);
}

void list_pkgs(struct repos *r, const char *limit, int verbose) {
	repos_read(r);
	print_pkgs(&r->pkgs, limit, verbose, 0);
}

int main(int argc, char **argv) {
	struct repos r;
	int opt, list = 0, verbose = 0;
	const char *limit = NULL, *rpmroot = "/";

	while ((opt = getopt(argc, argv, "lvr:h")) != -1) {
		switch (opt) {
			case 'l':
				list = 1;
				break;
			case 'v':
				verbose = 1;
				break;
			case 'r':
				rpmroot = optarg;
				break;
			case 'h':
			default:
				printf("usage: rpmreaper [options] [limit]\n");
				printf("  -l        list packages\n");
				printf("  -v        verbose listing\n");
				printf("  -r root   specify root (default /)\n");
				printf("  -h        print usage\n");
				return 0;
		}
	}

	repos_init(&r);
	rpm_fillrepo(repos_new(&r), rpmroot);

	if (optind < argc)
		limit = argv[optind];

	if (list)
		list_pkgs(&r, limit, verbose);
	else
		tui(&r, limit);

	repos_clean(&r);
	return 0;
}
