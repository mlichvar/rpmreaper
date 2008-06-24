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

#ifndef _RPM_H_
#define _RPM_H_

#include "pkg.h"

#define RPMMAXCNAME 1000

int rpmreaddb(struct pkgs *p);
int rpmcname(char *str, size_t size, const struct pkgs *p, uint pid);
int rpminfo(const struct pkgs *p, uint pid);
int rpmremove(const struct pkgs *p, int force);

#endif
