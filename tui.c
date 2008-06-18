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

#include "rpm.h"

#define FLAG_REQ	(1<<0)
#define FLAG_REQOR	(1<<1)
#define FLAG_REQBY	(1<<2)
#define FLAG_EDGE	(1<<3)

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
	if (pkg->status & PKG_DELETE)
		attron(COLOR_PAIR(4));
	else if (pkg->status & (PKG_BROKEN | PKG_TOBEBROKEN))
		attron(COLOR_PAIR(5));
	else if (pkg->status & (PKG_LEAF | PKG_PARTLEAF))
		attron(COLOR_PAIR(3));
	else
		attron(COLOR_PAIR(2));
}

void display_pkg_status(const struct pkg *pkg) {
	addch(pkg->status & PKG_DELETE ? 'D' : ' ');
	addch(pkg->status & PKG_LEAF ? 'L' : pkg->status & PKG_PARTLEAF ? 'l' : ' ');
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
	mvprintw(0, 0, "q:Quit  d,D:Del  u,U:Undel  r:Req  b:ReqBy  o:Sort  i:Info  c,C:Commit  l:Limit");
	hline(' ', COLS);
}

void display_status(const struct pkgs *p) {
	attron(COLOR_PAIR(1));
	move(LINES - 2, 0);
	hline('-', COLS);
	printw("---[ Pkgs: %d (", pkgs_get_size(p));
	display_size(p->pkgs_kbytes, 0);
	printw(")  Del: %d (", p->delete_pkgs);
	display_size(p->delete_pkgs_kbytes, 0);
	printw(") ]");
}

void display_liststatus(const struct pkglist *l) {
	const char * const sortnames[] = { "name", "flags", "size" };

	if (COLS > 78 && l->limit != NULL && l->limit[0] != '\0')
		mvprintw(LINES - 2, COLS - 27, "(limit)");
	if (COLS > 68)
		mvprintw(LINES - 2, COLS - 17, "(%s)", sortnames[l->sortby]);
	if (COLS > 58)
		mvprintw(LINES - 2, COLS - 7, "(%d%%)", l->first + l->lines > get_used_pkgs(l) ? 100 : (l->first + l->lines) * 100 / get_used_pkgs(l));
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

void display_pkg_info(const struct pkgs *p, uint pid) {
	endwin();
	rpminfo(p, pid);
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
			int edge, j, level1, level2, reqor = 0, oldreqor = 0;

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
					const chtype ch1[2][2] = {{ACS_LTEE, ACS_VLINE}, {ACS_LTEE, ACS_VLINE}};
					const chtype ch2[2][2] = {{ACS_HLINE, ACS_ULCORNER}, {ACS_BTEE, ACS_LTEE}};
					if (dir > 0) {
						addch(edge ? ACS_ULCORNER : ACS_LTEE);
						addch(reqor ? ' ' : ACS_HLINE);
						addch('<');
					} else {
						if (edge) {
							addch(ACS_LLCORNER);
							addch(oldreqor ? ACS_BTEE : ACS_HLINE);
						} else {
							addch(ch1[oldreqor][reqor]);
							addch(ch2[oldreqor][reqor]);
						}
						addch('>');
					}
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

	if (x > 0) {
		if (l->cursor + x < used)
			l->cursor += x;
		else
			l->cursor = used - 1;
	} else {
		if (l->cursor > -x)
			l->cursor += x;
		else
			l->cursor = 0;
	}

	if (l->cursor < l->first)
		l->first = l->cursor;
	else if (l->cursor >= l->first + l->lines)
		l->first = l->cursor - l->lines + 1;
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

void scroll_pkglist(struct pkglist *l, int x) {
	int used = get_used_pkgs(l);

	if (x > 0) {
		if (l->first + x >= used)
			move_cursor(l, x);
		else if (l->first + x < used)
			l->first += x;
		else
			l->first = (used > l->lines) ? used - 1 - l->lines : 0;
	} else {
		if (l->first == 0 || l->first + l->lines < -x )
			move_cursor(l, x);
		else if (l->first > -x)
			l->first += x;
		else
			l->first = 0;
	}

	if (l->cursor < l->first)
		l->cursor = l->first;
	else if (l->cursor >= l->first + l->lines)
		l->cursor = l->first + l->lines - 1;
}

int find_parent(const struct pkglist *l) {
	int level = get_row(l, l->cursor)->level;
	int used = get_used_pkgs(l);
	int i;

	if (!level)
		return l->cursor;
	for (i = l->cursor; i >= 0; i--)
		if (get_row(l, i)->level + 1 == level) {
			if (get_row(l, i)->flags & FLAG_REQ)
				return i;
			break;
		}
	for (i = l->cursor; i < used; i++)
		if (get_row(l, i)->level + 1 == level) {
			if (get_row(l, i)->flags & FLAG_REQBY)
				return i;
			break;
		}
	return l->cursor;
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

void toggle_req(struct pkglist *l, const struct pkgs *p, int reqby) {
	if (!reqby && get_row(l, l->cursor)->flags & FLAG_REQ) {
		move_rows(l, find_outer_edge(l, l->cursor, FLAG_REQ) + 1, l->cursor + 1);
		get_wrow(l, l->cursor)->flags &= ~FLAG_REQ;
	} else if (reqby && get_row(l, l->cursor)->flags & FLAG_REQBY) {
		int edge = find_outer_edge(l, l->cursor, FLAG_REQBY);

		move_rows(l, l->cursor, edge);
		l->cursor = edge;
		get_wrow(l, l->cursor)->flags &= ~FLAG_REQBY;
	} else {
		uint i, j, d, dep, deps, pid;
		const struct sets *r;
		struct row *row = NULL;

		r = reqby ? &p->required_by : &p->required;
		pid = get_row(l, l->cursor)->pid;
		deps = sets_get_set_size(r, pid);
		if (!deps)
			return;

		if (reqby) {
			move_rows(l, l->cursor, l->cursor + deps);
			l->cursor += deps;
		} else
			move_rows(l, l->cursor + 1, l->cursor + deps + 1);

		for (i = 0, dep = 1; i < sets_get_subsets(r, pid); i++) {
			d = sets_get_subset_size(r, pid, i);
			if (!d)
				continue;
			for (j = 0; j < d; j++, dep++) {
				row = get_wrow(l, l->cursor + (reqby ? -dep : dep));
				row->pid = sets_get(r, pid, i, j);
				row->level = get_row(l, l->cursor)->level + 1;
				row->flags = 0;
				if (i) 
					row->flags |= FLAG_REQOR;
			}
			sort_rows(l, p, l->cursor + (reqby ? -dep + 1 : dep - d), d, SORT_BY_NAME);
			if (i && !reqby)
				row->flags &= ~FLAG_REQOR;
		}
		row->flags |= FLAG_EDGE;
		get_wrow(l, l->cursor)->flags |= reqby ? FLAG_REQBY : FLAG_REQ;
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
			if ((r = p2->status - p1->status))
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

void fill_pkglist(struct pkglist *l, const struct pkgs *p) {
	uint i, j;
	regex_t reg;

	array_set_size(&l->rows, 0);
	l->cursor = l->first = 0;

	if (l->limit != NULL)
		regcomp(&reg, l->limit, REG_EXTENDED | REG_NOSUB);

	for (i = j = 0; i < pkgs_get_size(p); i++) {
		const struct pkg *pkg = pkgs_get(p, i);
		char nvra[1000];

		if (l->limit == NULL) {
			get_wrow(l, j++)->pid = i;
			continue;
		}
		snprintf(nvra, sizeof (nvra), "%s-%s-%s.%s",
				strings_get(&p->strings, pkg->name),
				strings_get(&p->strings, pkg->ver),
				strings_get(&p->strings, pkg->rel),
				strings_get(&p->strings, pkg->arch));
		if (regexec(&reg, nvra, 0, NULL, 0))
			continue;
		get_wrow(l, j++)->pid = i;
	}

	if (l->limit != NULL)
		regfree(&reg);
	sort_rows(l, p, 0, get_used_pkgs(l), l->sortby);
}

void init_pkglist(struct pkglist *l, const struct pkgs *p, int sortby, char *limit) {
	memset(l, 0, sizeof (struct pkglist));
	array_init(&l->rows, sizeof (struct row));

	l->lines = LINES - 3;
	l->sortby = sortby;
	l->limit = limit;

	fill_pkglist(l, p);
}

void clean_pkglist(struct pkglist *l) {
	free(l->limit);
	array_clean(&l->rows);
	memset(l, 0, sizeof (struct pkglist));
}

void sort_pkglist(struct pkglist *l, const struct pkgs *p) {
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

	fill_pkglist(l, p);
}

void limit_pkglist(struct pkglist *l, const struct pkgs *p, char *limit) {
	free(l->limit);
	l->limit = limit;
	fill_pkglist(l, p);
}

void search_pkglist(struct pkglist *l, const struct pkgs *p, const char *searchre, int dir) {
	uint c, used = get_used_pkgs(l);
	regex_t reg;

	if (searchre == NULL)
		return;

	regcomp(&reg, searchre, REG_EXTENDED | REG_NOSUB);

	for (c = (l->cursor + dir + used) % used; c != l->cursor; c = (c + dir + used) % used) {
		const struct pkg *pkg = pkgs_get(p, get_row(l, c)->pid);
		char nvra[1000];

		snprintf(nvra, sizeof (nvra), "%s-%s-%s.%s",
				strings_get(&p->strings, pkg->name),
				strings_get(&p->strings, pkg->ver),
				strings_get(&p->strings, pkg->rel),
				strings_get(&p->strings, pkg->arch));
		if (!regexec(&reg, nvra, 0, NULL, 0)) {
			l->cursor = c;
			break;
		}
	}

	regfree(&reg);
}

void print_pkg(const struct pkgs *p, uint pid) {
	const struct pkg *pkg = pkgs_get(p, pid);
	printf("%s-%s-%s.%s",
			strings_get(&p->strings, pkg->name),
			strings_get(&p->strings, pkg->ver),
			strings_get(&p->strings, pkg->rel),
			strings_get(&p->strings, pkg->arch));
}

void print_pkgs(const struct pkgs *p, int flags, int verbose, int oneline) {
	const struct pkg *pkg;
	uint i;

	for (i = 0; i < pkgs_get_size(p); i++) {
		pkg = pkgs_get(p, i);
		if (!(pkg->status & flags))
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
		printf("\n");
	}
	if (oneline)
		printf("\n");
}

void read_list(struct pkgs *p) {
	display_info_message("Reading rpmdb...");
	read_rpmdb(p);
	display_info_message("Matching deps...");
	pkgs_match_deps(p);
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

void reread_list(struct pkgs *p, struct pkglist *l) {
	pkgs_clean(p);
	pkgs_init(p);
	read_list(p);
	fill_pkglist(l, p);
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

void tui(const char *limit) {
	struct pkglist l;
	struct pkgs p;
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
		bkgdset(COLOR_PAIR(2));
	}
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	nonl();
	curs_set(0);
	erase();

	pkgs_init(&p);
	display_help();
	display_status(&p);
	read_list(&p);

	init_pkglist(&l, &p, SORT_BY_FLAGS, limit != NULL ? strdup(limit) : NULL);

	for (quit = 0; !quit; ) {
		move_cursor(&l, 0);

		erase();
		display_pkgs(&l, &p);
		draw_deplines(&l, &p);
		display_help();
		display_status(&p);
		display_liststatus(&l);

		if (!get_used_pkgs(&l))
			display_error_message("Nothing to select.");

		move(l.cursor - l.first + 1, 0);

		c = getch();

		switch (c) {
			case 'c':
			case 'C':
				if (ask_remove_pkgs(&p)) {
					endwin();
					if (rpmremove(&p, c == 'c' ? 0 : 1))
						sleep(2);
					reread_list(&p, &l);
				}
				break;
			case 'l':
				if ((s = readline("Limit: ")) != NULL)
					limit_pkglist(&l, &p, s);
				break;
			case 'o':
				sort_pkglist(&l, &p);
				break;
			case 'q':
				quit = 1;
				break;
			case 'x':
				quit = 2;
				break;
			/* ^R */
			case 'R' - 0x40:
				reread_list(&p, &l);
				break;
			/* ^L */
			case 'L' - 0x40:
				clear();
				break;
			case KEY_RESIZE:
				l.lines = LINES - 3;
				scroll_pkglist(&l, 0);
				break;
		}

		if (!get_used_pkgs(&l))
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
				move_cursor(&l, find_parent(&l) - l.cursor);
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
				scroll_pkglist(&l, -1000000);
				break;
			case KEY_END:
				scroll_pkglist(&l, 1000000);
				break;
			case KEY_BTAB:
				move_to_next_leaf(&l, &p, -1);
				break;
			case '\t':
				move_to_next_leaf(&l, &p, 1);
				break;
			case 'r':
				toggle_req(&l, &p, 0);
				break;
			case 'b':
				toggle_req(&l, &p, 1);
				break;
			case 'd':
				pkgs_delete(&p, get_row(&l, l.cursor)->pid, 0);
				break;
			case 'D':
				pkgs_delete(&p, get_row(&l, l.cursor)->pid, 1);
				break;
			case 'u':
				pkgs_undelete(&p, get_row(&l, l.cursor)->pid, 0);
				break;
			case 'U':
				pkgs_undelete(&p, get_row(&l, l.cursor)->pid, 1);
				break;
			case 'i':
				display_pkg_info(&p, get_row(&l, l.cursor)->pid);
				break;
			case '/':
			case '?':
				if ((s = readline("Search: ")) == NULL)
					break;
				free(searchre);
				searchre = s;
				searchdir = (c == '/') ? 1 : -1;
			case 'n':
				search_pkglist(&l, &p, searchre, searchdir);
				break;
			case 'N':
				search_pkglist(&l, &p, searchre, -searchdir);
				break;
		}
	}	
	if (quit < 2) {
		int remove = ask_remove_pkgs(&p);
		endwin();
		if (!remove)
			print_pkgs(&p, PKG_DELETE, 0, 1);
		else
			rpmremove(&p, 0);
	} else
		endwin();
	clean_pkglist(&l);
	pkgs_clean(&p);
	free(searchre);
}

void list_pkgs(int flags, int verbose) {
	struct pkgs p;

	pkgs_init(&p);
	read_rpmdb(&p);
	pkgs_match_deps(&p);
	print_pkgs(&p, flags, verbose, 0);
	pkgs_clean(&p);
}

int main(int argc, char **argv) {
	int opt, list_flags = 0, verbose = 0;

	while ((opt = getopt(argc, argv, "Llbav")) != -1) {
		switch (opt) {
			case 'L':
				list_flags |= PKG_LEAF;
				break;
			case 'l':
				list_flags |= PKG_PARTLEAF;
				break;
			case 'b':
				list_flags |= PKG_BROKEN;
				break;
			case 'a':
				list_flags |= PKG_ALL;
				break;
			case 'v':
				verbose = 1;
				break;
		}
	}

	if (list_flags)
		list_pkgs(list_flags, verbose);
	else
		tui(optind < argc ? argv[optind] : NULL);
	return 0;
}
